#pragma once

#include "Red/InkSpawner.hpp"

namespace Raw::InkWidgetLibrary
{
#if defined(__APPLE__)
// Mac ABI (R20). SpawnFromLocal/SpawnFromExternal return their Handle through x8 and have no out argument; the Windows
// uintptr_t result (the out pointer) does not exist. The two Async* functions take one more bool than the Windows
// declarations (always 0 from the game's callers on AsyncSpawnFromExternal, a real flag on AsyncSpawnFromLocal).
// The *Native objects are the hook targets and the calls to the original. SpawnFromLocal/SpawnFromExternal keep the Windows
// call shape for the services through the wrappers below.
constexpr auto SpawnFromLocalNative = Core::RawFunc<
    /* addr = */ Red::AddressLib::InkWidgetLibrary_SpawnFromLocal,
    /* type = */ Red::Handle<Red::ink::WidgetLibraryItemInstance> (*)(
        Red::ink::WidgetLibraryResource& aLibrary,
        Red::CName aItemName)>();

constexpr auto SpawnFromExternalNative = Core::RawFunc<
    /* addr = */ Red::AddressLib::InkWidgetLibrary_SpawnFromExternal,
    /* type = */ Red::Handle<Red::ink::WidgetLibraryItemInstance> (*)(
        Red::ink::WidgetLibraryResource& aLibrary,
        Red::ResourcePath aExternalPath,
        Red::CName aItemName)>();

constexpr auto AsyncSpawnFromLocal = Core::RawFunc<
    /* addr = */ Red::AddressLib::InkWidgetLibrary_AsyncSpawnFromLocal,
    /* type = */ bool (*)(
        Red::ink::WidgetLibraryResource& aLibrary,
        Red::InkSpawningInfo& aSpawningInfo,
        Red::CName aItemName,
        bool aFlag)>();

constexpr auto AsyncSpawnFromExternal = Core::RawFunc<
    /* addr = */ Red::AddressLib::InkWidgetLibrary_AsyncSpawnFromExternal,
    /* type = */ bool (*)(
        Red::ink::WidgetLibraryResource& aLibrary,
        Red::InkSpawningInfo& aSpawningInfo,
        Red::ResourcePath aExternalPath,
        Red::CName aItemName,
        bool aFlag)>();

struct SpawnFromLocalFn
{
    uintptr_t operator()(Red::ink::WidgetLibraryResource& aLibrary,
                         Red::Handle<Red::ink::WidgetLibraryItemInstance>& aInstance,
                         Red::CName aItemName) const
    {
        aInstance = SpawnFromLocalNative(aLibrary, aItemName);
        return reinterpret_cast<uintptr_t>(&aInstance);
    }
};
inline constexpr SpawnFromLocalFn SpawnFromLocal{};

struct SpawnFromExternalFn
{
    uintptr_t operator()(Red::ink::WidgetLibraryResource& aLibrary,
                         Red::Handle<Red::ink::WidgetLibraryItemInstance>& aInstance,
                         Red::ResourcePath aExternalPath,
                         Red::CName aItemName) const
    {
        aInstance = SpawnFromExternalNative(aLibrary, aExternalPath, aItemName);
        return reinterpret_cast<uintptr_t>(&aInstance);
    }
};
inline constexpr SpawnFromExternalFn SpawnFromExternal{};
#else
constexpr auto AsyncSpawnFromExternal = Core::RawFunc<
    /* addr = */ Red::AddressLib::InkWidgetLibrary_AsyncSpawnFromExternal,
    /* type = */ bool (*)(
        Red::ink::WidgetLibraryResource& aLibrary,
        Red::InkSpawningInfo& aSpawningInfo,
        Red::ResourcePath aExternalPath,
        Red::CName aItemName)>();

constexpr auto AsyncSpawnFromLocal = Core::RawFunc<
    /* addr = */ Red::AddressLib::InkWidgetLibrary_AsyncSpawnFromLocal,
    /* type = */ bool (*)(
        Red::ink::WidgetLibraryResource& aLibrary,
        Red::InkSpawningInfo& aSpawningInfo,
        Red::CName aItemName)>();

constexpr auto SpawnFromExternal = Core::RawFunc<
    /* addr = */ Red::AddressLib::InkWidgetLibrary_SpawnFromExternal,
    /* type = */ uintptr_t (*)(
        Red::ink::WidgetLibraryResource& aLibrary,
        Red::Handle<Red::ink::WidgetLibraryItemInstance>& aInstance,
        Red::ResourcePath aExternalPath,
        Red::CName aItemName)>();

constexpr auto SpawnFromLocal = Core::RawFunc<
    /* addr = */ Red::AddressLib::InkWidgetLibrary_SpawnFromLocal,
    /* type = */ uintptr_t (*)(
        Red::ink::WidgetLibraryResource& aLibrary,
        Red::Handle<Red::ink::WidgetLibraryItemInstance>& aInstance,
        Red::CName aItemName)>();
#endif
}
