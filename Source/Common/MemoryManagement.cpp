#include "MemoryManagement.h"
#include <sys/mman.h>
#include "MemoryManagement.h"

static bool TranslateFromMemProtect(MEM_PROTECTION memProtection, int & OsMemProtection)
{
    switch (memProtection)
    {
    case MEM_NOACCESS: OsMemProtection = PROT_NONE; break;
    case MEM_READONLY: OsMemProtection = PROT_READ; break;
    case MEM_READWRITE: OsMemProtection = PROT_READ | PROT_WRITE; break;
    case MEM_EXECUTE_READWRITE: OsMemProtection = PROT_READ | PROT_WRITE | PROT_EXEC; break;
    default:
        return false;
    }
    return true;
}


void * AllocateAddressSpace(size_t size, void * base_address)
{
    void * ptr = mmap((void *)0, size, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (ptr == MAP_FAILED)
    {
        return nullptr;
    }
    msync(ptr, size, MS_SYNC | MS_INVALIDATE);
    return ptr;
}

bool FreeAddressSpace(void * addr, size_t size)
{
    if (addr == nullptr)
    {
        return true;
    }
    msync(addr, size, MS_SYNC);
    munmap(addr, size);
    return true;
}

void * CommitMemory(void * addr, size_t size, MEM_PROTECTION memProtection)
{
    int OsMemProtection;
    if (!TranslateFromMemProtect(memProtection, OsMemProtection))
    {
        return nullptr;
    }
    void * ptr = mmap(addr, size, OsMemProtection, MAP_FIXED | MAP_SHARED | MAP_ANON, -1, 0);
    msync(addr, size, MS_SYNC | MS_INVALIDATE);
    return ptr;
}

bool DecommitMemory(void * addr, size_t size)
{
    // Instead of unmapping the address, we're just gonna trick
    // the TLB to mark this as a new mapped area which, due to
    // demand paging, will not be committed until used.

    mmap(addr, size, PROT_NONE, MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1, 0);
    msync(addr, size, MS_SYNC | MS_INVALIDATE);

    return true;
}

bool ProtectMemory(void * addr, size_t size, MEM_PROTECTION memProtection, MEM_PROTECTION * OldProtect)
{
    int OsMemProtection;
    if (!TranslateFromMemProtect(memProtection, OsMemProtection))
    {
        return false;
    }

    return mprotect(addr, size, OsMemProtection) == 0;
}
