#pragma once

// The Windows RED4ext SDK has the v1 plugin API (RED4ext::v1::PluginHandle, RED4ext::v1::Sdk). The macOS SDK port has
// the same types without the v1 namespace. These aliases let the shared sources keep the v1 names.

#if defined(__APPLE__)

#include <RED4ext/Api/Sdk.hpp>

namespace RED4ext::v1
{
using PluginHandle = ::RED4ext::PluginHandle;
using Sdk = ::RED4ext::Sdk;
}

#endif
