// Runtime ABI test for member-pointer RawFuncs and their hooks (lib/Core/Raw.hpp, lib/Core/Hooking/Detail.hpp).
// CI only, not part of the pull request.
//
// A class Game has real noinline member functions that return a non-trivial Handle-like class (16 bytes, user destructor), an 8
// byte trivial class, a 24 byte class, an int, and void. Every function is
//   - called through a member RawFunc (the target id is resolved by a fake resolver, then SetAddress points it at the code),
//   - hooked through HookHandler (Original, Before, After and Wrap flows, plus the other callback forms) with a fake
//     HookingDriver that records the detour and hands back the real function as "the original",
// and each detour is then called the way the game would call it: as a member function of Game.
//
// Build (arm64 or x64, any compiler that has C++20):
//   clang++ -std=c++20 -O2 -I standins -I <checkout>/lib member_call_test.cpp -o member_call_test
//   cl /std:c++20 /O2 /EHsc /I standins /I <checkout>\lib member_call_test.cpp
// With -DSIMULATE_MSVC_SHAPE (non-MSVC compilers only) Raw.hpp is compiled as if for MSVC x64, so the hidden result address
// shape is used, and the "game functions" for the class results are adapters with that shape around the real members. That
// exercises the hidden-result code paths on a machine without MSVC; the genuine proof is the MSVC build.

#include <concepts>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define NOINLINE __declspec(noinline)
#else
// The only Windows API that Raw.hpp uses.
inline void* GetModuleHandleW(const wchar_t*)
{
    return nullptr;
}
#define NOINLINE __attribute__((noinline))
#endif

#if defined(SIMULATE_MSVC_SHAPE)
#if defined(_MSC_VER) || defined(_M_X64)
#error SIMULATE_MSVC_SHAPE is for compilers other than MSVC
#endif
#define _MSC_VER 1938
#define _M_X64 100
#endif

#include "Core/Raw.hpp"

#if defined(SIMULATE_MSVC_SHAPE)
#undef _MSC_VER
#undef _M_X64
#endif

#include "Core/Hooking/Detail.hpp"
#include "Core/Hooking/HookingAgent.hpp"

// Stand-ins for the two services that Raw.hpp and the hooking headers declare and the full build defines elsewhere.

namespace
{
constexpr uint32_t MaxIds = 16;
uintptr_t g_resolved[MaxIds] = {};
Core::AddressResolver* g_resolver = nullptr;
Core::HookingDriver* g_driver = nullptr;
}

void Core::AddressResolver::SetDefault(AddressResolver& aResolver)
{
    g_resolver = &aResolver;
}

Core::AddressResolver& Core::AddressResolver::GetDefault()
{
    return *g_resolver;
}

void Core::HookingDriver::SetDefault(HookingDriver& aDriver)
{
    g_driver = &aDriver;
}

Core::HookingDriver& Core::HookingDriver::GetDefault()
{
    return *g_driver;
}

namespace
{
// Where the shape is hidden for real (MSVC x64) or simulated.
#if defined(SIMULATE_MSVC_SHAPE)
constexpr bool kSimulated = true;
constexpr bool kRealHidden = false;
#elif defined(_MSC_VER) && defined(_M_X64)
constexpr bool kSimulated = false;
constexpr bool kRealHidden = true;
#else
constexpr bool kSimulated = false;
constexpr bool kRealHidden = false;
#endif

static_assert(Core::Detail::MemberResultIsHidden == (kSimulated || kRealHidden));

// Results

int g_live = 0; // live Handle16 objects: constructions minus destructions

struct Name
{
    uint64_t hash; // like CName: an 8 byte trivial class
};

struct Handle16
{
    uint64_t id;
    uint64_t tag;

    explicit Handle16(uint64_t aId = 0, uint64_t aTag = 0)
        : id(aId)
        , tag(aTag)
    {
        ++g_live;
    }

    Handle16(const Handle16& aOther)
        : id(aOther.id)
        , tag(aOther.tag)
    {
        ++g_live;
    }

    Handle16(Handle16&& aOther) noexcept
        : id(aOther.id)
        , tag(aOther.tag)
    {
        aOther.id = 0;
        aOther.tag = 0;
        ++g_live;
    }

    Handle16& operator=(const Handle16&) = default;

    ~Handle16()
    {
        --g_live;
    }
};

struct Small8
{
    uint32_t a;
    uint32_t b;
};

struct Big24
{
    uint64_t a;
    uint64_t b;
    uint64_t c;
};

static_assert(sizeof(Handle16) == 16 && sizeof(Small8) == 8 && sizeof(Big24) == 24);

class Game
{
public:
    uint64_t base = 1000;
    uint64_t calls = 0;
    int field = 0;

