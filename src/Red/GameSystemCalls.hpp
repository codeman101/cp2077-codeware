#pragma once

// Calls into the game's dynamic entity ID system and population system with one shape on both platforms. The SDK declares these
// virtuals the Windows way (a hidden out argument). On Mac (R21, vtable slots verified against the game) the results come back
// differently, so the SDK has Apple declarations (RED4ext.SDK-macos, branch mac-full) and these helpers keep Codeware's call
// sites the same:
//   IDynamicEntityIDSystem::GetNextTransientID / GetNextPersistableID   Mac slots 0x1B8 / 0x1C0: the EntityID comes back in x0.
//   IPopulationSystem::FindEntity                                       Mac slot 0x1B8: the Handle<Entity> comes back through x8.

namespace Red
{
inline EntityID NextTransientEntityID(IDynamicEntityIDSystem* aSystem)
{
#if defined(__APPLE__)
    return aSystem->GetNextTransientID();
#else
    EntityID entityID{};
    aSystem->GetNextTransientID(entityID);
    return entityID;
#endif
}

inline EntityID NextPersistableEntityID(IDynamicEntityIDSystem* aSystem)
{
#if defined(__APPLE__)
    return aSystem->GetNextPersistableID();
#else
    EntityID entityID{};
    aSystem->GetNextPersistableID(entityID);
    return entityID;
#endif
}

inline Handle<Entity> FindPopulationEntity(IPopulationSystem* aSystem, uint32_t aDynamicEntityID)
{
#if defined(__APPLE__)
    return aSystem->FindEntity(aDynamicEntityID);
#else
    Handle<Entity> entity;
    aSystem->FindEntity(entity, aDynamicEntityID);
    return entity;
#endif
}
}
