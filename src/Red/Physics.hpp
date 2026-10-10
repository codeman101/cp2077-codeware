#pragma once

namespace Raw::PhysicsTraceResult
{
using ResultID = Core::OffsetPtr<0x48, uint32_t>;

#if defined(__APPLE__)
// Mac ABI (R21): the strong Handle comes back through x8 (resultID in w0, index in w1). Not the weak variant 0x409274C.
constexpr auto GetHitObjectNative = Core::RawFunc<
    /* addr = */ Red::AddressLib::PhysicsTraceResult_GetHitObject,
    /* type = */ Red::Handle<Red::ISerializable> (*)(uint32_t aResultID, uint32_t aIndex)>();

struct GetHitObjectFn
{
    void* operator()(Red::Handle<Red::ISerializable>& aOut, uint32_t aResultID, uint32_t aIndex) const
    {
        aOut = GetHitObjectNative(aResultID, aIndex);
        return aOut.instance;
    }
};
inline constexpr GetHitObjectFn GetHitObject{};
#else
constexpr auto GetHitObject = Core::RawFunc<
    /* addr = */ Red::AddressLib::PhysicsTraceResult_GetHitObject,
    /* type = */ void* (*)(Red::Handle<Red::ISerializable>& aOut, uint32_t aResultID, uint32_t aIndex)>();
#endif
}
