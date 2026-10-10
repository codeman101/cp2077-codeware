#include "MacAddressResolver.hpp"

#include "MacEngine.hpp"

#include <RED4ext/Relocation.hpp>

#include <cstdio>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace
{
// RVAs from the Mac image (Steam 2.3.1). 0xC0DE000x ids are Mac-only (job bodies, ScriptGameInstance builder) and are
// not in cyberpunk2077_addresses.json. The others are SDK hashes of Codeware's Library.hpp.
const std::unordered_map<uint32_t, uintptr_t>& MacRows()
{
    static const std::unordered_map<uint32_t, uintptr_t> rows = {
        {0xC0DE0001, 0x2257548}, // ResourceSerializer_MacLoadJob. Entry runs after deserialization.
        {0xC0DE0002, 0x2257830}, // ResourceSerializer_MacPostLoadJob. Entry runs after post-load.
        {4248638169, 0xC99368},  // Entity_Attach. void(Entity*, void* ctx). Not 0xC99378 (inside the prologue).
        {3491172781, 0x3340CF8}, // NodeRef_Create. Mac ABI uint64_t(const char*, uint32_t, bool).
        {1559305924, 0x1F42944}, // GetScriptGameInstance. The GetGameInstance script native (void(ctx, frame, out, type)).
        {0xC0DE0003, 0x1F44968}, // MacScriptGameInstanceCtor. void(ScriptGameInstance* out, GameInstance*). Mac-only id.
        // R18 B0. Engine timing hooks and the framework's GameInstance job. Entry and body matched against the Windows twin.
        {3273923080, 0x3D9E494}, // CBaseEngine_InitEngine. bool(CBaseEngine&, CGameOptions&). Windows 0x7CB080: same init-stage strings and options+0xB1.
        {3729789488, 0x3D9EFC8}, // CBaseEngine_LoadGatheredResources. bool(CBaseEngine&). Windows 0x303460: virtual slot 0xD0 (Mac 0xD8), helper call, bool.
        {4007601841, 0x3F0D900}, // CGameFramework_InitializeGameInstance. Job body void(closure, group); the first word of the closure is the CGameFramework, and the job frees the closure. Windows 0x16145C.
        {3993832650, 0x21ECFD8}, // ScriptBundle_dtor. void(ScriptBundle*). Windows 0x7B7C18: same member destruction order (+0x138 ... +0x90).
        // R18 B0/B1. Entity lifecycle, input, player, vehicle, component and sound rows. Each was matched against the Windows twin
        // by a read-only agent (strings, field offsets, callees, a caller on both sides) and rated by tools/addr-match/check-rows.py.
        {3490519617, 0xC985EC},  // Entity_Initialize. void(Entity*, JobQueue&, EntityInitializeRequest*). RequestComponents and four job schedulers are inlined. Windows 0x8B0190.
        {2296260874, 0xC95E24},  // Entity_RequestComponents. Identity matches Windows 0x8B116C, but Mac has no caller (inlined into Initialize and Reassemble): never hooked.
        {489494088, 0xCA41E0},   // EntityBuilder_ExtractComponentsJob. void(EntityBuilderJobParams*, void*). Windows 0x1CD4A8.
        {437791594, 0xCA08A0},   // EntityBuilder_ScheduleExtractComponentsJob. Mac void(EntityBuilder*, job::Builder&): the Windows signature differs. Declared only. Windows 0x8AC678.
        {2182550867, 0xC97E28},  // Entity_Assemble. void(Entity*, Request*). Not 0xC97E24, which is the base class no-op. Windows 0x8AB708.
        {1560690857, 0xC95744},  // Entity_Reassemble. void(Entity*, a2, CName, u64, DynArray&, Handle&), all six in registers. Windows 0xC27FD8.
        {3596356635, 0xC992C4},  // Entity_Uninitialize. void(Entity*). Not 0xC992C0 (base no-op). Dispose inlines it. Windows 0x402F74.
        {2515274237, 0xC999E0},  // Entity_Dispose. void(Entity*). Windows 0x8B5540.
        {2263681375, 0xC996C0},  // Entity_Detach. void(Entity*). Not 0xC996BC (base no-op). Windows 0x1B3A1C.
        {525805301, 0x378A7E8},  // Entity_ResolveRecordID. Mac uint64_t TweakDBID(Entity* unused, PopulationSpawnParameter*): no out pointer. Windows 0x7280AC.
        {673454961, 0x48842E0},  // InkSystem_ProcessInputEvents. void(InkSystem*, a2, RawInputBuffer&). Windows 0x39B8E8.
        {2050111212, 0x3FD4D70}, // PlayerSystem_OnPlayerSpawned. x0..x3 map to the Windows rcx..r9. Windows 0xA1452C.
        {1883248814, 0x12E6F44}, // VehicleController_ToggleAuxLights. void(controller, bool enable, uint lightType). Not the thunk 0x12E6F40. Windows 0x274564.
        {3370191745, 0xCA633C},  // IComponent_Toggle. void(IComponent*, bool). Ten instructions, tail call through a virtual slot. Windows 0x45A944.
        {2635337807, 0x8D5D830}, // InkSystem_Instance. Data: the global that holds the InkSystem pointer. Windows .data 0x3495C70.
        {3961524856, 0xDC8D0C},  // SoundBankManager_ReadSoundBanksJson. bool(manager). The body, not the wrapper 0xDC8CF8. Windows 0x7D4844.
        {1576079097, 0x9D479C},  // JobHandle_Wait. bool(JobHandle*), plain blocking wait. Windows 0x2AC294.
        // R19 B2. Object serialization. All three return through x8 on Mac (the Windows signatures take the out object first).
        {1944983789, 0x2161438}, // ObjectSerializer_Prepare. void(ObjectSerializer*, Params*). Hooked at the entry for the magic size limit. Windows 0xD0B690.
        {2646156567, 0x11D6FA4}, // ObjectSerializer_WriteToBuffer. DataBuffer(const Handle*, uint32 flags, uint32 sizeLimit), result through x8. Windows 0x2A672B8.
        {125247621,  0x11D6E58}, // ObjectSerializer_ReadFromStrean. Handle<ISerializable>(BaseStream*, CClass*), result through x8. Windows 0x4946F4.
        {2601784524, 0x2144744}, // ISerializable_Clone. Handle<ISerializable>(const Handle*), result through x8. Windows 0x74ED60.
        // R19 B3. Resource paths, the resource bank and localization.
        {3998356057, 0x21C90A4}, // ResourcePath_Create. uint64_t(const char* text, uint32 size): the hash comes back in x0. The same entry the NCC ArchiveXL hooked with the Windows shape and crashed. Windows 0x1DF644.
        {2288918343, 0x21B4AB8}, // ResourceBank_ForgetResource. Handle<CResource>(bank value, path), result through x8. Windows 0x261A4C.
        {3550098299, 0x2F67DB4}, // Localization_LoadOnScreens. Handle<PersistenceOnScreenEntries>(path), result through x8. Not the sibling 0x2F67BE0 (subtitle map). Windows 0x261018.
        // R15. Script validator and opcode table. Static analysis in R14, hooks in R15.
        {898639042, 0x21FBF90},  // ScriptValidator_Validate. bool(validator, ScriptBundle*, report).
        {560604283, 0x21FCFB8},  // ScriptValidator_ValidateProperty. bool(unused, ScriptProperty*, CProperty*).
        {4224523662, 0x21FD144}, // ScriptValidator_CompareType. bool(CBaseRTTIType* native, ScriptType* script). Plain call.
        {961751128, 0x908B798},  // ScriptOpCodes_Handlers. Data: 8-byte handler table, zero in the file, filled at run time.
        {2070546748, 0x2252EF4}, // ScriptOpCodes_Register. Fills the table (one call to 0x21FD4B8 per opcode). Not hooked.
        // R20 B4. Ink UI. Each row was matched against the Windows twin by a read-only agent (strings, callers on both sides, field
        // offsets) and rated by tools/addr-match/check-rows.py. Mac signatures are in src/Red/InkLibrary.hpp and InkCore.hpp.
        {1158555307, 0x4965DE0}, // InkWidgetLibrary_SpawnFromLocal. Handle<WidgetLibraryItemInstance>(lib&, CName), the Handle through x8. Windows 0x57F9C0.
        {506278179, 0x4965EC0},  // InkWidgetLibrary_SpawnFromExternal. Handle(lib&, ResourcePath, CName), the Handle through x8. Windows 0x9FB618.
        {118698863, 0x4965980},  // InkWidgetLibrary_AsyncSpawnFromLocal. bool(lib&, InkSpawningInfo&, CName, bool flag). Windows 0x57F26C.
        {1396063719, 0x4965B38}, // InkWidgetLibrary_AsyncSpawnFromExternal. bool(lib&, InkSpawningInfo&, ResourcePath, CName, bool flag). Windows 0x9FB2A8.
        {2698985195, 0x49799B8}, // InkSpawner_FinishAsyncSpawn. void(InkSpawningContext*, Handle<WidgetLibraryItemInstance>&): the call operator of the std::function behind InkSpawningInfo. Windows 0xFA63F0.
        {2671382832, 0x47CF584}, // InkLogicController_OnInitialize. void(controller): the base implementation shared by 82 vtables (slot 0x110). Windows 0x581514.
        {1159862457, 0x47CF720}, // InkLogicController_OnUninitialize. void(controller): base implementation (slot 0x118). Windows 0x4A2B40.
        {3589084925, 0x47CF848}, // InkLogicController_OnArrangeChildrenComplete. void(controller): base implementation (slot 0x120). Windows 0x32B78C.
        {1667176558, 0x49ADE28}, // InkWidget_GetLayer. Handle<inkLayerProxy>(inkWidget*): the Handle through x8, no out argument. Windows 0x2EE72C.
        {2516523266, 0x49B18F0}, // InkWidget_SetLayer. void(inkWidget*, const Handle<inkLayerProxy>*). Windows 0x4FCD4C.
        {492509908, 0x49B888C},  // InkWidget_ScriptReparent. Native script function body void(this, CStackFrame*, ret, retType), hooked HookAfter. Windows 0xF0F724.
        {3558023194, 0x49A3084}, // InkWidget_ScriptAddChild (inkCompoundWidget.AddChild). Native script function body, hooked HookAfter. Windows 0x2218FE4.
        {3357288308, 0x49BE828}, // InkWidget_TriggerEvent. bool(manager = widget+0x50, CName, Handle<Event> passed indirectly); the caller owns the copy. Windows 0x89A86C.
        {4792499, 0x48DC344},    // InkLayer_RegisterListener. void(inkLayer*, const Handle<IScriptable>*). Not the sibling 0x48DC420. Windows 0x881848.
        {2050958960, 0x48DB648}, // InkLayer_AttachWidgetTree. void(inkLayer*, const Handle<inkWidget>*, const Handle<inkLayerProxy>*): walker and visitor inlined. Not the clone 0x48D6834. Windows 0x581248.
        {4284882427, 0x4887524}, // InkSystem_ProcessCharacterEvent. bool(InkSystem*, EInputKey, EInputAction). Windows 0xC225C0.
        // R21 B5. World, quest and the update registrar. Each row was matched against the Windows twin by a read-only agent and rated by
        // tools/addr-match/check-rows.py.
        // MAC-NOT-YET: CNamePool_AddCString (4292220681) is not applicable on Mac. Its Windows table address is an unrelated vtable slot and the SDK overload Add(const CString&) resolves to CNamePool_AddCstr (0x3452DDC) with c_str(), which gives the same hash; no row and no hook.
        // MAC-NOT-YET: QuestsSystem_CreateContext (3144298192) has no standalone Mac function (clang inlined the wrapper into the 18 callers); Raw::QuestsSystem::CreateContext calls the QuestContext constructor (Mac-only id 0xC0DE0011) with the quest system fields.
        {2840271332, 0xD13C5C},  // RuntimeEntityRegistry_RegisterEntity. bool(registry, Entity*): true when newly inserted. Windows 0x8B57F4.
        {3878623943, 0xD14084},  // RuntimeEntityRegistry_UnregisterEntity. bool(registry, Entity*). Windows 0x8B5310.
        {1576214475, 0xD25E50},  // RuntimeSystemEntity_AttachEntity. void(system, const AttachRequest*): a two instruction thunk (ldr x0,[x0,#0x48]; b 0xCF6EBC), called not hooked. Windows 0x8B6928.
        {1737563910, 0xD25E58},  // RuntimeSystemEntity_DetachEntity. void(system, const Handle<Entity>*): a two instruction thunk. Windows 0xB839B8.
        {1239092911, 0x20CDE7C}, // TagSystem_AssignTag. void(red::TagSystem* = GameTagSystem + 0x48, EntityID, CName). Windows 0x42559C.
        {1875775378, 0x20CE358}, // TagSystem_UnassignTag. void(red::TagSystem*, EntityID, CName). Windows 0xFF1AD8.
        {1559565515, 0x14AD374}, // CommunitySystem_Update. void(CommunitySystem*, bool). Windows 0x3F226C.
        {1471948702, 0xD5AEE0},  // RuntimeSystemWeather_GetEnvironmentDefinition. Handle<worldEnvironmentDefinition>(system): the Handle through x8. Windows 0x1DB070.
        {1821516328, 0xD597A8},  // RuntimeSystemWeather_SetWeatherByIndex. (system, uint32 index, CName source, uint32 a5, uint32 priority, float blend in s0). Windows 0xA0592C.
        {2334794340, 0xD5C384},  // RuntimeSystemWeather_SetWeatherByName. bool(system, CName weather, CName source, uint32 a5, uint32 priority, float blend in s0). Windows 0x13CC1AC.
        {2679119876, 0xD5AA60},  // RuntimeSystemWeather_GetWeatherState. Handle<worldWeatherState>(system): the Handle through x8. Windows 0xA06808.
        {803085170, 0x42EAF14},  // PointOfInterestMappin_SetPhase. bool(mappin, phase): vtable slot 0x1D0 of PointOfInterestMappin on Mac (0x1C8 on Windows). Windows 0x27A6F20.
        {3227858325, 0x2CFD644}, // QuestPhaseInstance_ExecuteNode. uint8(phase, node*, QuestContext&, const Socket&, DynArray<Socket>&). Windows 0x58E5A8.
        {3423879019, 0x2CFBA64}, // QuestPhaseInstance_Initialize. (phase, QuestContext&, const Handle<resource>&, Handle<graph>&, const NodePath&, w5 = node id): the args that were on the Windows stack are in registers. Reached by a tail branch from the phase factory. Windows 0x257724.
        {779107884, 0x2CF5274},  // QuestPhaseInstance_ExecuteGraph. uint8(phase, QuestContext&, const WeakHandle<node>&, const Socket&, bool, DynArray<Socket>&). Windows 0x9481D8.
        {0xC0DE0011, 0x2CF313C}, // QuestContext_ctor (Mac-only id). QuestsSystem_CreateContext is inlined in the 18 Mac callers; this is the constructor it forwards to: ctx = ctor(ctx, a3, a4, a5, a6, *(qs+0xE8), qs+0xA8, qs+0xB8, qs+0x78, qs+0x1D0, *(qs+0x2C8), *(qs+0x40)). Windows 0x258F70.
        // R21 B5 entity rows (read-only agents, check-rows).
        {633871992, 0xC6C0C4},   // MorphTargetManager_ApplyMorphTarget. void(manager, CName target, CName region, float s0, bool w3): w3 is a live flag on Mac (the Windows function ignores its fifth argument; all nine game callers pass 0). Windows 0x1607FC.
        {1828854026, 0xC417F0},  // IPlacedComponent_SetTransform. void(component, const WorldTransform*): a frameless leaf that tail-branches to 0xC91DD0. Windows 0x574FC8.
        {2912168594, 0xC8B630},  // VisualController_LoadDependencies. JobHandle(VisualController*, w1 = a3): the JobHandle through x8. Windows 0x36B8E8.
        {3007126651, 0xE189F4},  // CMesh_FindAppearance. Handle<MeshAppearance>*(CMesh*, CName): the pointer comes back in x0, not x8. Windows 0x3CA158.
        {773462733, 0xE199D8},   // CMesh_GetAppearance. Windows 0x3CA128. (0xE19A1C is the shim that calls FindAppearance with the default name.)
        {3442350580, 0x1F8DFFC}, // PersistentObject_InitializeState. void(object, params): hooked HookBefore at the entry, called for the entity's own PersistentObject and once per component. Windows 0x84BA8C.
        // R21 B5/B6 player, render and physics rows.
        {1159928678, 0x124FE1C}, // VehicleGarageComponent_LoadSummonSetup. void(garage). The Windows table address (0x126DF78) is an RTTI type getter; the real function is Windows 0x126BF78 (it reads the 27 Vehicle.summon_setup flats).
        {3027836941, 0x1369A5C}, // VehicleSystem_ToggleGarageVehicle. bool(system, const GarageVehicleID*, bool): virtual (Windows slot 0x298, Mac 0x2A0), called directly. Windows 0x14FCFC8.
        {2996308699, 0x16A38D4}, // RenderData_Release. void(object): the generic intrusive release (refcount at +8, slot 8 on zero), a 9 instruction leaf: called, never hooked. Windows 0x1E6EB0.
        {2394822845, 0x4092830}, // PhysicsTraceResult_GetHitObject. Handle<ISerializable>(w0 = resultID, w1 = index): the strong Handle through x8. Not the weak variant 0x409274C. Windows 0x23F29C.
        {3068279668, 0xCDD130},  // EntitySpawner_SpawnEntity. SharedPtr<EntitySpawnerToken>(spawner, const EntitySpawnerRequest&): the SharedPtr through x8, no result argument. Called, not hooked. Windows 0x8B24B0.
        {552341017, 0xCE0D28},   // EntitySpawnerToken_dtor. void(token): releases the entity Handle (+0x10) and the SharedPtr (+0), frees nothing; 0xCE0DD0 is a one instruction thunk to it, not to be hooked. Windows 0xCD921C.
        {2091718968, 0xCE0EB0},  // EntitySpawnerToken_GetWaitHandle. JobHandle*(token). Not called by Codeware. Windows 0x8B2F6C.
        {1419388740, 0xE1F7B4},  // MeshAppearance_LoadMaterialSetupAsync. Handle<MeshAppearance>(appearance, w1 = a3): the Handle through x8, no out argument. Windows 0x21A670.
        {1679234269, 0xE1EE08},  // MeshAppearance_ReleaseReference. void(appearance): the same lock, release and map clear as MeshAppearanceEx::ResetMaterialCache. Windows 0x21AA4C.
        {1790971865, 0x16A78E4}, // RenderProxy_SetVisibility. void(proxy, bool): a 13 instruction leaf that tail-branches to UpdateVisibility, called not hooked. Windows 0x27FEBC.
        // R16a. SDK hashes the vendored SDK table lacks. Shared with the SDK header overlay (see CMakeLists.txt).
#include "MacSdkRows.inc"
    };
    return rows;
}
}

uintptr_t Support::MacAddressResolver::ResolveAddress(uint32_t aAddressID)
{
    // R18. Hashes with a plugin-owned stand-in (the engine pointer slot, the EngineTime frequency).
    if (const auto standIn = Mac::StandInFor(aAddressID))
    {
        return standIn;
    }

    const auto& rows = MacRows();
    const auto it = rows.find(aAddressID);
    if (it != rows.end())
    {
        return Red::MacGameBase() + it->second;
    }

    // The SDK resolver holds the shared Mac rows (CRTTISystem_Get, TweakDB, CClass, function constructors). In this
    // build it also falls back to the trap table (MacTrap.cpp) for a hash that has no row anywhere: the returned
    // address is a stub that logs "[cw] not on Mac yet: <name>" on its first call and returns zero. Only an id that
    // is not in the table at all (a Mac-only id without a row) comes back as 0.
    return RED4ext::UniversalRelocBase::Resolve(aAddressID);
}