    NOINLINE Handle16 Spawn(Name aName);
    NOINLINE Handle16 SpawnExt(Name aPath, Name aName);
    NOINLINE Small8 Small(int aValue);
    NOINLINE Big24 Big(int aValue, double aScale);
    NOINLINE int Sum(int aA, int aB);
    NOINLINE void Set(int aValue);
};

Handle16 Game::Spawn(Name aName)
{
    ++calls;
    return Handle16(base + aName.hash, aName.hash * 2);
}

Handle16 Game::SpawnExt(Name aPath, Name aName)
{
    ++calls;
    return Handle16(base + aPath.hash, aName.hash + 5);
}

Small8 Game::Small(int aValue)
{
    ++calls;
    return Small8{static_cast<uint32_t>(base) + static_cast<uint32_t>(aValue), static_cast<uint32_t>(aValue) * 2};
}

Big24 Game::Big(int aValue, double aScale)
{
    ++calls;
    return Big24{base, static_cast<uint64_t>(aValue), static_cast<uint64_t>(aScale * 10.0)};
}

int Game::Sum(int aA, int aB)
{
    ++calls;
    return aA + aB + static_cast<int>(base);
}

void Game::Set(int aValue)
{
    ++calls;
    field = aValue * 3;
}

// Test glue

struct FakeResolver final : Core::AddressResolver
{
    uintptr_t ResolveAddress(uint32_t aId) override
    {
        // The default is a nonzero stand-in so that SetAddress sticks; the tests put the real code address in for the cases
        // that need it.
        return aId < MaxIds && g_resolved[aId] ? g_resolved[aId] : 0x1000 + aId;
    }
};

struct FakeDriver final : Core::HookingDriver
{
    uintptr_t real = 0;
    uintptr_t target = 0;
    void* detour = nullptr;
    int attaches = 0;
    int detaches = 0;

    bool HookAttach(uintptr_t, void*) override
    {
        return false;
    }

    bool HookAttach(uintptr_t aAddress, void* aCallback, void** aOriginal) override
    {
        target = aAddress;
        detour = aCallback;
        *aOriginal = reinterpret_cast<void*>(real);
        ++attaches;
        return true;
    }

    bool HookDetach(uintptr_t) override
    {
        detour = nullptr;
        ++detaches;
        return true;
    }
};

FakeResolver s_resolver;
FakeDriver s_driver;

int g_failures = 0;
int g_cases = 0;
bool g_caseOk = true;
char g_caseName[160];

void Begin(const char* aGroup, const char* aCase)
{
    std::snprintf(g_caseName, sizeof(g_caseName), "%-12s %s", aGroup, aCase);
    g_caseOk = true;
    g_live = 0;
}

void Check(bool aOk, const char* aWhat)
{
    if (!aOk)
    {
        if (g_caseOk)
        {
            std::printf("FAIL  %s\n", g_caseName);
        }

        std::printf("        failed check: %s\n", aWhat);
        g_caseOk = false;
    }
}

void End()
{
    Check(g_live == 0, "every Handle16 constructed was destroyed exactly once");

    ++g_cases;

    if (g_caseOk)
    {
        std::printf("PASS  %s\n", g_caseName);
    }
    else
    {
        ++g_failures;
    }
}

// A case runs from its construction to the end of the scope; declare it first so that everything the case made is gone when it
// ends and the live count can be checked.
struct Case
{
    Case(const char* aGroup, const char* aCase)
    {
        Begin(aGroup, aCase);
    }

