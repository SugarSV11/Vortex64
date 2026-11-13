#include "DynamicLibrary.h"
#include "StdString.h"
#include <dlfcn.h>
#include <link.h>
#include <errno.h>
#include <unistd.h>

DynLibHandle DynamicLibraryOpen(const char * pccLibraryPath, bool ShowErrors)
{
    if (pccLibraryPath == nullptr)
    {
        return nullptr;
    }
    DynLibHandle Lib = (DynLibHandle)dlopen(pccLibraryPath, RTLD_NOW);
    return Lib;
}

void DynamicLibraryClose(DynLibHandle Lib)
{
    if (Lib != nullptr)
    {
        dlclose(Lib);
    }
}

void * DynamicLibraryGetProc(DynLibHandle Lib, const char * ProcedureName)
{
    if (ProcedureName == nullptr)
    {
        return nullptr;
    }

    // GetProcAddress only looks at the module itself, dlsym also finds symbols
    // exported by the libraries it depends on. A plugin helper module linked against
    // a real plugin (e.g. the Qt configuration module) must not answer for that
    // plugin's entry points, so the symbol has to be defined in this very object.
    void * Symbol = dlsym(Lib, ProcedureName);
    if (Symbol != nullptr)
    {
        struct link_map * Map = nullptr;
        Dl_info Info;
        if (dlinfo(Lib, RTLD_DI_LINKMAP, &Map) == 0 && Map != nullptr && dladdr(Symbol, &Info) != 0 && Info.dli_fbase != (void *)Map->l_addr)
        {
            return nullptr;
        }
    }
    return Symbol;
}
