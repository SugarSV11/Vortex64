#pragma once
#if defined(__amd64__) || defined(_M_X64)
#include <Project64-core/N64System/Recompiler/asmjit.h>
#include <map>
#include <string>

class CCodeBlock;

// Integer argument registers of the C ABI used by generated calls into C++
// helpers.  Microsoft x64 (Windows) passes the first four integer/pointer
// arguments in rcx, rdx, r8, r9; System V AMD64 (Linux) in rdi, rsi, rdx, rcx.
namespace X64Abi
{
#if defined(_WIN32)
static const asmjit::x86::Gp Arg0 = asmjit::x86::rcx;
static const asmjit::x86::Gp Arg1 = asmjit::x86::rdx;
static const asmjit::x86::Gp Arg2 = asmjit::x86::r8;
static const asmjit::x86::Gp Arg3 = asmjit::x86::r9;
#else
static const asmjit::x86::Gp Arg0 = asmjit::x86::rdi;
static const asmjit::x86::Gp Arg1 = asmjit::x86::rsi;
static const asmjit::x86::Gp Arg2 = asmjit::x86::rdx;
static const asmjit::x86::Gp Arg3 = asmjit::x86::rcx;
#endif
} // namespace X64Abi

class CX64Ops :
    public asmjit::x86::Assembler,
    public asmjit::Logger
{
public:
    CX64Ops(CCodeBlock & CodeBlock);

    void JnsLabel(const char * LabelName, asmjit::Label & JumpLabel);
    void JoLabel(const char * LabelName, asmjit::Label & JumpLabel);
    void JsLabel(const char * LabelName, asmjit::Label & JumpLabel);
    void JeLabel(const char * LabelName, asmjit::Label & JumpLabel);
    void JneLabel(const char * LabelName, asmjit::Label & JumpLabel);
    void JmpLabel(const char * LabelName, asmjit::Label & JumpLabel);
    void JleLabel(const char * LabelName, asmjit::Label & JumpLabel);
    void JlLabel(const char * LabelName, asmjit::Label & JumpLabel);
    void JgeLabel(const char * LabelName, asmjit::Label & JumpLabel);
    void JgLabel(const char * LabelName, asmjit::Label & JumpLabel);
    void CmpConstToVariable(void * Variable, const char * VariableName, uint32_t Const);
    void CmpRegToVariable(const asmjit::x86::Gp & Reg, void * Variable, const char * VariableName);
    void MoveConstToVariable(void * Variable, const char * VariableName, uint32_t Const);
    void MoveConst64ToVariable(void * Variable, const char * VariableName, uint64_t Const);
    void MoveConstToX64reg(const asmjit::x86::Gp & Reg, uint64_t Const, const char * ValueName = nullptr);
    void MoveVariableToX64reg(const asmjit::x86::Gp & Reg, void * Variable, const char * VariableName, bool SignExtend);
    void MoveVariable32ToX64reg(const asmjit::x86::Gp & Reg, void * Variable, const char * VariableName);
    void MoveVariable64ToX64reg(const asmjit::x86::Gp & Reg, void * Variable, const char * VariableName);
    void MoveVariable32SignExtendToX64reg(const asmjit::x86::Gp & Reg, void * Variable, const char * VariableName);
    void MovDwordToVariable(void * Variable, const char * VariableName, const asmjit::x86::Gp & Src);
    void MovQwordToVariable(void * Variable, const char * VariableName, const asmjit::x86::Gp & Src);
    void AddDwordFromVariable(const asmjit::x86::Gp & Reg, void * Variable, const char * VariableName);
    void SubVariableFromX64reg(const asmjit::x86::Gp & Reg, void * Variable, const char * VariableName);
    void SubConstFromVariable(uint32_t Const, void * Variable, const char * VariableName);
    void AndConstToVariable(void * Variable, const char * VariableName, uint32_t Const);
    void AndVariableToX64reg(const asmjit::x86::Gp & Reg, void * Variable, const char * VariableName);
    void AndVariable64ToX64reg(const asmjit::x86::Gp & Reg, void * Variable, const char * VariableName);
    void OrVariableToX64reg(const asmjit::x86::Gp & Reg, void * Variable, const char * VariableName);
    void XorVariableToX64reg(const asmjit::x86::Gp & Reg, void * Variable, const char * VariableName);
    void TestVariable(void * Variable, const char * VariableName, uint32_t Const);
    // A memory operand for a variable of the process: absolute when the address is in the low 2GB
    // (what an instruction can hold), else the address goes to the scratch register r11 first
    asmjit::x86::Mem Abs(const void * Address, uint32_t Size);
    void EnterPrimarySection();
    void EnterSecondarySection();
    void EnterSection(asmjit::Section * Section);
    bool InSecondarySection() const;
    void X64BreakPoint(const char * FileName, int32_t LineNumber);
    void CallFunc(uintptr_t FunctPtr, const char * FunctName);
    void CallThis(void * ThisPtr, uintptr_t FunctPtr, const char * FunctName);

private:
    CX64Ops(void);
    CX64Ops(const CX64Ops &);
    CX64Ops & operator=(const CX64Ops &);

    asmjit::Error _log(const char * data, size_t size) noexcept;
    void AddLabelSymbol(const asmjit::Label & Label, const char * Symbol);
    void AddNumberSymbol(uintptr_t Value, const char * Symbol);
    void AddNumberSymbol(uintptr_t Value, const std::string & Symbol);

    static void BreakPointNotification(const char * FileName, int32_t LineNumber);

    typedef struct
    {
        std::string Symbol;
        uint32_t Count;
    } NumberSymbol;

    typedef std::map<uintptr_t, NumberSymbol> NumberSymbolMap;

    NumberSymbolMap m_LabelSymbols;
    NumberSymbolMap m_NumberSymbols;
    CCodeBlock & m_CodeBlock;
    asmjit::Section * m_PrimarySection;
    asmjit::Section * m_SecondarySection;
    asmjit::Section * m_CurrentSection;
};

template <typename T>
uintptr_t MemberFuncAddress(T func)
{
    uintptr_t result = 0;
    memcpy(&result, &func, sizeof(uintptr_t));
    return result;
}

#endif