    ~Case()
    {
        End();
    }
};

// Events recorded by the callbacks.
int g_events = 0;
Game* g_seenSelf = nullptr;
uint64_t g_seenSum = 0;

void ResetEvents()
{
    g_events = 0;
    g_seenSelf = nullptr;
    g_seenSum = 0;
}

template<typename T>
uintptr_t CodeAddress(T aMember)
{
    static_assert(sizeof(T) >= sizeof(uintptr_t));

    uintptr_t address = 0;
    std::memcpy(&address, &aMember, sizeof(address));
    return address;
}

#if defined(SIMULATE_MSVC_SHAPE)
// Simulation of the MSVC shape for the class results, around the real members.
template<typename TMember, TMember VMember>
struct HiddenAdapter;

template<typename R, typename... TArgs, R (Game::*VMember)(TArgs...)>
struct HiddenAdapter<R (Game::*)(TArgs...), VMember>
{
    static R* Call(Game* aSelf, R* aResult, TArgs... aArgs)
    {
        return ::new (static_cast<void*>(aResult)) R((aSelf->*VMember)(std::forward<TArgs>(aArgs)...));
    }
};

template<typename TMember>
struct MemberReturn;

template<typename R, typename... TArgs>
struct MemberReturn<R (Game::*)(TArgs...)>
{
    using Type = R;
};

template<auto VMember>
uintptr_t GameCode()
{
    using Member = decltype(VMember);

    if constexpr (std::is_class_v<typename MemberReturn<Member>::Type>)
    {
        return reinterpret_cast<uintptr_t>(&HiddenAdapter<Member, VMember>::Call);
    }
    else
    {
        return CodeAddress(VMember);
    }
}
#else
// The address of the real member function.
template<auto VMember>
uintptr_t GameCode()
{
    return CodeAddress(VMember);
}
#endif

// Calls a detour the way the game calls a member function of Game.
template<typename R, typename... TArgs>
R CallAsMember(void* aDetour, Game* aSelf, TArgs... aArgs)
{
    if constexpr (kSimulated && std::is_class_v<R>)
    {
        auto func = reinterpret_cast<R* (*)(Game*, R*, TArgs...)>(aDetour);

        alignas(R) unsigned char storage[sizeof(R)];
        R* result = func(aSelf, reinterpret_cast<R*>(storage), std::forward<TArgs>(aArgs)...);
        R value(std::move(*result));
        result->~R();
        return value;
    }
    else
    {
        R (Game::*member)(TArgs...) = nullptr;
        std::memcpy(&member, &aDetour, sizeof(aDetour));
        return (aSelf->*member)(std::forward<TArgs>(aArgs)...);
    }
}

// The functions under test

template<typename TDerived, typename R, typename... A>
struct Callbacks
{
    static R Replace(Game* aSelf, A... aArgs)
    {
        ++g_events;
        g_seenSelf = aSelf;
        g_seenSum = TDerived::ArgSum(aArgs...);

        R result = TDerived::obj(aSelf, std::forward<A>(aArgs)...);
        TDerived::Tweak(result);
        return result;
    }

    static void Before(Game* aSelf, A... aArgs)
    {
        ++g_events;
        g_seenSelf = aSelf;
        g_seenSum = TDerived::ArgSum(aArgs...);
    }

    static void After(R& aResult, Game* aSelf, A... aArgs)
    {
        ++g_events;
        g_seenSelf = aSelf;
        g_seenSum = TDerived::ArgSum(aArgs...);
        TDerived::Tweak(aResult);
    }

    static R Wrap(R (*aOriginal)(Game*, A...), Game* aSelf, A... aArgs)
    {
        ++g_events;
        g_seenSelf = aSelf;
        g_seenSum = TDerived::ArgSum(aArgs...);

        R result = aOriginal(aSelf, std::forward<A>(aArgs)...);
        TDerived::Tweak(result);
        return result;
    }

    static void ResultObserver(R& aResult)
    {
        ++g_events;
        TDerived::Tweak(aResult);
    }

    static R ResultWrapper(R& aResult)
    {
        ++g_events;

        R copy = aResult;
        TDerived::Tweak(copy);
        return copy;
    }

    static void Blind()
    {
        ++g_events;
    }
};

struct SpawnFn : Callbacks<SpawnFn, Handle16, Name>
{
    static constexpr const char* name = "Spawn";
    static constexpr bool isVoid = false;
    static constexpr auto obj = Core::RawFunc<1, Handle16 (Game::*)(Name)>();
    using Raw = std::remove_cv_t<decltype(obj)>;
    using Out = Handle16;

    static constexpr Name arg{7};

    static uintptr_t Code() { return GameCode<&Game::Spawn>(); }
    static Out Direct(Game* g) { return g->Spawn(arg); }
    static Out ViaObj(Game* g) { return obj(g, arg); }
    static Out ViaInvoke(Game* g) { return Raw::Invoke(g, arg); }
    static Out ViaInvokeOriginal(Game* g) { return Raw::InvokeOriginal(g, arg); }
    static Out ViaDetour(void* d, Game* g) { return CallAsMember<Handle16, Name>(d, g, arg); }
    static uint64_t ArgSum(Name n) { return n.hash; }
    static uint64_t ArgSum() { return ArgSum(arg); }
    static void Tweak(Out& o) { o.tag += 100; }
    static Out Expected(bool aTweaked) { Out o(1000 + 7, 14); if (aTweaked) Tweak(o); return o; }
    static bool Same(const Out& a, const Out& b) { return a.id == b.id && a.tag == b.tag; }
};

struct SpawnExtFn : Callbacks<SpawnExtFn, Handle16, Name, Name>
{
    static constexpr const char* name = "SpawnExt";
    static constexpr bool isVoid = false;
    static constexpr auto obj = Core::RawFunc<2, Handle16 (Game::*)(Name, Name)>();
    using Raw = std::remove_cv_t<decltype(obj)>;
    using Out = Handle16;

