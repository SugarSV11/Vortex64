#include "stdafx.h"
#if defined(__amd64__) || defined(_M_X64)

#include <Vortex64-core/Debugger.h>
#include <Vortex64-core/N64System/Interpreter/InterpreterOps.h>
#include <Vortex64-core/N64System/Recompiler/SectionInfo.h>
#include <Vortex64-core/N64System/N64System.h>
#include <Vortex64-core/N64System/Recompiler/CodeBlock.h>
#include <Vortex64-core/N64System/Recompiler/CodeSection.h>
#include <Vortex64-core/N64System/Recompiler/Recompiler.h>
#include <Vortex64-core/N64System/Recompiler/x64-86/x64RecompilerOps.h>
#include <Vortex64-core/N64System/SystemGlobals.h>
uint32_t CX64RecompilerOps::m_TempValue32 = 0;
uint64_t CX64RecompilerOps::m_TempValue64 = 0;
uint64_t CX64RecompilerOps::m_BranchTarget = 0;

CX64RecompilerOps::CX64RecompilerOps(CN64System & System, CCodeBlock & CodeBlock) :
    CRecompilerOpsBase(System, CodeBlock),
    m_Recompiler(System.m_Recomp),
    m_Rom(*g_Rom),
    m_Assembler(CodeBlock),
    m_RegWorkingSet(CodeBlock, m_Assembler),
    m_MMU(System.m_MMU_VM),
    m_RegBeforeDelay(CodeBlock, m_Assembler),
    m_PipelineStage(PIPELINE_STAGE_NORMAL),
    m_EffectDelaySlot(false),
    m_PreStoredTarget(false),
    m_CompilePC(m_Instruction.Address32()),
    m_ColdEntryOffset(0),
    m_WarmEntryOffset(0),
    m_ExitLabelCount(0)
{
}

CX64RecompilerOps::~CX64RecompilerOps()
{
}

