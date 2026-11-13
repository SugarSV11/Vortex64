#pragma once
#include <stdint.h>

// MSVC's __interface makes every method pure virtual. The recompiler calls
// these through the vtable (slot 0 = Read32, slot 1 = Write32), so on other
// compilers they must be declared virtual explicitly and in the same order.
// No virtual destructor: it would shift the slots on the Itanium C++ ABI.
struct MemoryHandler
{
    virtual bool Read32(uint32_t Address, uint32_t & Value) = 0;
    virtual bool Write32(uint32_t Address, uint32_t Value, uint32_t Mask) = 0;
};