    static constexpr Name arg1{3};
    static constexpr Name arg2{9};

    static uintptr_t Code() { return GameCode<&Game::SpawnExt>(); }
    static Out Direct(Game* g) { return g->SpawnExt(arg1, arg2); }
    static Out ViaObj(Game* g) { return obj(g, arg1, arg2); }
    static Out ViaInvoke(Game* g) { return Raw::Invoke(g, arg1, arg2); }
    static Out ViaInvokeOriginal(Game* g) { return Raw::InvokeOriginal(g, arg1, arg2); }
    static Out ViaDetour(void* d, Game* g) { return CallAsMember<Handle16, Name, Name>(d, g, arg1, arg2); }
    static uint64_t ArgSum(Name a, Name b) { return a.hash * 100 + b.hash; }
    static uint64_t ArgSum() { return ArgSum(arg1, arg2); }
    static void Tweak(Out& o) { o.tag += 100; }
    static Out Expected(bool aTweaked) { Out o(1000 + 3, 9 + 5); if (aTweaked) Tweak(o); return o; }
    static bool Same(const Out& a, const Out& b) { return a.id == b.id && a.tag == b.tag; }
};

struct SmallFn : Callbacks<SmallFn, Small8, int>
{
    static constexpr const char* name = "Small";
    static constexpr bool isVoid = false;
    static constexpr auto obj = Core::RawFunc<3, Small8 (Game::*)(int)>();
    using Raw = std::remove_cv_t<decltype(obj)>;
    using Out = Small8;

    static constexpr int arg = 21;

    static uintptr_t Code() { return GameCode<&Game::Small>(); }
    static Out Direct(Game* g) { return g->Small(arg); }
    static Out ViaObj(Game* g) { return obj(g, arg); }
    static Out ViaInvoke(Game* g) { return Raw::Invoke(g, arg); }
    static Out ViaInvokeOriginal(Game* g) { return Raw::InvokeOriginal(g, arg); }
    static Out ViaDetour(void* d, Game* g) { return CallAsMember<Small8, int>(d, g, arg); }
    static uint64_t ArgSum(int v) { return static_cast<uint64_t>(v); }
    static uint64_t ArgSum() { return ArgSum(arg); }
    static void Tweak(Out& o) { o.b += 100; }
    static Out Expected(bool aTweaked) { Out o{1000 + 21, 42}; if (aTweaked) Tweak(o); return o; }
    static bool Same(const Out& a, const Out& b) { return a.a == b.a && a.b == b.b; }
};

struct BigFn : Callbacks<BigFn, Big24, int, double>
{
    static constexpr const char* name = "Big";
    static constexpr bool isVoid = false;
    static constexpr auto obj = Core::RawFunc<4, Big24 (Game::*)(int, double)>();
    using Raw = std::remove_cv_t<decltype(obj)>;
    using Out = Big24;

    static constexpr int arg1 = 5;
    static constexpr double arg2 = 2.5;

    static uintptr_t Code() { return GameCode<&Game::Big>(); }
    static Out Direct(Game* g) { return g->Big(arg1, arg2); }
    static Out ViaObj(Game* g) { return obj(g, arg1, arg2); }
    static Out ViaInvoke(Game* g) { return Raw::Invoke(g, arg1, arg2); }
    static Out ViaInvokeOriginal(Game* g) { return Raw::InvokeOriginal(g, arg1, arg2); }
    static Out ViaDetour(void* d, Game* g) { return CallAsMember<Big24, int, double>(d, g, arg1, arg2); }
    static uint64_t ArgSum(int a, double b) { return static_cast<uint64_t>(a) * 1000 + static_cast<uint64_t>(b * 10.0); }
    static uint64_t ArgSum() { return ArgSum(arg1, arg2); }
    static void Tweak(Out& o) { o.c += 100; }
    static Out Expected(bool aTweaked) { Out o{1000, 5, 25}; if (aTweaked) Tweak(o); return o; }
    static bool Same(const Out& a, const Out& b) { return a.a == b.a && a.b == b.b && a.c == b.c; }
};

struct SumFn : Callbacks<SumFn, int, int, int>
{
    static constexpr const char* name = "Sum";
    static constexpr bool isVoid = false;
    static constexpr auto obj = Core::RawFunc<5, int (Game::*)(int, int)>();
    using Raw = std::remove_cv_t<decltype(obj)>;
    using Out = int;

