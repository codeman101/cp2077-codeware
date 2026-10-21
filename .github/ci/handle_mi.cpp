#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/Generated/game/IGameSystem.hpp>
#include <cstdio>
using namespace RED4ext;
struct Other { virtual ~Other() = default; long pad[3]{}; };
struct MySys : game::IGameSystem {};
struct Multi : Other, IScriptable {};          // IScriptable is not the first base
static int failures = 0;
static void takesBase(const Handle<IScriptable>& h, IScriptable* expect, const char* what)
{ bool ok = h.instance == expect; failures += !ok; std::printf("%-28s %s\n", what, ok ? "ok" : "WRONG POINTER"); }
template<class D> static void fake(Handle<D>& h, D* p, RefCnt& rc) { rc.strongRefs = 100; rc.weakRefs = 1; h.instance = p; h.refCount = &rc; }
int main() {
  alignas(Multi) static char bm[sizeof(Multi)]; alignas(MySys) static char bs[sizeof(MySys)]; Multi& m = *reinterpret_cast<Multi*>(bm); MySys& s = *reinterpret_cast<MySys*>(bs); RefCnt rc1{}, rc2{};
  Handle<Multi> hm; fake(hm, &m, rc1);
  Handle<MySys> hs; fake(hs, &s, rc2);
  IScriptable* mBase = &m;
  std::printf("Multi* %p, as IScriptable* %p (offset %td)\n", (void*)&m, (void*)mBase, (char*)mBase - (char*)&m);
  DynArray<Handle<IScriptable>> arr;
  { Handle<IScriptable> b(hm); takesBase(b, mBase, "copy ctor (Multi)"); }
  { Handle<IScriptable> b = hm; takesBase(b, mBase, "copy-init (Multi)"); }
  takesBase(hm, mBase, "pass to const Handle<Base>&");
  { Handle<IScriptable> b; b = hm; takesBase(b, mBase, "assignment (Multi)"); }
  { Handle<IScriptable> b(hs); takesBase(b, &s, "copy ctor (MySys)"); }
  std::printf("refs after temporaries: %u (expect 100)\n", rc1.strongRefs);
  { Handle<Multi> tmp = hm; Handle<IScriptable> c(std::move(tmp)); takesBase(c, mBase, "move ctor (Multi)");
    std::printf("moved-from empty: %s\n", tmp.instance == nullptr && tmp.refCount == nullptr ? "ok" : "WRONG"); }
  std::printf("refs after move: %u (expect 100)\n", rc1.strongRefs);
  static_assert(!std::is_constructible_v<Handle<MySys>, Handle<IScriptable>>, "no downcast");
  static_assert(!std::is_constructible_v<Handle<Multi>, Handle<MySys>>, "no unrelated");
  hm.instance = nullptr; hm.refCount = nullptr; hs.instance = nullptr; hs.refCount = nullptr;
  if (rc1.strongRefs != 100) ++failures;
  std::printf("failures: %d\n", failures);
  return failures;
}
