#pragma once

#if defined(__amd64__) || defined(_M_X64)
//#define USE_ASMJITLITE
#endif

#ifdef USE_ASMJITLITE
#include <AsmJitLite/asmjit.h>
#else
// ASMJIT_STATIC is also defined on the command line (see CMakeLists.txt);
// asmjit itself just needs it defined once before the first asmjit header.
#ifndef ASMJIT_STATIC
#define ASMJIT_STATIC
#endif

#ifdef new
#pragma push_macro("new")
#undef new
#include <asmjit/asmjit.h>
#pragma pop_macro("new")
#else
#include <asmjit/asmjit.h>
#endif
#include <asmjit/a64.h>
#include <asmjit/arm.h>
#include <asmjit/arm/a64assembler.h>
#endif