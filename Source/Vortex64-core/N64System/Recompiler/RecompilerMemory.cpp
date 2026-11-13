#include "stdafx.h"

#include <Common/MemoryManagement.h>
#include <Vortex64-core/N64System/Recompiler/Recompiler.h>
#include <Vortex64-core/N64System/Recompiler/RecompilerMemory.h>
#include <Vortex64-core/N64System/SystemGlobals.h>
#ifndef _WIN32
#include <sys/mman.h>
#include <stdint.h>
#endif

#ifndef _WIN32
// The generated code addresses RDRAM (and other emulator memory) with rip-relative
// operands, which reach +-2GB. On Windows both reservations are MEM_TOP_DOWN and end
// up next to each other; a plain mmap() can leave them further apart, and asmjit then
// fails the relocation and leaves a garbage displacement in the code. So the code
// buffer is reserved close to RDRAM.
static uint8_t * AllocateNearAddress(size_t Size, uint8_t * Reference)
{
    if (Reference == nullptr)
    {
        return nullptr;
    }
    const uintptr_t Ref = (uintptr_t)Reference;
    const uintptr_t MaxDistance = 0x60000000;
    for (uintptr_t Step = 0x02000000; Step < MaxDistance; Step += 0x00400000)
    {
        for (int Direction = 0; Direction < 2; Direction++)
        {
            uintptr_t Hint = Direction == 0 ? Ref + Step : Ref - Step;
            if (Direction == 1 && Ref < Step + 0x10000)
            {
                continue;
            }
            void * Ptr = mmap((void *)Hint, Size, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_FIXED_NOREPLACE, -1, 0);
            if (Ptr == MAP_FAILED)
            {
                continue;
            }
            if ((uintptr_t)Ptr == Hint)
            {
                return (uint8_t *)Ptr;
            }
            munmap(Ptr, Size);
        }
    }
    return nullptr;
}
#endif

CRecompMemory::CRecompMemory() :
    m_RecompCode(nullptr),
    m_RecompSize(0)
{
    m_RecompPos = nullptr;
}

CRecompMemory::~CRecompMemory()
{
    if (m_RecompCode)
    {
        FreeAddressSpace(m_RecompCode, MaxCompileBufferSize + 4);
        m_RecompCode = nullptr;
    }
    m_RecompPos = nullptr;
}

bool CRecompMemory::AllocateMemory(uint8_t * NearAddress)
{
    WriteTrace(TraceRecompiler, TraceDebug, "Start");
    uint8_t * RecompCodeBase = nullptr;
#ifndef _WIN32
    RecompCodeBase = AllocateNearAddress(MaxCompileBufferSize + 4, NearAddress);
#else
    (void)NearAddress;
#endif
    if (RecompCodeBase == nullptr)
    {
        RecompCodeBase = (uint8_t *)AllocateAddressSpace(MaxCompileBufferSize + 4);
    }
    WriteTrace(TraceRecompiler, TraceDebug, "RecompCodeBase = %X", RecompCodeBase);
    if (RecompCodeBase == nullptr)
    {
        WriteTrace(TraceRecompiler, TraceError, "Failed to allocate RecompCodeBase");
        g_Notify->DisplayError(MSG_MEM_ALLOC_ERROR);
        return false;
    }

    m_RecompCode = (uint8_t *)CommitMemory(RecompCodeBase, InitialCompileBufferSize, MEM_EXECUTE_READWRITE);
    if (m_RecompCode == nullptr)
    {
        WriteTrace(TraceRecompiler, TraceError, "Failed to commit initial buffer");
        FreeAddressSpace(RecompCodeBase, MaxCompileBufferSize + 4);
        g_Notify->DisplayError(MSG_MEM_ALLOC_ERROR);
        return false;
    }
    m_RecompSize = InitialCompileBufferSize;
    m_RecompPos = m_RecompCode;
    memset(m_RecompCode, 0, InitialCompileBufferSize);
    WriteTrace(TraceRecompiler, TraceDebug, "Done");
    return true;
}

bool CRecompMemory::CheckRecompMem(uint32_t BlockSize)
{
    uint32_t Size = (uint32_t)((uint8_t *)m_RecompPos - (uint8_t *)m_RecompCode);
    if (BlockSize < 0x50000)
    {
        BlockSize = 0x50000;
    }
    if ((Size + BlockSize) < m_RecompSize)
    {
        return true;
    }
    if (m_RecompSize == MaxCompileBufferSize)
    {
        g_Recompiler->ResetRecompCode(true);
        Size = (uint32_t)((uint8_t *)m_RecompPos - (uint8_t *)m_RecompCode);
        if ((Size + BlockSize) < m_RecompSize)
        {
            return true;
        }
        return false;
    }
    if (BlockSize > IncreaseCompileBufferSize)
    {
        g_Notify->BreakPoint(__FILE__, __LINE__);
    }
    void * MemAddr = CommitMemory(m_RecompCode + m_RecompSize, IncreaseCompileBufferSize, MEM_EXECUTE_READWRITE);
    if (MemAddr == nullptr)
    {
        WriteTrace(TraceRecompiler, TraceError, "Failed to increase buffer");
        g_Notify->FatalError(MSG_MEM_ALLOC_ERROR);
    }
    m_RecompSize += IncreaseCompileBufferSize;
    return true;
}

void CRecompMemory::Reset()
{
    m_RecompPos = m_RecompCode;
}

void CRecompMemory::ShowMemUsed()
{
    uint32_t Size = (uint32_t)(m_RecompPos - m_RecompCode);
    uint32_t MB = Size / 0x100000;
    Size -= MB * 0x100000;
    uint32_t KB = Size / 1024;
    Size -= KB * 1024;

    uint32_t TotalAvaliable = m_RecompSize / 0x100000;

    g_Notify->DisplayMessage(0, stdstr_f("Memory used: %d mb %-3d kb %-3d bytes     Total Available: %d mb", MB, KB, Size, TotalAvaliable).c_str());
}