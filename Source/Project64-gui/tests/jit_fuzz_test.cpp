// Differential test of the x64 recompiler: random MIPS programs are run by the interpreter and by
// the recompiler from the same state (registers, FPU, memory) and everything they leave behind is
// compared. Needs an X server (Scripts/linux/run-gui-tests.sh starts Xvfb) because the game is
// loaded the way the application does it, with its plugins; the emulation thread is taken over
// by a hook (CN64System::m_CpuTestHook) before the CPU starts, so nothing of the game runs.
//
//   jit-fuzz-test <rom> [programs] [seed] [options]
//
//   options: nofastsp, nocache (no register caching), core32 / core64 (the core of the test), link=<0|1|2>
//   (the block linking), directed (only the directed programs) / nodirected, only=<n> (one
//   program), verbose (print every program), classes=<letters> (A alu, M memory, F fpu, B branch,
//   H hi/lo and multiply, T traps)
#include "../MainMenu.h"
#include "../MainWindow.h"

#include <Common/path.h>
#include <Project64-core/AppInit.h>
#include <Project64-core/N64System/Interpreter/InterpreterOps.h>
#include <Project64-core/N64System/Mips/R4300iInstruction.h>
#include <Project64-core/N64System/N64System.h>
#include <Project64-core/N64System/Recompiler/Recompiler.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/Plugins/Plugin.h>
#include <Project64-core/Settings.h>
#include <Project64/Settings/UISettings.h>

#include <QApplication>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>

// Access to the private parts of the core that the test needs (declared a friend there)
struct CN64SystemTestAccess
{
    static R4300iOp & Interpreter(CN64System & System)
    {
        return System.m_OpCodes;
    }
    static CRecompiler * Recompiler(CN64System & System)
    {
        return System.m_Recomp;
    }
    // core64: the interpreter's operations of a 64 bit core (the table is built for the setting)
    static void BuildInterpreter(CN64System & System, bool Force32bit)
    {
        System.m_OpCodes.BuildInterpreter(Force32bit);
    }
    static void ClearEnd(CN64System & System)
    {
        System.m_EndEmulation = false;
    }
    static bool Ended(CN64System & System)
    {
        return System.m_EndEmulation;
    }
    static void SetPipeline(CN64System & System, PIPELINE_STAGE Stage, uint64_t JumpTo)
    {
        System.m_PipelineStage = Stage;
        System.m_JumpToLocation = JumpTo;
    }
    static PIPELINE_STAGE Pipeline(CN64System & System)
    {
        return System.m_PipelineStage;
    }
    static uint64_t JumpTo(CN64System & System)
    {
        return System.m_JumpToLocation;
    }
    static bool CompileAndRun(CRecompiler & Recompiler)
    {
        static bool Allocated = false;
        if (!Allocated)
        {
            if (!Recompiler.CRecompMemory::AllocateMemory(g_MMU->Rdram()) || !Recompiler.CFunctionMap::AllocateMemory())
            {
                return false;
            }
            Allocated = true;
        }
        CCompiledFunc * Func = Recompiler.CompileCode();
        if (Func == nullptr)
        {
            return false;
        }
        (Func->Function())();
        return true;
    }
    static void ResetCode(CRecompiler & Recompiler)
    {
        Recompiler.ResetRecompCode(true);
    }
    // FUZZ_ASM=<file>: what the recompiler makes of the programs is written to the file
    static void StartAsmLog(CRecompiler & Recompiler)
    {
        g_DebugSettings.recordRecompilerAsm = true;
        Recompiler.StartLog();
    }
};

static std::atomic<int> g_Dialogs{0};
static std::atomic<int> g_LastBreakLine{0};
static std::atomic<int> g_LastBreakFile{0}; // a number for each file (g_BreakFiles)
static std::vector<std::string> g_BreakFiles{"?"};
static std::atomic<bool> g_Finished{false};
static std::atomic<int> g_Result{1};

static void DismissDialogs()
{
    for (QWidget * Widget : QApplication::topLevelWidgets())
    {
        QMessageBox * Box = qobject_cast<QMessageBox *>(Widget);
        if (Box != nullptr && Box->isVisible())
        {
            g_Dialogs++;
            const QStringList Lines = Box->text().split('\n');
            if (Lines.size() >= 3 && Lines[0].startsWith("Break point found"))
            {
                // the break points of the recompiler: the last one is what stopped the program
                g_LastBreakLine = Lines[2].toInt();
                const std::string File = Lines[1].mid(Lines[1].lastIndexOf('/') + 1).toStdString();
                size_t Number = 0;
                while (Number < g_BreakFiles.size() && g_BreakFiles[Number] != File)
                {
                    Number++;
                }
                if (Number == g_BreakFiles.size())
                {
                    g_BreakFiles.push_back(File);
                }
                g_LastBreakFile = (int)Number;
            }
            else
            {
                printf("dialog: %s\n", Box->text().replace('\n', ' ').left(150).toUtf8().constData());
                fflush(stdout);
            }
            Box->accept();
        }
    }
}

namespace
{
constexpr uint32_t ProgramBase = 0x80300000;
constexpr uint32_t ProgramSize = 0x4000;
constexpr uint32_t DataBase = 0x80378000;
constexpr uint32_t DataSize = 0x14000;
constexpr uint32_t DataPointer = 0x80380800; // the middle of the data: the offsets of the instructions reach 32KB either way
constexpr int WildReg = 26;                  // holds an address that is not mapped (the access raises the TLB exception)
constexpr uint32_t StackBase = 0x80390000;
constexpr int DataReg = 28;
constexpr int LoopReg = 27;

struct Options
{
    int Programs = 1000;
    uint32_t Seed = 1;
    int Only = -1;
    bool Verbose = false;
    std::string Classes = "AMFBHT";
    bool Core32 = false;
    bool NoFastSp = false;
    bool NoCache = false;
    bool Core64 = false;
    bool ForceCore32 = false;
    bool NoDirected = false;
    bool DirectedOnly = false;
    int Link = -1;
    int MaxFailures = 5;
};
Options g_Options;

// ---- encoding ----
uint32_t RType(int Funct, int Rs, int Rt, int Rd, int Sa = 0)
{
    return ((uint32_t)Rs << 21) | ((uint32_t)Rt << 16) | ((uint32_t)Rd << 11) | ((uint32_t)Sa << 6) | (uint32_t)Funct;
}
uint32_t IType(int Op, int Rs, int Rt, int Imm)
{
    return ((uint32_t)Op << 26) | ((uint32_t)Rs << 21) | ((uint32_t)Rt << 16) | ((uint32_t)Imm & 0xFFFF);
}
uint32_t Cop1(int Fmt, int Ft, int Fs, int Fd, int Funct)
{
    return (17u << 26) | ((uint32_t)Fmt << 21) | ((uint32_t)Ft << 16) | ((uint32_t)Fs << 11) | ((uint32_t)Fd << 6) | (uint32_t)Funct;
}

struct State
{
    uint64_t Pc;
    uint64_t Gpr[32];
    uint64_t Cp0[32];
    uint64_t Hi, Lo;
    uint32_t LLBit;
    uint32_t Fpcr[32];
    uint64_t Fpr[32];
    uint64_t Jump;
    PIPELINE_STAGE Stage;
    std::vector<uint8_t> Data;
    std::vector<uint8_t> Stack;
};

class Fuzzer
{
public:
    explicit Fuzzer(CN64System & System) :
        m_System(System)
    {
        memcpy(m_BaselineCp0, g_Reg->m_CP0, sizeof(m_BaselineCp0));
    }

    bool Run();

private:
    uint32_t Random(uint32_t N)
    {
        return N == 0 ? 0 : (uint32_t)(m_Rng() % N);
    }
    bool Chance(int Percent)
    {
        return (int)Random(100) < Percent;
    }
    uint64_t RandomValue();
    uint64_t RandomFloatBits(bool Double);
    int Reg(bool AllowZero = false, bool AllowRa = false);
    int Imm16();
    bool Allowed(uint32_t Word) const;
    uint32_t AluRaw();
    uint32_t HiLoRaw();
    uint32_t MemRaw();
    uint32_t FpuRaw();
    uint32_t TrapRaw();
    uint32_t Alu() { return Filtered(&Fuzzer::AluRaw); }
    uint32_t HiLo() { return Filtered(&Fuzzer::HiLoRaw); }
    uint32_t Mem() { return Filtered(&Fuzzer::MemRaw); }
    uint32_t Fpu() { return Filtered(&Fuzzer::FpuRaw); }
    uint32_t Trap() { return Filtered(&Fuzzer::TrapRaw); }
    uint32_t Filtered(uint32_t (Fuzzer::*Generator)())
    {
        for (;;)
        {
            const uint32_t Word = (this->*Generator)();
            if (Allowed(Word))
            {
                return Word;
            }
        }
    }
    void Branch(std::vector<uint32_t> & Program);
    void Jump(std::vector<uint32_t> & Program, uint32_t StartPc);
    void Generate(std::vector<uint32_t> & Program, uint32_t & StartPc);
    struct Counters
    {
        int Failures = 0, Ran = 0, Exceptions = 0;
        std::map<int, int> Breaks;
    };
    // One program: both ways, the comparison, the report. false when the run is to stop
    bool Evaluate(const std::string & Name, std::vector<uint32_t> & Program, uint32_t StartPc, State & Initial, Counters & C);
    // Programs for the problems found in the port (see Directed)
    bool Directed(Counters & C);
    bool DirectedBranches(Counters & C);
    bool DirectedJumps(Counters & C);
    bool DirectedMemory(Counters & C);
    bool DirectedArithmetic(Counters & C);
    bool DirectedSubu(Counters & C);
    bool DirectedLoops(Counters & C);
    void DirectedState(State & S, uint32_t StartPc);
    void LoadConstant(std::vector<uint32_t> & Program, int Rd, uint64_t Value);
    std::vector<uint64_t> EdgeValues() const;
    void GenerateState(State & S);
    void Apply(const State & S);
    void Capture(State & S);
    bool RunInterpreter(const State & Initial, State & Result);
    bool RunRecompiler(const State & Initial, State & Result);
    bool Compare(const State & A, const State & B, std::string & Differences);
    void Print(const std::vector<uint32_t> & Program, uint32_t StartPc, const State & Initial);
    void WriteProgram(const std::vector<uint32_t> & Program, uint32_t StartPc);
    // 0: the two agree, 1: a difference, 2: the recompiler stopped
    int Check(const std::vector<uint32_t> & Program, uint32_t StartPc, const State & Initial, std::string * Differences, std::string * Signature = nullptr);
    void Shrink(std::vector<uint32_t> & Program, uint32_t StartPc, const State & Initial);

