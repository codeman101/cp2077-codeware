#pragma once

// R30 (branch mac-v1 only). jackmaxwil's RED4ext-macos loader loads the plugins from a hook on the game's main(), before the RTTI system runs its
// register callbacks, and it has no Frida script. So the natives register the way the Windows plugins and his ports do it: through the game's own
// CRTTISystem::AddRegisterCallback, in place of the Frida script's NATREG call at InitScripts entry.
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
    rtti->AddRegisterCallback(&RunNativeRegistration);
    std::fprintf(stderr, "[%s] R30 RTTI register callback added (natives register when the game runs the callbacks)\n", aTag);
    return true;
}
}
#endif
