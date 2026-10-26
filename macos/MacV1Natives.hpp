#pragma once

// R30 (CP2077_MAC_V1 builds only: our public loader on game 2.5.0). The public RED4ext-macos loader (codeman101/RED4ext-macos, mac-2.5) loads the
// plugins from a hook on the game's WinMain, before the RTTI system runs its register callbacks, and it has no Frida script. So the natives register
// the way the Windows plugins do it: through the game's own CRTTISystem callbacks, in place of the Frida script's NATREG call at InitScripts entry.
// It is the POST-register callback (R30 J1): the game runs its own register callbacks first (its classes) and then the post-register callbacks
// in the order they were added (its own describe steps, added by its static registrars before main(), so before ours). Our entry points
// register AND describe (RedLib RunPending, Codeware's global publishing), so they must run after the game's describe steps, which is where the
// Frida script ran them. From the register callback they ran too early: 73 game properties instead of 19173, an empty global function map,
// and LoadScripts returned 0.
// Each of our three plugins adds the same callback. Whichever runs first calls the three exported entry points in the order the Frida script used
// (TweakXL, ArchiveXL, Codeware; each entry point runs once, the later callbacks find them done). An entry point of a plugin that is not loaded is
// skipped. The post-load step (codeware_postLoadScripts, which also runs archivexl_postLoadScripts) is Codeware's: see macos/MacV1.cpp.
#if defined(CP2077_MAC_V1)
#include <RED4ext/RTTISystem.hpp>

#include <cstdio>
#include <dlfcn.h>

namespace Mac::V1
{
inline void RunNativeRegistration()
{
    static const char* const names[] = {"tweakxl_registerNatives", "archivexl_registerNatives", "codeware_registerNatives"};
    for (const char* name : names)
    {
        if (auto* fn = reinterpret_cast<void (*)()>(dlsym(RTLD_DEFAULT, name)))
        {
            fn();
        }
    }
}

inline bool AddNativeRegistrationCallback(const char* aTag)
{
    auto* rtti = RED4ext::CRTTISystem::Get();
    if (!rtti)
    {
        std::fprintf(stderr, "[%s] R30 no RTTI system at plugin load: the natives cannot register\n", aTag);
        return false;
    }
    rtti->AddPostRegisterCallback(&RunNativeRegistration);
    std::fprintf(stderr, "[%s] R30 RTTI post-register callback added (natives register after the game's own describe steps)\n", aTag);
    return true;
}
}
#endif