    CN64System & m_System;
    uint64_t m_BaselineCp0[32];
    std::mt19937 m_Rng;
    std::string m_Classes;
};

uint64_t Fuzzer::RandomValue()
{
    switch (Random(8))
    {
    case 0: return Random(32);
    case 1: return (uint64_t)(int64_t)(int32_t)Random(0x10000) - 0x8000;
    case 2: return (uint64_t)(int64_t)(int32_t)m_Rng();
    case 3: return ((uint64_t)m_Rng() << 32) | m_Rng();
    case 4:
    {
        static const uint64_t Special[] = {0, 1, (uint64_t)-1, 0x7FFFFFFF, 0x80000000ull, 0xFFFFFFFFull, 0x7FFFFFFFFFFFFFFFull, 0x8000000000000000ull, 0xFFFFFFFF80000000ull, 0x100000000ull};
        return Special[Random(sizeof(Special) / sizeof(Special[0]))];
    }
    case 5: return DataBase + Random(DataSize / 4) * 4;
    case 6: return (uint64_t)(int64_t)(int32_t)(DataBase + Random(DataSize));
    default: return (uint64_t)(int64_t)(int32_t)(m_Rng() & 0xFFFF0000);
    }
}

uint64_t Fuzzer::RandomFloatBits(bool Double)
{
    switch (Random(10))
    {
    case 0: return Double ? 0 : 0;
    case 1:
    {
        // a normal number
        const double V = ((double)(int32_t)m_Rng()) / 65536.0;
        if (Double)
        {
            uint64_t B;
            memcpy(&B, &V, 8);
            return B;
        }
        const float F = (float)V;
        uint32_t B;
        memcpy(&B, &F, 4);
        return B;
    }
    case 2:
    {
        const double V = (double)(int32_t)Random(2000) - 1000.0;
        if (Double)
        {
            uint64_t B;
            memcpy(&B, &V, 8);
            return B;
        }
        const float F = (float)V;
        uint32_t B;
        memcpy(&B, &F, 4);
        return B;
    }
    case 3: return Double ? 0x7FF0000000000000ull : 0x7F800000u; // infinity
    case 4: return Double ? 0x7FF8000000000000ull : 0x7FC00000u; // quiet NaN
    case 5: return Double ? 0x0000000000000001ull : 0x00000001u; // denormal (unimplemented operation)
    default:
    {
        const double V = (double)(int32_t)m_Rng() / 1024.0;
        if (Double)
        {
            uint64_t B;
            memcpy(&B, &V, 8);
            return B;
        }
        const float F = (float)V;
        uint32_t B;
        memcpy(&B, &F, 4);
        return B;
    }
    }
}

int Fuzzer::Reg(bool AllowZero, bool AllowRa)
{
    const uint32_t R = Random(AllowRa ? 12 : 10);
    if (R == 0)
    {
        return AllowZero ? 0 : 1 + Random(8);
    }
    if (R == 9)
    {
        return AllowRa ? 31 : 1 + Random(8);
    }
    if (R >= 10)
    {
        return 31;
    }
    return R;
}

int Fuzzer::Imm16()
{
    switch (Random(6))
    {
    case 0: return Random(8);
    case 1: return -(int)Random(8);
    case 2: return 0x7FFF;
    case 3: return -0x8000;
    default: return (int)(m_Rng() & 0xFFFF);
    }
}

// A 32 bit core does not run the 64 bit operations (MIPS III) of the R4300i
bool Fuzzer::Allowed(uint32_t Word) const
{
    if (!g_Options.Core32)
    {
        return true;
    }
    const uint32_t Op = Word >> 26;
    switch (Op)
    {
    case 24: case 25: case 26: case 27: case 39: case 44: case 45: case 55: case 63: return false;
    case 0:
    {
        const uint32_t Funct = Word & 0x3F;
        return !(Funct == 20 || Funct == 22 || Funct == 23 || (Funct >= 28 && Funct <= 31) || (Funct >= 44 && Funct <= 47) || Funct == 56 || Funct == 58 || Funct == 59 || Funct == 60 || Funct == 62 || Funct == 63);
    }
    case 17:
    {
        const uint32_t Rs = (Word >> 21) & 0x1F;
        return Rs != 1 && Rs != 5;
    }
    default: return true;
    }
}

uint32_t Fuzzer::AluRaw()
{
    const int Rs = Reg(true), Rt = Reg(true), Rd = Reg();
    switch (Random(48))
    {
    case 0: return IType(8, Rs, Rd, Imm16());   // ADDI
    case 1: return IType(9, Rs, Rd, Imm16());   // ADDIU
    case 2: return IType(10, Rs, Rd, Imm16());  // SLTI
    case 3: return IType(11, Rs, Rd, Imm16());  // SLTIU
    case 4: return IType(12, Rs, Rd, Imm16());  // ANDI
    case 5: return IType(13, Rs, Rd, Imm16());  // ORI
    case 6: return IType(14, Rs, Rd, Imm16());  // XORI
    case 7: return IType(15, 0, Rd, Imm16());   // LUI
    case 8: return IType(24, Rs, Rd, Imm16());  // DADDI
    case 9: return IType(25, Rs, Rd, Imm16());  // DADDIU
    case 10: return RType(0, 0, Rt, Rd, Random(32));    // SLL
    case 11: return RType(2, 0, Rt, Rd, Random(32));    // SRL
    case 12: return RType(3, 0, Rt, Rd, Random(32));    // SRA
    case 13: return RType(4, Rs, Rt, Rd);               // SLLV
    case 14: return RType(6, Rs, Rt, Rd);               // SRLV
    case 15: return RType(7, Rs, Rt, Rd);               // SRAV
    case 16: return RType(20, Rs, Rt, Rd);              // DSLLV
    case 17: return RType(22, Rs, Rt, Rd);              // DSRLV
    case 18: return RType(23, Rs, Rt, Rd);              // DSRAV
    case 19: return RType(32, Rs, Rt, Rd);              // ADD
    case 20: return RType(33, Rs, Rt, Rd);              // ADDU
    case 21: return RType(34, Rs, Rt, Rd);              // SUB
    case 22: return RType(35, Rs, Rt, Rd);              // SUBU
    case 23: return RType(36, Rs, Rt, Rd);              // AND
    case 24: return RType(37, Rs, Rt, Rd);              // OR
    case 25: return RType(38, Rs, Rt, Rd);              // XOR
    case 26: return RType(39, Rs, Rt, Rd);              // NOR
    case 27: return RType(42, Rs, Rt, Rd);              // SLT
    case 28: return RType(43, Rs, Rt, Rd);              // SLTU
    case 29: return RType(44, Rs, Rt, Rd);              // DADD
    case 30: return RType(45, Rs, Rt, Rd);              // DADDU
    case 31: return RType(46, Rs, Rt, Rd);              // DSUB
    case 32: return RType(47, Rs, Rt, Rd);              // DSUBU
    case 33: return RType(56, 0, Rt, Rd, Random(32));   // DSLL
    case 34: return RType(58, 0, Rt, Rd, Random(32));   // DSRL
    case 35: return RType(59, 0, Rt, Rd, Random(32));   // DSRA
    case 36: return RType(60, 0, Rt, Rd, Random(32));   // DSLL32
    case 37: return RType(62, 0, Rt, Rd, Random(32));   // DSRL32
    case 38: return RType(63, 0, Rt, Rd, Random(32));   // DSRA32
    case 39: return RType(15, 0, 0, 0);                 // SYNC
    case 40: return RType(0, 0, 0, 0, 0);               // NOP
    case 41: return IType(9, Rs, Rd, Imm16());          // ADDIU again (the most common)
    case 42: return RType(33, Rs, Rt, Rd);              // ADDU again
    case 43: return IType(13, Rs, Rd, Imm16());         // ORI again
    case 44: return RType(37, Rs, 0, Rd);               // move
    case 45: return IType(15, 0, Rd, Imm16());          // LUI again
    case 46: return IType(9, 29, 29, (int)(Random(4) * 8) - 16); // ADDIU sp, sp, small
    default: return RType(36, Rs, Rt, Rd);
    }
}

uint32_t Fuzzer::HiLoRaw()
{
    const int Rs = Reg(true), Rt = Reg(true), Rd = Reg();
    switch (Random(14))
    {
    case 0: return RType(16, 0, 0, Rd);   // MFHI
    case 1: return RType(17, Rs, 0, 0);   // MTHI
    case 2: return RType(18, 0, 0, Rd);   // MFLO
    case 3: return RType(19, Rs, 0, 0);   // MTLO
    case 4: return RType(24, Rs, Rt, 0);  // MULT
    case 5: return RType(25, Rs, Rt, 0);  // MULTU
    case 6: return RType(26, Rs, Rt, 0);  // DIV
    case 7: return RType(27, Rs, Rt, 0);  // DIVU
    case 8: return RType(28, Rs, Rt, 0);  // DMULT
    case 9: return RType(29, Rs, Rt, 0);  // DMULTU
    case 10: return RType(30, Rs, Rt, 0); // DDIV
    case 11: return RType(31, Rs, Rt, 0); // DDIVU
    case 12: return RType(18, 0, 0, Rd);
    default: return RType(16, 0, 0, Rd);
    }
}

uint32_t Fuzzer::TrapRaw()
{
    const int Rs = Reg(true), Rt = Reg(true);
    switch (Random(12))
    {
    case 0: return RType(48, Rs, Rt, 0);
    case 1: return RType(49, Rs, Rt, 0);
    case 2: return RType(50, Rs, Rt, 0);
    case 3: return RType(51, Rs, Rt, 0);
    case 4: return RType(52, Rs, Rt, 0);
    case 5: return RType(54, Rs, Rt, 0);
    case 6: return IType(1, Rs, 8, Imm16());
    case 7: return IType(1, Rs, 9, Imm16());
    case 8: return IType(1, Rs, 10, Imm16());
    case 9: return IType(1, Rs, 11, Imm16());
    case 10: return IType(1, Rs, 12, Imm16());
    default: return IType(1, Rs, 14, Imm16());
    }
}

uint32_t Fuzzer::MemRaw()
{
    // the base is the data pointer (mostly), the stack pointer or any register (a wild address)
    int Base = DataReg;
    int Offset = (int)Random(DataSize / 2) - (int)(DataSize / 4);
    const uint32_t BaseKind = Random(10);
    if (BaseKind == 0)
    {
        Base = 29;
        Offset = (int)Random(64) * 4 - 32;
    }
    else if (BaseKind == 1)
    {
        Base = WildReg;
        Offset = Imm16();
    }
    else if (BaseKind == 2)
    {
        Offset = Imm16(); // wild
    }
    const int Rt = Reg(true);
    const int Op = (int)Random(22);
    const bool Align = !Chance(8); // an unaligned access now and then (the address error)
    int Size = 1;
    int OpCode = 0;
    switch (Op)
    {
    case 0: OpCode = 32; Size = 1; break;  // LB
    case 1: OpCode = 36; Size = 1; break;  // LBU
    case 2: OpCode = 33; Size = 2; break;  // LH
    case 3: OpCode = 37; Size = 2; break;  // LHU
    case 4: OpCode = 35; Size = 4; break;  // LW
    case 5: OpCode = 39; Size = 4; break;  // LWU
    case 6: OpCode = 55; Size = 8; break;  // LD
    case 7: OpCode = 34; Size = 1; break;  // LWL
    case 8: OpCode = 38; Size = 1; break;  // LWR
    case 9: OpCode = 26; Size = 1; break;  // LDL
    case 10: OpCode = 27; Size = 1; break; // LDR
    case 11: OpCode = 40; Size = 1; break; // SB
    case 12: OpCode = 41; Size = 2; break; // SH
    case 13: OpCode = 43; Size = 4; break; // SW
    case 14: OpCode = 63; Size = 8; break; // SD
    case 15: OpCode = 42; Size = 1; break; // SWL
    case 16: OpCode = 46; Size = 1; break; // SWR
    case 17: OpCode = 44; Size = 1; break; // SDL
    case 18: OpCode = 45; Size = 1; break; // SDR
    case 19: OpCode = 43; Size = 4; break; // SW again
    case 20: OpCode = 35; Size = 4; break; // LW again
    default: OpCode = 48; Size = 4; break; // LL
    }
    // the unaligned operations (and LL) of the interpreter read memory through a raw pointer: no wild addresses
    if ((Op >= 7 && Op <= 10) || (Op >= 15 && Op <= 18) || Op == 21)
    {
        Base = DataReg;
        Offset = (int)Random(DataSize / 2) - (int)(DataSize / 4);
    }
    if (Align && Size > 1)
    {
        Offset &= ~(Size - 1);
    }
    const int Dest = (OpCode >= 32 && OpCode <= 39) || OpCode == 26 || OpCode == 27 || OpCode == 48 ? (Rt == 0 || Rt == 29 ? 1 + Random(8) : Rt) : Rt;
    return IType(OpCode, Base, Dest, Offset);
}

uint32_t Fuzzer::FpuRaw()
{
    const int Fs = Random(32), Ft = Random(32), Fd = Random(32);
    const int Rt = Reg(true);
    const int Dest = Rt == 0 ? 1 + Random(8) : Rt;
    const bool D = Chance(50);
    const int Fmt = D ? 17 : 16;
    switch (Random(34))
    {
    case 0: return Cop1(0, Dest, Fs, 0, 0);   // MFC1
    case 1: return Cop1(1, Dest, Fs, 0, 0);   // DMFC1
    case 2: return Cop1(2, Dest, Random(2) ? 31 : 0, 0, 0); // CFC1
    case 3: return Cop1(4, Rt, Fs, 0, 0);     // MTC1
    case 4: return Cop1(5, Rt, Fs, 0, 0);     // DMTC1
    case 5: return Chance(30) ? Cop1(6, Rt, 31, 0, 0) : Cop1(2, Dest, 31, 0, 0); // CTC1
    case 6: return Cop1(Fmt, Ft, Fs, Fd, 0);  // ADD
    case 7: return Cop1(Fmt, Ft, Fs, Fd, 1);  // SUB
    case 8: return Cop1(Fmt, Ft, Fs, Fd, 2);  // MUL
    case 9: return Cop1(Fmt, Ft, Fs, Fd, 3);  // DIV
    case 10: return Cop1(Fmt, 0, Fs, Fd, 4);  // SQRT
    case 11: return Cop1(Fmt, 0, Fs, Fd, 5);  // ABS
    case 12: return Cop1(Fmt, 0, Fs, Fd, 6);  // MOV
    case 13: return Cop1(Fmt, 0, Fs, Fd, 7);  // NEG
    case 14: return Cop1(Fmt, 0, Fs, Fd, 8 + Random(8)); // ROUND/TRUNC/CEIL/FLOOR .L/.W
    case 15: return Cop1(Fmt, 0, Fs, Fd, 32 + (D ? 0 : 1)); // CVT.S (from D) / CVT.D (from S)
    case 16: return Cop1(Fmt, 0, Fs, Fd, 36);  // CVT.W
    case 17: return Cop1(Fmt, 0, Fs, Fd, 37);  // CVT.L
    case 18: return Cop1(20, 0, Fs, Fd, Chance(50) ? 32 : 33); // CVT.S.W / CVT.D.W
    case 19: return Cop1(21, 0, Fs, Fd, Chance(50) ? 32 : 33); // CVT.S.L / CVT.D.L
    case 20:
    case 21:
    case 22: return Cop1(Fmt, Ft, Fs, 0, 48 + Random(16)); // C.cond
    case 23: return IType(49, DataReg, Ft, (int)(Random(DataSize / 8)) * 4 - 0x1000); // LWC1
    case 24: return IType(57, DataReg, Ft, (int)(Random(DataSize / 8)) * 4 - 0x1000); // SWC1
    case 25: return IType(53, DataReg, Ft, (int)(Random(DataSize / 16)) * 8 - 0x1000); // LDC1
    case 26: return IType(61, DataReg, Ft, (int)(Random(DataSize / 16)) * 8 - 0x1000); // SDC1
    default: return Cop1(Fmt, Ft, Fs, Fd, Random(4)); // arithmetic
    }
}

// A forward branch over one to three instructions (its delay slot is one more), or a short loop
void Fuzzer::Branch(std::vector<uint32_t> & Program)
{
    const int Rs = Reg(true), Rt = Reg(true);
    const uint32_t Kind = Random(14);
    const int Skip = 1 + (int)Random(3);
    const bool Fp = m_Classes.find('F') != std::string::npos;
    if (Kind == 13)
    {
        // a loop: counter, body, decrement, branch back with a delay slot
        const int Count = 1 + (int)Random(4);
        Program.push_back(IType(9, 0, LoopReg, Count));
        const int Body = 1 + (int)Random(4);
        for (int i = 0; i < Body; i++)
        {
            Program.push_back(Alu());
        }
        Program.push_back(IType(9, LoopReg, LoopReg, -1));
        Program.push_back(IType(5, LoopReg, 0, -(Body + 2) - 0)); // BNE r27, r0, back to the first body instruction
        Program.push_back(Alu());                                    // delay slot
        return;
    }
    uint32_t Branch = 0;
    switch (Kind)
    {
    case 0: Branch = IType(4, Rs, Rt, Skip + 1); break;  // BEQ
    case 1: Branch = IType(5, Rs, Rt, Skip + 1); break;  // BNE
    case 2: Branch = IType(6, Rs, 0, Skip + 1); break;   // BLEZ
    case 3: Branch = IType(7, Rs, 0, Skip + 1); break;   // BGTZ
    case 4: Branch = IType(1, Rs, 0, Skip + 1); break;   // BLTZ
    case 5: Branch = IType(1, Rs, 1, Skip + 1); break;   // BGEZ
    case 6: Branch = IType(20, Rs, Rt, Skip + 1); break; // BEQL
    case 7: Branch = IType(21, Rs, Rt, Skip + 1); break; // BNEL
    case 8: Branch = IType(22, Rs, 0, Skip + 1); break;  // BLEZL
    case 9: Branch = IType(23, Rs, 0, Skip + 1); break;  // BGTZL
    case 10: Branch = IType(1, Rs, 2, Skip + 1); break;  // BLTZL
    case 11: Branch = IType(1, Rs, 3, Skip + 1); break;  // BGEZL
    default:
        if (Fp)
        {
            Branch = (17u << 26) | (8u << 21) | ((uint32_t)Random(4) << 16) | (uint32_t)(Skip + 1); // BC1F/T/FL/TL
        }
        else
        {
            Branch = IType(4, Rs, Rt, Skip + 1);
        }
        break;
    }
    if (Chance(15) && Kind != 13)
    {
        // and link: BLTZAL / BGEZAL / BGEZALL / BLTZALL
        const int Rts[] = {16, 17, 19, 19};
        Branch = IType(1, Rs, Rts[Random(4)], Skip + 1);
    }
    Program.push_back(Branch);
    Program.push_back(Chance(15) ? RType(0, 0, 0, 0) : Alu()); // delay slot
    for (int i = 0; i < Skip; i++)
    {
        Program.push_back(Alu());
    }
}

// J, JAL, JR and JALR: a jump forward, a call and its return, a jump to an address that is calculated
void Fuzzer::Jump(std::vector<uint32_t> & Program, uint32_t StartPc)
{
    auto Pc = [&]() { return StartPc + (uint32_t)Program.size() * 4; };
    auto Delay = [&]() { return Chance(20) ? RType(0, 0, 0, 0) : (Chance(20) ? Mem() : Alu()); };
    auto Absolute = [&](int Reg, uint32_t Address) {
        Program.push_back(IType(15, 0, Reg, (int)(Address >> 16)));
        Program.push_back(IType(13, Reg, Reg, (int)(Address & 0xFFFF)));
    };
    const uint32_t Kind = Random(5);
    const int Skip = 1 + (int)Random(3);
    switch (Kind)
    {
    case 0:
    {
        // J forward over Skip instructions
        const uint32_t Target = Pc() + 8 + (uint32_t)Skip * 4;
        Program.push_back((2u << 26) | ((Target >> 2) & 0x3FFFFFF));
        Program.push_back(Delay());
        for (int i = 0; i < Skip; i++)
        {
            Program.push_back(Alu());
        }
        break;
    }
    case 1:
    {
        // JAL to a procedure after the code, that returns with JR RA
        const int Body = 1 + (int)Random(3);
        const uint32_t Target = Pc() + 8 + 8 + 0; // after: jal, delay, j End, delay
        Program.push_back((3u << 26) | ((Target >> 2) & 0x3FFFFFF));
        Program.push_back(Delay());
        const uint32_t End = Pc() + 8 + (uint32_t)(Body + 2) * 4;
        Program.push_back((2u << 26) | ((End >> 2) & 0x3FFFFFF)); // j End
        Program.push_back(RType(0, 0, 0, 0));
        for (int i = 0; i < Body; i++)
        {
            Program.push_back(Alu());
        }
        Program.push_back(RType(8, 31, 0, 0)); // jr ra
        Program.push_back(Delay());
        break;
    }
    case 2:
    {
        // JR to the address that was calculated
        const int Reg1 = 1 + (int)Random(8);
        const uint32_t Target = Pc() + 8 + 8 + (uint32_t)Skip * 4;
        Absolute(Reg1, Target);
        Program.push_back(RType(8, Reg1, 0, 0));
        Program.push_back(Delay());
        for (int i = 0; i < Skip; i++)
        {
            Program.push_back(Alu());
        }
        break;
    }
    case 3:
    {
        // JALR
        const int Reg1 = 1 + (int)Random(8);
        const int Link = Chance(70) ? 31 : 1 + (int)Random(8);
        const uint32_t Target = Pc() + 8 + 8 + (uint32_t)Skip * 4;
        Absolute(Reg1, Target);
        Program.push_back(RType(9, Reg1, 0, Link));
        Program.push_back(Delay());
        for (int i = 0; i < Skip; i++)
        {
            Program.push_back(Alu());
        }
        break;
    }
    default:
    {
        // JAL, the link is used
        const uint32_t Target = Pc() + 8 + (uint32_t)Skip * 4;
        Program.push_back((3u << 26) | ((Target >> 2) & 0x3FFFFFF));
        Program.push_back(Delay());
        for (int i = 0; i < Skip; i++)
        {
            Program.push_back(Alu());
        }
        Program.push_back(RType(33, 31, 0, 1 + (int)Random(8)));
        break;
    }
    }
}

void Fuzzer::Generate(std::vector<uint32_t> & Program, uint32_t & StartPc)
{
    Program.clear();
    // where the program is placed: some programs cross a page boundary
    StartPc = ProgramBase + (Chance(30) ? 0x1000 - 4 * (1 + Random(40)) : 4 * Random(512));
    // The registers start as constants for part of the registers: that is how a compiled block
    // sees the code after a LUI / ORI
    const int Consts = (int)Random(6);
    for (int i = 0; i < Consts; i++)
    {
        const int Rd = Reg(false, true);
        const uint32_t Value = (uint32_t)m_Rng();
        Program.push_back(IType(15, 0, Rd, (int)(Value >> 16)));
        if (Chance(70))
        {
            Program.push_back(IType(13, Rd, Rd, (int)(Value & 0xFFFF)));
        }
    }
    if (Chance(30))
    {
        const uint32_t Base = DataPointer;
        Program.push_back(IType(15, 0, DataReg, (int)(Base >> 16)));
        Program.push_back(IType(13, DataReg, DataReg, (int)(Base & 0xFFFF)));
    }
    if (Chance(20))
    {
        Program.push_back(IType(15, 0, 29, (int)(StackBase >> 16)));
        Program.push_back(IType(13, 29, 29, (int)(StackBase & 0xFFFF)));
    }
    const int Length = 4 + (int)Random(36);
    const std::string & C = m_Classes;
    for (int i = 0; i < Length; i++)
    {
        const char Class = C[Random((uint32_t)C.size())];
        switch (Class)
        {
        case 'A': Program.push_back(Alu()); break;
        case 'M': Program.push_back(Mem()); break;
        case 'F': Program.push_back(Fpu()); break;
        case 'H': Program.push_back(HiLo()); break;
        case 'T': Program.push_back(Chance(25) ? Trap() : Alu()); break;
        case 'B': Chance(30) ? Jump(Program, StartPc) : Branch(Program); break;
        }
    }
    Program.push_back(RType(12, 0, 0, 0)); // SYSCALL ends the program (the exception vector is outside)
}

void Fuzzer::GenerateState(State & S)
{
    memset(&S, 0, sizeof(S));
    S.Data.resize(DataSize);
    S.Stack.resize(0x1000);
    for (auto & B : S.Data)
    {
        B = (uint8_t)m_Rng();
    }
    for (auto & B : S.Stack)
    {
        B = (uint8_t)m_Rng();
    }
    for (int i = 1; i < 32; i++)
    {
        S.Gpr[i] = RandomValue();
        if (g_Options.Core32)
        {
            S.Gpr[i] = (uint64_t)(int64_t)(int32_t)S.Gpr[i];
        }
    }
    S.Gpr[DataReg] = (uint64_t)(int64_t)(int32_t)DataPointer;
    S.Gpr[29] = (uint64_t)(int64_t)(int32_t)(StackBase + 0x400);
    S.Gpr[LoopReg] = 0;
    // an address of the user segment: no TLB entry, no memory (a TLB miss exception) - what a wild pointer is
    S.Gpr[WildReg] = (uint64_t)(int64_t)(int32_t)((m_Rng() & 0x7FFFF000u) | (Chance(50) ? 0 : (m_Rng() & 0xFFF)));
    if (Chance(20))
    {
        // or an address in the TLB mapped segment of the kernel
        S.Gpr[WildReg] = (uint64_t)(int64_t)(int32_t)(0xC0000000u | (m_Rng() & 0x1FFFFFFCu));
    }
    S.Hi = RandomValue();
    S.Lo = RandomValue();
    if (g_Options.Core32)
    {
        // what a 32 bit core leaves in HI and LO
        S.Hi = (uint64_t)(int64_t)(int32_t)S.Hi;
        S.Lo = (uint64_t)(int64_t)(int32_t)S.Lo;
    }
    S.LLBit = 0;
    for (int i = 0; i < 32; i++)
    {
        const bool Double = Chance(50);
        S.Fpr[i] = RandomFloatBits(Double);
        if (Double)
        {
            // keep the other half of a pair different too
        }
    }
    const uint32_t Rounding = Random(4);
    const uint32_t Enables = Chance(15) ? (Random(32) << 7) : 0;
    S.Fpcr[31] = Rounding | Enables | (Chance(30) ? 0x00800000u : 0);
    S.Stage = PIPELINE_STAGE_NORMAL;
    S.Jump = 0;
    memcpy(S.Cp0, m_BaselineCp0, sizeof(S.Cp0));
    // the FPU usable, no interrupts, not in an exception
    S.Cp0[12] = (m_BaselineCp0[12] | 0x20000000) & ~(uint64_t)0x7;
    S.Cp0[13] = 0;
    S.Cp0[14] = 0;
}

void Fuzzer::Capture(State & S)
{
    S.Pc = g_Reg->m_PROGRAM_COUNTER;
    for (int i = 0; i < 32; i++)
    {
        S.Gpr[i] = g_Reg->m_GPR[i].UDW;
        S.Cp0[i] = g_Reg->m_CP0[i];
        S.Fpr[i] = g_Reg->m_FPR[i].UDW;
        S.Fpcr[i] = g_Reg->m_FPCR[i];
    }
    S.Hi = g_Reg->m_HI.UDW;
    S.Lo = g_Reg->m_LO.UDW;
    S.LLBit = g_Reg->m_LLBit;
    S.Stage = CN64SystemTestAccess::Pipeline(m_System);
    S.Jump = CN64SystemTestAccess::JumpTo(m_System);
    S.Data.assign(g_MMU->Rdram() + (DataBase & 0x1FFFFFFF), g_MMU->Rdram() + (DataBase & 0x1FFFFFFF) + DataSize);
    S.Stack.assign(g_MMU->Rdram() + (StackBase & 0x1FFFFFFF), g_MMU->Rdram() + (StackBase & 0x1FFFFFFF) + 0x1000);
}

void Fuzzer::Apply(const State & S)
{
    g_Reg->m_PROGRAM_COUNTER = S.Pc;
    for (int i = 0; i < 32; i++)
    {
        g_Reg->m_GPR[i].UDW = S.Gpr[i];
        g_Reg->m_FPR[i].UDW = S.Fpr[i];
        g_Reg->m_FPCR[i] = S.Fpcr[i];
    }
    g_Reg->m_GPR[0].UDW = 0;
    for (int i = 0; i < 32; i++)
    {
        if (i != 9 && i != 11)
        {
            g_Reg->m_CP0[i] = S.Cp0[i];
        }
    }
    g_Reg->m_HI.UDW = S.Hi;
    g_Reg->m_LO.UDW = S.Lo;
    g_Reg->m_LLBit = S.LLBit;
    memcpy(g_MMU->Rdram() + (DataBase & 0x1FFFFFFF), S.Data.data(), DataSize);
    memcpy(g_MMU->Rdram() + (StackBase & 0x1FFFFFFF), S.Stack.data(), 0x1000);
    CN64SystemTestAccess::SetPipeline(m_System, PIPELINE_STAGE_NORMAL, 0);
    // a break point of the recompiler ends the emulation: the test goes on
    CN64SystemTestAccess::ClearEnd(m_System);
    *g_NextTimer = 0x7FFFFFF0;
}

bool InRegion(uint64_t Pc)
{
    return (uint32_t)Pc >= ProgramBase && (uint32_t)Pc < ProgramBase + ProgramSize;
}

bool Fuzzer::RunInterpreter(const State & Initial, State & Result)
{
    Apply(Initial);
    R4300iOp & Ops = CN64SystemTestAccess::Interpreter(m_System);
    int Steps = 0;
    while (InRegion(g_Reg->m_PROGRAM_COUNTER) && Steps++ < 20000)
    {
        if (getenv("FUZZ_TRACE") != nullptr)
        {
            printf("  I %016llX stage=%d cause=%llX\n", (unsigned long long)g_Reg->m_PROGRAM_COUNTER, (int)CN64SystemTestAccess::Pipeline(m_System), (unsigned long long)g_Reg->m_CP0[13]);
        }
        Ops.ExecuteOps(g_GameSettings.countPerOp);
    }
    Capture(Result);
    // a program that does not end is not a test (the recompiler could not be stopped either)
    return Steps < 20000;
}

bool Fuzzer::RunRecompiler(const State & Initial, State & Result)
{
    Apply(Initial);
    const int DialogsBefore = g_Dialogs;
    g_LastBreakLine = 0;
    CRecompiler * Recompiler = CN64SystemTestAccess::Recompiler(m_System);
    // the stack pointer was set from outside the recompiled code
    Recompiler->ResetMemoryStackPos();
    int Steps = 0;
    while (InRegion(g_Reg->m_PROGRAM_COUNTER) && Steps++ < 20000)
    {
        *g_NextTimer = 0x7FFFFFF0;
        if (!CN64SystemTestAccess::CompileAndRun(*Recompiler))
        {
            return false;
        }
        if (CN64SystemTestAccess::Ended(m_System))
        {
            // a break point of the recompiler (its notification ends the emulation); its message
            // comes through the GUI thread
            for (int i = 0; i < 100 && g_Dialogs == DialogsBefore; i++)
            {
                QThread::msleep(20);
            }
            return false;
        }
    }
    Capture(Result);
    return true;
}

bool Fuzzer::Compare(const State & A, const State & B, std::string & D)
{
    char Line[200];
    bool Same = true;
    auto Diff = [&](const char * What, int Index, uint64_t X, uint64_t Y) {
        snprintf(Line, sizeof(Line), "  %s%s%d: interpreter %016llX recompiler %016llX\n", What, Index >= 0 ? " " : "", Index >= 0 ? Index : 0, (unsigned long long)X, (unsigned long long)Y);
        if (Index < 0)
        {
            snprintf(Line, sizeof(Line), "  %s: interpreter %016llX recompiler %016llX\n", What, (unsigned long long)X, (unsigned long long)Y);
        }
        D += Line;
        Same = false;
    };
    if (A.Pc != B.Pc)
    {
        Diff("PC", -1, A.Pc, B.Pc);
    }
    for (int i = 1; i < 32; i++)
    {
        if (A.Gpr[i] != B.Gpr[i])
        {
            Diff("GPR", i, A.Gpr[i], B.Gpr[i]);
        }
    }
    if (A.Hi != B.Hi)
    {
        Diff("HI", -1, A.Hi, B.Hi);
    }
    if (A.Lo != B.Lo)
    {
        Diff("LO", -1, A.Lo, B.Lo);
    }
    for (int i = 0; i < 32; i++)
    {
        if (A.Fpr[i] != B.Fpr[i])
        {
            Diff("FPR", i, A.Fpr[i], B.Fpr[i]);
        }
        // Count, Compare and Random are not compared (timers)
        if (i != 9 && i != 11 && i != 1 && A.Cp0[i] != B.Cp0[i])
        {
            Diff("CP0", i, A.Cp0[i], B.Cp0[i]);
        }
    }
    if (A.Fpcr[31] != B.Fpcr[31])
    {
        Diff("FPCR31", -1, A.Fpcr[31], B.Fpcr[31]);
    }
    if (A.LLBit != B.LLBit)
    {
        Diff("LLBit", -1, A.LLBit, B.LLBit);
    }
    if (A.Stage != B.Stage)
    {
        Diff("pipeline stage", -1, A.Stage, B.Stage);
    }
    if (A.Data != B.Data)
    {
        for (size_t i = 0; i < A.Data.size(); i++)
        {
            if (A.Data[i] != B.Data[i])
            {
                snprintf(Line, sizeof(Line), "  data +%04zX: interpreter %02X recompiler %02X\n", i, A.Data[i], B.Data[i]);
                D += Line;
                Same = false;
                break;
            }
        }
    }
    if (A.Stack != B.Stack)
    {
        for (size_t i = 0; i < A.Stack.size(); i++)
        {
            if (A.Stack[i] != B.Stack[i])
            {
                snprintf(Line, sizeof(Line), "  stack +%04zX: interpreter %02X recompiler %02X\n", i, A.Stack[i], B.Stack[i]);
                D += Line;
                Same = false;
                break;
            }
        }
    }
    return Same;
}

void Fuzzer::Print(const std::vector<uint32_t> & Program, uint32_t StartPc, const State & Initial)
{
    printf("  program at %08X:\n", StartPc);
    for (size_t i = 0; i < Program.size(); i++)
    {
        const uint32_t Pc = StartPc + (uint32_t)i * 4;
        if (Program[i] == 0 && !g_Options.Verbose)
        {
            continue;
        }
        printf("    %08X: %08X  %s\n", Pc, Program[i], R4300iInstruction((int32_t)Pc, Program[i]).NameAndParam().c_str());
    }
    printf("  initial:");
    for (int i = 1; i < 32; i++)
    {
        if (i % 4 == 1)
        {
            printf("\n   ");
        }
        printf(" r%-2d=%016llX", i, (unsigned long long)Initial.Gpr[i]);
    }
    printf("\n  HI=%016llX LO=%016llX FPCR31=%08X\n", (unsigned long long)Initial.Hi, (unsigned long long)Initial.Lo, Initial.Fpcr[31]);
    fflush(stdout);
}

void Fuzzer::WriteProgram(const std::vector<uint32_t> & Program, uint32_t StartPc)
{
    uint8_t * Rdram = g_MMU->Rdram();
    memset(Rdram + (ProgramBase & 0x1FFFFFFF), 0, ProgramSize);
    for (size_t i = 0; i < Program.size(); i++)
    {
        const uint32_t Phys = (StartPc + (uint32_t)i * 4) & 0x1FFFFFFF;
        // RDRAM is stored a word at a time in the byte order of the host
        *(uint32_t *)(Rdram + Phys) = Program[i];
    }
    CN64SystemTestAccess::ResetCode(*CN64SystemTestAccess::Recompiler(m_System));
    *g_NextTimer = 0x7FFFFFF0;
}

int Fuzzer::Check(const std::vector<uint32_t> & Program, uint32_t StartPc, const State & Initial, std::string * Differences, std::string * Signature)
{
    WriteProgram(Program, StartPc);
    State Expected, Actual;
    if (!RunInterpreter(Initial, Expected))
    {
        return 3;
    }
    const bool Compiled = RunRecompiler(Initial, Actual);
    if (!Compiled)
    {
        if (Signature != nullptr)
        {
            *Signature = "break point " + std::to_string((int)g_LastBreakFile) + ":" + std::to_string((int)g_LastBreakLine);
        }
        return 2;
    }
    std::string Local;
    const bool Same = Compare(Expected, Actual, Differences != nullptr ? *Differences : Local);
    if (!Same && Signature != nullptr)
    {
        // what differs first names the problem
        const std::string & D = Differences != nullptr ? *Differences : Local;
        *Signature = D.substr(0, D.find(':'));
    }
    return Same ? 0 : 1;
}

// The smallest program that still shows the problem: instructions are replaced by NOPs while it does
void Fuzzer::Shrink(std::vector<uint32_t> & Program, uint32_t StartPc, const State & Initial)
{
    if (getenv("FUZZ_NOSHRINK") != nullptr)
    {
        return;
    }
    std::string Signature;
    std::string Differences;
    const int Kind = Check(Program, StartPc, Initial, &Differences, &Signature);
    if (Kind == 0)
    {
        return;
    }
    for (bool Changed = true; Changed;)
    {
        Changed = false;
        for (size_t i = 0; i + 1 < Program.size(); i++)
        {
            if (Program[i] == 0)
            {
                continue;
            }
            const uint32_t Saved = Program[i];
            Program[i] = 0;
            std::string NewSignature, NewDifferences;
            if (Check(Program, StartPc, Initial, &NewDifferences, &NewSignature) == Kind && NewSignature == Signature)
            {
                Changed = true;
            }
            else
            {
                Program[i] = Saved;
            }
        }
    }
}

bool Fuzzer::Evaluate(const std::string & Name, std::vector<uint32_t> & Program, uint32_t StartPc, State & Initial, Counters & C)
{
    CRecompiler * Recompiler = CN64SystemTestAccess::Recompiler(m_System);
    WriteProgram(Program, StartPc);

    State Expected, Actual;
    if (!RunInterpreter(Initial, Expected))
    {
        return true;
    }
    const bool Compiled = RunRecompiler(Initial, Actual);
    C.Ran++;
    if (Expected.Cp0[13] & 0x7C)
    {
        C.Exceptions++;
    }
    if (g_Options.Verbose)
    {
        printf("%s: %zu instructions\n", Name.c_str(), Program.size());
        Print(Program, StartPc, Initial);
    }
    if (!Compiled)
    {
        const int Line = g_LastBreakLine + 100000 * (int)g_LastBreakFile;
        C.Breaks[Line]++;
        if (C.Breaks[Line] == 1)
        {
            printf("%s: the recompiler stopped at a break point, %s line %d\n", Name.c_str(), g_BreakFiles[(size_t)(int)g_LastBreakFile].c_str(), (int)g_LastBreakLine);
            Shrink(Program, StartPc, Initial);
            Print(Program, StartPc, Initial);
        }
        return true;
    }
    std::string Differences;
    if (!Compare(Expected, Actual, Differences))
    {
        C.Failures++;
        Shrink(Program, StartPc, Initial);
        Differences.clear();
        if (getenv("FUZZ_ASM") != nullptr)
        {
            // the code of the reduced program
            g_Settings->SaveString(Directory_Log, getenv("FUZZ_ASM"));
            CN64SystemTestAccess::StartAsmLog(*Recompiler);
        }
        Check(Program, StartPc, Initial, &Differences);
        printf("%s (seed %u) DIFFERS (reduced):\n%s", Name.c_str(), g_Options.Seed, Differences.c_str());
        Print(Program, StartPc, Initial);
        if (C.Failures >= g_Options.MaxFailures)
        {
            return false;
        }
    }
    return true;
}

// ---- programs for the problems found while the recompiler was ported ----
//
// The random programs reach these only by chance. Each family goes through all the combinations of
// its parameters, always with the same state, so a run of them is the same every time:
//   branches: every branch (normal, likely, linking, COP1) x edge values of the operands (in
//     registers that are known to the compiler - constants - or not) x the delay slot (nothing, an
//     effect that is seen afterwards, a change of the compared register, a store) x taken or not x
//     the place (also the last word of a page: the delay slot is on the next one) x the target
//     (over one instruction, or the delay slot itself)
//   jumps: J, JAL, JR, JALR with the target known or not, the link register, the same places
//   memory: every load and store x every alignment x edge values of the data and of the register
//   arithmetic: every operation on edge values (overflow, sign extension, division by zero or by -1)
//   loops: a counter loop closed by every kind of branch (section linking)

std::vector<uint64_t> Fuzzer::EdgeValues() const
{
    std::vector<uint64_t> V = {0, 1, ~0ull, 0x7FFFFFFFull, 0xFFFFFFFF80000000ull, 0x80000000ull, 0x100000000ull, 0x8000000000000000ull, 0x7FFFFFFFFFFFFFFFull, 2, 0xFFFFFFFFFFFFFFFEull};
    if (g_Options.Core32)
    {
        for (auto & X : V)
        {
            X = (uint64_t)(int64_t)(int32_t)X;
        }
        std::vector<uint64_t> U;
        for (uint64_t X : V)
        {
            bool Found = false;
            for (uint64_t Y : U)
            {
                Found = Found || Y == X;
            }
            if (!Found)
            {
                U.push_back(X);
            }
        }
        V = U;
    }
    return V;
}

// Rd = Value, with instructions that the compiler follows (it then knows the register)
void Fuzzer::LoadConstant(std::vector<uint32_t> & Program, int Rd, uint64_t Value)
{
    if (Value == (uint64_t)(int64_t)(int32_t)Value)
    {
        Program.push_back(IType(15, 0, Rd, (int)((Value >> 16) & 0xFFFF)));
        Program.push_back(IType(13, Rd, Rd, (int)(Value & 0xFFFF)));
        return;
    }
    const uint32_t Hi = (uint32_t)(Value >> 32), Lo = (uint32_t)Value;
    Program.push_back(IType(15, 0, Rd, (int)(Hi >> 16)));
    Program.push_back(IType(13, Rd, Rd, (int)(Hi & 0xFFFF)));
    Program.push_back(RType(60, 0, Rd, Rd, 0)); // DSLL32
    Program.push_back(IType(15, 0, 12, (int)(Lo >> 16)));
    Program.push_back(IType(13, 12, 12, (int)(Lo & 0xFFFF)));
    Program.push_back(RType(60, 0, 12, 12, 0)); // DSLL32
    Program.push_back(RType(62, 0, 12, 12, 0)); // DSRL32: the low half zero extended
    Program.push_back(RType(37, Rd, 12, Rd));   // OR
}

void Fuzzer::DirectedState(State & S, uint32_t StartPc)
{
    m_Rng.seed(0x5eed + (uint32_t)StartPc);
    GenerateState(S);
    S.Pc = (uint64_t)(int64_t)(int32_t)StartPc;
    for (int i = 8; i <= 12; i++)
    {
        S.Gpr[i] = 0;
    }
    S.Gpr[20] = 0;
    S.Gpr[21] = 0;
}

bool Fuzzer::DirectedBranches(Counters & C)
{
    struct BranchKind
    {
        const char * Name;
        int Op, Rt;
        bool TwoRegs, Cop1;
    };
    const BranchKind Kinds[] = {
        {"BEQ", 4, -1, true, false}, {"BNE", 5, -1, true, false}, {"BLEZ", 6, 0, false, false}, {"BGTZ", 7, 0, false, false},
        {"BLTZ", 1, 0, false, false}, {"BGEZ", 1, 1, false, false}, {"BLTZAL", 1, 16, false, false}, {"BGEZAL", 1, 17, false, false},
        {"BEQL", 20, -1, true, false}, {"BNEL", 21, -1, true, false}, {"BLEZL", 22, 0, false, false}, {"BGTZL", 23, 0, false, false},
        {"BLTZL", 1, 2, false, false}, {"BGEZL", 1, 3, false, false}, {"BGEZALL", 1, 19, false, false}, // BLTZALL (rt 18) is not an operation of the interpreter (upstream)
        {"BC1F", 17, 0, false, true}, {"BC1T", 17, 1, false, true}, {"BC1FL", 17, 2, false, true}, {"BC1TL", 17, 3, false, true},
    };
    const std::vector<uint64_t> Values = EdgeValues();
    // The delay slots: nothing, an effect seen at the end, a change of the compared register, a store, a load
    auto DelaySlot = [&](int Kind) -> uint32_t {
        switch (Kind)
        {
        case 0: return 0;
        case 1: return IType(13, 10, 10, 0x0100);  // ORI r10, r10, 0x100
        case 2: return IType(9, 8, 8, 1);          // ADDIU r8, r8, 1
        case 3: return IType(43, DataReg, 8, 0x40); // SW r8, 0x40(DataReg)
        default: return IType(35, DataReg, 11, 0x40); // LW r11, 0x40(DataReg)
        }
    };
    int Case = 0;
    for (const BranchKind & K : Kinds)
    {
        // operand setups: how many of the combinations of values
        std::vector<std::pair<uint64_t, uint64_t>> Operands;
        if (K.Cop1)
        {
            Operands = {{0, 0}, {1, 0}};
        }
        else if (K.TwoRegs)
        {
            for (uint64_t A : Values)
            {
                for (uint64_t B : Values)
                {
                    Operands.push_back({A, B});
                }
            }
        }
        else
        {
            for (uint64_t A : Values)
            {
                Operands.push_back({A, 0});
            }
        }
        for (const auto & Op : Operands)
        {
            // 0: both registers unknown to the compiler, 1: known (constants), 2: the first known, 3: the second known, 4: one register
            const int Setups = K.Cop1 ? 1 : (K.TwoRegs ? 5 : 2);
            for (int Setup = 0; Setup < Setups; Setup++)
            {
                for (int Delay = 0; Delay < 5; Delay++)
                {
                    for (int Place = 0; Place < 3; Place++)
                    {
                        for (int Target = 0; Target < 2; Target++)
                        {
                            if (Setup == 4 && Op.first != Op.second)
                            {
                                // one register: the value is the first
                            }
                            std::vector<uint32_t> Prefix;
                            if (Setup == 1 || Setup == 2)
                            {
                                LoadConstant(Prefix, 8, Op.first);
                            }
                            if (Setup == 1 || Setup == 3)
                            {
                                LoadConstant(Prefix, 9, Op.second);
                            }
                            const int Rs = 8;
                            const int Rt = Setup == 4 ? 8 : 9;
                            const int Offset = Target == 0 ? 2 : 0; // over one instruction / the delay slot
                            uint32_t Branch;
                            if (K.Cop1)
                            {
                                Branch = (17u << 26) | (8u << 21) | ((uint32_t)K.Rt << 16) | (uint32_t)Offset;
                            }
                            else if (K.TwoRegs)
                            {
                                Branch = IType(K.Op, Rs, Rt, Offset);
                            }
                            else
                            {
                                Branch = IType(K.Op, Rs, K.Rt, Offset);
                            }
                            std::vector<uint32_t> Program = Prefix;
                            Program.push_back(Branch);
                            Program.push_back(DelaySlot(Delay));
                            Program.push_back(IType(13, 20, 20, 1)); // the way that does not branch
                            Program.push_back(IType(13, 21, 21, 2)); // the target
                            Program.push_back(IType(13, 22, 22, 4)); // after both
                            Program.push_back(RType(12, 0, 0, 0));
                            // the place: Place 0 anywhere, 1 the branch is the last word of a page, 2 the one before it
                            const uint32_t BranchIndex = (uint32_t)Prefix.size();
                            const uint32_t Wanted = Place == 0 ? 0x100 + 4 * BranchIndex : (Place == 1 ? 0xFFC : 0xFF8);
                            const uint32_t StartPc = ProgramBase + Wanted - 4 * BranchIndex;
                            State Initial;
                            DirectedState(Initial, StartPc + (uint32_t)Case);
                            Initial.Pc = (uint64_t)(int64_t)(int32_t)StartPc;
                            Initial.Gpr[8] = Op.first;
                            Initial.Gpr[9] = Setup == 4 ? Op.first : Op.second;
                            if (K.Cop1)
                            {
                                Initial.Fpcr[31] = Op.first != 0 ? 0x800000 : 0;
                            }
                            Case++;
                            char Name[120];
                            snprintf(Name, sizeof(Name), "directed branch %s a=%llX b=%llX setup=%d delay=%d place=%d target=%d", K.Name, (unsigned long long)Op.first, (unsigned long long)Op.second, Setup, Delay, Place, Target);
                            if (!Evaluate(Name, Program, StartPc, Initial, C))
                            {
                                return false;
                            }
                        }
                    }
                }
            }
        }
    }
    return true;
}

bool Fuzzer::DirectedJumps(Counters & C)
{
    int Case = 0;
    // 0 J, 1 JAL, 2 JR, 3 JALR rd=ra, 4 JALR rd=other, 5 JALR rd=rs
    for (int Kind = 0; Kind < 6; Kind++)
    {
        for (int Known = 0; Known < 2; Known++)
        {
            for (int Delay = 0; Delay < 4; Delay++)
            {
                for (int Place = 0; Place < 3; Place++)
                {
                    for (int Skip = 0; Skip < 3; Skip++)
                    {
                        std::vector<uint32_t> Prefix;
                        // the position of the jump is known only after the prefix: the target is calculated for it
                        const uint32_t PrefixSize = (Kind >= 2 && Known) ? 2 : 0;
                        const uint32_t BranchIndex = PrefixSize;
                        const uint32_t Wanted = Place == 0 ? 0x100 + 4 * BranchIndex : (Place == 1 ? 0xFFC : 0xFF8);
                        const uint32_t StartPc = ProgramBase + Wanted - 4 * BranchIndex;
                        const uint32_t JumpPc = StartPc + 4 * BranchIndex;
                        const uint32_t Target = JumpPc + 8 + 4 * (uint32_t)Skip;
                        if (Kind >= 2 && Known)
                        {
                            LoadConstant(Prefix, 8, (uint64_t)(int64_t)(int32_t)Target);
                        }
                        std::vector<uint32_t> Program = Prefix;
                        switch (Kind)
                        {
                        case 0: Program.push_back((2u << 26) | ((Target >> 2) & 0x3FFFFFF)); break;
                        case 1: Program.push_back((3u << 26) | ((Target >> 2) & 0x3FFFFFF)); break;
                        case 2: Program.push_back(RType(8, 8, 0, 0)); break;
                        case 3: Program.push_back(RType(9, 8, 0, 31)); break;
                        case 4: Program.push_back(RType(9, 8, 0, 14)); break;
                        default: Program.push_back(RType(9, 8, 0, 8)); break;
                        }
                        switch (Delay)
                        {
                        case 0: Program.push_back(0); break;
                        case 1: Program.push_back(IType(13, 10, 10, 0x0100)); break;
                        case 2: Program.push_back(IType(9, 8, 8, 4)); break; // changes the register of the jump
                        default: Program.push_back(IType(9, 31, 11, 0)); break; // reads the link register
                        }
                        for (int i = 0; i < Skip; i++)
                        {
                            Program.push_back(IType(13, 20, 20, (int)(1u << i))); // skipped
                        }
                        Program.push_back(IType(13, 21, 21, 2));
                        Program.push_back(RType(12, 0, 0, 0));
                        State Initial;
                        DirectedState(Initial, StartPc + (uint32_t)Case);
                        Initial.Pc = (uint64_t)(int64_t)(int32_t)StartPc;
                        Initial.Gpr[8] = (uint64_t)(int64_t)(int32_t)Target;
                        Initial.Gpr[31] = 0x123456789ull;
                        if (g_Options.Core32)
                        {
                            Initial.Gpr[31] = (uint64_t)(int64_t)(int32_t)0x23456789;
                        }
                        Case++;
                        char Name[120];
                        snprintf(Name, sizeof(Name), "directed jump kind=%d known=%d delay=%d place=%d skip=%d", Kind, Known, Delay, Place, Skip);
                        if (!Evaluate(Name, Program, StartPc, Initial, C))
                        {
                            return false;
                        }
                    }
                }
            }
        }
    }
    return true;
}

bool Fuzzer::DirectedMemory(Counters & C)
{
    // Op, name, store?
    // (LLD and SCD, ops 52 and 60, are not operations of the interpreter: upstream)
    const int Ops[] = {32, 36, 33, 37, 35, 39, 55, 34, 38, 26, 27, 40, 41, 43, 63, 42, 46, 44, 45, 48, 56};
    const uint64_t Values[] = {0, ~0ull, 0x8000000080808080ull, 0x7F7F7F7F7F7F7F7Full, 0x0123456789ABCDEFull};
    int Case = 0;
    for (int Op : Ops)
    {
        for (int Offset = 0; Offset < 16; Offset++)
        {
            for (int Fill = 0; Fill < 4; Fill++)
            {
                for (uint64_t Value : Values)
                {
                    for (int Base = 0; Base < 2; Base++)
                    {
                        const uint32_t Word = IType(Op, DataReg, 9, 0x100 + Offset);
                        if (!Allowed(Word))
                        {
                            continue;
                        }
                        std::vector<uint32_t> Program;
                        if (Base == 1)
                        {
                            // the base register is a constant of the compiler
                            LoadConstant(Program, DataReg, (uint64_t)(int64_t)(int32_t)DataPointer);
                        }
                        Program.push_back(Word);
                        Program.push_back(IType(9, 9, 11, 0)); // ADDIU r11, r9, 0: what the register has now
                        Program.push_back(RType(12, 0, 0, 0));
                        const uint32_t StartPc = ProgramBase + 0x200;
                        State Initial;
                        DirectedState(Initial, StartPc + (uint32_t)Case);
                        Initial.Pc = (uint64_t)(int64_t)(int32_t)StartPc;
                        Initial.Gpr[9] = g_Options.Core32 ? (uint64_t)(int64_t)(int32_t)Value : Value;
                        for (size_t i = 0; i < Initial.Data.size(); i++)
                        {
                            if (Fill == 0)
                            {
                                Initial.Data[i] = 0x80;
                            }
                            else if (Fill == 1)
                            {
                                Initial.Data[i] = 0x7F;
                            }
                            else if (Fill == 2)
                            {
                                Initial.Data[i] = 0xFF;
                            } // 3: the random data
                        }
                        Case++;
                        char Name[120];
                        snprintf(Name, sizeof(Name), "directed memory op=%d offset=%d fill=%d value=%llX base=%d", Op, Offset, Fill, (unsigned long long)Value, Base);
                        if (!Evaluate(Name, Program, StartPc, Initial, C))
                        {
                            return false;
                        }
                    }
                }
            }
        }
    }
    return true;
}

bool Fuzzer::DirectedArithmetic(Counters & C)
{
    const std::vector<uint64_t> Values = EdgeValues();
    // R type: ADD ADDU SUB SUBU AND OR XOR NOR SLT SLTU DADD DADDU DSUB DSUBU SLLV SRLV SRAV DSLLV DSRLV DSRAV
    // MULT MULTU DIV DIVU DMULT DMULTU DDIV DDIVU
    const int Functs[] = {32, 33, 34, 35, 36, 37, 38, 39, 42, 43, 44, 45, 46, 47, 4, 6, 7, 20, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
    int Case = 0;
    for (int Funct : Functs)
    {
        for (uint64_t A : Values)
        {
            for (uint64_t B : Values)
            {
                for (int Known = 0; Known < 3; Known++)
                {
                    const uint32_t Word = RType(Funct, 8, 9, 10);
                    if (!Allowed(Word))
                    {
                        continue;
                    }
                    std::vector<uint32_t> Program;
                    if (Known == 1 || Known == 2)
                    {
                        LoadConstant(Program, 8, A);
                    }
                    if (Known == 2)
                    {
                        LoadConstant(Program, 9, B);
                    }
                    Program.push_back(Word);
                    Program.push_back(RType(16, 0, 0, 11)); // MFHI r11
                    Program.push_back(RType(18, 0, 0, 12)); // MFLO r12
                    Program.push_back(RType(12, 0, 0, 0));
                    const uint32_t StartPc = ProgramBase + 0x300;
                    State Initial;
                    DirectedState(Initial, StartPc + (uint32_t)Case);
                    Initial.Pc = (uint64_t)(int64_t)(int32_t)StartPc;
                    Initial.Gpr[8] = A;
                    Initial.Gpr[9] = B;
                    Case++;
                    char Name[120];
                    snprintf(Name, sizeof(Name), "directed arithmetic funct=%d a=%llX b=%llX known=%d", Funct, (unsigned long long)A, (unsigned long long)B, Known);
                    if (!Evaluate(Name, Program, StartPc, Initial, C))
                    {
                        return false;
                    }
                }
            }
        }
    }
    // I type: ADDI ADDIU SLTI SLTIU ANDI ORI XORI DADDI DADDIU with edge immediates
    const int Ops[] = {8, 9, 10, 11, 12, 13, 14, 24, 25};
    const int Immediates[] = {0, 1, -1, 0x7FFF, -0x8000, 0x8000 - 1, 0xFFFF};
    for (int Op : Ops)
    {
        for (uint64_t A : Values)
        {
            for (int Imm : Immediates)
            {
                for (int Known = 0; Known < 2; Known++)
                {
                    const uint32_t Word = IType(Op, 8, 10, Imm);
                    if (!Allowed(Word))
                    {
                        continue;
                    }
                    std::vector<uint32_t> Program;
                    if (Known)
                    {
                        LoadConstant(Program, 8, A);
                    }
                    Program.push_back(Word);
                    Program.push_back(RType(12, 0, 0, 0));
                    const uint32_t StartPc = ProgramBase + 0x300;
                    State Initial;
                    DirectedState(Initial, StartPc + (uint32_t)Case);
                    Initial.Pc = (uint64_t)(int64_t)(int32_t)StartPc;
                    Initial.Gpr[8] = A;
                    Case++;
                    char Name[120];
                    snprintf(Name, sizeof(Name), "directed immediate op=%d a=%llX imm=%d known=%d", Op, (unsigned long long)A, Imm, Known);
                    if (!Evaluate(Name, Program, StartPc, Initial, C))
                    {
                        return false;
                    }
                }
            }
        }
    }
    // shifts by a constant: SLL SRL SRA DSLL DSRL DSRA DSLL32 DSRL32 DSRA32
    const int Shifts[] = {0, 2, 3, 56, 58, 59, 60, 62, 63};
    for (int Funct : Shifts)
    {
        for (uint64_t A : Values)
        {
            for (int Sa : {0, 1, 7, 31})
            {
                const uint32_t Word = RType(Funct, 0, 8, 10, Sa);
                if (!Allowed(Word))
                {
                    continue;
                }
                std::vector<uint32_t> Program = {Word, RType(12, 0, 0, 0)};
                const uint32_t StartPc = ProgramBase + 0x300;
                State Initial;
                DirectedState(Initial, StartPc + (uint32_t)Case);
                Initial.Pc = (uint64_t)(int64_t)(int32_t)StartPc;
                Initial.Gpr[8] = A;
                Case++;
                char Name[120];
                snprintf(Name, sizeof(Name), "directed shift funct=%d a=%llX sa=%d", Funct, (unsigned long long)A, Sa);
                if (!Evaluate(Name, Program, StartPc, Initial, C))
                {
                    return false;
                }
            }
        }
    }
    return true;
}

// A counter loop: the branch that closes it (also a likely one, which skips its delay slot at the
// end), the delay slot, the places: the sections are linked into a loop
bool Fuzzer::DirectedLoops(Counters & C)
{
    int Case = 0;
    const int Closers[] = {5, 21, 7, 23, 1 /*BGEZ... see below*/};
    for (int Closer : Closers)
    {
        for (int Count = 1; Count <= 4; Count++)
        {
            for (int Delay = 0; Delay < 3; Delay++)
            {
                for (int Known = 0; Known < 2; Known++)
                {
                    for (int Body = 0; Body < 3; Body++)
                    {
                        std::vector<uint32_t> Program;
                        if (Known)
                        {
                            LoadConstant(Program, 8, (uint64_t)Count);
                        }
                        const uint32_t Top = (uint32_t)Program.size();
                        for (int i = 0; i < Body; i++)
                        {
                            Program.push_back(IType(13, 10, 10, 1 << i));
                        }
                        Program.push_back(IType(9, 11, 11, 1)); // r11 counts the passes
                        Program.push_back(IType(9, 8, 8, -1));  // r8--
                        const uint32_t BranchIndex = (uint32_t)Program.size();
                        const int Offset = (int)Top - (int)BranchIndex - 1;
                        if (Closer == 1)
                        {
                            Program.push_back(IType(1, 8, 1, Offset)); // BGEZ r8: runs one more than the count
                        }
                        else if (Closer == 7 || Closer == 23)
                        {
                            Program.push_back(IType(Closer, 8, 0, Offset)); // BGTZ(L)
                        }
                        else
                        {
                            Program.push_back(IType(Closer, 8, 0, Offset)); // BNE(L) r8, r0
                        }
                        Program.push_back(Delay == 0 ? 0 : (Delay == 1 ? IType(13, 12, 12, 0x10) : IType(9, 12, 12, 1)));
                        Program.push_back(IType(13, 21, 21, 2));
                        Program.push_back(RType(12, 0, 0, 0));
                        const uint32_t StartPc = ProgramBase + 0x400 + 0x1000 * (uint32_t)(Case % 2) - 4 * (uint32_t)(Case % 3);
                        State Initial;
                        DirectedState(Initial, StartPc + (uint32_t)Case);
                        Initial.Pc = (uint64_t)(int64_t)(int32_t)StartPc;
                        Initial.Gpr[8] = (uint64_t)Count;
                        Case++;
                        char Name[120];
                        snprintf(Name, sizeof(Name), "directed loop closer=%d count=%d delay=%d known=%d body=%d", Closer, Count, Delay, Known, Body);
                        if (!Evaluate(Name, Program, StartPc, Initial, C))
                        {
                            return false;
                        }
                    }
                }
            }
        }
    }
    return true;
}

// SUBU against results written down here, not against the other implementation: the low 32 bits of
// the operands are subtracted with wraparound and the result is sign extended to 64 bits (the high
// halves of the operands do not matter). Both the interpreter and the recompiler must give them.
bool Fuzzer::DirectedSubu(Counters & C)
{
    struct SubuCase
    {
        uint64_t A, B, Result;
    };
    static const SubuCase Cases[] = {
        {0, 0, 0},
        {5, 3, 2},
        {3, 5, 0xFFFFFFFFFFFFFFFEull},
        {0, 1, 0xFFFFFFFFFFFFFFFFull},
        {0x7FFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF80000000ull},                  // INT_MAX - (-1) wraps to INT_MIN
        {0xFFFFFFFF80000000ull, 1, 0x000000007FFFFFFFull},               // INT_MIN - 1 wraps to INT_MAX
        {0x80000000, 0x7FFFFFFF, 1},                                     // INT_MIN - INT_MAX
        {0x7FFFFFFF, 0x80000000, 0xFFFFFFFFFFFFFFFFull},                 // INT_MAX - INT_MIN
        {0x80000000, 0x80000000, 0},
        {0xFFFFFFFF, 0xFFFFFFFF, 0},
        {0, 0x80000000, 0xFFFFFFFF80000000ull},                          // 0 - INT_MIN stays INT_MIN
        {0x100000000ull, 1, 0xFFFFFFFFFFFFFFFFull},                      // the high halves are ignored
        {0xDEADBEEF00000005ull, 0x1234567800000003ull, 2},
        {0x0000000100000000ull, 0xFFFFFFFF00000000ull, 0},
        {0x8000000000000000ull, 0x7FFFFFFFFFFFFFFFull, 1},
    };
    int Case = 0;
    for (const SubuCase & K : Cases)
    {
        for (int Known = 0; Known < 3; Known++)
        {
            std::vector<uint32_t> Program;
            if (Known == 1 || Known == 2)
            {
                LoadConstant(Program, 8, K.A);
            }
            if (Known == 2)
            {
                LoadConstant(Program, 9, K.B);
            }
            Program.push_back(RType(35, 8, 9, 10)); // SUBU r10, r8, r9
            Program.push_back(RType(12, 0, 0, 0));
            const uint32_t StartPc = ProgramBase + 0x300;
            State Initial;
            DirectedState(Initial, StartPc + (uint32_t)Case);
            Initial.Pc = (uint64_t)(int64_t)(int32_t)StartPc;
            Initial.Gpr[8] = K.A;
            Initial.Gpr[9] = K.B;
            Initial.Gpr[10] = 0x5555555555555555ull;
            Case++;
            WriteProgram(Program, StartPc);
            State Expected, Actual;
            const bool Interpreted = RunInterpreter(Initial, Expected);
            const bool Compiled = RunRecompiler(Initial, Actual);
            C.Ran++;
            if (!Interpreted || !Compiled || Expected.Gpr[10] != K.Result || Actual.Gpr[10] != K.Result)
            {
                printf("FAIL directed SUBU %016llX - %016llX known=%d: expected %016llX, interpreter %016llX%s, recompiler %016llX%s\n", (unsigned long long)K.A,
                       (unsigned long long)K.B, Known, (unsigned long long)K.Result, (unsigned long long)Expected.Gpr[10], Interpreted ? "" : " (not run)",
                       (unsigned long long)Actual.Gpr[10], Compiled ? "" : " (stopped at a break point)");
                C.Failures++;
                return false;
            }
        }
    }
    printf("directed SUBU: %zu cases x 3 give the MIPS results in the interpreter and in the recompiler\n", sizeof(Cases) / sizeof(Cases[0]));
    return true;
}

bool Fuzzer::Directed(Counters & C)
{
    return DirectedBranches(C) && DirectedJumps(C) && DirectedLoops(C) && DirectedMemory(C) && DirectedArithmetic(C) && DirectedSubu(C);
}

bool Fuzzer::Run()
{
    m_Classes = g_Options.Classes;
    // a 32 bit core: registers hold sign extended 32 bit values and the 64 bit operations do not occur
    g_Options.Core32 = g_GameSettings.core32Bit;
    if (g_Options.NoFastSp)
    {
        g_GameSettings.fastSP = false;
    }
    if (g_Options.NoCache)
    {
        g_GameSettings.regCaching = false;
    }
    if (g_Options.Link >= 0)
    {
        g_GameSettings.blockLinkingMode = (BLOCK_LINKING_MODE)g_Options.Link;
    }
    if (g_Options.Core64)
    {
        g_GameSettings.core32Bit = false;
        CN64SystemTestAccess::BuildInterpreter(m_System, false);
    }
    if (g_Options.ForceCore32)
    {
        g_GameSettings.core32Bit = true;
        CN64SystemTestAccess::BuildInterpreter(m_System, true);
    }
    g_Options.Core32 = g_GameSettings.core32Bit;
    printf("jit-fuzz: core32Bit=%d fastSP=%d regCaching=%d linking=%d countPerOp=%u cpu=%d\n", (int)g_GameSettings.core32Bit, (int)g_GameSettings.fastSP, (int)g_GameSettings.regCaching, (int)g_GameSettings.blockLinkingMode, g_GameSettings.countPerOp, (int)g_Settings->LoadDword(Game_CpuType));
    fflush(stdout);
    CRecompiler * Recompiler = CN64SystemTestAccess::Recompiler(m_System);
    if (Recompiler == nullptr)
    {
        printf("FAIL: the system has no recompiler\n");
        return false;
    }
    // the code of the programs is read from RDRAM: clear the area first
    memset(g_MMU->Rdram() + (ProgramBase & 0x1FFFFFFF), 0, ProgramSize);

    Counters C;
    if (!g_Options.NoDirected && g_Options.Only < 0)
    {
        Directed(C);
        printf("jit-fuzz directed: %d programs, %d different, %d stopped at a break point\n", C.Ran, C.Failures, [&]() { int T = 0; for (const auto & B : C.Breaks) { T += B.second; } return T; }());
        fflush(stdout);
    }
    if (g_Options.DirectedOnly)
    {
        int T = 0;
        for (const auto & B : C.Breaks)
        {
            T += B.second;
        }
        return C.Failures == 0 && T == 0;
    }
    const int First = g_Options.Only >= 0 ? g_Options.Only : 0;
    const int Last = g_Options.Only >= 0 ? g_Options.Only + 1 : g_Options.Programs;
    for (int Index = First; Index < Last; Index++)
    {
        m_Rng.seed(g_Options.Seed * 1000003u + (uint32_t)Index);
        std::vector<uint32_t> Program;
        uint32_t StartPc;
        Generate(Program, StartPc);
        State Initial;
        GenerateState(Initial);
        Initial.Pc = (uint64_t)(int64_t)(int32_t)StartPc;
        if (!Evaluate("program " + std::to_string(Index), Program, StartPc, Initial, C))
        {
            break;
        }
    }
    int Failures = C.Failures, Ran = C.Ran, Exceptions = C.Exceptions;
    std::map<int, int> & Breaks = C.Breaks;
    int BreakTotal = 0;
    for (const auto & B : Breaks)
    {
        BreakTotal += B.second;
        printf("break point at %s line %d: %d programs\n", g_BreakFiles[(size_t)(B.first / 100000)].c_str(), B.first % 100000, B.second);
    }
    printf("jit-fuzz: %d programs, %d ended in an exception, %d different, %d stopped at a break point\n", Ran, Exceptions, Failures, BreakTotal);
    fflush(stdout);
    return Failures == 0 && BreakTotal == 0;
}

void FuzzHook(CN64System & System)
{
    Fuzzer F(System);
    const bool Ok = F.Run();
    g_Result = Ok ? 0 : 1;
    g_Finished = true;
}
} // namespace

int main(int argc, char ** argv)
{
    if (argc < 2)
    {
        printf("usage: %s <rom> [programs] [seed] [nofastsp|nocache|core32|core64|link=N|nodirected|directed|verbose|only=N|classes=AMFBHT]\n", argv[0]);
        return 2;
    }
    const QString Rom = QString::fromLocal8Bit(argv[1]);
    int Positional = 0;
    for (int i = 2; i < argc; i++)
    {
        const std::string Arg = argv[i];
        if (Arg == "nofastsp")
        {
            g_Options.NoFastSp = true;
        }
        else if (Arg == "nocache")
        {
            g_Options.NoCache = true;
        }
        else if (Arg == "verbose")
        {
            g_Options.Verbose = true;
        }
        else if (Arg == "core64")
        {
            g_Options.Core64 = true;
        }
        else if (Arg == "core32")
        {
            g_Options.ForceCore32 = true;
        }
        else if (Arg == "nodirected")
        {
            g_Options.NoDirected = true;
        }
        else if (Arg == "directed")
        {
            g_Options.DirectedOnly = true;
        }
        else if (Arg.rfind("link=", 0) == 0)
        {
            g_Options.Link = atoi(Arg.c_str() + 5);
        }
        else if (Arg.rfind("fail=", 0) == 0)
        {
            g_Options.MaxFailures = atoi(Arg.c_str() + 5);
        }
        else if (Arg.rfind("only=", 0) == 0)
        {
            g_Options.Only = atoi(Arg.c_str() + 5);
        }
        else if (Arg.rfind("classes=", 0) == 0)
        {
            g_Options.Classes = Arg.substr(8);
        }
        else if (Positional == 0)
        {
            g_Options.Programs = atoi(Arg.c_str());
            Positional++;
        }
        else
        {
            g_Options.Seed = (uint32_t)strtoul(Arg.c_str(), nullptr, 10);
        }
    }
    setenv("SDL_VIDEODRIVER", "x11", 0);
    QApplication App(argc, argv);
    MainWindow Window;
    QTimer DismissTimer;
    QObject::connect(&DismissTimer, &QTimer::timeout, &DismissDialogs);
    DismissTimer.start(20);
    const std::string Base = CPath(CPath::MODULE_DIRECTORY).GetDirectory();
    char * Args[] = {argv[0], nullptr};
    if (!AppInit(Window.GetNotify(), Base.c_str(), 1, Args))
    {
        return 2;
    }
    UISettingsSaveBool(Setting_AutoSleep, false);
    g_Settings->SaveBool(Setting_ForceInterpreterCPU, false);
    g_Settings->SaveDword(Game_CpuType, CPU_Recompiler);
    Window.CreateLanguageMenus();
    Window.InitPlugins();
    Window.show();

    CN64System::m_CpuTestHook = &FuzzHook;
    Window.OpenImage(Rom);
    QElapsedTimer Timer;
    Timer.start();
    while (!g_Finished && Timer.elapsed() < 3600 * 1000)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    CN64System::m_CpuTestHook = nullptr;
    Window.close();
    QApplication::processEvents(QEventLoop::AllEvents, 200);
    AppCleanup();
    printf(g_Result == 0 && g_Finished ? "PASS\n" : "FAILED\n");
    return g_Result == 0 && g_Finished ? 0 : 1;
}
