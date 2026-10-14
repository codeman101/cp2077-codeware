#pragma once

// Stand-in for lib/Core/Stl.hpp (which needs TiltedCore and tsl): only what Core/Hooking/Detail.hpp uses.

#include <memory>
#include <utility>

namespace Core
{
template<class T>
using UniquePtr = std::unique_ptr<T>;

template<typename T, typename... Args>
auto MakeUnique(Args&&... aArgs)
{
    return UniquePtr<T>(new T(std::forward<Args>(aArgs)...));
}
}
