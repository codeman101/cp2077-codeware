#pragma once

#include "Core/Foundation/Feature.hpp"
#include "Core/Hooking/HookingAgent.hpp"
#include "Core/Logging/LoggingAgent.hpp"
#include "Red/InkLibrary.hpp"

namespace App
{
class WidgetSpawningService
    : public Core::Feature
    , public Core::HookingAgent
    , public Core::LoggingAgent
{
public:
    static constexpr auto WidgetSpawnEventName = "InkWidget/Spawn";

    static void ToggleWidgetSpawnEvent(bool aState);

protected:
    void OnBootstrap() override;
    void OnShutdown() override;

    static Red::Handle<Red::ink::WidgetLibraryItemInstance> OnSpawnLocal(Red::ink::WidgetLibraryResource* aLibrary,
                                                                         Red::CName aItemName);
    static Red::Handle<Red::ink::WidgetLibraryItemInstance> OnSpawnExternal(Red::ink::WidgetLibraryResource* aLibrary,
                                                                            Red::ResourcePath aExternalPath,
                                                                            Red::CName aItemName);
#if !defined(__APPLE__)
    static bool OnAsyncSpawnLocal(Red::ink::WidgetLibraryResource& aLibrary,
                                  Red::InkSpawningInfo& aSpawningInfo,
                                  Red::CName aItemName);
    static bool OnAsyncSpawnExternal(Red::ink::WidgetLibraryResource& aLibrary,
                                     Red::InkSpawningInfo& aSpawningInfo,
                                     Red::ResourcePath aExternalPath,
                                     Red::CName aItemName);
#endif
    static void OnFinishAsyncSpawn(Red::InkSpawningContext& aContext,
                                   Red::Handle<Red::ink::WidgetLibraryItemInstance>& aInstance);

#if defined(__APPLE__)
    // R20. The Mac ABI forms of the two async spawn hooks (see src/Red/InkLibrary.hpp): they carry one more bool than the
    // Windows ones.
    static bool OnAsyncSpawnLocalMac(Red::ink::WidgetLibraryResource& aLibrary,
                                     Red::InkSpawningInfo& aSpawningInfo,
                                     Red::CName aItemName,
                                     bool aFlag);
    static bool OnAsyncSpawnExternalMac(Red::ink::WidgetLibraryResource& aLibrary,
                                        Red::InkSpawningInfo& aSpawningInfo,
                                        Red::ResourcePath aExternalPath,
                                        Red::CName aItemName,
                                        bool aFlag);
    static void MacCheckSpawnedInstance(const Red::Handle<Red::ink::WidgetLibraryItemInstance>& aInstance, const char* aWhere);
#endif

    inline static void InjectDependency(Red::ink::WidgetLibraryResource& aLibrary,
                                        Red::ResourcePath aExternalPath);
    inline static void InjectController(Red::Handle<Red::ink::WidgetLibraryItemInstance>& aInstance,
                                        Red::CName aControllerName);
    inline static void InheritProperties(Red::IScriptable* aTarget, Red::IScriptable* aSource);

    inline static std::shared_mutex s_mutex;
    inline static bool s_widgetSpawnEventEnabled{false};
};
}
