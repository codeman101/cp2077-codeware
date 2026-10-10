#pragma once

namespace Red
{
struct MappinData
{
    uint64_t id;
    Handle<IMappin> instance;
};
}

namespace Raw::MappinSystem
{
using MappinsLock = Core::OffsetPtr<0x198, Red::SharedSpinLock>;
using MappinsData = Core::OffsetPtr<0x1A0, Red::SortedArray<Red::MappinData>>;

// Mac (R21, checked against the game): SetPoiMappinPhase is Windows + 8 (Mac 0x1D8). GetPoiMappinPhase is Mac 0x438: the 0x3C8 below
// is a flag-setter thunk in the 2.3.1 executable (Windows 0x3C0 and 0x3C8), the function that reads the phase is Windows 0x430, found
// by body and by the call site that compares the result with 5. The Windows value is left as upstream has it (listed under "Upstream (Windows) findings" in macos/NOT_YET.md).
constexpr auto SetPoiMappinPhase = Core::RawVFunc<
    /* addr = */ 0x1D0,
    /* type = */ void (Red::gamemappinsIMappinSystem::*)(const uint32_t aJournalPath,
                                                         const Red::gamedataMappinPhase aMappinPhase),
    /* verified = */ true>();

constexpr auto GetPoiMappinPhase = Core::RawVFunc<
#if defined(__APPLE__)
    /* addr = */ 0x430,
#else
    /* addr = */ 0x3C8,
#endif
    /* type = */ Red::gamedataMappinPhase (Red::gamemappinsIMappinSystem::*)(const uint32_t aJournalPath),
    /* verified = */ true>();
}

namespace Raw::PointOfInterestMappin
{
#if defined(__APPLE__)
// R21: 0x88 on Mac (the Mac SetPhase reads it with ldr w23,[x19,#0x88] and the constructor zero-stores it).
using JournalHash = Core::OffsetPtr<0x88, uint32_t>;
#else
using JournalHash = Core::OffsetPtr<0x8C, uint32_t>;
#endif

constexpr auto SetPhase = Core::RawFunc<
    /* addr = */ Red::AddressLib::PointOfInterestMappin_SetPhase,
    /* type = */ bool (*)(void* aMappin, Red::gamedataMappinPhase aMappinPhase)>();
}
