#pragma once

#include <RED4ext/Scripting/Natives/Generated/ent/EntityTemplate.hpp>

namespace Red
{
struct EntitySpawner;

struct EntitySpawnerToken
{
    using AllocatorType = Memory::EngineAllocator;

    inline ~EntitySpawnerToken()
    {
        constexpr auto DestructToken = Core::RawFunc<
            /* addr = */ Red::AddressLib::EntitySpawnerToken_dtor,
            /* type = */ void (*)(Red::EntitySpawnerToken*)>();

        DestructToken(this);
#if defined(__APPLE__)
        // R21: the game's destructor (Mac 0xCE0D28; Windows 0xCD921C does the same) releases the entity Handle at +0x10 without clearing
        // it, and the member destructor below would release it a second time. Clear the Handle words so that second release is a no-op.
        // The game destroys nothing else this struct owns.
        reinterpret_cast<uint64_t*>(&entity)[0] = 0;
        reinterpret_cast<uint64_t*>(&entity)[1] = 0;
#endif
    }

    uint64_t unk00;        // 00
    uint64_t unk10;        // 08
    Handle<Entity> entity; // 10
    bool detached;         // 20
    bool failed;           // 21
    bool aborted;          // 22
};
RED4EXT_ASSERT_SIZE(EntitySpawnerToken, 0x28);
RED4EXT_ASSERT_OFFSET(EntitySpawnerToken, entity, 0x10);
RED4EXT_ASSERT_OFFSET(EntitySpawnerToken, failed, 0x21);

using EntitySpawnerCallback = FlexCallback<void (*)(const SharedPtr<EntitySpawnerToken>&)>;

struct EntitySpawnerRequest
{
    ResourcePath templatePath;      // 00
    WorldTransform transform;       // 10
    EntityID entityID;              // 30
    CName appearanceName;           // 38
    uint64_t unk40;                 // 40
    EntitySpawnerCallback callback; // 48
    bool detached;                  // 78
    bool dissolve;                  // 79
    uint8_t unk7A[0xE1 - 0x7A];     // 7A
    uint8_t priority{1};            // E1
    uint8_t unkE2;                  // E2
};
RED4EXT_ASSERT_SIZE(EntitySpawnerRequest, 0xF0);
RED4EXT_ASSERT_OFFSET(EntitySpawnerRequest, transform, 0x10);
RED4EXT_ASSERT_OFFSET(EntitySpawnerRequest, entityID, 0x30);
RED4EXT_ASSERT_OFFSET(EntitySpawnerRequest, callback, 0x48);
RED4EXT_ASSERT_OFFSET(EntitySpawnerRequest, detached, 0x78);
RED4EXT_ASSERT_OFFSET(EntitySpawnerRequest, priority, 0xE1);

struct EntitySpawner
{
    // The SharedPtr comes back by value: through x8 on the Mac, through the result address after "this" in the Windows build.
    SharedPtr<EntitySpawnerToken> SpawnEntity(const EntitySpawnerRequest& aRequest)
    {
        constexpr auto SpawnEntity = Core::RawFunc<
            /* addr = */ Red::AddressLib::EntitySpawner_SpawnEntity,
            /* type = */ Red::SharedPtr<Red::EntitySpawnerToken> (Red::EntitySpawner::*)(
                const Red::EntitySpawnerRequest& aRequest)>();

        return SpawnEntity(this, aRequest);
    }
};
}