void CX64RecompilerOps::Compile_TrapCompare(RecompilerTrapCompare /*CompareType*/)
{
    // TEQ, TNE, TGE, TGEU, TLT, TLTU and the immediate forms: the interpreter compares and raises the trap
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::CompileInterpreterOp(int32_t DestReg)
{
    m_CodeBlock.Log("      // interpreter: %s", m_Instruction.NameAndParam().c_str());
    const bool InDelaySlot = m_PipelineStage != PIPELINE_STAGE_NORMAL;

    // The interpreter reads and writes the registers in memory, and the cached copies are
    // dropped (what is known about a register is not known after the operation)
    m_RegWorkingSet.WriteBackRegisters();
    m_Assembler.MoveConstToVariable(&g_Reg->m_PROGRAM_COUNTER, "PROGRAM_COUNTER", m_CompilePC);
    m_Assembler.MoveConstToVariable(&g_System->m_OpCodes.m_Opcode.Value, "R4300iOp::m_Opcode.Value", m_Opcode.Value);
    if (InDelaySlot)
    {
        m_Assembler.MoveConstToVariable(&g_System->m_PipelineStage, "g_System->m_PipelineStage", PIPELINE_STAGE_JUMP);
    }
    // An exception sets m_JumpToLocation to its vector (never 0), nothing else the opcodes do touches it
    m_Assembler.MoveConstToVariable(&g_System->m_JumpToLocation, "System->m_JumpToLocation", 0);

    m_RegWorkingSet.BeforeCallDirect();
    m_Assembler.sub(asmjit::x86::rsp, 32);
    m_Assembler.CallThis(&g_System->m_OpCodes, MemberFuncAddress(&R4300iOp::ExecuteFallbackOpcode), "R4300iOp::ExecuteFallbackOpcode");
    m_Assembler.add(asmjit::x86::rsp, 32);
    m_RegWorkingSet.AfterCallDirect();

    m_Assembler.CmpConstToVariable(&g_System->m_JumpToLocation, "System->m_JumpToLocation", 0);
    CompileExit(m_CompilePC, (uint32_t)-1, m_RegWorkingSet.WithAddedCycles(g_GameSettings.countPerOp), ExitReason_Exception, &CX64Ops::JneLabel);
    if (InDelaySlot)
    {
        m_Assembler.MoveConstToVariable(&g_System->m_PipelineStage, "g_System->m_PipelineStage", PIPELINE_STAGE_NORMAL);
    }
    if (g_GameSettings.fastSP && DestReg == 29)
    {
        ResetMemoryStack();
    }
}

// ---- the integer operations of a 32 bit core ----
// Every register holds a sign extended 32 bit value (written back that way), so the operations
// work on the low 32 bits. A 64 bit core is left to the interpreter's implementation for now.

// Rd = Rs <Op> Rt, or Rs <Op> an immediate. Op: 0 add, 1 sub, 2 and, 3 or, 4 xor
void CX64RecompilerOps::CompileBinary32(int32_t Op, int32_t Rd, int32_t Rs, int32_t Rt, bool UseImm, uint32_t Imm)
{
    if (Rd == 0)
    {
        return;
    }
    uint32_t A = 0, B = 0;
    bool AConst = Rs == 0 || m_RegWorkingSet.IsConst(Rs);
    bool BConst = UseImm || Rt == 0 || m_RegWorkingSet.IsConst(Rt);
    if (AConst)
    {
        A = Rs == 0 ? 0 : m_RegWorkingSet.GetMipsRegLo(Rs);
    }
    if (BConst)
    {
        B = UseImm ? Imm : (Rt == 0 ? 0 : m_RegWorkingSet.GetMipsRegLo(Rt));
    }
    if (AConst && BConst)
    {
        uint32_t Result = 0;
        switch (Op)
        {
        case 0: Result = A + B; break;
        case 1: Result = A - B; break;
        case 2: Result = A & B; break;
        case 3: Result = A | B; break;
        default: Result = A ^ B; break;
        }
        if (m_RegWorkingSet.IsMapped(Rd))
        {
            m_RegWorkingSet.UnMap_GPR(Rd, false);
        }
        m_RegWorkingSet.SetMipsRegLo(Rd, Result);
        m_RegWorkingSet.SetMipsRegState(Rd, CRegBase::STATE_CONST_32_SIGN);
        return;
    }

    // the operations but subtraction do not care about the order of the operands: the destination
    // is the first one when it is also an operand
    if (Op != 1 && !UseImm && Rd == Rt && Rd != Rs)
    {
        std::swap(Rs, Rt);
        std::swap(AConst, BConst);
        std::swap(A, B);
    }
    const bool BMapped = !UseImm && !BConst && m_RegWorkingSet.IsMapped(Rt);
    if (BMapped)
    {
        m_RegWorkingSet.ProtectGPR(Rt);
    }
    const bool NeedTemp = !UseImm && Rt == Rd && Rd != Rs;
    asmjit::x86::Gp Target;
    asmjit::x86::Gp Temp;
    if (NeedTemp)
    {
        // Rd = Rs - Rd: the result is made apart and then moved into the register of Rd
        Temp = m_RegWorkingSet.Map_TempReg(asmjit::x86::Gpd(), Rs);
        Target = Temp;
    }
    else
    {
        m_RegWorkingSet.Map_GPR_32bit(Rd, true, Rs);
        Target = m_RegWorkingSet.GetMipsRegMap(Rd).r32();
    }

    if (BConst)
    {
        switch (Op)
        {
        case 0: m_Assembler.add(Target.r32(), B); break;
        case 1: m_Assembler.sub(Target.r32(), B); break;
        case 2: m_Assembler.and_(Target.r32(), B); break;
        case 3: m_Assembler.or_(Target.r32(), B); break;
        default: m_Assembler.xor_(Target.r32(), B); break;
        }
    }
    else if (m_RegWorkingSet.IsMapped(Rt) || (NeedTemp && m_RegWorkingSet.IsMapped(Rt)))
    {
        const asmjit::x86::Gp Source = m_RegWorkingSet.GetMipsRegMap(Rt).r32();
        switch (Op)
        {
        case 0: m_Assembler.add(Target.r32(), Source); break;
        case 1: m_Assembler.sub(Target.r32(), Source); break;
        case 2: m_Assembler.and_(Target.r32(), Source); break;
        case 3: m_Assembler.or_(Target.r32(), Source); break;
        default: m_Assembler.xor_(Target.r32(), Source); break;
        }
    }
    else
    {
        void * Variable = &m_Reg.m_GPR[Rt].W[0];
        const char * Name = CRegName::GPR_Lo[Rt];
        switch (Op)
        {
        case 0: m_Assembler.AddDwordFromVariable(Target, Variable, Name); break;
        case 1: m_Assembler.SubVariableFromX64reg(Target, Variable, Name); break;
        case 2: m_Assembler.AndVariableToX64reg(Target, Variable, Name); break;
        case 3: m_Assembler.OrVariableToX64reg(Target, Variable, Name); break;
        default: m_Assembler.XorVariableToX64reg(Target, Variable, Name); break;
        }
    }
    if (NeedTemp)
    {
        m_RegWorkingSet.Map_GPR_32bit(Rd, true, -1);
        m_Assembler.mov(m_RegWorkingSet.GetMipsRegMap(Rd).r32(), Temp.r32());
    }
}

// Rd = (Rs < Rt) or (Rs < immediate), signed or unsigned
void CX64RecompilerOps::CompileSetLess32(bool Unsigned, int32_t Rd, int32_t Rs, int32_t Rt, bool UseImm, int32_t Imm)
{
    if (Rd == 0)
    {
        return;
    }
    const bool AConst = Rs == 0 || m_RegWorkingSet.IsConst(Rs);
    const bool BConst = UseImm || Rt == 0 || m_RegWorkingSet.IsConst(Rt);
    uint32_t A = 0, B = 0;
    if (AConst)
    {
        A = Rs == 0 ? 0 : m_RegWorkingSet.GetMipsRegLo(Rs);
    }
    if (BConst)
    {
        B = UseImm ? (uint32_t)Imm : (Rt == 0 ? 0 : m_RegWorkingSet.GetMipsRegLo(Rt));
    }
    if (AConst && BConst)
    {
        const bool Less = Unsigned ? A < B : (int32_t)A < (int32_t)B;
        if (m_RegWorkingSet.IsMapped(Rd))
        {
            m_RegWorkingSet.UnMap_GPR(Rd, false);
        }
        m_RegWorkingSet.SetMipsRegLo(Rd, Less ? 1 : 0);
        m_RegWorkingSet.SetMipsRegState(Rd, CRegBase::STATE_CONST_32_SIGN);
        return;
    }

    if (!AConst && m_RegWorkingSet.IsMapped(Rs))
    {
        m_RegWorkingSet.ProtectGPR(Rs);
    }
    if (!BConst && m_RegWorkingSet.IsMapped(Rt))
    {
        m_RegWorkingSet.ProtectGPR(Rt);
    }
    // the left operand in a register
    asmjit::x86::Gp Left;
    if (!AConst && m_RegWorkingSet.IsMapped(Rs))
    {
        Left = m_RegWorkingSet.GetMipsRegMap(Rs).r32();
    }
    else
    {
        Left = m_RegWorkingSet.Map_TempReg(asmjit::x86::Gpd(), Rs);
    }
    m_RegWorkingSet.SetX64Protected(Left.id(), true);
    // the right operand in a register unless it is a constant or in memory
    asmjit::x86::Gp Right;
    if (!BConst && m_RegWorkingSet.IsMapped(Rt))
    {
        Right = m_RegWorkingSet.GetMipsRegMap(Rt).r32();
    }
    // the register of the result, taken before the compare because taking it can emit code
    const asmjit::x86::Gp Result = m_RegWorkingSet.Map_TempReg(asmjit::x86::Gpd(), -1);
    if (BConst)
    {
        m_Assembler.cmp(Left.r32(), B);
    }
    else if (Right.isValid())
    {
        m_Assembler.cmp(Left.r32(), Right.r32());
    }
    else
    {
        m_Assembler.CmpRegToVariable(Left, &m_Reg.m_GPR[Rt].W[0], CRegName::GPR_Lo[Rt]);
    }
    if (Unsigned)
    {
        m_Assembler.setb(Result.r8Lo());
    }
    else
    {
        m_Assembler.setl(Result.r8Lo());
    }
    m_Assembler.movzx(Result.r32(), Result.r8Lo());
    m_RegWorkingSet.Map_GPR_32bit(Rd, true, -1);
    m_Assembler.mov(m_RegWorkingSet.GetMipsRegMap(Rd).r32(), Result.r32());
}

// MTLO / MTHI: the register's value is stored as 64 bits
void CX64RecompilerOps::CompileMoveToHiLo(void * Variable, const char * Name)
{
    uint64_t Value;
    if (m_Opcode.rs == 0)
    {
        m_Assembler.MoveConst64ToVariable(Variable, Name, 0);
    }
    else if (ConstValue(m_Opcode.rs, Value))
    {
        m_Assembler.MoveConst64ToVariable(Variable, Name, g_GameSettings.core32Bit ? (uint64_t)(int64_t)(int32_t)Value : Value);
    }
    else
    {
        m_RegWorkingSet.ProtectGPR(m_Opcode.rs);
        if (m_RegWorkingSet.IsMapped(m_Opcode.rs) && m_RegWorkingSet.Is64Bit(m_Opcode.rs))
        {
            m_Assembler.MovQwordToVariable(Variable, Name, m_RegWorkingSet.GetMipsRegMap(m_Opcode.rs));
            return;
        }
        const asmjit::x86::Gp Temp = m_RegWorkingSet.Map_TempReg(asmjit::x86::Gpq(), -1, asmjit::RegType::kX86_Gpq);
        if (m_RegWorkingSet.IsMapped(m_Opcode.rs))
        {
            if (g_GameSettings.core32Bit || m_RegWorkingSet.IsSigned(m_Opcode.rs))
            {
                m_Assembler.movsxd(Temp.r64(), m_RegWorkingSet.GetMipsRegMap(m_Opcode.rs).r32());
            }
            else
            {
                m_Assembler.mov(Temp.r32(), m_RegWorkingSet.GetMipsRegMap(m_Opcode.rs).r32());
            }
        }
        else
        {
            m_Assembler.MoveVariable64ToX64reg(Temp, &m_Reg.m_GPR[m_Opcode.rs].UDW, CRegName::GPR[m_Opcode.rs]);
        }
        m_Assembler.MovQwordToVariable(Variable, Name, Temp);
    }
}

void CX64RecompilerOps::OverflowDelaySlot(bool TestTimer)
{
    m_RegWorkingSet.SetBlockCycleCount(m_RegWorkingSet.GetBlockCycleCount() + g_GameSettings.countPerOp);
    m_RegWorkingSet.WriteBackRegisters();
    UpdateCounters(m_RegWorkingSet, false, true);
    if (m_PipelineStage == PIPELINE_STAGE_DELAY_SLOT)
    {
        m_Assembler.MoveVariable64ToX64reg(asmjit::x86::rax, &g_System->m_JumpToLocation, "System::m_JumpToLocation");
        m_Assembler.MovQwordToVariable(&m_Reg.m_PROGRAM_COUNTER, "PROGRAM_COUNTER", asmjit::x86::rax);
        m_Assembler.MoveVariable64ToX64reg(asmjit::x86::rax, &g_System->m_JumpDelayLocation, "System::JumpDelayLocation");
        m_Assembler.MovQwordToVariable(&g_System->m_JumpToLocation, "System::m_JumpToLocation", asmjit::x86::rax);
    }
    else
    {
        m_Assembler.MoveConstToVariable(&m_Reg.m_PROGRAM_COUNTER, "PROGRAM_COUNTER", m_CompilePC + 4);
    }
    m_Assembler.MoveConstToVariable(&g_System->m_PipelineStage, "System->m_PipelineStage", PIPELINE_STAGE_JUMP);
    if (g_SyncSystem)
    {
        m_Assembler.sub(asmjit::x86::rsp, 32);
        m_Assembler.CallThis(g_BaseSystem, MemberFuncAddress(&CN64System::SyncSystem), "CN64System::SyncSystem");
        m_Assembler.add(asmjit::x86::rsp, 32);
    }
    if (TestTimer)
    {
        m_Assembler.mov(asmjit::x86::r11, reinterpret_cast<uintptr_t>(&g_System->m_TestTimer));
        m_Assembler.mov(asmjit::x86::byte_ptr(asmjit::x86::r11), 1);
    }

    m_Assembler.mov(X64Abi::Arg1.r32(), g_GameSettings.countPerOp);
    m_Assembler.sub(asmjit::x86::rsp, 32);
    m_Assembler.CallThis(&g_System->m_OpCodes, MemberFuncAddress(&R4300iOp::ExecuteOps), "R4300iOp::ExecuteOps");
    m_Assembler.add(asmjit::x86::rsp, 32);

    if (g_GameSettings.fastSP && g_Recompiler)
    {
        m_Assembler.sub(asmjit::x86::rsp, 32);
        m_Assembler.CallThis(g_Recompiler, MemberFuncAddress(&CRecompiler::ResetMemoryStackPos), "CRecompiler::ResetMemoryStackPos");
        m_Assembler.add(asmjit::x86::rsp, 32);
    }
    if (g_SyncSystem)
    {
        UpdateSyncCPU(m_RegWorkingSet, g_GameSettings.countPerOp);
    }
    ExitCodeBlock();
    m_PipelineStage = PIPELINE_STAGE_END_BLOCK;
}

// The value of a register that is known at compile time (as the 64 bit value, a 32 bit one is
// extended the way the register state says)
bool CX64RecompilerOps::ConstValue(int32_t MipsReg, uint64_t & Value) const
{
    if (!m_RegWorkingSet.IsConst(MipsReg))
    {
        return false;
    }
    if (m_RegWorkingSet.Is64Bit(MipsReg))
    {
        Value = m_RegWorkingSet.GetMipsReg(MipsReg);
    }
    else if (m_RegWorkingSet.IsSigned(MipsReg))
    {
        Value = (uint64_t)(int64_t)m_RegWorkingSet.GetMipsRegLo_S(MipsReg);
    }
    else
    {
        Value = m_RegWorkingSet.GetMipsRegLo(MipsReg);
    }
    return true;
}

// A register holding the value of a MIPS register for a compare: the cached register itself when it
// can be used as it is, else a temporary. A 32 bit core compares the low 32 bits, a 64 bit core 64 bits.
asmjit::x86::Gp CX64RecompilerOps::LoadForCompare(int32_t MipsReg, bool Use64)
{
    m_RegWorkingSet.ProtectGPR(MipsReg);
    if (!Use64)
    {
        if (m_RegWorkingSet.IsMapped(MipsReg))
        {
            return m_RegWorkingSet.GetMipsRegMap(MipsReg).r32();
        }
        const asmjit::x86::Gp Tmp = m_RegWorkingSet.Map_TempReg(asmjit::x86::Gpd(), -1);
        m_Assembler.MoveVariableToX64reg(Tmp, &m_Reg.m_GPR[MipsReg].UW[0], CRegName::GPR_Lo[MipsReg], false);
        return Tmp.r32();
    }
    if (m_RegWorkingSet.IsMapped(MipsReg) && m_RegWorkingSet.Is64Bit(MipsReg))
    {
        return m_RegWorkingSet.GetMipsRegMap(MipsReg).r64();
    }
    const asmjit::x86::Gp Tmp = m_RegWorkingSet.Map_TempReg(asmjit::x86::Gpq(), -1, asmjit::RegType::kX86_Gpq);
    if (m_RegWorkingSet.IsMapped(MipsReg))
    {
        if (m_RegWorkingSet.IsSigned(MipsReg))
        {
            m_Assembler.movsxd(Tmp.r64(), m_RegWorkingSet.GetMipsRegMap(MipsReg).r32());
        }
        else
        {
            m_Assembler.mov(Tmp.r32(), m_RegWorkingSet.GetMipsRegMap(MipsReg).r32());
        }
    }
    else
    {
        m_Assembler.MoveVariable64ToX64reg(Tmp, &m_Reg.m_GPR[MipsReg].UDW, CRegName::GPR[MipsReg]);
    }
    return Tmp.r64();
}

// The conditional jumps of a compare branch: to the jump target when Taken, to the continue
// target when NotTaken (the target that is not next in the code is jumped to)
void CX64RecompilerOps::BranchOnCondition(void (CX64Ops::*Taken)(const char *, asmjit::Label &), void (CX64Ops::*NotTaken)(const char *, asmjit::Label &))
{
    if (m_Section->m_Cont.FallThrough)
    {
        m_Section->m_Jump.LinkLocation = m_Assembler.newLabel();
        (m_Assembler.*Taken)(m_Section->m_Jump.BranchLabel.c_str(), m_Section->m_Jump.LinkLocation);
    }
    else if (m_Section->m_Jump.FallThrough)
    {
        m_Section->m_Cont.LinkLocation = m_Assembler.newLabel();
        (m_Assembler.*NotTaken)(m_Section->m_Cont.BranchLabel.c_str(), m_Section->m_Cont.LinkLocation);
    }
    else
    {
        m_Section->m_Cont.LinkLocation = m_Assembler.newLabel();
        (m_Assembler.*NotTaken)(m_Section->m_Cont.BranchLabel.c_str(), m_Section->m_Cont.LinkLocation);
        m_Section->m_Jump.LinkLocation = m_Assembler.newLabel();
        m_Assembler.JmpLabel(m_Section->m_Jump.BranchLabel.c_str(), m_Section->m_Jump.LinkLocation);
    }
}

// BLTZ (0), BGEZ (1), BLEZ (2), BGTZ (3): rs compared with 0
void CX64RecompilerOps::SignBranchCompare(int32_t Kind)
{
    uint64_t Value;
    if (ConstValue(m_Opcode.rs, Value))
    {
        const int64_t Signed = g_GameSettings.core32Bit ? (int64_t)(int32_t)Value : (int64_t)Value;
        bool Taken = false;
        switch (Kind)
        {
        case 0: Taken = Signed < 0; break;
        case 1: Taken = Signed >= 0; break;
        case 2: Taken = Signed <= 0; break;
        default: Taken = Signed > 0; break;
        }
        m_Section->m_Jump.FallThrough = Taken;
        m_Section->m_Cont.FallThrough = !Taken;
        return;
    }
    const asmjit::x86::Gp Reg = LoadForCompare(m_Opcode.rs, !g_GameSettings.core32Bit);
    m_Assembler.test(Reg, Reg);
    switch (Kind)
    {
    case 0: BranchOnCondition(&CX64Ops::JlLabel, &CX64Ops::JgeLabel); break;
    case 1: BranchOnCondition(&CX64Ops::JgeLabel, &CX64Ops::JlLabel); break;
    case 2: BranchOnCondition(&CX64Ops::JleLabel, &CX64Ops::JgLabel); break;
    default: BranchOnCondition(&CX64Ops::JgLabel, &CX64Ops::JleLabel); break;
    }
}

// BEQ / BNE: rs compared with rt
void CX64RecompilerOps::EqualBranchCompare(bool BranchOnEqual)
{
    uint64_t ValueRs, ValueRt;
    const bool Use64 = !g_GameSettings.core32Bit;
    const bool RsConst = ConstValue(m_Opcode.rs, ValueRs);
    const bool RtConst = ConstValue(m_Opcode.rt, ValueRt);
    if (!Use64)
    {
        ValueRs = (uint32_t)ValueRs;
        ValueRt = (uint32_t)ValueRt;
    }
    if ((RsConst && RtConst) || m_Opcode.rs == m_Opcode.rt)
    {
        const bool Equal = m_Opcode.rs == m_Opcode.rt || ValueRs == ValueRt;
        const bool Taken = BranchOnEqual ? Equal : !Equal;
        m_Section->m_Jump.FallThrough = Taken;
        m_Section->m_Cont.FallThrough = !Taken;
        return;
    }

    if (RsConst || RtConst)
    {
        const int32_t Variable = RsConst ? m_Opcode.rt : m_Opcode.rs;
        const uint64_t Const = RsConst ? ValueRs : ValueRt;
        const asmjit::x86::Gp Reg = LoadForCompare(Variable, Use64);
        if (Const == 0)
        {
            m_Assembler.test(Reg, Reg);
        }
        else if (!Use64 || Const == (uint64_t)(int64_t)(int32_t)Const)
        {
            m_Assembler.cmp(Reg, (int32_t)Const);
        }
        else
        {
            const asmjit::x86::Gp ConstReg = m_RegWorkingSet.Map_TempReg(asmjit::x86::Gpq(), -1, asmjit::RegType::kX86_Gpq);
            m_Assembler.MoveConstToX64reg(ConstReg, Const);
            m_Assembler.cmp(Reg, ConstReg.r64());
        }
    }
    else
    {
        const asmjit::x86::Gp Reg1 = LoadForCompare(m_Opcode.rs, Use64);
        m_RegWorkingSet.SetX64Protected(Reg1.id(), true);
        const asmjit::x86::Gp Reg2 = LoadForCompare(m_Opcode.rt, Use64);
        m_Assembler.cmp(Reg1, Reg2);
    }
    if (BranchOnEqual)
    {
        BranchOnCondition(&CX64Ops::JeLabel, &CX64Ops::JneLabel);
    }
    else
    {
        BranchOnCondition(&CX64Ops::JneLabel, &CX64Ops::JeLabel);
    }
}

void CX64RecompilerOps::CompileCop1Test()
{
    if (m_RegWorkingSet.GetFpuBeenUsed())
    {
        return;
    }

    m_Assembler.TestVariable(&g_Reg->STATUS_REGISTER, "STATUS_REGISTER", STATUS_CU1);
    CompileExit(m_CompilePC, m_CompilePC, m_RegWorkingSet.WithAddedCycles(g_GameSettings.countPerOp), ExitReason_COP1Unuseable, &CX64Ops::JeLabel);
    m_RegWorkingSet.SetFpuBeenUsed(true);
}

void CX64RecompilerOps::Compile_BranchCompare(RecompilerBranchCompare CompareType)
{
    switch (CompareType)
    {
    case RecompilerBranchCompare_BEQ: BEQ_Compare(); break;
    case RecompilerBranchCompare_BNE: BNE_Compare(); break;
    case RecompilerBranchCompare_BLTZ: BLTZ_Compare(); break;
    case RecompilerBranchCompare_BLEZ: BLEZ_Compare(); break;
    case RecompilerBranchCompare_BGTZ: BGTZ_Compare(); break;
    case RecompilerBranchCompare_BGEZ: BGEZ_Compare(); break;
    case RecompilerBranchCompare_COP1BCF: COP1_BCF_Compare(); break;
    case RecompilerBranchCompare_COP1BCT: COP1_BCT_Compare(); break;
    default:
        g_Notify->BreakPoint(__FILE__, __LINE__);
    }
}

void CX64RecompilerOps::Compile_Branch(RecompilerBranchCompare CompareType, bool Link)
{
    if (m_PipelineStage == PIPELINE_STAGE_DELAY_SLOT)
    {
        // a branch in the delay slot of a branch: the interpreter does what follows
        Compile_BranchCompare(CompareType);
        if (m_Section->m_Jump.FallThrough || (!m_Section->m_Cont.FallThrough && m_Section->m_Jump.LinkLocation.isValid()))
        {
            LinkJump(m_Section->m_Jump);
            m_Assembler.MoveConst64ToVariable(&g_System->m_JumpDelayLocation, "System::m_JumpDelayLocation", m_CompilePC + ((int16_t)m_Opcode.offset << 2) + 8);
            if (m_Section->m_Cont.LinkLocation.isValid())
            {
                g_Notify->BreakPoint(__FILE__, __LINE__);
            }
        }
        if (m_Section->m_Cont.FallThrough)
        {
            LinkJump(m_Section->m_Cont);
            m_Assembler.MoveConst64ToVariable(&g_System->m_JumpDelayLocation, "System::m_JumpDelayLocation", m_CompilePC + 8);
            if (m_Section->m_Jump.LinkLocation.isValid())
            {
                g_Notify->BreakPoint(__FILE__, __LINE__);
            }
        }
        if (m_Section->m_Jump.LinkLocation.isValid() || m_Section->m_Cont.LinkLocation.isValid())
        {
            g_Notify->BreakPoint(__FILE__, __LINE__);
        }
        if (Link)
        {
            m_RegWorkingSet.Map_GPR_32bit(31, true, -1);
            m_Assembler.MoveVariableToX64reg(m_RegWorkingSet.GetMipsRegMap(31), &g_System->m_JumpToLocation, "System::m_JumpToLocation", true);
            m_Assembler.add(m_RegWorkingSet.GetMipsRegMap(31).r32(), 4);
        }
        OverflowDelaySlot(false);
    }
    else if (m_PipelineStage == PIPELINE_STAGE_NORMAL)
    {
        if (CompareType == RecompilerBranchCompare_COP1BCF || CompareType == RecompilerBranchCompare_COP1BCT)
        {
            CompileCop1Test();
            m_Assembler.AndConstToVariable(&m_Reg.m_FPCR[31], "FPCR[31]", (uint32_t)(~0x0003F000));
        }
        if (m_CompilePC + ((int16_t)m_Opcode.offset << 2) + 4 == m_CompilePC + 8 && (m_CompilePC & 0xFFC) != 0xFFC)
        {
            m_PipelineStage = PIPELINE_STAGE_DO_DELAY_SLOT;
            return;
        }

        if ((m_CompilePC & 0xFFC) != 0xFFC)
        {
            R4300iOpcode DelaySlot;
            m_EffectDelaySlot = g_MMU->MemoryValue32((uint32_t)(m_CompilePC + 4), DelaySlot.Value) && R4300iInstruction(m_CompilePC, m_Opcode.Value).DelaySlotEffectsCompare(DelaySlot.Value);
        }
        else
        {
            m_EffectDelaySlot = true;
        }
        m_Section->m_Jump.JumpPC = (uint32_t)m_CompilePC;
        m_Section->m_Jump.TargetPC = (uint32_t)(m_CompilePC + ((int16_t)m_Opcode.offset << 2) + 4);
        if (m_PipelineStage == PIPELINE_STAGE_DELAY_SLOT)
        {
            m_Section->m_Jump.TargetPC += 4;
            m_EffectDelaySlot = true;
        }
        if (m_Section->m_JumpSection != nullptr)
        {
            m_Section->m_Jump.BranchLabel = stdstr_f("Section_%d", ((CCodeSection *)m_Section->m_JumpSection)->m_SectionID);
        }
        else
        {
            m_Section->m_Jump.BranchLabel = stdstr_f("Exit_%X_jump_%X", m_Section->m_EnterPC, m_Section->m_Jump.TargetPC);
        }
        m_Section->m_Jump.LinkLocation = asmjit::Label();
        m_Section->m_Jump.LinkLocation2 = asmjit::Label();
        m_Section->m_Jump.DoneDelaySlot = false;
        m_Section->m_Cont.JumpPC = (uint32_t)m_CompilePC;
        m_Section->m_Cont.TargetPC = (uint32_t)(m_CompilePC + 8);
        if (m_Section->m_ContinueSection != nullptr)
        {
            m_Section->m_Cont.BranchLabel = stdstr_f("Section_%d", ((CCodeSection *)m_Section->m_ContinueSection)->m_SectionID);
        }
        else
        {
            m_Section->m_Cont.BranchLabel = stdstr_f("Exit_%X_continue_%X", m_Section->m_EnterPC, m_Section->m_Cont.TargetPC);
        }
        m_Section->m_Cont.LinkLocation = asmjit::Label();
        m_Section->m_Cont.LinkLocation2 = asmjit::Label();
        m_Section->m_Cont.DoneDelaySlot = false;
        m_Section->m_Jump.FallThrough = m_Section->m_Jump.TargetPC < m_Section->m_Cont.TargetPC;
        m_Section->m_Cont.FallThrough = !m_Section->m_Jump.FallThrough;

        if (Link)
        {
            R4300iInstruction Instruction(m_CompilePC, m_Opcode.Value);
            uint32_t ReadReg1, ReadReg2;
            Instruction.ReadsGPR(ReadReg1, ReadReg2);

            if (ReadReg1 != 31 && ReadReg2 != 31)
            {
                m_RegWorkingSet.UnMap_GPR(31, false);
                m_RegWorkingSet.SetMipsRegLo(31, (uint32_t)m_CompilePC + 8);
                m_RegWorkingSet.SetMipsRegState(31, CRegInfo::STATE_CONST_32_SIGN);
            }
            else
            {
                m_Section->m_Cont.LinkAddress = (uint32_t)(m_CompilePC + 8);
                m_Section->m_Jump.LinkAddress = (uint32_t)(m_CompilePC + 8);
            }
        }
        if (m_EffectDelaySlot)
        {
            if ((m_CompilePC & 0xFFC) != 0xFFC)
            {
                m_Section->m_Cont.BranchLabel = m_Section->m_ContinueSection != nullptr ? "Continue" : stdstr_f("ExitBlock_%X_Continue", m_Section->m_EnterPC);
                m_Section->m_Jump.BranchLabel = m_Section->m_JumpSection != nullptr ? "Jump" : stdstr_f("ExitBlock_%X_Jump", m_Section->m_EnterPC);
            }
            else
            {
                m_Section->m_Cont.BranchLabel = "Continue";
                m_Section->m_Jump.BranchLabel = "Jump";
            }
            if (m_Section->m_Jump.TargetPC != m_Section->m_Cont.TargetPC)
            {
                Compile_BranchCompare(CompareType);
            }
            if (!m_Section->m_Jump.FallThrough && !m_Section->m_Cont.FallThrough)
            {
                if (m_Section->m_Jump.LinkLocation.isValid())
                {
                    LinkJump(m_Section->m_Jump);
                    m_Section->m_Jump.FallThrough = true;
                }
                else if (m_Section->m_Cont.LinkLocation.isValid())
                {
                    LinkJump(m_Section->m_Cont);
                    m_Section->m_Cont.FallThrough = true;
                }
            }
            if ((m_CompilePC & 0xFFC) == 0xFFC)
            {
                // the delay slot is on the next page: the interpreter runs it with the target of the branch
                asmjit::Label DelayLinkLocation;
                if (m_Section->m_Jump.FallThrough)
                {
                    if (m_Section->m_Jump.LinkLocation.isValid() || m_Section->m_Jump.LinkLocation2.isValid())
                    {
                        g_Notify->BreakPoint(__FILE__, __LINE__);
                    }
                    m_Assembler.MoveConst64ToVariable(&g_System->m_JumpToLocation, "System::m_JumpToLocation", (int)m_Section->m_Jump.TargetPC);
                }
                else if (m_Section->m_Cont.FallThrough)
                {
                    if (m_Section->m_Cont.LinkLocation.isValid() || m_Section->m_Cont.LinkLocation2.isValid())
                    {
                        g_Notify->BreakPoint(__FILE__, __LINE__);
                    }
                    m_Assembler.MoveConst64ToVariable(&g_System->m_JumpToLocation, "System::m_JumpToLocation", (int)m_Section->m_Cont.TargetPC);
                }

                if (m_Section->m_Jump.LinkLocation.isValid() || m_Section->m_Jump.LinkLocation2.isValid())
                {
                    if (DelayLinkLocation.isValid())
                    {
                        g_Notify->BreakPoint(__FILE__, __LINE__);
                    }
                    DelayLinkLocation = m_Assembler.newLabel();
                    m_Assembler.JmpLabel("DoDelaySlot", DelayLinkLocation);

                    LinkJump(m_Section->m_Jump);
                    m_Assembler.MoveConst64ToVariable(&g_System->m_JumpToLocation, "System::m_JumpToLocation", (int)m_Section->m_Jump.TargetPC);
                }
                if (m_Section->m_Cont.LinkLocation.isValid() || m_Section->m_Cont.LinkLocation2.isValid())
                {
                    if (DelayLinkLocation.isValid())
                    {
                        g_Notify->BreakPoint(__FILE__, __LINE__);
                    }
                    DelayLinkLocation = m_Assembler.newLabel();
                    m_Assembler.JmpLabel("DoDelaySlot", DelayLinkLocation);

                    LinkJump(m_Section->m_Cont);
                    m_Assembler.MoveConst64ToVariable(&g_System->m_JumpToLocation, "System::m_JumpToLocation", (int)m_Section->m_Cont.TargetPC);
                }
                if (DelayLinkLocation.isValid())
                {
                    m_CodeBlock.Log("");
                    m_Assembler.bind(DelayLinkLocation);
                }
                OverflowDelaySlot(false);
                return;
            }
            m_RegWorkingSet.ResetRegisterProtection();
            m_RegBeforeDelay = m_RegWorkingSet;
        }
        if (m_PipelineStage == PIPELINE_STAGE_NORMAL)
        {
            m_PipelineStage = PIPELINE_STAGE_DO_DELAY_SLOT;
        }
        else
        {
            m_RegWorkingSet.SetBlockCycleCount(m_RegWorkingSet.GetBlockCycleCount() + g_GameSettings.countPerOp);
            m_Section->m_Jump.RegSet = m_RegWorkingSet;
            m_Section->m_Cont.RegSet = m_RegWorkingSet;
            m_Section->GenerateSectionLinkage();
            m_PipelineStage = PIPELINE_STAGE_END_BLOCK;
        }
    }
    else if (m_PipelineStage == PIPELINE_STAGE_DELAY_SLOT_DONE)
    {
        if (m_CompilePC + ((int16_t)m_Opcode.offset << 2) + 4 == m_CompilePC + 8)
        {
            m_PipelineStage = PIPELINE_STAGE_NORMAL;
            m_RegWorkingSet.SetBlockCycleCount(m_RegWorkingSet.GetBlockCycleCount() - g_GameSettings.countPerOp);
            SetCurrentPC((uint32_t)(GetCurrentPC() + 4));
            return;
        }
        if (m_EffectDelaySlot)
        {
            CJumpInfo * FallInfo = m_Section->m_Jump.FallThrough ? &m_Section->m_Jump : &m_Section->m_Cont;
            CJumpInfo * JumpInfo = m_Section->m_Jump.FallThrough ? &m_Section->m_Cont : &m_Section->m_Jump;

            if (FallInfo->FallThrough && !FallInfo->DoneDelaySlot)
            {
                m_RegWorkingSet.ResetRegisterProtection();
                FallInfo->RegSet = m_RegWorkingSet;
                if (FallInfo == &m_Section->m_Jump)
                {
                    if (m_Section->m_JumpSection != nullptr)
                    {
                        m_Section->m_Jump.BranchLabel = stdstr_f("Section_%d", ((CCodeSection *)m_Section->m_JumpSection)->m_SectionID);
                    }
                    else
                    {
                        m_Section->m_Jump.BranchLabel = "ExitBlock";
                    }
                    if (FallInfo->TargetPC <= (uint32_t)m_CompilePC)
                    {
                        UpdateCounters(FallInfo->RegSet, true, true, true);
                        m_CodeBlock.Log("CompileSystemCheck 12");
                        CompileSystemCheck(FallInfo->TargetPC, FallInfo->RegSet);
                        m_RegWorkingSet.ResetRegisterProtection();
                        FallInfo->Reason = ExitReason_NormalNoSysCheck;
                        FallInfo->JumpPC = (uint32_t)-1;
                    }
                }
                else
                {
                    if (m_Section->m_ContinueSection != nullptr)
                    {
                        m_Section->m_Cont.BranchLabel = stdstr_f("Section_%d", ((CCodeSection *)m_Section->m_ContinueSection)->m_SectionID);
                    }
                    else
                    {
                        m_Section->m_Cont.BranchLabel = stdstr_f("ExitBlock_%X_Continue", m_Section->m_EnterPC);
                    }
                }
                FallInfo->DoneDelaySlot = true;
                if (!JumpInfo->DoneDelaySlot)
                {
                    FallInfo->FallThrough = false;
                    FallInfo->LinkLocation = m_Assembler.newLabel();
                    m_Assembler.JmpLabel(FallInfo->BranchLabel.c_str(), FallInfo->LinkLocation);

                    if (JumpInfo->LinkLocation.isValid())
                    {
                        LinkJump(*JumpInfo);
                        JumpInfo->FallThrough = true;
                        m_PipelineStage = PIPELINE_STAGE_DO_DELAY_SLOT;
                        m_RegWorkingSet = m_RegBeforeDelay;
                        return;
                    }
                }
            }
        }
        else
        {
            if (m_Section->m_Jump.TargetPC != m_Section->m_Cont.TargetPC)
            {
                Compile_BranchCompare(CompareType);
                m_RegWorkingSet.ResetRegisterProtection();
                m_Section->m_Cont.RegSet = m_RegWorkingSet;
                m_Section->m_Jump.RegSet = m_RegWorkingSet;
                if (m_Section->m_Cont.LinkAddress != (uint32_t)-1)
                {
                    m_Section->m_Cont.RegSet.UnMap_GPR(31, false);
                    m_Section->m_Cont.RegSet.SetMipsRegLo(31, m_Section->m_Cont.LinkAddress);
                    m_Section->m_Cont.RegSet.SetMipsRegState(31, CRegInfo::STATE_CONST_32_SIGN);
                    m_Section->m_Cont.LinkAddress = (uint32_t)-1;
                }
                if (m_Section->m_Jump.LinkAddress != (uint32_t)-1)
                {
                    m_Section->m_Jump.RegSet.UnMap_GPR(31, false);
                    m_Section->m_Jump.RegSet.SetMipsRegLo(31, m_Section->m_Jump.LinkAddress);
                    m_Section->m_Jump.RegSet.SetMipsRegState(31, CRegInfo::STATE_CONST_32_SIGN);
                    m_Section->m_Jump.LinkAddress = (uint32_t)-1;
                }
            }
            else
            {
                m_Section->m_Jump.FallThrough = false;
                m_Section->m_Cont.FallThrough = true;
                m_Section->m_Cont.RegSet = m_RegWorkingSet;
                if (m_Section->m_ContinueSection == nullptr && m_Section->m_JumpSection != nullptr)
                {
                    m_Section->m_ContinueSection = m_Section->m_JumpSection;
                    m_Section->m_JumpSection = nullptr;
                }
                if (m_Section->m_ContinueSection != nullptr)
                {
                    m_Section->m_Cont.BranchLabel = stdstr_f("Section_%d", ((CCodeSection *)m_Section->m_ContinueSection)->m_SectionID);
                }
                else
                {
                    m_Section->m_Cont.BranchLabel = "ExitBlock";
                }
            }
        }
        m_Section->GenerateSectionLinkage();
        m_PipelineStage = PIPELINE_STAGE_END_BLOCK;
    }
    else
    {
        g_Notify->BreakPoint(__FILE__, __LINE__);
    }
}

void CX64RecompilerOps::Compile_BranchLikely(RecompilerBranchCompare CompareType, bool Link)
{
    if (m_PipelineStage == PIPELINE_STAGE_NORMAL)
    {
        if (CompareType == RecompilerBranchCompare_COP1BCF || CompareType == RecompilerBranchCompare_COP1BCT)
        {
            CompileCop1Test();
            m_Assembler.AndConstToVariable(&m_Reg.m_FPCR[31], "FPCR[31]", (uint32_t)(~0x0003F000));
        }
        // the addresses of the two ways of the branch. In a linked block the analysis has them: the jump
        // section of a branch likely starts at its delay slot (PC + 4), not at the branch target
        if (!GameLinkBlocks() || (m_CompilePC & 0xFFC) == 0xFFC)
        {
            m_Section->m_Jump.JumpPC = (uint32_t)m_CompilePC;
            m_Section->m_Jump.TargetPC = (uint32_t)(m_CompilePC + ((int16_t)m_Opcode.offset << 2) + 4);
            m_Section->m_Cont.JumpPC = (uint32_t)m_CompilePC;
            m_Section->m_Cont.TargetPC = (uint32_t)(m_CompilePC + 8);
        }
        else
        {
            if (m_Section->m_Jump.JumpPC != (uint32_t)m_CompilePC)
            {
                g_Notify->BreakPoint(__FILE__, __LINE__);
            }
            if (m_Section->m_ContinueSection == nullptr)
            {
                // the analysis made no section for the way that skips the delay slot (the block ends
                // before it): it leaves the block there
                m_Section->m_Cont.JumpPC = (uint32_t)m_CompilePC;
                m_Section->m_Cont.TargetPC = (uint32_t)(m_CompilePC + 8);
            }
            else if (m_Section->m_Cont.JumpPC != (uint32_t)m_CompilePC || m_Section->m_Cont.TargetPC != (uint32_t)(m_CompilePC + 8))
            {
                g_Notify->BreakPoint(__FILE__, __LINE__);
            }
        }

        if (m_Section->m_JumpSection != nullptr)
        {
            m_Section->m_Jump.BranchLabel = stdstr_f("Section_%d", ((CCodeSection *)m_Section->m_JumpSection)->m_SectionID);
        }
        else
        {
            m_Section->m_Jump.BranchLabel = "ExitBlock";
        }

        if (m_Section->m_ContinueSection != nullptr)
        {
            m_Section->m_Cont.BranchLabel = stdstr_f("Section_%d", ((CCodeSection *)m_Section->m_ContinueSection)->m_SectionID);
        }
        else
        {
            m_Section->m_Cont.BranchLabel = "ExitBlock";
        }

        m_Section->m_Jump.FallThrough = true;
        m_Section->m_Jump.LinkLocation = asmjit::Label();
        m_Section->m_Jump.LinkLocation2 = asmjit::Label();
        m_Section->m_Cont.FallThrough = false;
        m_Section->m_Cont.LinkLocation = asmjit::Label();
        m_Section->m_Cont.LinkLocation2 = asmjit::Label();
        if (Link)
        {
            R4300iInstruction Instruction(m_CompilePC, m_Opcode.Value);
            uint32_t ReadReg1, ReadReg2;
            Instruction.ReadsGPR(ReadReg1, ReadReg2);

            if (ReadReg1 != 31 && ReadReg2 != 31)
            {
                m_RegWorkingSet.UnMap_GPR(31, false);
                m_RegWorkingSet.SetMipsRegLo(31, (uint32_t)(m_CompilePC + 8));
                m_RegWorkingSet.SetMipsRegState(31, CRegInfo::STATE_CONST_32_SIGN);
            }
            else
            {
                m_Section->m_Cont.LinkAddress = (uint32_t)(m_CompilePC + 8);
                m_Section->m_Jump.LinkAddress = (uint32_t)(m_CompilePC + 8);
            }
        }

        Compile_BranchCompare(CompareType);
        m_RegWorkingSet.ResetRegisterProtection();

        m_Section->m_Cont.RegSet = m_RegWorkingSet;
        m_Section->m_Cont.RegSet.SetBlockCycleCount(m_Section->m_Cont.RegSet.GetBlockCycleCount() + g_GameSettings.countPerOp);
        if (m_Section->m_Cont.LinkAddress != (uint32_t)-1)
        {
            m_Section->m_Cont.RegSet.UnMap_GPR(31, false);
            m_Section->m_Cont.RegSet.SetMipsRegLo(31, m_Section->m_Cont.LinkAddress);
            m_Section->m_Cont.RegSet.SetMipsRegState(31, CRegInfo::STATE_CONST_32_SIGN);
            m_Section->m_Cont.LinkAddress = (uint32_t)-1;
        }
        if ((m_CompilePC & 0xFFC) == 0xFFC)
        {
            if (m_Section->m_Cont.FallThrough)
            {
                if (m_Section->m_Jump.LinkLocation.isValid())
                {
                    g_Notify->BreakPoint(__FILE__, __LINE__);
                }
            }

            if (m_Section->m_Jump.LinkLocation.isValid() || m_Section->m_Jump.FallThrough)
            {
                LinkJump(m_Section->m_Jump);

                m_Assembler.MoveConst64ToVariable(&g_System->m_JumpToLocation, "System::m_JumpToLocation", (int)m_Section->m_Jump.TargetPC);
                OverflowDelaySlot(false);
                m_CodeBlock.Log("      ");
                m_CodeBlock.Log("      %s:", m_Section->m_Cont.BranchLabel.c_str());
            }
            else if (!m_Section->m_Cont.FallThrough)
            {
                g_Notify->BreakPoint(__FILE__, __LINE__);
            }

            LinkJump(m_Section->m_Cont);
            CompileExit(m_CompilePC, m_CompilePC + 8, m_Section->m_Cont.RegSet, ExitReason_Normal);
            return;
        }
        else
        {
            m_PipelineStage = PIPELINE_STAGE_DO_DELAY_SLOT;
        }

        if (GameLinkBlocks())
        {
            m_Section->m_Jump.RegSet = m_RegWorkingSet;
            m_Section->m_Jump.RegSet.SetBlockCycleCount(m_Section->m_Jump.RegSet.GetBlockCycleCount() + g_GameSettings.countPerOp);
            if (m_Section->m_Jump.LinkAddress != (uint32_t)-1)
            {
                m_Section->m_Jump.RegSet.UnMap_GPR(31, false);
                m_Section->m_Jump.RegSet.SetMipsRegLo(31, m_Section->m_Jump.LinkAddress);
                m_Section->m_Jump.RegSet.SetMipsRegState(31, CRegInfo::STATE_CONST_32_SIGN);
                m_Section->m_Jump.LinkAddress = (uint32_t)-1;
            }
            m_Section->GenerateSectionLinkage();
            m_PipelineStage = PIPELINE_STAGE_END_BLOCK;
        }
        else
        {
            if (m_Section->m_Cont.FallThrough)
            {
                if (m_Section->m_Jump.LinkLocation.isValid())
                {
                    g_Notify->BreakPoint(__FILE__, __LINE__);
                }
                m_Section->GenerateSectionLinkage();
                m_PipelineStage = PIPELINE_STAGE_END_BLOCK;
            }
        }
    }
    else if (m_PipelineStage == PIPELINE_STAGE_DELAY_SLOT_DONE)
    {
        m_RegWorkingSet.ResetRegisterProtection();
        m_Section->m_Jump.RegSet = m_RegWorkingSet;
        m_Section->m_Jump.RegSet.SetBlockCycleCount(m_Section->m_Jump.RegSet.GetBlockCycleCount());
        if (m_Section->m_Jump.LinkAddress != (uint32_t)-1)
        {
            m_Section->m_Jump.RegSet.UnMap_GPR(31, false);
            m_Section->m_Jump.RegSet.SetMipsRegLo(31, m_Section->m_Jump.LinkAddress);
            m_Section->m_Jump.RegSet.SetMipsRegState(31, CRegInfo::STATE_CONST_32_SIGN);
            m_Section->m_Jump.LinkAddress = (uint32_t)-1;
        }
        m_Section->GenerateSectionLinkage();
        m_PipelineStage = PIPELINE_STAGE_END_BLOCK;
    }
    else if (g_DebugSettings.haveDebugger)
    {
        g_Notify->DisplayError(stdstr_f("WTF\n%s\nNextInstruction = %X", __FUNCTION__, m_PipelineStage).c_str());
    }
}

void CX64RecompilerOps::BNE_Compare()
{
    EqualBranchCompare(false);
}

void CX64RecompilerOps::BEQ_Compare()
{
    EqualBranchCompare(true);
}

void CX64RecompilerOps::BGTZ_Compare()
{
    SignBranchCompare(3);
}

void CX64RecompilerOps::BLEZ_Compare()
{
    SignBranchCompare(2);
}

void CX64RecompilerOps::BLTZ_Compare()
{
    SignBranchCompare(0);
}

void CX64RecompilerOps::BGEZ_Compare()
{
    SignBranchCompare(1);
}

void CX64RecompilerOps::COP1_BCF_Compare()
{
    m_Assembler.TestVariable(&m_Reg.m_FPCR[31], "_FPCR[31]", FPCSR_C);
    BranchOnCondition(&CX64Ops::JeLabel, &CX64Ops::JneLabel);
}

void CX64RecompilerOps::COP1_BCT_Compare()
{
    m_Assembler.TestVariable(&m_Reg.m_FPCR[31], "_FPCR[31]", FPCSR_C);
    BranchOnCondition(&CX64Ops::JneLabel, &CX64Ops::JeLabel);
}

// The 64 bit value of a register is stored in a variable (the program counter, the target of a
// jump): the register is read here, before the delay slot can change it
void CX64RecompilerOps::StoreRegisterTo64(void * Variable, const char * Name, int32_t MipsReg)
{
    uint64_t Value;
    if (MipsReg == 0)
    {
        m_Assembler.MoveConst64ToVariable(Variable, Name, 0);
    }
    else if (ConstValue(MipsReg, Value))
    {
        m_Assembler.MoveConst64ToVariable(Variable, Name, g_GameSettings.core32Bit ? (uint64_t)(int64_t)(int32_t)Value : Value);
    }
    else
    {
        m_RegWorkingSet.ProtectGPR(MipsReg);
        if (m_RegWorkingSet.IsMapped(MipsReg) && m_RegWorkingSet.Is64Bit(MipsReg))
        {
            m_Assembler.MovQwordToVariable(Variable, Name, m_RegWorkingSet.GetMipsRegMap(MipsReg));
            return;
        }
        const asmjit::x86::Gp Temp = m_RegWorkingSet.Map_TempReg(asmjit::x86::Gpq(), -1, asmjit::RegType::kX86_Gpq);
        if (m_RegWorkingSet.IsMapped(MipsReg))
        {
            if (g_GameSettings.core32Bit || m_RegWorkingSet.IsSigned(MipsReg))
            {
                m_Assembler.movsxd(Temp.r64(), m_RegWorkingSet.GetMipsRegMap(MipsReg).r32());
            }
            else
            {
                m_Assembler.mov(Temp.r32(), m_RegWorkingSet.GetMipsRegMap(MipsReg).r32());
            }
        }
        else
        {
            m_Assembler.MoveVariable64ToX64reg(Temp, &m_Reg.m_GPR[MipsReg].UDW, CRegName::GPR[MipsReg]);
        }
        m_Assembler.MovQwordToVariable(Variable, Name, Temp);
    }
}

// the target of a jump that was taken before the delay slot is the program counter again
void CX64RecompilerOps::LoadBranchTargetToPC()
{
    const asmjit::x86::Gp Temp = m_RegWorkingSet.Map_TempReg(asmjit::x86::Gpq(), -1, asmjit::RegType::kX86_Gpq);
    m_Assembler.MoveVariable64ToX64reg(Temp, &m_BranchTarget, "BranchTarget");
    m_Assembler.MovQwordToVariable(&m_Reg.m_PROGRAM_COUNTER, "PROGRAM_COUNTER", Temp);
}

void CX64RecompilerOps::J()
{
    if (m_PipelineStage == PIPELINE_STAGE_NORMAL)
    {
        if ((m_CompilePC & 0xFFC) == 0xFFC)
        {
            m_Assembler.MoveConst64ToVariable(&g_System->m_JumpToLocation, "System::m_JumpToLocation", (int32_t)((m_CompilePC & 0xF0000000) + (m_Opcode.target << 2)));
            OverflowDelaySlot(false);
            return;
        }
        R4300iOpcode DelaySlot;
        g_MMU->MemoryValue32((uint32_t)(m_CompilePC + 4), DelaySlot.Value);
        if (R4300iInstruction(m_CompilePC + 4, DelaySlot.Value).HasDelaySlot())
        {
            // a jump in the delay slot: the interpreter does it
            m_Assembler.MoveConst64ToVariable(&g_System->m_JumpToLocation, "System::m_JumpToLocation", (int32_t)((m_CompilePC & 0xF0000000) + (m_Opcode.target << 2)));
            m_PipelineStage = PIPELINE_STAGE_DO_DELAY_SLOT;
            return;
        }

        m_Section->m_Jump.TargetPC = (m_CompilePC & 0xF0000000) + (m_Opcode.target << 2);
        m_Section->m_Jump.JumpPC = (uint32_t)(m_CompilePC);
        if (m_Section->m_JumpSection != nullptr)
        {
            m_Section->m_Jump.BranchLabel = stdstr_f("Section_%d", ((CCodeSection *)m_Section->m_JumpSection)->m_SectionID);
        }
        else
        {
            m_Section->m_Jump.BranchLabel = "ExitBlock";
        }
        m_Section->m_Jump.FallThrough = true;
        m_Section->m_Jump.LinkLocation = asmjit::Label();
        m_Section->m_Jump.LinkLocation2 = asmjit::Label();
        m_PipelineStage = PIPELINE_STAGE_DO_DELAY_SLOT;
    }
    else if (m_PipelineStage == PIPELINE_STAGE_DELAY_SLOT_DONE)
    {
        m_Section->m_Jump.RegSet = m_RegWorkingSet;
        m_Section->GenerateSectionLinkage();
        m_PipelineStage = PIPELINE_STAGE_END_BLOCK;
    }
    else if (g_DebugSettings.haveDebugger)
    {
        g_Notify->DisplayError(stdstr_f("WTF\n\nJ\nNextInstruction = %X", m_PipelineStage).c_str());
    }
}

void CX64RecompilerOps::JAL()
{
    if (m_PipelineStage == PIPELINE_STAGE_NORMAL)
    {
        m_RegWorkingSet.Map_GPR_32bit(31, true, -1);
        m_Assembler.MoveVariableToX64reg(m_RegWorkingSet.GetMipsRegMap(31), &m_Reg.m_PROGRAM_COUNTER, "_PROGRAM_COUNTER", false);
        m_Assembler.and_(m_RegWorkingSet.GetMipsRegMap(31).r32(), 0xF0000000);
        m_Assembler.add(m_RegWorkingSet.GetMipsRegMap(31).r32(), (uint32_t)((m_CompilePC + 8) & ~0xF0000000));
        if ((m_CompilePC & 0xFFC) == 0xFFC)
        {
            m_Assembler.MoveConst64ToVariable(&g_System->m_JumpToLocation, "System::m_JumpToLocation", (int32_t)((m_CompilePC & 0xF0000000) + (m_Opcode.target << 2)));
            OverflowDelaySlot(false);
            return;
        }
        m_Section->m_Jump.TargetPC = (m_CompilePC & 0xF0000000) + (m_Opcode.target << 2);
        m_Section->m_Jump.JumpPC = (uint32_t)m_CompilePC;
        if (m_Section->m_JumpSection != nullptr)
        {
            m_Section->m_Jump.BranchLabel = stdstr_f("Section_%d", ((CCodeSection *)m_Section->m_JumpSection)->m_SectionID);
        }
        else
        {
            m_Section->m_Jump.BranchLabel = "ExitBlock";
        }
        m_Section->m_Jump.FallThrough = true;
        m_Section->m_Jump.LinkLocation = asmjit::Label();
        m_Section->m_Jump.LinkLocation2 = asmjit::Label();
        m_PipelineStage = PIPELINE_STAGE_DO_DELAY_SLOT;
    }
    else if (m_PipelineStage == PIPELINE_STAGE_DELAY_SLOT_DONE)
    {
        if (m_Section->m_JumpSection)
        {
            m_Section->m_Jump.RegSet = m_RegWorkingSet;
            m_Section->GenerateSectionLinkage();
        }
        else
        {
            m_RegWorkingSet.WriteBackRegisters();

            const asmjit::x86::Gp PCReg = m_RegWorkingSet.Map_TempReg(asmjit::x86::Gpd(), -1);
            m_Assembler.MoveVariableToX64reg(PCReg, &m_Reg.m_PROGRAM_COUNTER, "_PROGRAM_COUNTER", false);
            m_Assembler.and_(PCReg.r32(), 0xF0000000);
            m_Assembler.add(PCReg.r32(), m_Opcode.target << 2);
            m_Assembler.MovDwordToVariable(&m_Reg.m_PROGRAM_COUNTER, "PROGRAM_COUNTER", PCReg);

            const uint64_t TargetPC = (m_CompilePC & 0xFFFFFFFFF0000000) + (m_Opcode.target << 2);
            const bool bCheck = TargetPC <= m_CompilePC;
            if (bCheck)
            {
                UpdateCounters(m_RegWorkingSet, bCheck, true);
            }
            CompileExit((uint32_t)-1, (uint32_t)-1, m_RegWorkingSet, bCheck ? ExitReason_Normal : ExitReason_NormalNoSysCheck);
        }
        m_PipelineStage = PIPELINE_STAGE_END_BLOCK;
    }
    else
    {
        g_Notify->BreakPoint(__FILE__, __LINE__);
    }
}

void CX64RecompilerOps::ADDI()
{
    // overflow raises an exception: a 32 bit core compiles it, with a register operand
    if (!g_GameSettings.core32Bit || m_Opcode.rt == 0 || m_Opcode.rs == 0 || m_RegWorkingSet.IsConst(m_Opcode.rs))
    {
        CompileInterpreterOp(m_Opcode.rt);
        return;
    }
    if (m_RegWorkingSet.IsMapped(m_Opcode.rs))
    {
        m_RegWorkingSet.ProtectGPR(m_Opcode.rs);
    }
    const asmjit::x86::Gp Reg = m_RegWorkingSet.Map_TempReg(asmjit::x86::Gpd(), m_Opcode.rs);
    m_RegWorkingSet.SetX64Protected(Reg.id(), true);
    m_Assembler.add(Reg.r32(), (int32_t)((int16_t)m_Opcode.immediate));
    CompileExit(m_CompilePC, m_CompilePC, m_RegWorkingSet.WithAddedCycles(g_GameSettings.countPerOp), ExitReason_ExceptionOverflow, &CX64Ops::JoLabel);
    m_RegWorkingSet.Map_GPR_32bit(m_Opcode.rt, true, -1);
    m_Assembler.mov(m_RegWorkingSet.GetMipsRegMap(m_Opcode.rt).r32(), Reg.r32());
    if (g_GameSettings.fastSP && m_Opcode.rt == 29)
    {
        m_RegWorkingSet.ResetRegisterProtection();
        ResetMemoryStack();
    }
}

void CX64RecompilerOps::ADDIU()
{
    if (m_Opcode.rt == 0)
    {
        return;
    }
    if (!g_GameSettings.core32Bit)
    {
        CompileInterpreterOp(m_Opcode.rt);
        return;
    }
    // the stack pointer moves in the memory of the host as well
    if (g_GameSettings.fastSP && m_Opcode.rs == 29 && m_Opcode.rt == 29)
    {
        const asmjit::x86::Gp StackReg = m_RegWorkingSet.Map_MemoryStack(asmjit::x86::Gpq(), true, true);
        m_Assembler.add(StackReg.r64(), (int32_t)((int16_t)m_Opcode.immediate));
    }
    CompileBinary32(0, m_Opcode.rt, m_Opcode.rs, -1, true, (uint32_t)(int32_t)(int16_t)m_Opcode.immediate);
    if (g_GameSettings.fastSP && m_Opcode.rt == 29 && m_Opcode.rs != 29)
    {
        m_RegWorkingSet.ResetRegisterProtection();
        ResetMemoryStack();
    }
}

void CX64RecompilerOps::SLTI()
{
    if (!g_GameSettings.core32Bit)
    {
        CompileInterpreterOp(m_Opcode.rt);
        return;
    }
    CompileSetLess32(false, m_Opcode.rt, m_Opcode.rs, -1, true, (int16_t)m_Opcode.immediate);
}

void CX64RecompilerOps::SLTIU()
{
    if (!g_GameSettings.core32Bit)
    {
        CompileInterpreterOp(m_Opcode.rt);
        return;
    }
    CompileSetLess32(true, m_Opcode.rt, m_Opcode.rs, -1, true, (int16_t)m_Opcode.immediate);
}

void CX64RecompilerOps::ANDI()
{
    if (!g_GameSettings.core32Bit)
    {
        CompileInterpreterOp(m_Opcode.rt);
        return;
    }
    CompileBinary32(2, m_Opcode.rt, m_Opcode.rs, -1, true, m_Opcode.immediate);
    if (g_GameSettings.fastSP && m_Opcode.rt == 29)
    {
        m_RegWorkingSet.ResetRegisterProtection();
        ResetMemoryStack();
    }
}

void CX64RecompilerOps::ORI()
{
    if (!g_GameSettings.core32Bit)
    {
        CompileInterpreterOp(m_Opcode.rt);
        return;
    }
    CompileBinary32(3, m_Opcode.rt, m_Opcode.rs, -1, true, m_Opcode.immediate);
    if (g_GameSettings.fastSP && m_Opcode.rt == 29)
    {
        m_RegWorkingSet.ResetRegisterProtection();
        ResetMemoryStack();
    }
}

void CX64RecompilerOps::XORI()
{
    if (!g_GameSettings.core32Bit)
    {
        CompileInterpreterOp(m_Opcode.rt);
        return;
    }
    CompileBinary32(4, m_Opcode.rt, m_Opcode.rs, -1, true, m_Opcode.immediate);
    if (g_GameSettings.fastSP && m_Opcode.rt == 29)
    {
        m_RegWorkingSet.ResetRegisterProtection();
        ResetMemoryStack();
    }
}

void CX64RecompilerOps::LUI()
{
    if (m_Opcode.rt == 0)
    {
        return;
    }

    if (g_GameSettings.fastSP && m_Opcode.rt == 29)
    {
        uint32_t Address = 0;

        m_MMU.VAddrToPAddr(((int16_t)m_Opcode.offset << 16), Address);
        const uint64_t stackPtrVal = (uint64_t)(Address + m_MMU.Rdram());
        void * const var = &m_Recompiler->MemoryStackPos();
        m_Assembler.MoveConst64ToVariable(var, "MemoryStack", stackPtrVal);
    }

    m_RegWorkingSet.UnMap_GPR(m_Opcode.rt, false);
    m_RegWorkingSet.SetMipsRegLo(m_Opcode.rt, (uint32_t)((int16_t)m_Opcode.offset << 16));
    m_RegWorkingSet.SetMipsRegState(m_Opcode.rt, CRegBase::STATE_CONST_32_SIGN);
}

void CX64RecompilerOps::DADDI()
{
    CompileInterpreterOp(m_Opcode.rt);
}

void CX64RecompilerOps::DADDIU()
{
    CompileInterpreterOp(m_Opcode.rt);
}

void CX64RecompilerOps::LDL()
{
    CompileInterpreterOp(m_Opcode.rt);
}

void CX64RecompilerOps::LDR()
{
    CompileInterpreterOp(m_Opcode.rt);
}

void CX64RecompilerOps::RESERVED31()
{
    UnknownOpcode();
}

void CX64RecompilerOps::LWL()
{
    CompileInterpreterOp(m_Opcode.rt);
}

void CX64RecompilerOps::LWR()
{
    CompileInterpreterOp(m_Opcode.rt);
}

void CX64RecompilerOps::LWU()
{
    CompileInterpreterOp(m_Opcode.rt);
}

void CX64RecompilerOps::SWL()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::SWR()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::SDL()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::SDR()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::CACHE()
{
    if (g_Settings->LoadDword(Game_SMM_Cache) == 0)
    {
        return;
    }

    switch (m_Opcode.rt)
    {
    case 0:
    case 16:
        m_RegWorkingSet.BeforeCallDirect();
        if (m_RegWorkingSet.IsConst(m_Opcode.base))
        {
            const uint32_t Address = m_RegWorkingSet.GetMipsRegLo(m_Opcode.base) + (int16_t)m_Opcode.offset;
            m_Assembler.mov(X64Abi::Arg1.r32(), Address);
        }
        else if (m_RegWorkingSet.IsMapped(m_Opcode.base))
        {
            m_Assembler.lea(X64Abi::Arg1.r32(), asmjit::x86::dword_ptr(m_RegWorkingSet.GetMipsRegMap(m_Opcode.base).r32(), (int32_t)(int16_t)m_Opcode.offset));
        }
        else
        {
            m_Assembler.MoveVariableToX64reg(X64Abi::Arg1.r32(), &m_Reg.m_GPR[m_Opcode.base].UW[0], CRegName::GPR_Lo[m_Opcode.base], false);
            m_Assembler.add(X64Abi::Arg1.r32(), (int32_t)(int16_t)m_Opcode.offset);
        }
        m_Assembler.mov(X64Abi::Arg2.r32(), 0x20);
        m_Assembler.mov(X64Abi::Arg3.r32(), (uint32_t)CRecompiler::Remove_Cache);
        m_Assembler.sub(asmjit::x86::rsp, 32);
        m_Assembler.CallThis(m_Recompiler, MemberFuncAddress(&CRecompiler::ClearRecompCode_Virt), "CRecompiler::ClearRecompCode_Virt");
        m_Assembler.add(asmjit::x86::rsp, 32);
        m_RegWorkingSet.AfterCallDirect();
        break;
    case 1:
    case 3:
    case 13:
    case 5:
    case 8:
    case 9:
    case 17:
    case 21:
    case 25:
        break;
    default:
        if (g_DebugSettings.haveDebugger)
        {
            g_Notify->DisplayError(stdstr_f("cache: %d", m_Opcode.rt).c_str());
        }
        break;
    }
}

void CX64RecompilerOps::LL()
{
    CompileInterpreterOp(m_Opcode.rt);
}

void CX64RecompilerOps::LWC1()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::LDC1()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::SC()
{
    CompileInterpreterOp(m_Opcode.rt);
}

void CX64RecompilerOps::SWC1()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::SDC1()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::SPECIAL_SLL()
{
    if (m_Opcode.rd == 0)
    {
        return;
    }
    if (m_RegWorkingSet.IsConst(m_Opcode.rt))
    {
        if (m_RegWorkingSet.IsMapped(m_Opcode.rd))
        {
            m_RegWorkingSet.UnMap_GPR(m_Opcode.rd, false);
        }

        m_RegWorkingSet.SetMipsRegLo(m_Opcode.rd, m_RegWorkingSet.GetMipsRegLo(m_Opcode.rt) << m_Opcode.sa);
        m_RegWorkingSet.SetMipsRegState(m_Opcode.rd, CRegInfo::STATE_CONST_32_SIGN);
    }
    else
    {
        m_RegWorkingSet.Map_GPR_32bit(m_Opcode.rd, true, m_Opcode.rt);
        m_Assembler.shl(m_RegWorkingSet.GetMipsRegMap(m_Opcode.rd).r32(), (uint8_t)m_Opcode.sa);
    }
}

void CX64RecompilerOps::SPECIAL_SRL()
{
    if (m_Opcode.rd == 0)
    {
        return;
    }

    if (m_RegWorkingSet.IsConst(m_Opcode.rt))
    {
        if (m_RegWorkingSet.IsMapped(m_Opcode.rd))
        {
            m_RegWorkingSet.UnMap_GPR(m_Opcode.rd, false);
        }
        m_RegWorkingSet.SetMipsRegLo(m_Opcode.rd, m_RegWorkingSet.GetMipsRegLo(m_Opcode.rt) >> m_Opcode.sa);
        m_RegWorkingSet.SetMipsRegState(m_Opcode.rd, CRegInfo::STATE_CONST_32_SIGN);
    }
    else
    {
        m_RegWorkingSet.Map_GPR_32bit(m_Opcode.rd, true, m_Opcode.rt);
        m_Assembler.shr(m_RegWorkingSet.GetMipsRegMap(m_Opcode.rd).r32(), (uint8_t)m_Opcode.sa);
    }
}

void CX64RecompilerOps::SPECIAL_SRA()
{
    if (m_Opcode.rd == 0)
    {
        return;
    }
    if (m_RegWorkingSet.IsConst(m_Opcode.rt))
    {
        if (m_RegWorkingSet.IsMapped(m_Opcode.rd))
        {
            m_RegWorkingSet.UnMap_GPR(m_Opcode.rd, false);
        }
        m_RegWorkingSet.SetMipsRegLo(m_Opcode.rd, m_RegWorkingSet.Is64Bit(m_Opcode.rt) ? (uint32_t)(int32_t)((int64_t)m_RegWorkingSet.GetMipsReg(m_Opcode.rt) >> m_Opcode.sa) : (uint32_t)(m_RegWorkingSet.GetMipsRegLo_S(m_Opcode.rt) >> m_Opcode.sa));
        m_RegWorkingSet.SetMipsRegState(m_Opcode.rd, CRegBase::STATE_CONST_32_SIGN);
    }
    else if (g_GameSettings.core32Bit)
    {
        m_RegWorkingSet.Map_GPR_32bit(m_Opcode.rd, true, m_Opcode.rt);
        m_Assembler.sar(m_RegWorkingSet.GetMipsRegMap(m_Opcode.rd).r32(), (uint8_t)m_Opcode.sa);
    }
    else
    {
        CompileInterpreterOp(m_Opcode.rd);
    }
}

void CX64RecompilerOps::SPECIAL_SLLV()
{
    if (m_Opcode.rd == 0)
    {
        return;
    }
    if (!g_GameSettings.core32Bit || m_RegWorkingSet.IsConst(m_Opcode.rs))
    {
        CompileInterpreterOp(m_Opcode.rd);
        return;
    }
    m_RegWorkingSet.Map_TempReg(asmjit::x86::ecx, m_Opcode.rs);
    m_Assembler.and_(asmjit::x86::ecx, 0x1F);
    m_RegWorkingSet.Map_GPR_32bit(m_Opcode.rd, true, m_Opcode.rt);
    m_Assembler.shl(m_RegWorkingSet.GetMipsRegMap(m_Opcode.rd).r32(), asmjit::x86::cl);
}

void CX64RecompilerOps::SPECIAL_SRLV()
{
    if (m_Opcode.rd == 0)
    {
        return;
    }
    if (!g_GameSettings.core32Bit || m_RegWorkingSet.IsConst(m_Opcode.rs))
    {
        CompileInterpreterOp(m_Opcode.rd);
        return;
    }
    m_RegWorkingSet.Map_TempReg(asmjit::x86::ecx, m_Opcode.rs);
    m_Assembler.and_(asmjit::x86::ecx, 0x1F);
    m_RegWorkingSet.Map_GPR_32bit(m_Opcode.rd, true, m_Opcode.rt);
    m_Assembler.shr(m_RegWorkingSet.GetMipsRegMap(m_Opcode.rd).r32(), asmjit::x86::cl);
}

void CX64RecompilerOps::SPECIAL_SRAV()
{
    CompileInterpreterOp(m_Opcode.rd);
}

void CX64RecompilerOps::SPECIAL_JR()
{
    if (m_PipelineStage == PIPELINE_STAGE_NORMAL)
    {
        if ((m_CompilePC & 0xFFC) == 0xFFC)
        {
            StoreRegisterTo64(&g_System->m_JumpToLocation, "System::m_JumpToLocation", m_Opcode.rs);
            m_RegWorkingSet.WriteBackRegisters();
            OverflowDelaySlot(true);
            return;
        }

        m_Section->m_Jump.FallThrough = false;
        m_Section->m_Jump.LinkLocation = asmjit::Label();
        m_Section->m_Jump.LinkLocation2 = asmjit::Label();
        m_Section->m_Cont.FallThrough = false;
        m_Section->m_Cont.LinkLocation = asmjit::Label();
        m_Section->m_Cont.LinkLocation2 = asmjit::Label();

        R4300iOpcode DelaySlot;
        if (g_MMU->MemoryValue32((uint32_t)(m_CompilePC + 4), DelaySlot.Value) &&
            R4300iInstruction(m_CompilePC, m_Opcode.Value).DelaySlotEffectsCompare(DelaySlot.Value))
        {
            // the delay slot changes the register: the target is taken now (not into the program
            // counter: the operations of the delay slot that may raise an exception set that)
            StoreRegisterTo64(&m_BranchTarget, "BranchTarget", m_Opcode.rs);
        }
        m_PipelineStage = PIPELINE_STAGE_DO_DELAY_SLOT;
    }
    else if (m_PipelineStage == PIPELINE_STAGE_DELAY_SLOT_DONE)
    {
        R4300iOpcode DelaySlot;
        if (g_MMU->MemoryValue32((uint32_t)(m_CompilePC + 4), DelaySlot.Value) && R4300iInstruction(m_CompilePC, m_Opcode.Value).DelaySlotEffectsCompare(DelaySlot.Value))
        {
            LoadBranchTargetToPC();
            CompileExit(m_CompilePC, (uint32_t)-1, m_RegWorkingSet, ExitReason_CheckPCAlignment);
        }
        else
        {
            StoreRegisterTo64(&m_Reg.m_PROGRAM_COUNTER, "PROGRAM_COUNTER", m_Opcode.rs);
            UpdateCounters(m_RegWorkingSet, true, true, false);
            CompileExit((uint32_t)-1, (uint32_t)-1, m_RegWorkingSet, ExitReason_CheckPCAlignment);
            if (m_Section->m_JumpSection)
            {
                m_Section->GenerateSectionLinkage();
            }
        }
        m_PipelineStage = PIPELINE_STAGE_END_BLOCK;
    }
    else if (g_DebugSettings.haveDebugger)
    {
        g_Notify->DisplayError(stdstr_f("WTF\n\nBranch\nNextInstruction = %X", m_PipelineStage).c_str());
    }
}

void CX64RecompilerOps::SPECIAL_JALR()
{
    if (m_PipelineStage == PIPELINE_STAGE_NORMAL)
    {
        R4300iOpcode DelaySlot;
        const bool DelayEffects = g_MMU->MemoryValue32((uint32_t)(m_CompilePC + 4), DelaySlot.Value) &&
                                  R4300iInstruction(m_CompilePC, m_Opcode.Value).DelaySlotEffectsCompare(DelaySlot.Value);
        if ((DelayEffects || m_Opcode.rd == m_Opcode.rs) && (m_CompilePC & 0xFFC) != 0xFFC)
        {
            // the target is taken now: the delay slot, or the link register, is the register
            StoreRegisterTo64(&m_BranchTarget, "BranchTarget", m_Opcode.rs);
            m_PreStoredTarget = true;
        }
        else
        {
            m_PreStoredTarget = false;
        }
        if ((m_CompilePC & 0xFFC) == 0xFFC)
        {
            StoreRegisterTo64(&g_System->m_JumpToLocation, "System::m_JumpToLocation", m_Opcode.rs);
        }
        if (m_Opcode.rd != 0)
        {
            m_RegWorkingSet.UnMap_GPR(m_Opcode.rd, false);
            m_RegWorkingSet.SetMipsRegLo(m_Opcode.rd, (uint32_t)(m_CompilePC + 8));
            m_RegWorkingSet.SetMipsRegState(m_Opcode.rd, CRegInfo::STATE_CONST_32_SIGN);
        }
        if ((m_CompilePC & 0xFFC) == 0xFFC)
        {
            m_RegWorkingSet.WriteBackRegisters();
            OverflowDelaySlot(true);
            return;
        }

        m_Section->m_Jump.FallThrough = false;
        m_Section->m_Jump.LinkLocation = asmjit::Label();
        m_Section->m_Jump.LinkLocation2 = asmjit::Label();
        m_Section->m_Cont.FallThrough = false;
        m_Section->m_Cont.LinkLocation = asmjit::Label();
        m_Section->m_Cont.LinkLocation2 = asmjit::Label();

        m_PipelineStage = PIPELINE_STAGE_DO_DELAY_SLOT;
    }
    else if (m_PipelineStage == PIPELINE_STAGE_DELAY_SLOT_DONE)
    {
        if (m_PreStoredTarget)
        {
            LoadBranchTargetToPC();
            UpdateCounters(m_RegWorkingSet, true, true, false);
            CompileExit((uint32_t)-1, (uint32_t)-1, m_RegWorkingSet, ExitReason_CheckPCAlignment);
            if (m_Section->m_JumpSection)
            {
                m_Section->GenerateSectionLinkage();
            }
        }
        else
        {
            StoreRegisterTo64(&m_Reg.m_PROGRAM_COUNTER, "PROGRAM_COUNTER", m_Opcode.rs);
            UpdateCounters(m_RegWorkingSet, true, true, false);
            CompileExit((uint32_t)-1, (uint32_t)-1, m_RegWorkingSet, ExitReason_CheckPCAlignment);
            if (m_Section->m_JumpSection)
            {
                m_Section->GenerateSectionLinkage();
            }
        }
        m_PipelineStage = PIPELINE_STAGE_END_BLOCK;
    }
    else if (g_DebugSettings.haveDebugger)
    {
        g_Notify->DisplayError(stdstr_f("WTF\n\nBranch\nNextInstruction = %X", m_PipelineStage).c_str());
    }
}

void CX64RecompilerOps::SPECIAL_SYSCALL()
{
    // always raises the exception: the interpreter does it and the block ends
    CompileInterpreterOp(-1);
    if (m_PipelineStage == PIPELINE_STAGE_NORMAL)
    {
        m_PipelineStage = PIPELINE_STAGE_END_BLOCK;
    }
}

void CX64RecompilerOps::SPECIAL_BREAK()
{
    CompileInterpreterOp(-1);
    if (m_PipelineStage == PIPELINE_STAGE_NORMAL)
    {
        m_PipelineStage = PIPELINE_STAGE_END_BLOCK;
    }
}

void CX64RecompilerOps::SPECIAL_SYNC()
{
}

void CX64RecompilerOps::SPECIAL_MFLO()
{
    if (m_Opcode.rd == 0)
    {
        return;
    }

    m_RegWorkingSet.Map_GPR_64bit(m_Opcode.rd, -1);
    m_Assembler.MoveVariable64ToX64reg(m_RegWorkingSet.GetMipsRegMap(m_Opcode.rd), &m_Reg.m_LO.UDW, "RegLO.UDW");
}

void CX64RecompilerOps::SPECIAL_MTLO()
{
    CompileMoveToHiLo(&m_Reg.m_LO.UDW, "RegLO.UDW");
}

void CX64RecompilerOps::SPECIAL_MFHI()
{
    if (m_Opcode.rd == 0)
    {
        return;
    }

    m_RegWorkingSet.Map_GPR_64bit(m_Opcode.rd, -1);
    m_Assembler.MoveVariable64ToX64reg(m_RegWorkingSet.GetMipsRegMap(m_Opcode.rd), &m_Reg.m_HI.UDW, "RegHI.UDW");
}

void CX64RecompilerOps::SPECIAL_MTHI()
{
    CompileMoveToHiLo(&m_Reg.m_HI.UDW, "RegHI.UDW");
}

void CX64RecompilerOps::SPECIAL_DSLLV()
{
    CompileInterpreterOp(m_Opcode.rd);
}

void CX64RecompilerOps::SPECIAL_DSRLV()
{
    CompileInterpreterOp(m_Opcode.rd);
}

void CX64RecompilerOps::SPECIAL_DSRAV()
{
    CompileInterpreterOp(m_Opcode.rd);
}

void CX64RecompilerOps::SPECIAL_MULT()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::SPECIAL_MULTU()
{
    m_RegWorkingSet.SetX64Protected(asmjit::x86::edx.id(), true);
    m_RegWorkingSet.Map_TempReg(asmjit::x86::eax, m_Opcode.rs);
    m_RegWorkingSet.SetX64Protected(asmjit::x86::edx.id(), false);
    m_RegWorkingSet.Map_TempReg(asmjit::x86::edx, m_Opcode.rt);

    m_Assembler.mul(asmjit::x86::edx);

    m_Assembler.MovDwordToVariable(&m_Reg.m_LO.UW[0], "RegLO.UW[0]", asmjit::x86::eax);
    m_Assembler.MovDwordToVariable(&m_Reg.m_HI.UW[0], "RegHI.UW[0]", asmjit::x86::edx);
    m_Assembler.sar(asmjit::x86::eax, 31);
    m_Assembler.sar(asmjit::x86::edx, 31);
    m_Assembler.MovDwordToVariable(&m_Reg.m_LO.UW[1], "RegLO.UW[1]", asmjit::x86::eax);
    m_Assembler.MovDwordToVariable(&m_Reg.m_HI.UW[1], "RegHI.UW[1]", asmjit::x86::edx);
}

void CX64RecompilerOps::SPECIAL_DIV()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::SPECIAL_DIVU()
{
    m_RegWorkingSet.Map_TempReg(asmjit::x86::eax, m_Opcode.rs);
    m_RegWorkingSet.Map_TempReg(asmjit::x86::edx, -1);
    const asmjit::x86::Gp Divisor = m_RegWorkingSet.Map_TempReg(asmjit::x86::Gp(), m_Opcode.rt);

    // Same as R4300iOp::SPECIAL_DIVU: a zero divisor does not trap on the
    // R4300i (LO = -1, HI = sign extended RS) but div would raise #DE on x86
    asmjit::Label DivideByZero = m_Assembler.newLabel();
    asmjit::Label Done = m_Assembler.newLabel();
    m_Assembler.xor_(asmjit::x86::edx, asmjit::x86::edx);
    m_Assembler.test(Divisor.r32(), Divisor.r32());
    m_Assembler.JeLabel("DivuByZero", DivideByZero);
    m_Assembler.div(Divisor.r32());
    m_Assembler.MovDwordToVariable(&m_Reg.m_LO.UW[0], "RegLO.UW[0]", asmjit::x86::eax);
    m_Assembler.MovDwordToVariable(&m_Reg.m_HI.UW[0], "RegHI.UW[0]", asmjit::x86::edx);
    m_Assembler.sar(asmjit::x86::eax, 31);
    m_Assembler.sar(asmjit::x86::edx, 31);
    m_Assembler.MovDwordToVariable(&m_Reg.m_LO.UW[1], "RegLO.UW[1]", asmjit::x86::eax);
    m_Assembler.MovDwordToVariable(&m_Reg.m_HI.UW[1], "RegHI.UW[1]", asmjit::x86::edx);
    m_Assembler.JmpLabel("DivuDone", Done);

    m_Assembler.bind(DivideByZero);
    m_Assembler.MovDwordToVariable(&m_Reg.m_HI.UW[0], "RegHI.UW[0]", asmjit::x86::eax);
    m_Assembler.sar(asmjit::x86::eax, 31);
    m_Assembler.MovDwordToVariable(&m_Reg.m_HI.UW[1], "RegHI.UW[1]", asmjit::x86::eax);
    m_Assembler.MoveConstToVariable(&m_Reg.m_LO.UW[0], "RegLO.UW[0]", 0xFFFFFFFF);
    m_Assembler.MoveConstToVariable(&m_Reg.m_LO.UW[1], "RegLO.UW[1]", 0xFFFFFFFF);
    m_Assembler.bind(Done);
}

void CX64RecompilerOps::SPECIAL_DMULT()
{
    // one-operand imul writes rdx:rax; same register setup as SPECIAL_DMULTU
    m_RegWorkingSet.SetX64Protected(asmjit::x86::edx.id(), true);
    m_RegWorkingSet.Map_TempReg(asmjit::x86::rax, m_Opcode.rs, asmjit::RegType::kX86_Gpq);
    m_RegWorkingSet.SetX64Protected(asmjit::x86::edx.id(), false);
    m_RegWorkingSet.Map_TempReg(asmjit::x86::rdx, m_Opcode.rt, asmjit::RegType::kX86_Gpq);

    m_Assembler.imul(asmjit::x86::rdx);

    m_Assembler.MovQwordToVariable(&m_Reg.m_LO.UDW, "RegLO.UDW", asmjit::x86::rax);
    m_Assembler.MovQwordToVariable(&m_Reg.m_HI.UDW, "RegHI.UDW", asmjit::x86::rdx);
}

void CX64RecompilerOps::SPECIAL_DMULTU()
{
    m_RegWorkingSet.SetX64Protected(asmjit::x86::edx.id(), true);
    m_RegWorkingSet.Map_TempReg(asmjit::x86::rax, m_Opcode.rs, asmjit::RegType::kX86_Gpq);
    m_RegWorkingSet.SetX64Protected(asmjit::x86::edx.id(), false);
    m_RegWorkingSet.Map_TempReg(asmjit::x86::rdx, m_Opcode.rt, asmjit::RegType::kX86_Gpq);

    m_Assembler.mul(asmjit::x86::rdx);

    m_Assembler.MovQwordToVariable(&m_Reg.m_LO.UDW, "RegLO.UDW", asmjit::x86::rax);
    m_Assembler.MovQwordToVariable(&m_Reg.m_HI.UDW, "RegHI.UDW", asmjit::x86::rdx);
}

void CX64RecompilerOps::SPECIAL_DDIV()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::SPECIAL_DDIVU()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::SPECIAL_ADD()
{
    // overflow raises an exception: a 32 bit core compiles the register forms, the interpreter does the rest
    if (!g_GameSettings.core32Bit || m_Opcode.rd == 0 || m_RegWorkingSet.IsConst(m_Opcode.rs) || m_RegWorkingSet.IsConst(m_Opcode.rt) || m_Opcode.rs == 0 || m_Opcode.rt == 0)
    {
        CompileInterpreterOp(m_Opcode.rd);
        return;
    }
    const int source1 = m_Opcode.rd == m_Opcode.rt ? m_Opcode.rt : m_Opcode.rs;
    const int source2 = m_Opcode.rd == m_Opcode.rt ? m_Opcode.rs : m_Opcode.rt;

    m_RegWorkingSet.ProtectGPR(m_Opcode.rd);
    if (m_RegWorkingSet.IsMapped(source2))
    {
        m_RegWorkingSet.ProtectGPR(source2);
    }
    const asmjit::x86::Gp Reg = m_RegWorkingSet.Map_TempReg(asmjit::x86::Gpd(), source1);
    m_RegWorkingSet.SetX64Protected(Reg.id(), true);
    if (m_RegWorkingSet.IsMapped(source2))
    {
        m_Assembler.add(Reg.r32(), m_RegWorkingSet.GetMipsRegMap(source2).r32());
    }
    else
    {
        m_Assembler.AddDwordFromVariable(Reg, &m_Reg.m_GPR[source2].W[0], CRegName::GPR_Lo[source2]);
    }
    CompileExit(m_CompilePC, m_CompilePC, m_RegWorkingSet.WithAddedCycles(g_GameSettings.countPerOp), ExitReason_ExceptionOverflow, &CX64Ops::JoLabel);
    m_RegWorkingSet.Map_GPR_32bit(m_Opcode.rd, true, -1);
    m_Assembler.mov(m_RegWorkingSet.GetMipsRegMap(m_Opcode.rd).r32(), Reg.r32());
    if (g_GameSettings.fastSP && m_Opcode.rd == 29)
    {
        m_RegWorkingSet.ResetRegisterProtection();
        ResetMemoryStack();
    }
}

void CX64RecompilerOps::SPECIAL_ADDU()
{
    if (!g_GameSettings.core32Bit)
    {
        CompileInterpreterOp(m_Opcode.rd);
        return;
    }
    CompileBinary32(0, m_Opcode.rd, m_Opcode.rs, m_Opcode.rt, false, 0);
    if (g_GameSettings.fastSP && m_Opcode.rd == 29)
    {
        m_RegWorkingSet.ResetRegisterProtection();
        ResetMemoryStack();
    }
}

void CX64RecompilerOps::SPECIAL_SUB()
{
    CompileInterpreterOp(m_Opcode.rd);
}

void CX64RecompilerOps::SPECIAL_SUBU()
{
    if (!g_GameSettings.core32Bit)
    {
        CompileInterpreterOp(m_Opcode.rd);
        return;
    }
    CompileBinary32(1, m_Opcode.rd, m_Opcode.rs, m_Opcode.rt, false, 0);
    if (g_GameSettings.fastSP && m_Opcode.rd == 29)
    {
        m_RegWorkingSet.ResetRegisterProtection();
        ResetMemoryStack();
    }
}

void CX64RecompilerOps::SPECIAL_AND()
{
    if (!g_GameSettings.core32Bit)
    {
        CompileInterpreterOp(m_Opcode.rd);
        return;
    }
    CompileBinary32(2, m_Opcode.rd, m_Opcode.rs, m_Opcode.rt, false, 0);
    if (g_GameSettings.fastSP && m_Opcode.rd == 29)
    {
        m_RegWorkingSet.ResetRegisterProtection();
        ResetMemoryStack();
    }
}

void CX64RecompilerOps::SPECIAL_OR()
{
    if (!g_GameSettings.core32Bit)
    {
        CompileInterpreterOp(m_Opcode.rd);
        return;
    }
    CompileBinary32(3, m_Opcode.rd, m_Opcode.rs, m_Opcode.rt, false, 0);
    if (g_GameSettings.fastSP && m_Opcode.rd == 29)
    {
        m_RegWorkingSet.ResetRegisterProtection();
        ResetMemoryStack();
    }
}

void CX64RecompilerOps::SPECIAL_XOR()
{
    if (!g_GameSettings.core32Bit)
    {
        CompileInterpreterOp(m_Opcode.rd);
        return;
    }
    CompileBinary32(4, m_Opcode.rd, m_Opcode.rs, m_Opcode.rt, false, 0);
    if (g_GameSettings.fastSP && m_Opcode.rd == 29)
    {
        m_RegWorkingSet.ResetRegisterProtection();
        ResetMemoryStack();
    }
}

void CX64RecompilerOps::SPECIAL_NOR()
{
    CompileInterpreterOp(m_Opcode.rd);
}

void CX64RecompilerOps::SPECIAL_SLT()
{
    if (!g_GameSettings.core32Bit)
    {
        CompileInterpreterOp(m_Opcode.rd);
        return;
    }
    CompileSetLess32(false, m_Opcode.rd, m_Opcode.rs, m_Opcode.rt, false, 0);
}

void CX64RecompilerOps::SPECIAL_SLTU()
{
    if (!g_GameSettings.core32Bit)
    {
        CompileInterpreterOp(m_Opcode.rd);
        return;
    }
    CompileSetLess32(true, m_Opcode.rd, m_Opcode.rs, m_Opcode.rt, false, 0);
}

void CX64RecompilerOps::SPECIAL_DADD()
{
    CompileInterpreterOp(m_Opcode.rd);
}

void CX64RecompilerOps::SPECIAL_DADDU()
{
    CompileInterpreterOp(m_Opcode.rd);
}

void CX64RecompilerOps::SPECIAL_DSUB()
{
    CompileInterpreterOp(m_Opcode.rd);
}

void CX64RecompilerOps::SPECIAL_DSUBU()
{
    CompileInterpreterOp(m_Opcode.rd);
}

void CX64RecompilerOps::SPECIAL_DSLL()
{
    CompileInterpreterOp(m_Opcode.rd);
}

void CX64RecompilerOps::SPECIAL_DSRL()
{
    CompileInterpreterOp(m_Opcode.rd);
}

void CX64RecompilerOps::SPECIAL_DSRA()
{
    CompileInterpreterOp(m_Opcode.rd);
}

void CX64RecompilerOps::SPECIAL_DSLL32()
{
    CompileInterpreterOp(m_Opcode.rd);
}

void CX64RecompilerOps::SPECIAL_DSRL32()
{
    CompileInterpreterOp(m_Opcode.rd);
}

void CX64RecompilerOps::SPECIAL_DSRA32()
{
    CompileInterpreterOp(m_Opcode.rd);
}

void CX64RecompilerOps::COP0_MF()
{
    if (m_Opcode.rd == CRegisters::COP0Reg_Count)
    {
        UpdateCounters(m_RegWorkingSet, false, true);
    }

    if (m_Opcode.rt == 0)
    {
        return;
    }

    if (m_RegWorkingSet.IsMapped(m_Opcode.rt))
    {
        m_RegWorkingSet.UnMap_GPR(m_Opcode.rt, false);
    }

    m_RegWorkingSet.BeforeCallDirect();
    m_Assembler.mov(X64Abi::Arg1.r32(), m_Opcode.rd);
    m_Assembler.sub(asmjit::x86::rsp, 32);
    m_Assembler.CallThis(&m_Reg, MemberFuncAddress(&CRegisters::Cop0_MF), "CRegisters::Cop0_MF");
    m_Assembler.add(asmjit::x86::rsp, 32);
    m_Assembler.MovDwordToVariable(&m_Reg.m_GPR[m_Opcode.rt].UW[0], CRegName::GPR_Lo[m_Opcode.rt], asmjit::x86::eax);
    m_RegWorkingSet.AfterCallDirect();
    m_RegWorkingSet.Map_GPR_32bit(m_Opcode.rt, true, -1);
    m_Assembler.MoveVariableToX64reg(m_RegWorkingSet.GetMipsRegMap(m_Opcode.rt), &m_Reg.m_GPR[m_Opcode.rt].UW[0], CRegName::GPR_Lo[m_Opcode.rt], true);
}

void CX64RecompilerOps::COP0_DMF()
{
    CompileInterpreterOp(m_Opcode.rt);
}

void CX64RecompilerOps::COP0_MT()
{
    if (m_Opcode.rd == CRegisters::COP0Reg_Wired || m_Opcode.rd == CRegisters::COP0Reg_Compare || m_Opcode.rd == CRegisters::COP0Reg_Count)
    {
        UpdateCounters(m_RegWorkingSet, false, true);
    }
    m_RegWorkingSet.BeforeCallDirect();
    if (m_RegWorkingSet.IsConst(m_Opcode.rt))
    {
        m_Assembler.MoveConstToX64reg(X64Abi::Arg2, (uint64_t)(int64_t)m_RegWorkingSet.GetMipsRegLo_S(m_Opcode.rt));
    }
    else if (m_RegWorkingSet.IsMapped(m_Opcode.rt))
    {
        m_Assembler.movsxd(X64Abi::Arg2, m_RegWorkingSet.GetMipsRegMap(m_Opcode.rt).r32());
    }
    else
    {
        m_Assembler.MoveVariableToX64reg(X64Abi::Arg2, &m_Reg.m_GPR[m_Opcode.rt].UW[0], CRegName::GPR_Lo[m_Opcode.rt], true);
    }
    m_Assembler.mov(X64Abi::Arg1.r32(), m_Opcode.rd);
    m_Assembler.sub(asmjit::x86::rsp, 32);
    m_Assembler.CallThis(&m_Reg, MemberFuncAddress(&CRegisters::Cop0_MT), "CRegisters::Cop0_MT");
    m_Assembler.add(asmjit::x86::rsp, 32);
    m_RegWorkingSet.AfterCallDirect();
}

void CX64RecompilerOps::COP0_DMT()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP0_CO_TLBR()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP0_CO_TLBWI()
{
    m_RegWorkingSet.BeforeCallDirect();
    m_Assembler.MoveVariableToX64reg(X64Abi::Arg1.r32(), &m_Reg.INDEX_REGISTER, "INDEX_REGISTER", false);
    m_Assembler.and_(X64Abi::Arg1.r32(), 0x1F);
    m_Assembler.xor_(X64Abi::Arg2.r32(), X64Abi::Arg2.r32());
    m_Assembler.sub(asmjit::x86::rsp, 32);
    m_Assembler.CallThis(&m_TLB, MemberFuncAddress(&CTLB::WriteEntry), "CTLB::WriteEntry");
    m_Assembler.add(asmjit::x86::rsp, 32);
    m_RegWorkingSet.AfterCallDirect();
}

void CX64RecompilerOps::COP0_CO_TLBWR()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP0_CO_TLBP()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP0_CO_ERET()
{
    m_RegWorkingSet.SetBlockCycleCount(m_RegWorkingSet.GetBlockCycleCount() + g_GameSettings.countPerOp);
    m_RegWorkingSet.WriteBackRegisters();
    m_RegWorkingSet.BeforeCallDirect();
    m_Assembler.sub(asmjit::x86::rsp, 32);
    m_Assembler.CallThis(&m_System.m_OpCodes, MemberFuncAddress(&R4300iOp::COP0_CO_ERET), "R4300iOp::COP0_CO_ERET");
    m_Assembler.add(asmjit::x86::rsp, 32);
    m_RegWorkingSet.AfterCallDirect();

    UpdateCounters(m_RegWorkingSet, true, true);
    CompileExit(m_CompilePC, (uint32_t)-1, m_RegWorkingSet, ExitReason_Eret);
    m_PipelineStage = PIPELINE_STAGE_END_BLOCK;
}

void CX64RecompilerOps::COP1_MF()
{
    CompileInterpreterOp(m_Opcode.rt);
}

void CX64RecompilerOps::COP1_DMF()
{
    CompileInterpreterOp(m_Opcode.rt);
}

void CX64RecompilerOps::COP1_CF()
{
    CompileInterpreterOp(m_Opcode.rt);
}

void CX64RecompilerOps::COP1_MT()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_DMT()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_CT()
{
    // writing the control register can raise a floating point exception
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_S_ADD()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_S_SUB()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_S_MUL()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_S_DIV()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_S_ABS()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_S_NEG()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_S_SQRT()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_S_MOV()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_S_ROUND_L()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_S_TRUNC_L()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_S_CEIL_L()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_S_FLOOR_L()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_S_ROUND_W()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_S_TRUNC_W()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_S_CEIL_W()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_S_FLOOR_W()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_S_CVT_D()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_S_CVT_W()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_S_CVT_L()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_S_CMP()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_D_ADD()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_D_SUB()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_D_MUL()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_D_DIV()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_D_ABS()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_D_NEG()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_D_SQRT()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_D_MOV()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_D_ROUND_L()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_D_TRUNC_L()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_D_CEIL_L()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_D_FLOOR_L()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_D_ROUND_W()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_D_TRUNC_W()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_D_CEIL_W()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_D_FLOOR_W()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_D_CVT_S()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_D_CVT_W()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_D_CVT_L()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_D_CMP()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_W_CVT_S()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_W_CVT_D()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_L_CVT_S()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::COP1_L_CVT_D()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::UnknownOpcode()
{
    m_CodeBlock.Log("  %X Unhandled opcode: %s", m_CompilePC, R4300iInstruction(m_CompilePC, m_Opcode.Value).NameAndParam().c_str());

    m_RegWorkingSet.WriteBackRegisters();
    UpdateCounters(m_RegWorkingSet, false, true);
    m_Assembler.MoveConstToVariable(&g_Reg->m_PROGRAM_COUNTER, "PROGRAM_COUNTER", m_CompilePC);
    if (g_SyncSystem)
    {
        m_Assembler.sub(asmjit::x86::rsp, 32);
        m_Assembler.CallThis(g_BaseSystem, MemberFuncAddress(&CN64System::SyncSystem), "CN64System::SyncSystem");
        m_Assembler.add(asmjit::x86::rsp, 32);
    }
    m_Assembler.MoveConstToVariable(&g_System->m_OpCodes.m_Opcode.Value, "R4300iOp::m_Opcode.Value", m_Opcode.Value);
    m_Assembler.sub(asmjit::x86::rsp, 32);
    m_Assembler.CallThis(&g_System->m_OpCodes, MemberFuncAddress(&R4300iOp::UnknownOpcode), "R4300iOp::UnknownOpcode");
    m_Assembler.add(asmjit::x86::rsp, 32);
    ExitCodeBlock();
    if (m_PipelineStage == PIPELINE_STAGE_NORMAL)
    {
        m_PipelineStage = PIPELINE_STAGE_END_BLOCK;
    }
}

void CX64RecompilerOps::EnterCodeBlock()
{
#if !defined(_WIN32)
    // System V AMD64: rbx, rbp and r12-r15 are callee-saved and are used by the
    // register cache, so preserve them for the C++ caller (6 pushes keep the
    // frame size below unchanged modulo 16).
    m_Assembler.push(asmjit::x86::rbx);
    m_Assembler.push(asmjit::x86::rbp);
    m_Assembler.push(asmjit::x86::r12);
    m_Assembler.push(asmjit::x86::r13);
    m_Assembler.push(asmjit::x86::r14);
    m_Assembler.push(asmjit::x86::r15);
#endif
    m_Assembler.sub(asmjit::x86::rsp, FunctionStackSize);
}

void CX64RecompilerOps::CompileExitCode()
{
}

void CX64RecompilerOps::CompileInPermLoop(CRegInfo & RegSet, uint32_t ProgramCounter)
{
    m_Assembler.MoveConstToVariable(&m_Reg.m_PROGRAM_COUNTER, "PROGRAM_COUNTER", ProgramCounter);
    RegSet.WriteBackRegisters();
    UpdateCounters(RegSet, false, true, false);
    m_Assembler.sub(asmjit::x86::rsp, 32);
    m_Assembler.CallThis(&g_System->m_OpCodes, MemberFuncAddress(&R4300iOp::InPermLoop), "R4300iOp::InPermLoop");
    m_Assembler.CallThis(g_SystemTimer, MemberFuncAddress(&CSystemTimer::TimerDone), "CSystemTimer::TimerDone");
    m_Assembler.add(asmjit::x86::rsp, 32);
    m_CodeBlock.Log("CompileSystemCheck 3");
    CompileSystemCheck((uint32_t)-1, RegSet);
    if (g_SyncSystem)
    {
        m_Assembler.sub(asmjit::x86::rsp, 32);
        m_Assembler.CallThis(g_BaseSystem, MemberFuncAddress(&CN64System::SyncSystem), "CN64System::SyncSystem");
        m_Assembler.add(asmjit::x86::rsp, 32);
    }
}

// A section that more than one path leads to (a join, the head of a loop) is entered with every
// MIPS register in memory (see InheritParentInfo), so what each path has to do is write its
// registers back
void CX64RecompilerOps::SyncRegState(const CRegInfo & SyncTo)
{
    for (int32_t i = 1; i < 32; i++)
    {
        if (SyncTo.GetMipsRegState(i) != CRegBase::STATE_UNKNOWN)
        {
            m_CodeBlock.Log("Section is entered with register %d (%s) in state %d", i, CRegName::GPR[i], SyncTo.GetMipsRegState(i));
            g_Notify->BreakPoint(__FILE__, __LINE__);
        }
    }
    m_RegWorkingSet.WriteBackRegisters();
    m_RegWorkingSet.SetFpuBeenUsed(false);
    m_RegWorkingSet.SetRoundingModel(CRegBase::RoundUnknown);
}

CX64RegInfo & CX64RecompilerOps::GetRegWorkingSet(void)
{
    return m_RegWorkingSet;
}

void CX64RecompilerOps::SetRegWorkingSet(const CX64RegInfo & RegInfo)
{
    m_RegWorkingSet = RegInfo;
}

bool CX64RecompilerOps::InheritParentInfo()
{
    m_Section->DisplaySectionInformation();

    if (m_Section->m_ParentSection.empty())
    {
        SetRegWorkingSet(m_Section->m_RegEnter);
        return true;
    }

    if (m_Section->m_ParentSection.size() == 1)
    {
        CCodeSection * Parent = *(m_Section->m_ParentSection.begin());
        if (!Parent->m_EnterLabel.isValid())
        {
            g_Notify->BreakPoint(__FILE__, __LINE__);
        }
        CJumpInfo * JumpInfo = m_Section == Parent->m_ContinueSection ? &Parent->m_Cont : &Parent->m_Jump;

        m_Section->m_RegEnter = JumpInfo->RegSet;
        LinkJump(*JumpInfo);
        SetRegWorkingSet(m_Section->m_RegEnter);
        return true;
    }

    // Multiple parents: the section is entered with all the registers in memory. The compiled
    // parents are linked to the start of the section, the one that falls through (there is at
    // most one) is the code in front of it, every other one gets a copy of the write back of
    // its own registers placed where its jump lands.
    BLOCK_PARENT_LIST ParentList;
    CCodeSection::SECTION_LIST::iterator iter;
    size_t NoOfCompiledParents = 0;
    for (int Pass = 0; Pass < 2; Pass++)
    {
        for (iter = m_Section->m_ParentSection.begin(); iter != m_Section->m_ParentSection.end(); iter++)
        {
            CCodeSection * Parent = *iter;
            BLOCK_PARENT BlockParent;

            if (Parent->m_EnterLabel.isValid() != (Pass == 0))
            {
                continue;
            }
            BlockParent.Parent = Parent;
            if (Parent->m_JumpSection != Parent->m_ContinueSection)
            {
                BlockParent.JumpInfo = m_Section == Parent->m_ContinueSection ? &Parent->m_Cont : &Parent->m_Jump;
                ParentList.push_back(BlockParent);
            }
            else
            {
                BlockParent.JumpInfo = &Parent->m_Cont;
                ParentList.push_back(BlockParent);
                BlockParent.JumpInfo = &Parent->m_Jump;
                ParentList.push_back(BlockParent);
            }
        }
        if (Pass == 0)
        {
            NoOfCompiledParents = ParentList.size();
        }
    }
    if (NoOfCompiledParents == 0)
    {
        g_Notify->BreakPoint(__FILE__, __LINE__);
        return false;
    }

    int FirstParent = -1;
    for (size_t i = 0; i < NoOfCompiledParents; i++)
    {
        if (!ParentList[i].JumpInfo->FallThrough)
        {
            continue;
        }
        if (FirstParent != -1)
        {
            g_Notify->BreakPoint(__FILE__, __LINE__);
        }
        FirstParent = (int)i;
    }
    if (FirstParent == -1)
    {
        FirstParent = 0;
    }

    CJumpInfo * JumpInfo = ParentList[FirstParent].JumpInfo;
    SetRegWorkingSet(JumpInfo->RegSet);
    m_RegWorkingSet.ResetRegisterProtection();
    LinkJump(*JumpInfo);

    if (JumpInfo->Reason == ExitReason_NormalNoSysCheck)
    {
        if (JumpInfo->RegSet.GetBlockCycleCount() != 0 || JumpInfo->JumpPC != (uint32_t)-1)
        {
            g_Notify->BreakPoint(__FILE__, __LINE__);
        }
    }
    else
    {
        UpdateCounters(JumpInfo->RegSet, m_Section->m_EnterPC < JumpInfo->JumpPC, true);
        if (JumpInfo->JumpPC == (uint32_t)-1)
        {
            g_Notify->BreakPoint(__FILE__, __LINE__);
        }
        if (m_Section->m_EnterPC <= JumpInfo->JumpPC)
        {
            m_CodeBlock.Log("CompileSystemCheck 10");
            CompileSystemCheck(m_Section->m_EnterPC, GetRegWorkingSet());
        }
    }
    JumpInfo->FallThrough = false;

    m_RegWorkingSet.WriteBackRegisters();
    m_RegWorkingSet.SetFpuBeenUsed(false);
    m_RegWorkingSet.SetRoundingModel(CRegBase::RoundUnknown);
    m_Section->m_RegEnter = m_RegWorkingSet;

    const stdstr_f Label("Section_%d", m_Section->m_SectionID);
    int CurrentParent = FirstParent;
    for (size_t i = 0; i < NoOfCompiledParents; i++)
    {
        if ((int)i == FirstParent)
        {
            continue;
        }
        // the previous parent's code jumps over the code of this one to the start of the section
        JumpInfo = ParentList[CurrentParent].JumpInfo;
        JumpInfo->LinkLocation = m_Assembler.newLabel();
        m_Assembler.JmpLabel(Label.c_str(), JumpInfo->LinkLocation);
        JumpInfo->LinkLocation2 = asmjit::Label();

        CurrentParent = (int)i;
        CCodeSection * Parent = ParentList[CurrentParent].Parent;
        JumpInfo = ParentList[CurrentParent].JumpInfo;
        m_CodeBlock.Log("   Section_%d (from %d):", m_Section->m_SectionID, Parent->m_SectionID);
        if (JumpInfo->LinkLocation.isValid())
        {
            m_Assembler.bind(JumpInfo->LinkLocation);
            JumpInfo->LinkLocation = asmjit::Label();
            if (JumpInfo->LinkLocation2.isValid())
            {
                m_Assembler.bind(JumpInfo->LinkLocation2);
                JumpInfo->LinkLocation2 = asmjit::Label();
            }
        }
        SetRegWorkingSet(JumpInfo->RegSet);
        if (m_Section->m_EnterPC < JumpInfo->JumpPC)
        {
            UpdateCounters(m_RegWorkingSet, true, true);
            m_CodeBlock.Log("CompileSystemCheck 11");
            CompileSystemCheck(m_Section->m_EnterPC, m_RegWorkingSet);
        }
        else
        {
            UpdateCounters(m_RegWorkingSet, false, true);
        }
        SyncRegState(m_Section->m_RegEnter);
    }

    for (size_t i = 0; i < NoOfCompiledParents; i++)
    {
        LinkJump(*ParentList[i].JumpInfo);
    }

    SetRegWorkingSet(m_Section->m_RegEnter);
    m_CodeBlock.Log("   Section_%d:", m_Section->m_SectionID);
    m_Section->m_RegEnter.SetBlockCycleCount(0);
    return true;
}

void CX64RecompilerOps::LinkJump(CJumpInfo & JumpInfo, uint32_t /*SectionID*/, uint32_t /*FromSectionID*/)
{
    if (JumpInfo.LinkLocation.isValid())
    {
        m_CodeBlock.Log("");
        m_Assembler.bind(JumpInfo.LinkLocation);
        JumpInfo.LinkLocation = asmjit::Label();
        if (JumpInfo.LinkLocation2.isValid())
        {
            m_Assembler.bind(JumpInfo.LinkLocation2);
            JumpInfo.LinkLocation2 = asmjit::Label();
        }
    }
}

void CX64RecompilerOps::JumpToSection(CCodeSection * Section)
{
    m_Assembler.JmpLabel(stdstr_f("Section_%d", Section->m_SectionID).c_str(), Section->m_EnterLabel);
}

void CX64RecompilerOps::JumpToUnknown(CJumpInfo * JumpInfo)
{
    JumpInfo->LinkLocation = m_Assembler.newLabel();
    m_Assembler.JmpLabel(JumpInfo->BranchLabel.c_str(), JumpInfo->LinkLocation);
}

void CX64RecompilerOps::SetCurrentPC(uint32_t ProgramCounter)
{
    uint32_t Value;
    if (!m_MMU.MemoryValue32(ProgramCounter, Value))
    {
        g_Notify->FatalError(GS(MSG_FAIL_LOAD_WORD));
    }
    m_Instruction = R4300iInstruction((int32_t)ProgramCounter, Value);
}

uint32_t CX64RecompilerOps::GetCurrentPC(void)
{
    return m_CompilePC;
}

void CX64RecompilerOps::SetCurrentSection(CCodeSection * section)
{
    m_Section = section;
}

void CX64RecompilerOps::SetNextStepType(PIPELINE_STAGE StepType)
{
    m_PipelineStage = StepType;
}

PIPELINE_STAGE CX64RecompilerOps::GetNextStepType(void)
{
    return m_PipelineStage;
}

const R4300iOpcode & CX64RecompilerOps::GetOpcode(void) const
{
    return m_Opcode;
}

void CX64RecompilerOps::PreCompileOpcode(void)
{
    if (m_PipelineStage != PIPELINE_STAGE_DELAY_SLOT_DONE)
    {
        m_CodeBlock.Log("  %X %s", (uint32_t)m_CompilePC, m_Instruction.NameAndParam().c_str());
    }
    m_RegWorkingSet.ResetRegisterProtection();
}

void CX64RecompilerOps::PostCompileOpcode(void)
{
    m_RegWorkingSet.SetBlockCycleCount(m_RegWorkingSet.GetBlockCycleCount() + g_GameSettings.countPerOp);
    if (!g_GameSettings.regCaching)
    {
        m_RegWorkingSet.WriteBackRegisters();
    }
}

void CX64RecompilerOps::CompileExit(uint32_t JumpPC, uint32_t TargetPC, CRegInfo ExitRegSet, ExitReason reason, void (CX64Ops::*x64Jmp)(const char * LabelName, asmjit::Label & JumpLabel), const asmjit::x86::Gp * BadVAddrReg)
{
    if (x64Jmp != nullptr)
    {
        asmjit::Label ExitLabel = m_Assembler.newLabel();
        const stdstr ExitName = stdstr_f("Exit_%08X_%d", JumpPC, m_ExitLabelCount++);
        (m_Assembler.*x64Jmp)(ExitName.c_str(), ExitLabel);
        m_Assembler.EnterSecondarySection();
        m_Assembler.bind(ExitLabel);
        CompileExit((uint32_t)-1, TargetPC, ExitRegSet, reason, nullptr, BadVAddrReg);
        m_Assembler.EnterPrimarySection();
        return;
    }

    // The faulting address of an address error is in a register that writing back the MIPS
    // registers may change: it goes to the first argument of the call that raises the exception
    const bool AddressInArg1 = (reason == ExitReason_AddressErrorExceptionRead32 || reason == ExitReason_AddressErrorExceptionWrite32) && BadVAddrReg != nullptr && BadVAddrReg->isValid();
    if (AddressInArg1)
    {
        m_Assembler.movsxd(X64Abi::Arg1, BadVAddrReg->r32());
    }
    ExitRegSet.WriteBackRegisters();
    if (TargetPC != (uint32_t)-1)
    {
        m_Assembler.MoveConstToVariable(&g_Reg->m_PROGRAM_COUNTER, "PROGRAM_COUNTER", TargetPC);
        UpdateCounters(ExitRegSet, TargetPC <= JumpPC && JumpPC != (uint32_t)-1, reason == ExitReason_Normal);
    }
    else
    {
        UpdateCounters(ExitRegSet, false, reason == ExitReason_Normal, reason != ExitReason_Normal);
    }

    switch (reason)
    {
    case ExitReason_Normal:
    case ExitReason_CheckPCAlignment:
    case ExitReason_NormalNoSysCheck:
        ExitRegSet.SetBlockCycleCount(0);
        if ((reason == ExitReason_Normal || reason == ExitReason_CheckPCAlignment) && (TargetPC == (uint32_t)-1 || TargetPC <= JumpPC))
        {
            CompileSystemCheck((uint32_t)-1, ExitRegSet);
        }
        if (reason == ExitReason_CheckPCAlignment)
        {
            m_Assembler.MoveVariable64ToX64reg(asmjit::x86::rax, &g_Reg->m_PROGRAM_COUNTER, "PROGRAM_COUNTER");
            m_Assembler.test(asmjit::x86::eax, 3);
            asmjit::Label ValidPCJump = m_Assembler.newLabel();
            m_Assembler.JeLabel("ValidPC", ValidPCJump);
            m_Assembler.mov(X64Abi::Arg1, asmjit::x86::rax);
            m_Assembler.mov(X64Abi::Arg2.r32(), 1);
            m_Assembler.sub(asmjit::x86::rsp, 32);
            m_Assembler.CallThis(g_Reg, MemberFuncAddress(&CRegisters::DoAddressError), "CRegisters::DoAddressError");
            m_Assembler.add(asmjit::x86::rsp, 32);
            m_Assembler.MoveVariableToX64reg(asmjit::x86::eax, &g_System->m_JumpToLocation, "System->m_JumpToLocation", false);
            m_Assembler.MovDwordToVariable(&g_Reg->m_PROGRAM_COUNTER, "PROGRAM_COUNTER", asmjit::x86::eax);
            m_Assembler.cdq();
            m_Assembler.MovDwordToVariable((void *)(((uint8_t *)&g_Reg->m_PROGRAM_COUNTER) + 4), "PROGRAM_COUNTER+4", asmjit::x86::edx);
            m_Assembler.MoveConstToVariable(&g_System->m_PipelineStage, "g_System->m_PipelineStage", PIPELINE_STAGE_NORMAL);
            ExitCodeBlock();
            m_Assembler.bind(ValidPCJump);
        }
        ExitCodeBlock();
        break;
    case ExitReason_ExceptionOverflow:
    {
        const bool InDelaySlot = m_PipelineStage == PIPELINE_STAGE_JUMP || m_PipelineStage == PIPELINE_STAGE_DELAY_SLOT;
        m_Assembler.MoveConstToVariable(&g_System->m_PipelineStage, "System->m_PipelineStage", InDelaySlot ? PIPELINE_STAGE_JUMP : PIPELINE_STAGE_NORMAL);
        m_Assembler.mov(X64Abi::Arg1, EXC_OV);
        m_Assembler.xor_(X64Abi::Arg2.r32(), X64Abi::Arg2.r32());
        m_Assembler.sub(asmjit::x86::rsp, 32);
        m_Assembler.CallThis(g_Reg, MemberFuncAddress(&CRegisters::TriggerException), "CRegisters::TriggerException");
        m_Assembler.add(asmjit::x86::rsp, 32);
        m_Assembler.MoveVariable32ToX64reg(asmjit::x86::eax, &g_System->m_JumpToLocation, "System->m_JumpToLocation");
        m_Assembler.MovDwordToVariable(&g_Reg->m_PROGRAM_COUNTER, "PROGRAM_COUNTER", asmjit::x86::eax);
        m_Assembler.cdq();
        m_Assembler.MovDwordToVariable((void *)(((uint8_t *)&g_Reg->m_PROGRAM_COUNTER) + 4), "PROGRAM_COUNTER+4", asmjit::x86::edx);
        m_Assembler.MoveConstToVariable(&g_System->m_PipelineStage, "System->m_PipelineStage", PIPELINE_STAGE_NORMAL);
        ExitCodeBlock();
        break;
    }
    case ExitReason_COP1Unuseable:
    {
        const bool InDelaySlot = m_PipelineStage == PIPELINE_STAGE_JUMP || m_PipelineStage == PIPELINE_STAGE_DELAY_SLOT;
        m_Assembler.MoveConstToVariable(&g_System->m_PipelineStage, "System->m_PipelineStage", InDelaySlot ? PIPELINE_STAGE_JUMP : PIPELINE_STAGE_NORMAL);
        m_Assembler.mov(X64Abi::Arg1, EXC_CPU);
        m_Assembler.mov(X64Abi::Arg2.r32(), 1);
        m_Assembler.sub(asmjit::x86::rsp, 32);
        m_Assembler.CallThis(g_Reg, MemberFuncAddress(&CRegisters::TriggerException), "CRegisters::TriggerException");
        m_Assembler.add(asmjit::x86::rsp, 32);
        m_Assembler.MoveVariableToX64reg(asmjit::x86::eax, &g_System->m_JumpToLocation, "System->m_JumpToLocation", false);
        m_Assembler.MovDwordToVariable(&g_Reg->m_PROGRAM_COUNTER, "PROGRAM_COUNTER", asmjit::x86::eax);
        m_Assembler.cdq();
        m_Assembler.MovDwordToVariable((void *)(((uint8_t *)&g_Reg->m_PROGRAM_COUNTER) + 4), "PROGRAM_COUNTER+4", asmjit::x86::edx);
        m_Assembler.MoveConstToVariable(&g_System->m_PipelineStage, "System->m_PipelineStage", PIPELINE_STAGE_NORMAL);
        ExitRegSet.SetBlockCycleCount(0);
        UpdateCounters(ExitRegSet, true, false, false);
        ExitCodeBlock();
        break;
    }
    case ExitReason_AddressErrorExceptionRead32:
    {
        const bool InDelaySlot = m_PipelineStage == PIPELINE_STAGE_JUMP || m_PipelineStage == PIPELINE_STAGE_DELAY_SLOT;
        if (!AddressInArg1)
        {
            m_Assembler.movsxd(X64Abi::Arg1, m_Assembler.Abs(&m_TempValue32, 4));
        }
        m_Assembler.MoveConstToVariable(&g_System->m_PipelineStage, "System->m_PipelineStage", InDelaySlot ? PIPELINE_STAGE_JUMP : PIPELINE_STAGE_NORMAL);
        m_Assembler.mov(X64Abi::Arg2.r32(), 1);
        m_Assembler.sub(asmjit::x86::rsp, 32);
        m_Assembler.CallThis(g_Reg, MemberFuncAddress(&CRegisters::DoAddressError), "CRegisters::DoAddressError");
        m_Assembler.add(asmjit::x86::rsp, 32);
        m_Assembler.MoveVariable32ToX64reg(asmjit::x86::eax, &g_System->m_JumpToLocation, "System->m_JumpToLocation");
        m_Assembler.MovDwordToVariable(&g_Reg->m_PROGRAM_COUNTER, "PROGRAM_COUNTER", asmjit::x86::eax);
        m_Assembler.cdq();
        m_Assembler.MovDwordToVariable((void *)(((uint8_t *)&g_Reg->m_PROGRAM_COUNTER) + 4), "PROGRAM_COUNTER+4", asmjit::x86::edx);
        m_Assembler.MoveConstToVariable(&g_System->m_PipelineStage, "System->m_PipelineStage", PIPELINE_STAGE_NORMAL);
        ExitCodeBlock();
        break;
    }
    case ExitReason_AddressErrorExceptionWrite32:
    {
        const bool InDelaySlot = m_PipelineStage == PIPELINE_STAGE_JUMP || m_PipelineStage == PIPELINE_STAGE_DELAY_SLOT;
        if (!AddressInArg1)
        {
            m_Assembler.MoveVariableToX64reg(X64Abi::Arg1, &m_TempValue32, "m_TempValue32", true);
        }
        m_Assembler.MoveConstToVariable(&g_System->m_PipelineStage, "System->m_PipelineStage", InDelaySlot ? PIPELINE_STAGE_JUMP : PIPELINE_STAGE_NORMAL);
        m_Assembler.xor_(X64Abi::Arg2.r32(), X64Abi::Arg2.r32());
        m_Assembler.sub(asmjit::x86::rsp, 32);
        m_Assembler.CallThis(g_Reg, MemberFuncAddress(&CRegisters::DoAddressError), "CRegisters::DoAddressError");
        m_Assembler.add(asmjit::x86::rsp, 32);
        m_Assembler.MoveVariableToX64reg(asmjit::x86::eax, &g_System->m_JumpToLocation, "System->m_JumpToLocation", false);
        m_Assembler.MovDwordToVariable(&g_Reg->m_PROGRAM_COUNTER, "PROGRAM_COUNTER", asmjit::x86::eax);
        m_Assembler.cdq();
        m_Assembler.MovDwordToVariable((void *)(((uint8_t *)&g_Reg->m_PROGRAM_COUNTER) + 4), "PROGRAM_COUNTER+4", asmjit::x86::edx);
        m_Assembler.MoveConstToVariable(&g_System->m_PipelineStage, "System->m_PipelineStage", PIPELINE_STAGE_NORMAL);
        ExitCodeBlock();
        break;
    }

    case ExitReason_Eret:
        m_Assembler.MoveVariable64ToX64reg(asmjit::x86::rax, &m_System.m_JumpToLocation, "System->m_JumpToLocation");
        m_Assembler.MovQwordToVariable(&m_Reg.m_PROGRAM_COUNTER, "PROGRAM_COUNTER", asmjit::x86::rax);
        m_Assembler.MoveConstToVariable(&m_System.m_PipelineStage, "System->m_PipelineStage", PIPELINE_STAGE_NORMAL);
        CompileSystemCheck((uint32_t)-1, ExitRegSet);
        ExitCodeBlock();
        break;
    case ExitReason_Exception:
        m_Assembler.MoveVariableToX64reg(asmjit::x86::eax, &g_System->m_JumpToLocation, "System->m_JumpToLocation", false);
        m_Assembler.MovDwordToVariable(&g_Reg->m_PROGRAM_COUNTER, "PROGRAM_COUNTER", asmjit::x86::eax);
        m_Assembler.cdq();
        m_Assembler.MovDwordToVariable(reinterpret_cast<void *>(reinterpret_cast<uint8_t *>(&g_Reg->m_PROGRAM_COUNTER) + 4), "PROGRAM_COUNTER+4", asmjit::x86::edx);
        m_Assembler.MoveConstToVariable(&g_System->m_PipelineStage, "System->m_PipelineStage", PIPELINE_STAGE_NORMAL);
        if (TargetPC == (uint32_t)-1)
        {
            ExitRegSet.SetBlockCycleCount(0);
            UpdateCounters(ExitRegSet, false, false, false);
        }
        ExitCodeBlock();
        break;
    default:
        WriteTrace(TraceRecompiler, TraceError, "CX64RecompilerOps::CompileExit: unhandled exit reason (%d)", reason);
        g_Notify->BreakPoint(__FILE__, __LINE__);
    }
}

void CX64RecompilerOps::ExitCodeBlock(void)
{
    if (g_SyncSystem)
    {
        m_Assembler.sub(asmjit::x86::rsp, 32);
        m_Assembler.CallThis(g_BaseSystem, MemberFuncAddress(&CN64System::SyncSystem), "CN64System::SyncSystem");
        m_Assembler.add(asmjit::x86::rsp, 32);
    }
    m_Assembler.add(asmjit::x86::rsp, FunctionStackSize);
#if !defined(_WIN32)
    m_Assembler.pop(asmjit::x86::r15);
    m_Assembler.pop(asmjit::x86::r14);
    m_Assembler.pop(asmjit::x86::r13);
    m_Assembler.pop(asmjit::x86::r12);
    m_Assembler.pop(asmjit::x86::rbp);
    m_Assembler.pop(asmjit::x86::rbx);
#endif
    m_Assembler.ret();
}

void CX64RecompilerOps::UpdateSyncCPU(CRegInfo & RegSet, uint32_t Cycles)
{
    if (!g_SyncSystem)
    {
        return;
    }
    m_CodeBlock.Log("");
    m_CodeBlock.Log("      // Updating sync CPU");
    RegSet.BeforeCallDirect();
    m_Assembler.mov(X64Abi::Arg1, Cycles);
    m_Assembler.sub(asmjit::x86::rsp, 32);
    m_Assembler.CallThis(g_System, MemberFuncAddress(&CN64System::UpdateSyncCPU), "CN64System::UpdateSyncCPU");
    m_Assembler.add(asmjit::x86::rsp, 32);
    RegSet.AfterCallDirect();
}

void CX64RecompilerOps::UpdateCounters(CRegInfo & RegSet, bool CheckTimer, bool ClearValues, bool UpdateTimer)
{
    if (RegSet.GetBlockCycleCount() != 0)
    {
        UpdateSyncCPU(RegSet, RegSet.GetBlockCycleCount());
        m_CodeBlock.Log("");
        m_CodeBlock.Log("      // Update counter");
        m_Assembler.SubConstFromVariable(RegSet.GetBlockCycleCount(), g_NextTimer, "g_NextTimer");
        if (ClearValues)
        {
            RegSet.SetBlockCycleCount(0);
        }
    }
    else if (CheckTimer)
    {
        m_Assembler.CmpConstToVariable(g_NextTimer, "g_NextTimer", 0);
    }

    if (CheckTimer)
    {
        asmjit::Label ContinueFromTimerTest = m_Assembler.newLabel();
        const bool InSecondary = m_Assembler.InSecondarySection();

        if (InSecondary)
        {
            m_Assembler.JnsLabel("Continue_From_Timer_Test", ContinueFromTimerTest);
        }
        else
        {
            asmjit::Label TimerDonePath = m_Assembler.newLabel();
            m_Assembler.JsLabel("Timer_Done_Path", TimerDonePath);
            m_CodeBlock.Log("");
            m_Assembler.bind(ContinueFromTimerTest);
            m_Assembler.EnterSecondarySection();
            m_Assembler.bind(TimerDonePath);
        }

        RegSet.BeforeCallDirect();
        m_Assembler.sub(asmjit::x86::rsp, 32);
        m_Assembler.CallThis(g_SystemTimer, MemberFuncAddress(&CSystemTimer::TimerDone), "CSystemTimer::TimerDone");
        m_Assembler.add(asmjit::x86::rsp, 32);
        RegSet.AfterCallDirect();

        if (InSecondary)
        {
            m_Assembler.bind(ContinueFromTimerTest);
        }
        else
        {
            m_Assembler.JmpLabel("Continue_From_Timer_Test", ContinueFromTimerTest);
            m_Assembler.EnterPrimarySection();
        }
    }

    if ((UpdateTimer || g_GameSettings.overClockModifier != 1) && g_SyncSystem)
    {
        RegSet.BeforeCallDirect();
        m_Assembler.sub(asmjit::x86::rsp, 32);
        m_Assembler.CallThis(g_SystemTimer, MemberFuncAddress(&CSystemTimer::UpdateTimers), "CSystemTimer::UpdateTimers");
        m_Assembler.add(asmjit::x86::rsp, 32);
        RegSet.AfterCallDirect();
    }
}

void CX64RecompilerOps::CompileSystemCheck(uint32_t TargetPC, const CRegInfo & RegSet)
{
    m_Assembler.cmp(m_Assembler.Abs(&m_SystemEvents.DoSomething(), 1), 0);
    asmjit::Label ContinueFromInterruptTest = m_Assembler.newLabel();
    m_Assembler.JeLabel("Continue_From_Interrupt_Test", ContinueFromInterruptTest);

    if (TargetPC != (uint32_t)-1)
    {
        m_Assembler.MoveConstToVariable(&g_Reg->m_PROGRAM_COUNTER, "PROGRAM_COUNTER", TargetPC);
    }

    CRegInfo RegSetCopy(RegSet);
    RegSetCopy.WriteBackRegisters();

    m_Assembler.sub(asmjit::x86::rsp, 32);
    m_Assembler.CallThis(&m_SystemEvents, MemberFuncAddress(&CSystemEvents::ExecuteEvents), "CSystemEvents::ExecuteEvents");
    m_Assembler.add(asmjit::x86::rsp, 32);

    ExitCodeBlock();
    m_CodeBlock.Log("");
    m_Assembler.bind(ContinueFromInterruptTest);
}

// The debugger stops at an execution breakpoint: what is run before the block is left is the
// breakpoint handling of the debugger (as in the x86 recompiler)
void CX64RecompilerOps::CompilerBreakPoint(void)
{
    g_Settings->SaveBool(Debugger_SteppingOps, true);
    g_Recompiler->DebuggerStep();
}

void CX64RecompilerOps::BreakPointDelaySlot(void)
{
    g_System->m_OpCodes.ExecuteOps(g_GameSettings.countPerOp);
    if (g_SyncSystem)
    {
        g_System->UpdateSyncCPU(g_GameSettings.countPerOp);
        g_System->SyncSystem();
    }
    if (g_Debugger->ExecutionBP((uint32_t)g_Reg->m_PROGRAM_COUNTER))
    {
        CompilerBreakPoint();
    }
    if (g_System->PipelineStage() != PIPELINE_STAGE_NORMAL)
    {
        g_System->m_OpCodes.ExecuteOps(g_GameSettings.countPerOp);
        if (g_SyncSystem)
        {
            g_System->UpdateSyncCPU(g_GameSettings.countPerOp);
            g_System->SyncSystem();
        }
    }
}

void CX64RecompilerOps::CompileExecuteBP(void)
{
    const bool InDelaySlot = m_PipelineStage == PIPELINE_STAGE_JUMP || m_PipelineStage == PIPELINE_STAGE_DELAY_SLOT;
    if (InDelaySlot)
    {
        g_Notify->BreakPoint(__FILE__, __LINE__);
    }
    m_RegWorkingSet.WriteBackRegisters();
    UpdateCounters(m_RegWorkingSet, true, true);
    m_Assembler.MoveConstToVariable(&m_Reg.m_PROGRAM_COUNTER, "PROGRAM_COUNTER", m_CompilePC);
    if (g_SyncSystem)
    {
        m_Assembler.sub(asmjit::x86::rsp, 32);
        m_Assembler.CallThis(g_BaseSystem, MemberFuncAddress(&CN64System::SyncSystem), "CN64System::SyncSystem");
        m_Assembler.add(asmjit::x86::rsp, 32);
    }
    m_Assembler.sub(asmjit::x86::rsp, 32);
    m_Assembler.CallFunc(reinterpret_cast<uintptr_t>(&CX64RecompilerOps::CompilerBreakPoint), "CX64RecompilerOps::CompilerBreakPoint");
    m_Assembler.add(asmjit::x86::rsp, 32);
    ExitCodeBlock();
    m_PipelineStage = PIPELINE_STAGE_END_BLOCK;
}

void CX64RecompilerOps::CompileExecuteDelaySlotBP(void)
{
    const bool InDelaySlot = m_PipelineStage == PIPELINE_STAGE_JUMP || m_PipelineStage == PIPELINE_STAGE_DELAY_SLOT;
    if (InDelaySlot)
    {
        g_Notify->BreakPoint(__FILE__, __LINE__);
    }
    m_RegWorkingSet.WriteBackRegisters();
    UpdateCounters(m_RegWorkingSet, true, true);
    m_Assembler.MoveConstToVariable(&m_Reg.m_PROGRAM_COUNTER, "PROGRAM_COUNTER", m_CompilePC);
    if (g_SyncSystem)
    {
        m_Assembler.sub(asmjit::x86::rsp, 32);
        m_Assembler.CallThis(g_BaseSystem, MemberFuncAddress(&CN64System::SyncSystem), "CN64System::SyncSystem");
        m_Assembler.add(asmjit::x86::rsp, 32);
    }
    m_Assembler.sub(asmjit::x86::rsp, 32);
    m_Assembler.CallFunc(reinterpret_cast<uintptr_t>(&CX64RecompilerOps::BreakPointDelaySlot), "CX64RecompilerOps::BreakPointDelaySlot");
    m_Assembler.add(asmjit::x86::rsp, 32);
    ExitCodeBlock();
    m_PipelineStage = PIPELINE_STAGE_END_BLOCK;
}

uint32_t CX64RecompilerOps::ColdEntryOffset(void) const
{
    return m_ColdEntryOffset;
}

uint32_t CX64RecompilerOps::WarmEntryOffset(void) const
{
    return m_WarmEntryOffset;
}

// ---- loads and stores ----
// A 32 bit core: the 8, 16 and 32 bit accesses of the usual mapped memory are compiled, everything
// else (the other sizes, the unaligned operations, a debugger watching the memory, a 64 bit core)
// is done by the interpreter's implementation, which also raises every exception.

void CX64RecompilerOps::LB()
{
    CompileLoad(8, true);
}

void CX64RecompilerOps::LBU()
{
    CompileLoad(8, false);
}

void CX64RecompilerOps::LH()
{
    CompileLoad(16, true);
}

void CX64RecompilerOps::LHU()
{
    CompileLoad(16, false);
}

void CX64RecompilerOps::LW()
{
    CompileLoad(32, true);
}

void CX64RecompilerOps::LD()
{
    CompileInterpreterOp(m_Opcode.rt);
}

void CX64RecompilerOps::SB()
{
    CompileStore(8);
}

void CX64RecompilerOps::SH()
{
    CompileStore(16);
}

void CX64RecompilerOps::SW()
{
    CompileStore(32);
}

void CX64RecompilerOps::SD()
{
    CompileInterpreterOp(-1);
}

void CX64RecompilerOps::CompileLoad(int32_t Size, bool SignExtend)
{
    const int32_t Rt = m_Opcode.rt;
    if (!g_GameSettings.core32Bit || g_DebugSettings.haveReadBP)
    {
        CompileInterpreterOp(Rt);
        return;
    }

    // The stack of the game is at a fixed place in the memory of the host
    if (Size == 32 && m_Opcode.base == 29 && g_GameSettings.fastSP && Rt != 0)
    {
        m_RegWorkingSet.Map_GPR_32bit(Rt, true, -1);
        const asmjit::x86::Gp & Dest = m_RegWorkingSet.GetMipsRegMap(Rt);
        const asmjit::x86::Gp StackReg = m_RegWorkingSet.Map_MemoryStack(asmjit::x86::Gpq(), true, true);
        m_Assembler.mov(Dest.r32(), asmjit::x86::dword_ptr(StackReg, (int32_t)((int16_t)m_Opcode.offset)));
        if (Rt == 29)
        {
            m_RegWorkingSet.ResetRegisterProtection();
            ResetMemoryStack();
        }
        return;
    }

    const asmjit::x86::Gp AddressReg = BaseOffsetAddress(false);
    m_RegWorkingSet.SetX64Protected(AddressReg.id(), true);
    const asmjit::x86::Gp Value = m_RegWorkingSet.Map_TempReg(asmjit::x86::Gpd(), -1);
    const CRegInfo ExitRegSet = m_RegWorkingSet.WithAddedCycles(g_GameSettings.countPerOp);

    if (Size > 8)
    {
        m_Assembler.test(AddressReg.r32(), Size / 8 - 1);
        CompileExit(m_CompilePC, m_CompilePC, ExitRegSet, ExitReason_AddressErrorExceptionRead32, &CX64Ops::JneLabel, &AddressReg);
    }

    const asmjit::x86::Gp PageIndexReg = m_RegWorkingSet.Map_TempReg(asmjit::x86::Gpd(), -1);
    m_Assembler.mov(PageIndexReg.r32(), AddressReg.r32());
    m_Assembler.shr(PageIndexReg.r32(), 12);
    const asmjit::x86::Gp HostOffsetReg = m_RegWorkingSet.Map_TempReg(asmjit::x86::Gpq(), -1, asmjit::RegType::kX86_Gpq);
    m_Assembler.MoveVariable64ToX64reg(HostOffsetReg, &m_MMU.m_MemoryReadMap, "MMU->m_MemoryReadMap");
    m_Assembler.mov(HostOffsetReg.r64(), asmjit::x86::qword_ptr(HostOffsetReg, PageIndexReg, 3));
    m_Assembler.cmp(HostOffsetReg.r64(), (int64_t)-1);

    const stdstr AfterLoadLabel = stdstr_f("MemoryReadMap_%X_AfterLoad", m_CompilePC);
    asmjit::Label AfterLoad = m_Assembler.newLabel();
    asmjit::Label SlowPath = m_Assembler.newLabel();
    const CX64RegInfo PreMapRegSet(m_RegWorkingSet);
    m_Assembler.JeLabel(stdstr_f("MemoryReadMap_%X_Miss", m_CompilePC).c_str(), SlowPath);

    if (Size == 32)
    {
        m_Assembler.mov(Value.r32(), asmjit::x86::dword_ptr(AddressReg, HostOffsetReg));
    }
    else if (Size == 16)
    {
        m_Assembler.xor_(AddressReg.r32(), 2);
        if (SignExtend)
        {
            m_Assembler.movsx(Value.r32(), asmjit::x86::word_ptr(AddressReg, HostOffsetReg));
        }
        else
        {
            m_Assembler.movzx(Value.r32(), asmjit::x86::word_ptr(AddressReg, HostOffsetReg));
        }
    }
    else
    {
        m_Assembler.xor_(AddressReg.r32(), 3);
        if (SignExtend)
        {
            m_Assembler.movsx(Value.r32(), asmjit::x86::byte_ptr(AddressReg, HostOffsetReg));
        }
        else
        {
            m_Assembler.movzx(Value.r32(), asmjit::x86::byte_ptr(AddressReg, HostOffsetReg));
        }
    }

    // not plain memory (a mapped address, the hardware registers): the memory of the system is asked
    m_Assembler.EnterSecondarySection();
    m_CodeBlock.Log("");
    m_CodeBlock.Log("      MemoryReadMap_%X_Miss:", m_CompilePC);
    m_Assembler.bind(SlowPath);
    m_RegWorkingSet = PreMapRegSet;
    m_Assembler.MoveConstToVariable(&g_Reg->m_PROGRAM_COUNTER, "PROGRAM_COUNTER", m_CompilePC);
    if (m_PipelineStage != PIPELINE_STAGE_NORMAL)
    {
        m_Assembler.MoveConstToVariable(&g_System->m_PipelineStage, "g_System->m_PipelineStage", PIPELINE_STAGE_JUMP);
    }
    m_RegWorkingSet.BeforeCallDirect();
    m_Assembler.mov(X64Abi::Arg1.r32(), AddressReg.r32());
    m_Assembler.MoveConstToVariable(&m_TempValue32, "TempValue32", 0);
    m_Assembler.MoveConstToX64reg(X64Abi::Arg2, reinterpret_cast<uintptr_t>(&m_TempValue32), "TempValue32");
    m_Assembler.sub(asmjit::x86::rsp, 32);
    if (Size == 8)
    {
        m_Assembler.CallThis(&m_MMU, MemberFuncAddress(&CMipsMemoryVM::LB_VAddr32), "CMipsMemoryVM::LB_VAddr32");
    }
    else if (Size == 16)
    {
        m_Assembler.CallThis(&m_MMU, MemberFuncAddress(&CMipsMemoryVM::LH_VAddr32), "CMipsMemoryVM::LH_VAddr32");
    }
    else
    {
        m_Assembler.CallThis(&m_MMU, MemberFuncAddress(&CMipsMemoryVM::LW_VAddr32), "CMipsMemoryVM::LW_VAddr32");
    }
    m_Assembler.add(asmjit::x86::rsp, 32);
    m_Assembler.test(asmjit::x86::al, asmjit::x86::al);
    m_RegWorkingSet.AfterCallDirect();
    asmjit::Label SlowPathException = m_Assembler.newLabel();
    m_Assembler.JeLabel(stdstr_f("MemoryReadMap_%X_Exception", m_CompilePC).c_str(), SlowPathException);
    m_Assembler.MoveVariableToX64reg(Value, &m_TempValue32, "TempValue32", false);
    if (Size == 8 && SignExtend)
    {
        m_Assembler.movsx(Value.r32(), Value.r8Lo());
    }
    else if (Size == 16 && SignExtend)
    {
        m_Assembler.movsx(Value.r32(), Value.r16());
    }
    if (m_PipelineStage != PIPELINE_STAGE_NORMAL)
    {
        m_Assembler.MoveConstToVariable(&g_System->m_PipelineStage, "g_System->m_PipelineStage", PIPELINE_STAGE_NORMAL);
    }
    m_Assembler.JmpLabel(AfterLoadLabel.c_str(), AfterLoad);
    m_CodeBlock.Log("");
    m_CodeBlock.Log("      MemoryReadMap_%X_Exception:", m_CompilePC);
    m_Assembler.bind(SlowPathException);
    CompileExit(m_CompilePC, (uint32_t)-1, ExitRegSet, ExitReason_Exception);
    m_Assembler.EnterPrimarySection();
    m_CodeBlock.Log("");
    m_CodeBlock.Log("      %s:", AfterLoadLabel.c_str());
    m_Assembler.bind(AfterLoad);

    // the load is done: the register has the value (a load into $zero is only the access)
    m_RegWorkingSet = PreMapRegSet;
    if (Rt != 0)
    {
        m_RegWorkingSet.Map_GPR_32bit(Rt, true, -1);
        m_Assembler.mov(m_RegWorkingSet.GetMipsRegMap(Rt).r32(), Value.r32());
    }
    if (g_GameSettings.fastSP && Rt == 29)
    {
        m_RegWorkingSet.ResetRegisterProtection();
        ResetMemoryStack();
    }
}

void CX64RecompilerOps::CompileStore(int32_t Size)
{
    if (!g_GameSettings.core32Bit || g_DebugSettings.haveWriteBP)
    {
        CompileInterpreterOp(-1);
        return;
    }
    const int32_t Rt = m_Opcode.rt;

    // the value: a constant, a cached register, or loaded into a temporary
    const bool ValueConst = Rt == 0 || m_RegWorkingSet.IsConst(Rt);
    const uint32_t ConstValue32 = Rt == 0 ? 0 : (ValueConst ? m_RegWorkingSet.GetMipsRegLo(Rt) : 0);
    asmjit::x86::Gp ValueReg;
    if (!ValueConst)
    {
        if (m_RegWorkingSet.IsMapped(Rt))
        {
            m_RegWorkingSet.ProtectGPR(Rt);
            ValueReg = m_RegWorkingSet.GetMipsRegMap(Rt).r32();
        }
        else
        {
            ValueReg = m_RegWorkingSet.Map_TempReg(asmjit::x86::Gpd(), Rt);
        }
        m_RegWorkingSet.SetX64Protected(ValueReg.id(), true);
    }

    if (Size == 32 && m_Opcode.base == 29 && g_GameSettings.fastSP)
    {
        const asmjit::x86::Gp StackReg = m_RegWorkingSet.Map_MemoryStack(asmjit::x86::Gpq(), true, true);
        const asmjit::x86::Mem Target = asmjit::x86::dword_ptr(StackReg, (int32_t)((int16_t)m_Opcode.offset));
        if (ValueConst)
        {
            m_Assembler.mov(Target, ConstValue32);
        }
        else
        {
            m_Assembler.mov(Target, ValueReg.r32());
        }
        return;
    }

    const asmjit::x86::Gp AddressReg = BaseOffsetAddress(false);
    m_RegWorkingSet.SetX64Protected(AddressReg.id(), true);
    const CRegInfo ExitRegSet = m_RegWorkingSet.WithAddedCycles(g_GameSettings.countPerOp);
    if (Size > 8)
    {
        m_Assembler.test(AddressReg.r32(), Size / 8 - 1);
        CompileExit(m_CompilePC, m_CompilePC, ExitRegSet, ExitReason_AddressErrorExceptionWrite32, &CX64Ops::JneLabel, &AddressReg);
    }

    const asmjit::x86::Gp PageIndexReg = m_RegWorkingSet.Map_TempReg(asmjit::x86::Gpd(), -1);
    m_Assembler.mov(PageIndexReg.r32(), AddressReg.r32());
    m_Assembler.shr(PageIndexReg.r32(), 12);
    const asmjit::x86::Gp HostOffsetReg = m_RegWorkingSet.Map_TempReg(asmjit::x86::Gpq(), -1, asmjit::RegType::kX86_Gpq);
    m_Assembler.MoveVariable64ToX64reg(HostOffsetReg, &m_MMU.m_MemoryWriteMap, "MMU->m_MemoryWriteMap");
    m_Assembler.mov(HostOffsetReg.r64(), asmjit::x86::qword_ptr(HostOffsetReg, PageIndexReg, 3));
    m_Assembler.cmp(HostOffsetReg.r64(), (int64_t)-1);

    const stdstr AfterStoreLabel = stdstr_f("MemoryWriteMap_%X_AfterStore", m_CompilePC);
    asmjit::Label AfterStore = m_Assembler.newLabel();
    asmjit::Label SlowPath = m_Assembler.newLabel();
    const CX64RegInfo PreMapRegSet(m_RegWorkingSet);
    m_Assembler.JeLabel(stdstr_f("MemoryWriteMap_%X_Miss", m_CompilePC).c_str(), SlowPath);

    if (Size == 32)
    {
        const asmjit::x86::Mem Target = asmjit::x86::dword_ptr(AddressReg, HostOffsetReg);
        if (ValueConst)
        {
            m_Assembler.mov(Target, ConstValue32);
        }
        else
        {
            m_Assembler.mov(Target, ValueReg.r32());
        }
    }
    else if (Size == 16)
    {
        m_Assembler.xor_(AddressReg.r32(), 2);
        const asmjit::x86::Mem Target = asmjit::x86::word_ptr(AddressReg, HostOffsetReg);
        if (ValueConst)
        {
            m_Assembler.mov(Target, (uint16_t)ConstValue32);
        }
        else
        {
            m_Assembler.mov(Target, ValueReg.r16());
        }
    }
    else
    {
        m_Assembler.xor_(AddressReg.r32(), 3);
        const asmjit::x86::Mem Target = asmjit::x86::byte_ptr(AddressReg, HostOffsetReg);
        if (ValueConst)
        {
            m_Assembler.mov(Target, (uint8_t)ConstValue32);
        }
        else
        {
            m_Assembler.mov(Target, ValueReg.r8Lo());
        }
    }

    m_Assembler.EnterSecondarySection();
    m_CodeBlock.Log("");
    m_CodeBlock.Log("      MemoryWriteMap_%X_Miss:", m_CompilePC);
    m_Assembler.bind(SlowPath);
    m_RegWorkingSet = PreMapRegSet;
    m_Assembler.MoveConstToVariable(&g_Reg->m_PROGRAM_COUNTER, "PROGRAM_COUNTER", m_CompilePC);
    if (m_PipelineStage != PIPELINE_STAGE_NORMAL)
    {
        m_Assembler.MoveConstToVariable(&g_System->m_PipelineStage, "g_System->m_PipelineStage", PIPELINE_STAGE_JUMP);
    }
    m_RegWorkingSet.BeforeCallDirect();
    m_Assembler.mov(X64Abi::Arg1.r32(), AddressReg.r32());
    if (ValueConst)
    {
        m_Assembler.mov(X64Abi::Arg2.r32(), Size == 8 ? (ConstValue32 & 0xFF) : (Size == 16 ? (ConstValue32 & 0xFFFF) : ConstValue32));
    }
    else
    {
        m_Assembler.mov(X64Abi::Arg2.r32(), ValueReg.r32());
    }
    m_Assembler.sub(asmjit::x86::rsp, 32);
    if (Size == 8)
    {
        m_Assembler.CallThis(&m_MMU, MemberFuncAddress(&CMipsMemoryVM::SB_VAddr32), "CMipsMemoryVM::SB_VAddr32");
    }
    else if (Size == 16)
    {
        m_Assembler.CallThis(&m_MMU, MemberFuncAddress(&CMipsMemoryVM::SH_VAddr32), "CMipsMemoryVM::SH_VAddr32");
    }
    else
    {
        m_Assembler.CallThis(&m_MMU, MemberFuncAddress(&CMipsMemoryVM::SW_VAddr32), "CMipsMemoryVM::SW_VAddr32");
    }
    m_Assembler.add(asmjit::x86::rsp, 32);
    m_Assembler.test(asmjit::x86::al, asmjit::x86::al);
    m_RegWorkingSet.AfterCallDirect();
    asmjit::Label SlowPathException = m_Assembler.newLabel();
    m_Assembler.JeLabel(stdstr_f("MemoryWriteMap_%X_Exception", m_CompilePC).c_str(), SlowPathException);
    if (m_PipelineStage != PIPELINE_STAGE_NORMAL)
    {
        m_Assembler.MoveConstToVariable(&g_System->m_PipelineStage, "g_System->m_PipelineStage", PIPELINE_STAGE_NORMAL);
    }
    m_Assembler.JmpLabel(AfterStoreLabel.c_str(), AfterStore);
    m_CodeBlock.Log("");
    m_CodeBlock.Log("      MemoryWriteMap_%X_Exception:", m_CompilePC);
    m_Assembler.bind(SlowPathException);
    CompileExit(m_CompilePC, (uint32_t)-1, ExitRegSet, ExitReason_Exception);
    m_Assembler.EnterPrimarySection();
    m_CodeBlock.Log("");
    m_CodeBlock.Log("      %s:", AfterStoreLabel.c_str());
    m_Assembler.bind(AfterStore);
    m_RegWorkingSet = PreMapRegSet;
}

asmjit::x86::Gp CX64RecompilerOps::BaseOffsetAddress(bool UseBaseRegister)
{
    asmjit::x86::Gp AddressReg;
    if (m_RegWorkingSet.IsMapped(m_Opcode.base))
    {
        const asmjit::x86::Gp & BaseReg = m_RegWorkingSet.GetMipsRegMap(m_Opcode.base);
        if (m_Opcode.offset != 0)
        {
            m_RegWorkingSet.ProtectGPR(m_Opcode.base);
            AddressReg = m_RegWorkingSet.Map_TempReg(asmjit::x86::Gpd(), -1);
            m_Assembler.lea(AddressReg, asmjit::x86::ptr(BaseReg.r32(), (int16_t)m_Opcode.offset));
        }
        else if (UseBaseRegister)
        {
            m_RegWorkingSet.ProtectGPR(m_Opcode.base);
            AddressReg = BaseReg;
        }
        else
        {
            AddressReg = m_RegWorkingSet.Map_TempReg(asmjit::x86::Gpd(), m_Opcode.base);
        }
    }
    else
    {
        AddressReg = m_RegWorkingSet.Map_TempReg(asmjit::x86::Gpd(), m_Opcode.base);
        if (m_Opcode.offset != 0)
        {
            m_Assembler.add(AddressReg.r32(), (int32_t)((int16_t)m_Opcode.offset));
        }
    }
    return AddressReg;
}

void CX64RecompilerOps::ResetMemoryStack()
{
    constexpr int32_t MipsReg = 29;
    if (m_RegWorkingSet.IsConst(MipsReg))
    {
        m_Assembler.MoveConstToVariable(&m_Reg.m_GPR[MipsReg].UW[0], CRegName::GPR_Lo[MipsReg], m_RegWorkingSet.GetMipsRegLo(MipsReg));
    }
    else if (m_RegWorkingSet.IsMapped(MipsReg))
    {
        m_Assembler.MovDwordToVariable(&m_Reg.m_GPR[MipsReg].UW[0], CRegName::GPR_Lo[MipsReg], m_RegWorkingSet.GetMipsRegMap(MipsReg));
    }

    const asmjit::x86::Gp MemoryStackReg = m_RegWorkingSet.Get_MemoryStack();
    if (MemoryStackReg.isValid())
    {
        m_CodeBlock.Log("    regcache: unallocate memory stack");
        m_RegWorkingSet.SetX64Mapped(MemoryStackReg.id(), CX64RegInfo::NotMapped);
    }

    m_RegWorkingSet.BeforeCallDirect();
    m_Assembler.sub(asmjit::x86::rsp, 32);
    m_Assembler.CallThis(m_Recompiler, MemberFuncAddress(&CRecompiler::ResetMemoryStackPos), "CRecompiler::ResetMemoryStackPos");
    m_Assembler.add(asmjit::x86::rsp, 32);
    m_RegWorkingSet.AfterCallDirect();
}

#endif