    static constexpr int arg1 = 30;
    static constexpr int arg2 = 12;

    static uintptr_t Code() { return GameCode<&Game::Sum>(); }
    static Out Direct(Game* g) { return g->Sum(arg1, arg2); }
    static Out ViaObj(Game* g) { return obj(g, arg1, arg2); }
    static Out ViaInvoke(Game* g) { return Raw::Invoke(g, arg1, arg2); }
    static Out ViaInvokeOriginal(Game* g) { return Raw::InvokeOriginal(g, arg1, arg2); }
    static Out ViaDetour(void* d, Game* g) { return CallAsMember<int, int, int>(d, g, arg1, arg2); }
    static uint64_t ArgSum(int a, int b) { return static_cast<uint64_t>(a) * 100 + static_cast<uint64_t>(b); }
    static uint64_t ArgSum() { return ArgSum(arg1, arg2); }
    static void Tweak(Out& o) { o += 100; }
    static Out Expected(bool aTweaked) { Out o = 1000 + 30 + 12; if (aTweaked) Tweak(o); return o; }
    static bool Same(const Out& a, const Out& b) { return a == b; }
};

// void result: the observable result is the field that Set writes.
struct SetFn
{
    static constexpr const char* name = "Set";
    static constexpr bool isVoid = true;
    static constexpr auto obj = Core::RawFunc<6, void (Game::*)(int)>();
    using Raw = std::remove_cv_t<decltype(obj)>;
    using Out = int;

    static constexpr int arg = 14;

    static uintptr_t Code() { return GameCode<&Game::Set>(); }
    static Out Direct(Game* g) { g->Set(arg); return g->field; }
    static Out ViaObj(Game* g) { obj(g, arg); return g->field; }
    static Out ViaInvoke(Game* g) { Raw::Invoke(g, arg); return g->field; }
    static Out ViaInvokeOriginal(Game* g) { Raw::InvokeOriginal(g, arg); return g->field; }
    static Out ViaDetour(void* d, Game* g) { CallAsMember<void, int>(d, g, arg); return g->field; }
    static uint64_t ArgSum(int v) { return static_cast<uint64_t>(v); }
    static uint64_t ArgSum() { return ArgSum(arg); }
    static Out Expected(bool aTweaked) { return arg * 3 + (aTweaked ? 100 : 0); }
    static bool Same(const Out& a, const Out& b) { return a == b; }

    static void Replace(Game* aSelf, int aValue)
    {
        ++g_events;
        g_seenSelf = aSelf;
        g_seenSum = ArgSum(aValue);

        obj(aSelf, aValue);
        aSelf->field += 100;
    }

    static void Before(Game* aSelf, int aValue)
    {
        ++g_events;
        g_seenSelf = aSelf;
        g_seenSum = ArgSum(aValue);
    }

    static void After(Game* aSelf, int aValue)
    {
        ++g_events;
        g_seenSelf = aSelf;
        g_seenSum = ArgSum(aValue);
        aSelf->field += 100;
    }

    static void Wrap(void (*aOriginal)(Game*, int), Game* aSelf, int aValue)
    {
        ++g_events;
        g_seenSelf = aSelf;
        g_seenSum = ArgSum(aValue);

        aOriginal(aSelf, aValue);
        aSelf->field += 100;
    }

