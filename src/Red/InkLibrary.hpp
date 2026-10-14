#pragma once

#include "Red/InkSpawner.hpp"

namespace Raw::InkWidgetLibrary
{
#if defined(__APPLE__)
// Mac ABI (R20). The two Async* functions take one more bool than the Windows declarations (always 0 from the game's
// callers on AsyncSpawnFromExternal, a real flag on AsyncSpawnFromLocal).
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
#endif

constexpr auto SpawnFromExternal = Core::RawFunc<
    /* addr = */ Red::AddressLib::InkWidgetLibrary_SpawnFromExternal,
    /* type = */ Red::Handle<Red::ink::WidgetLibraryItemInstance> (Red::ink::WidgetLibraryResource::*)(
        Red::ResourcePath aExternalPath,
        Red::CName aItemName)>();

constexpr auto SpawnFromLocal = Core::RawFunc<
    /* addr = */ Red::AddressLib::InkWidgetLibrary_SpawnFromLocal,
    /* type = */ Red::Handle<Red::ink::WidgetLibraryItemInstance> (Red::ink::WidgetLibraryResource::*)(
        Red::CName aItemName)>();
}
