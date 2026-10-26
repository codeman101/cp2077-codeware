// R30 (public-loader build, CP2077_MAC_V1). The post-load step on our RED4ext-macos loader (codeman101/RED4ext-macos, branch mac-2.5), game 2.5.0.
//
// The private launcher's Frida script (frida-modded.js) calls codeware_postLoadScripts when the game's script loader wrapper returns (late types,
// ArchiveXL's post-load step, the script services). The public loader has no Frida script, so Codeware hooks the same wrapper itself and calls
// codeware_postLoadScripts right after it returns.
// The wrapper (game 2.5.0, RVA 0x4D7A9F8, 260 bytes; 0x3D99E44 in 2.3.1): (loader, profile byte, flags) -> bool. Engine initialisation (0x4D7EFB8,
// once per boot) and the load-scripts-from-path method (0x4D7F974) are its only callers. It tail-jumps to the explicit-path loader 0x4D7ABBC when the
// blob path is set (-scriptsBlobPath) and calls the default loader 0x4D7AE9C otherwise, then returns the engine's scriptsLoaded byte (+0x54). In 2.3.1
// both loaders were tail calls; in 2.5 the default one is a call, which does not matter here. The post-load step still runs once only; a second call is
// logged and skipped. Evidence: the 2.5 frida-modded.js of the everyday install, tools/r30/hook-check.py (the engine can patch the target), and the
// prologue check below.
// The RVA is a game 2.5.0 value (Mac build 5814786). The loader refuses another game build through its address database UUID; the prologue words are
// checked anyway, so a different build skips the hook (with a log line) instead of patching an unrelated function.
#if defined(CP2077_MAC_V1)
#include "Core/Hooking/HookingDriver.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" void codeware_postLoadScripts(); // macos/MacSupport.cpp

namespace Red
{
uintptr_t MacGameBase();
}

namespace
{
constexpr uintptr_t kScriptLoaderWrapperRva = 0x4D7A9F8;

// The first eight instructions of the wrapper in 2.5.0: stp x24,x23,[sp,#-0x40]!; stp x22,x21; stp x20,x19; stp x29,x30; add x29,sp,#0x30;
// mov x21,x2; mov x20,x1; mov x19,x0.
constexpr uint32_t kScriptLoaderWrapperPrologue[8] = {0xA9BC5FF8, 0xA90157F6, 0xA9024FF4, 0xA9037BFD,
                                                      0x9100C3FD, 0xAA0203F5, 0xAA0103F4, 0xAA0003F3};

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
    if (std::memcmp(reinterpret_cast<const void*>(target), kScriptLoaderWrapperPrologue, sizeof(kScriptLoaderWrapperPrologue)) != 0)
    {
        std::fprintf(stderr, "[cw] R30 script loader wrapper hook skipped: the code at rva 0x%lx is not the game 2.5.0 wrapper "
                             "(codeware_postLoadScripts will not run)\n",
                     static_cast<unsigned long>(kScriptLoaderWrapperRva));
        return false;
    }
    const bool ok = Core::HookingDriver::GetDefault().HookAttach(target, reinterpret_cast<void*>(&OnScriptLoaderWrapper),
                                                                 reinterpret_cast<void**>(&g_wrapperOriginal));
    std::fprintf(stderr, "[cw] R30 script loader wrapper hook (rva 0x%lx) attached=%d\n", static_cast<unsigned long>(kScriptLoaderWrapperRva),
                 ok ? 1 : 0);
    return ok;
}
}
#endif
