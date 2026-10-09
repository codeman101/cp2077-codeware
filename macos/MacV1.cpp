// R30 (branch mac-v1 only). The post-load step on jackmaxwil's RED4ext-macos loader.
//
// The Frida script of the old runtime called codeware_postLoadScripts when the game's script loader returned (frida-clean.js hooks the default
// loader 0x3D9A028; frida-modded.js, R29, hooks the wrapper 0x3D99E44 because -scriptsBlobPath takes the explicit-path loader 0x3D99EAC). His loader
// has no Frida script, so Codeware hooks the same wrapper itself and calls codeware_postLoadScripts (late types, ArchiveXL's post-load step, the
// script services) right after it returns.
// The wrapper: Initialize (0x3D9E494, call at 0x3D9E600, once per boot) calls it with (loader, profile byte, flags); it takes 0x3D99EAC when the
// blob path is set and tail-calls 0x3D9A028 otherwise. Both loaders are reached only through it (tail calls at 0x3D99EA8 and 0x3D99E94, no other
// code or data reference). Its other caller, 0x3D9EDD0 in 0x3D9ED9C (load scripts from a path), is referenced by nothing in this build. The
// post-load step still runs once only; a second call is logged and skipped. Evidence: tools/r29/README.md in the cp2077-mac workspace.
#if defined(CP2077_MAC_V1)
#include "Core/Hooking/HookingDriver.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>

extern "C" void codeware_postLoadScripts(); // macos/MacSupport.cpp

namespace Red
{
uintptr_t MacGameBase();
}

namespace
{
constexpr uintptr_t kScriptLoaderWrapperRva = 0x3D99E44;

using ScriptLoaderWrapper_t = bool (*)(void* aLoader, uint64_t aProfile, uint64_t aFlags);
ScriptLoaderWrapper_t g_wrapperOriginal = nullptr;
std::atomic<int> g_wrapperCalls{0};
std::atomic<bool> g_postLoadDone{false};

bool OnScriptLoaderWrapper(void* aLoader, uint64_t aProfile, uint64_t aFlags)
{
    const bool ok = g_wrapperOriginal(aLoader, aProfile, aFlags);
    const int call = ++g_wrapperCalls;
    std::fprintf(stderr, "[cw] R30 script loader returned %d (call %d)\n", ok ? 1 : 0, call);
    if (g_postLoadDone.exchange(true))
    {
        std::fprintf(stderr, "[cw] R30 codeware_postLoadScripts skipped: already ran once this session (call %d)\n", call);
        return ok;
    }
    codeware_postLoadScripts();
    std::fprintf(stderr, "[cw] R30 codeware_postLoadScripts called\n");
    std::fflush(stderr);
    return ok;
}
}

namespace Mac::V1
{
bool InstallScriptLoadedHook()
{
    const auto target = Red::MacGameBase() + kScriptLoaderWrapperRva;
    const bool ok = Core::HookingDriver::GetDefault().HookAttach(target, reinterpret_cast<void*>(&OnScriptLoaderWrapper),
                                                                 reinterpret_cast<void**>(&g_wrapperOriginal));
    std::fprintf(stderr, "[cw] R30 script loader wrapper hook (rva 0x%lx) attached=%d\n", static_cast<unsigned long>(kScriptLoaderWrapperRva),
                 ok ? 1 : 0);
    return ok;
}
}
#endif