    static void Blind()
    {
        ++g_events;
    }
};

// Cases

template<typename Fn>
void SetOriginalAddress()
{
    g_resolved[Fn::Raw::target] = Fn::Code();
    Fn::Raw::ResetAddress();
}

template<typename Fn>
void RunCalls()
{
    using Raw = typename Fn::Raw;

    // Target 1 (here: the id of the function) resolves to a stand-in, then SetAddress points the raw at the code.
    g_resolved[Raw::target] = 0;
    Raw::ResetAddress();

    {
        Case _case(Fn::name, "call direct (the game itself)");
        Game game;
        auto out = Fn::Direct(&game);
        Check(Fn::Same(out, Fn::Expected(false)), "result of the plain call");
        Check(game.calls == 1, "ran once");
    }

    Raw::SetAddress(Fn::Code());

    {
        Case _case(Fn::name, "call RawFunc operator()");
        Game game;
        auto out = Fn::ViaObj(&game);
        Check(Fn::Same(out, Fn::Expected(false)), "result");
        Check(game.calls == 1, "ran once");
    }

    {
        Case _case(Fn::name, "call RawFunc::Invoke");
        Game game;
        auto out = Fn::ViaInvoke(&game);
        Check(Fn::Same(out, Fn::Expected(false)), "result");
        Check(game.calls == 1, "ran once");
    }

    {
        Case _case(Fn::name, "call RawFunc::InvokeOriginal");
        SetOriginalAddress<Fn>();
        Game game;
        auto out = Fn::ViaInvokeOriginal(&game);
        Check(Fn::Same(out, Fn::Expected(false)), "result");
        Check(game.calls == 1, "ran once");
        Check(Raw::GetOriginalAddress() == Fn::Code(), "original address");
    }
}

template<typename Fn, Core::HookFlow Flow, auto Callback, Core::HookRun Run = Core::HookRun::Default>
void RunHook(const char* aLabel, bool aExpectTweaked, bool aSeesArgs = true)
{
    using Raw = typename Fn::Raw;
    using Callable = decltype(Callback);
    using Wrapper = Core::Detail::HookWrapper<Callable>;
    using Handler = Core::Detail::HookHandler<Raw, Wrapper, Flow, Run>;

    static_assert(Core::Detail::HookFlowTraits<Flow, Raw, Callable>::IsCompatible);
    static_assert(Core::Detail::HookFlowTraits<Flow, Raw, Callable>::IsCompatibleNonMember);

    Case _case(Fn::name, aLabel);
    ResetEvents();
    SetOriginalAddress<Fn>();
    s_driver = FakeDriver{};
    s_driver.real = Fn::Code();

    Game game;

    auto attached = Handler::Attach(s_driver, Wrapper(Callback));
    Check(attached.success, "attach");
    Check(s_driver.detour != nullptr, "the driver got a detour");
    Check(s_driver.target == Fn::Code(), "hooked address");
    Check(Raw::GetAddress() == Fn::Code(), "after the attach the raw points at the original");
    Check(Core::Detail::HookInstance<Raw>::IsAttached(), "attached");

    if (s_driver.detour)
    {
        auto out = Fn::ViaDetour(s_driver.detour, &game);

        Check(Fn::Same(out, Fn::Expected(aExpectTweaked)), "result of the call through the detour");
        Check(game.calls == 1, "the original ran once");
        Check(g_events == 1, "the callback ran once");

        if (aSeesArgs)
        {
            Check(g_seenSelf == &game, "the callback got the right this");
            Check(g_seenSum == Fn::ArgSum(), "the callback got the right arguments");
        }
    }

    if constexpr (Run == Core::HookRun::Once)
    {
        Check(!Core::Detail::HookInstance<Raw>::IsAttached(), "the run-once hook detached itself");
        Check(s_driver.detaches == 1, "the driver detached once");
        Check(!Handler::Detach(), "a second detach does nothing");
    }
    else
    {
        Check(Handler::Detach(), "detach");
        Check(!Core::Detail::HookInstance<Raw>::IsAttached(), "detached");
        Check(s_driver.detaches == 1, "the driver detached once");
    }

    Check(Raw::GetAddress() == Fn::Code(), "after the detach the raw is reset");
}

template<typename Fn>
void RunFlows()
{
    using Core::HookFlow;
    using Core::HookRun;

    RunHook<Fn, HookFlow::Original, &Fn::Replace>("hook original", true);
    RunHook<Fn, HookFlow::Before, &Fn::Before>("hook before", false);
    RunHook<Fn, HookFlow::After, &Fn::After>("hook after (tail observer)", true);
    RunHook<Fn, HookFlow::Wrap, &Fn::Wrap>("hook wrap", true);

    if constexpr (!Fn::isVoid)
    {
        RunHook<Fn, HookFlow::After, &Fn::ResultObserver>("hook after (result observer)", true, false);
        RunHook<Fn, HookFlow::After, &Fn::ResultWrapper>("hook after (result wrapper)", true, false);
        RunHook<Fn, HookFlow::Before, &Fn::Blind>("hook before (blind observer)", false, false);
    }
    else
    {
        RunHook<Fn, HookFlow::Before, &Fn::Blind>("hook before (blind observer)", false, false);
    }

    RunHook<Fn, HookFlow::Before, &Fn::Before, HookRun::Once>("hook before, run once", false);
    RunHook<Fn, HookFlow::Original, &Fn::Replace, HookRun::Once>("hook original, run once", true);
}

// Callback forms: a method of an object, a lambda through HookingAgent, and the original passed by pointer to Attach.

struct Listener
{
    int count = 0;

