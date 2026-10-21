#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/Generated/game/IGameSystem.hpp>
#include <cstdio>
#include <utility>
using namespace RED4ext;
struct Other { virtual ~Other() = default; long pad[3]{}; };
struct MySys : game::IGameSystem {};
struct Multi : Other, IScriptable {}; // IScriptable is not the first base

static int failures = 0;
static void check(bool ok, const char* what) { failures += !ok; std::printf("%-46s %s\n", what, ok ? "ok" : "FAIL"); }
static IScriptable* viaConstRef(const Handle<IScriptable>& h) { return h.instance; }
static void outParam(Handle<IScriptable>& out) { (void)out; }
[[maybe_unused]] static void emplaceCompiles(DynArray<Handle<IScriptable>>& a, Handle<Multi>& h) { a.EmplaceBack(h); } // compile-only: DynArray allocates through the game
template<class H> concept BindsToOutParam = requires(H&& h) { outParam(std::forward<H>(h)); };
template<class D> static void fake(Handle<D>& h, D* p, RefCnt& rc) { rc.strongRefs = 100; rc.weakRefs = 1; h.instance = p; h.refCount = &rc; }

int main()
{
    alignas(Multi) static char bm[sizeof(Multi)];
    alignas(MySys) static char bs[sizeof(MySys)];
    Multi* m = reinterpret_cast<Multi*>(bm);
    MySys* s = reinterpret_cast<MySys*>(bs);
    RefCnt rc1{}, rc2{};
    Handle<Multi> hm; fake(hm, m, rc1);
    Handle<MySys> hs; fake(hs, s, rc2);
    const Handle<Multi>& chm = hm;
    IScriptable* mBase = m;
    std::printf("Multi* %p, IScriptable subobject %p (offset %td)\n\n", (void*)m, (void*)mBase, (char*)mBase - (char*)m);

    std::puts("Converted by the compiler (pointer adjusted):");
    { Handle<IScriptable> b(hm); check(b.instance == mBase, "direct-init from lvalue"); }
    { Handle<IScriptable> b = chm; check(b.instance == mBase, "copy-init from const lvalue"); }
    check(viaConstRef(chm) == mBase, "const Handle<Base>& from const lvalue");
    check(viaConstRef(Handle<Multi>(hm)) == mBase, "const Handle<Base>& from rvalue");
    { Handle<Multi> t = hm; Handle<IScriptable> c(std::move(t)); check(c.instance == mBase && !t.instance, "move-construct"); }
    { Handle<IScriptable> b; b = Handle<Multi>(hm); check(b.instance == mBase, "assign from rvalue"); }
    { Handle<IScriptable> b(hs); check(b.instance == s, "direct-init, single inheritance"); }

    std::puts("\nAliased through operator Handle<U>&() & (non-const lvalue):");
    { Handle<IScriptable> b = hs; check(b.instance == s, "copy-init, single inheritance"); }
    check(viaConstRef(hs) == s, "const Handle<Base>&, single inheritance");
    { Handle<IScriptable>& r = hs; check(r.instance == static_cast<IScriptable*>(s), "Handle<Base>& out-param, single inheritance"); }
    { Handle<IScriptable> b = hm; std::printf("%-46s %s\n", "copy-init, Multi (alias, unadjusted)", b.instance == mBase ? "adjusted" : "unadjusted"); }

    std::puts("\nCompile-time:");
    static_assert(BindsToOutParam<Handle<MySys>&>, "lvalue binds to Handle<Base>&");
    static_assert(!BindsToOutParam<Handle<MySys>>, "temporary does not bind to Handle<Base>&");
    static_assert(!BindsToOutParam<const Handle<MySys>&>, "const lvalue does not bind to Handle<Base>&");
    static_assert(!std::is_constructible_v<Handle<MySys>, Handle<IScriptable>>, "no downcast");
    static_assert(!std::is_constructible_v<Handle<Multi>, Handle<MySys>>, "no unrelated");
    std::puts("static_asserts passed (out-param only from non-const lvalues, no downcast, no unrelated)");

    check(rc1.strongRefs == 100 && rc2.strongRefs == 100, "reference counts balanced");
    hm.instance = nullptr; hm.refCount = nullptr; hs.instance = nullptr; hs.refCount = nullptr;
    std::printf("\nfailures: %d\n", failures);
    return failures;
}