    Handle16 OnSpawn(Game* aSelf, Name aName)
    {
        ++count;
        Handle16 result = SpawnFn::obj(aSelf, aName);
        result.tag += 100;
        return result;
    }
};

struct Agent : Core::HookingAgent
{
    static bool HookSpawnWithLambda(int* aCounter)
    {
        static int* s_counter;
        s_counter = aCounter;

        return Hook<SpawnFn::obj>([](Game* aSelf, Name aName) -> Handle16 {
            ++*s_counter;
            Handle16 result = SpawnFn::obj(aSelf, aName);
            result.tag += 100;
            return result;
        }).success;
    }

    static bool HookSumAfter()
    {
        return HookAfter<SumFn::obj>(&SumFn::ResultObserver).success;
    }

    static bool IsSpawnHooked()
    {
        return IsHooked<SpawnFn::obj>();
    }

    static bool UnhookSpawn()
    {
        return Unhook<SpawnFn::obj>();
    }

    static bool UnhookSum()
    {
        return Unhook<SumFn::obj>();
    }
};

void RunCallbackForms()
{
    using Raw = SpawnFn::Raw;

    {
        Case _case("Spawn", "hook with a method callback");
        ResetEvents();
        SetOriginalAddress<SpawnFn>();
        s_driver = FakeDriver{};
        s_driver.real = SpawnFn::Code();

        using Wrapper = Core::Detail::HookWrapper<Handle16 (Listener::*)(Game*, Name)>;
        using Handler = Core::Detail::HookHandler<Raw, Wrapper, Core::HookFlow::Original>;

        static_assert(Core::Detail::HookFlowTraits<Core::HookFlow::Original, Raw, Handle16 (Listener::*)(Game*, Name)>::IsCompatibleMember);

        Listener listener;
        Game game;

        Check(Handler::Attach(s_driver, Wrapper(&Listener::OnSpawn, &listener)).success, "attach");
        auto out = SpawnFn::ViaDetour(s_driver.detour, &game);
        Check(SpawnFn::Same(out, SpawnFn::Expected(true)), "result");
        Check(listener.count == 1 && game.calls == 1, "the method ran once and so did the original");
        Check(Handler::Detach(), "detach");
    }

    {
        Case _case("Spawn", "HookingAgent::Hook with a lambda");
        SetOriginalAddress<SpawnFn>();
        s_driver = FakeDriver{};
        s_driver.real = SpawnFn::Code();
        Core::HookingDriver::SetDefault(s_driver);

        int counter = 0;
        Game game;

        Check(Agent::HookSpawnWithLambda(&counter), "hook");
        Check(Agent::IsSpawnHooked(), "IsHooked");
        auto out = SpawnFn::ViaDetour(s_driver.detour, &game);
        Check(SpawnFn::Same(out, SpawnFn::Expected(true)), "result");
        Check(counter == 1 && game.calls == 1, "the lambda ran once and so did the original");
        Check(Agent::UnhookSpawn(), "unhook");
        Check(!Agent::IsSpawnHooked(), "not hooked any more");
    }

    {
        Case _case("Sum", "HookingAgent::HookAfter");
        SetOriginalAddress<SumFn>();
        s_driver = FakeDriver{};
        s_driver.real = SumFn::Code();
        Core::HookingDriver::SetDefault(s_driver);

        ResetEvents();
        Game game;

        Check(Agent::HookSumAfter(), "hook");
        auto out = SumFn::ViaDetour(s_driver.detour, &game);
        Check(SumFn::Same(out, SumFn::Expected(true)), "result");
        Check(Agent::UnhookSum(), "unhook");
    }

    {
        Case _case("Spawn", "the original pointer given to Attach");
        SetOriginalAddress<SpawnFn>();
        s_driver = FakeDriver{};
        s_driver.real = SpawnFn::Code();

        using Wrapper = Core::Detail::HookWrapper<decltype(&SpawnFn::Replace)>;
        using Handler = Core::Detail::HookHandler<Raw, Wrapper, Core::HookFlow::Original>;

        Raw::Callable original = nullptr;
        Check(Handler::Attach(s_driver, Wrapper(&SpawnFn::Replace), &original).success, "attach");
        Check(reinterpret_cast<uintptr_t>(original) == SpawnFn::Code(), "the original is the one the driver gave");
        Check(Handler::Detach(), "detach");
    }
}

void StaticChecks()
{
    using Core::Detail::MemberShape;

    constexpr bool hidden = Core::Detail::MemberResultIsHidden;

    static_assert(std::is_same_v<MemberShape<Game, int, int, int>, int (*)(Game*, int, int)>);
    static_assert(std::is_same_v<MemberShape<Game, void, int>, void (*)(Game*, int)>);
    static_assert(std::is_same_v<MemberShape<Game, int&, int>, int& (*)(Game*, int)>);
    static_assert(std::is_same_v<MemberShape<Game, Handle16, Name>,
                                 std::conditional_t<hidden, Handle16* (*)(Game*, Handle16*, Name), Handle16 (*)(Game*, Name)>>);
    static_assert(std::is_same_v<MemberShape<Game, Small8, int>,
                                 std::conditional_t<hidden, Small8* (*)(Game*, Small8*, int), Small8 (*)(Game*, int)>>);

    // The base keeps working as the callable type of the raw.
    static_assert(std::is_same_v<SpawnFn::Raw::Callable, MemberShape<Game, Handle16, Name>>);
    static_assert(std::is_same_v<SumFn::Raw::Callable, int (*)(Game*, int, int)>);

    // Callback forms of the free function with "this" first are accepted, other forms are not.
    using Core::HookFlow;
    using Core::Detail::HookFlowTraits;

    static_assert(HookFlowTraits<HookFlow::Original, SpawnFn::Raw, Handle16 (*)(Game*, Name)>::IsCompatible);
    static_assert(!HookFlowTraits<HookFlow::Original, SpawnFn::Raw, Handle16 (*)(Name)>::IsCompatible);
    static_assert(!HookFlowTraits<HookFlow::Original, SpawnFn::Raw, void (*)(Game*, Name)>::IsCompatible);
    static_assert(HookFlowTraits<HookFlow::Before, SpawnFn::Raw, void (*)(Game*, Name)>::IsCompatible);
    static_assert(HookFlowTraits<HookFlow::Before, SpawnFn::Raw, void (*)()>::IsCompatible);
    static_assert(HookFlowTraits<HookFlow::After, SpawnFn::Raw, void (*)(Handle16&, Game*, Name)>::IsCompatible);
    static_assert(HookFlowTraits<HookFlow::After, SpawnFn::Raw, void (*)(Handle16&)>::IsCompatible);
    static_assert(HookFlowTraits<HookFlow::After, SpawnFn::Raw, Handle16 (*)(Handle16&)>::IsCompatible);
    static_assert(HookFlowTraits<HookFlow::After, SpawnFn::Raw, Handle16 (*)(Handle16&, Game*, Name)>::IsCompatible);
    static_assert(HookFlowTraits<HookFlow::Wrap, SpawnFn::Raw, Handle16 (*)(Handle16 (*)(Game*, Name), Game*, Name)>::IsCompatible);
    static_assert(!HookFlowTraits<HookFlow::Wrap, SpawnFn::Raw, Handle16 (*)(Handle16* (*)(Game*, Handle16*, Name), Game*, Name)>::IsCompatible);
    static_assert(HookFlowTraits<HookFlow::After, SetFn::Raw, void (*)(Game*, int)>::IsCompatible);
    static_assert(HookFlowTraits<HookFlow::Wrap, SetFn::Raw, void (*)(void (*)(Game*, int), Game*, int)>::IsCompatible);

    // The call operator is there exactly when the shape is the hidden one.
    static_assert(std::is_invocable_r_v<Handle16, const SpawnFn::Raw&, Game*, Name>);
    static_assert(std::is_invocable_r_v<int, const SumFn::Raw&, Game*, int, int>);
    static_assert(std::is_invocable_v<const SetFn::Raw&, Game*, int>);
}
}

int main()
{
    Core::AddressResolver::SetDefault(s_resolver);
    Core::HookingDriver::SetDefault(s_driver);

    StaticChecks();

    std::printf("target: %s, member results %s the hidden result address shape\n",
                kSimulated ? "simulated MSVC x64 shape (not MSVC)" : (kRealHidden ? "MSVC x64" : "native"),
                Core::Detail::MemberResultIsHidden ? "use" : "do not use");

    RunCalls<SpawnFn>();
    RunFlows<SpawnFn>();
    RunCalls<SpawnExtFn>();
    RunFlows<SpawnExtFn>();
    RunCalls<SmallFn>();
    RunFlows<SmallFn>();
    RunCalls<BigFn>();
    RunFlows<BigFn>();
    RunCalls<SumFn>();
    RunFlows<SumFn>();
    RunCalls<SetFn>();
    RunFlows<SetFn>();
    RunCallbackForms();

    std::printf("\n%d cases, %d failed\n", g_cases, g_failures);
    std::printf("%s\n", g_failures == 0 ? "ALL PASS" : "FAILED");

    return g_failures == 0 ? 0 : 1;
}
