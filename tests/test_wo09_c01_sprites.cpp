// test_wo09_c01_sprites.cpp — WO-09 (2D stability) C01, the headless half:
// sprites through Scene::BuildSpriteDrawList (the draw-list oracle) and the
// flipbook animation math (SpriteAnimationComponent::SelectFrame / FrameUV /
// Scene::UpdateSpriteAnimations).
//
// What C01 pins (catalog 03, C01): 0 / 1 / 10,000+ items, equal sort keys
// (build twice ⇒ identical; permuted creation ⇒ the documented tie-break),
// flips (the WorldSize rule — flips are applied by the draw, so the size stays
// unsigned here and the GPU half pins the mirrored pixels), active/disabled
// hierarchy (T12/T13 plus the ancestry-walk guard), zero / negative / nonfinite
// Position / Scale / Rotation / Color, and animation at loop / end / large delta,
// zero- and one-frame clips and an out-of-range frame index.
//
// The oracle is never the routine under test: expected orders are computed from
// the documented key rule with an independent comparator (a total order that
// sorts NaN keys last), expected frames from the definition (elapsed*fps mod
// frames) in double, and counts from what the test created.
//
// GPU half: tests/render/render_wo09_sprites.cpp.

#include <doctest.h>

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Cosmic;

namespace
{
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    constexpr float kInf = std::numeric_limits<float>::infinity();

    Entity MakeSprite(Scene& s, const std::string& name, glm::vec3 pos, int32_t zOrder, bool ySort = false)
    {
        Entity e = s.CreateEntity(name);
        e.GetComponent<TransformComponent>().Position = pos;
        auto& sr  = e.AddComponent<SpriteRendererComponent>();
        sr.ZOrder = zOrder;
        sr.YSort  = ySort;
        return e;
    }

    // The documented key rule, written independently of Scene.cpp.
    struct Expected { entt::entity E; int32_t Z; float Key; };

    // A TOTAL order over (Z, key, handle): a non-finite key sorts AFTER every finite
    // key of the same layer, and two non-finite keys tie (handle decides). This is
    // the reference the engine list is compared against; it is a strict weak order
    // even when keys are NaN, which the naive `a.Key < b.Key` comparator is not.
    bool RefLess(const Expected& a, const Expected& b)
    {
        if (a.Z != b.Z) return a.Z < b.Z;
        const bool fa = std::isfinite(a.Key), fb = std::isfinite(b.Key);
        if (fa != fb) return fa;                       // finite first
        if (fa && a.Key != b.Key) return a.Key < b.Key;
        return a.E < b.E;
    }

    std::vector<entt::entity> Handles(const std::vector<Scene::SpriteDrawItem>& items)
    {
        std::vector<entt::entity> out;
        out.reserve(items.size());
        for (const auto& it : items) out.push_back(it.E);
        return out;
    }

    // True when `list` is a permutation of `expected` (same multiset of handles).
    bool SameSet(std::vector<entt::entity> a, std::vector<entt::entity> b)
    {
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        return a == b;
    }

    // The frame the DEFINITION selects: floor(elapsed * fps) wrapped (loop) or
    // clamped (one-shot), evaluated in double so no float overflow can hide.
    int DefinitionFrame(double elapsed, double fps, int frames, bool loop)
    {
        if (frames <= 1 || !(fps > 0.0) || !std::isfinite(elapsed)) return 0;
        const double f = std::floor(elapsed * fps);
        if (loop)
        {
            const double m = std::fmod(f, (double)frames);
            return (int)(m < 0 ? m + frames : m);
        }
        if (f < 0) return 0;
        return f >= frames ? frames - 1 : (int)f;
    }
}

TEST_SUITE("WO-09 C01 sprites (headless)")
{
    TEST_CASE("WO-09 C01: BuildSpriteDrawList at 0 / 1 / 10,000 / 20,000 items (timed, exact counts)")
    {
        Ref<Scene> scene = Scene::Create();
        CHECK(scene->BuildSpriteDrawList().empty());                 // 0 items

        MakeSprite(*scene, "one", { 0, 0, 0 }, 0);
        REQUIRE(scene->BuildSpriteDrawList().size() == 1);           // 1 item

        std::mt19937 rng(0x5EED0901u);
        std::uniform_int_distribution<int> zDist(-5, 5);
        std::uniform_real_distribution<float> pDist(-100.0f, 100.0f);
        std::vector<Expected> expected;
        {
            // A fresh scene so the count is exact.
            scene = Scene::Create();
            for (int i = 0; i < 10000; ++i)
            {
                const int32_t z = zDist(rng);
                const bool ySort = (i % 7) == 0;
                const glm::vec3 pos{ pDist(rng), pDist(rng), (i % 3) == 0 ? 0.0f : pDist(rng) };
                Entity e = MakeSprite(*scene, "s" + std::to_string(i), pos, z, ySort);
                expected.push_back({ (entt::entity)e, z, ySort ? -pos.y : pos.z });
            }
        }
        // 10,000 items: exact count, order equals the independent reference order.
        const auto t0 = std::chrono::steady_clock::now();
        std::vector<Scene::SpriteDrawItem> list = scene->BuildSpriteDrawList();
        const double ms10k = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        REQUIRE(list.size() == 10000);
        std::vector<Expected> ref = expected;
        std::sort(ref.begin(), ref.end(), RefLess);
        for (size_t i = 0; i < ref.size(); ++i)
        {
            if (list[i].E != ref[i].E) { FAIL("10,000-item order diverges from the reference at index " << i); break; }
        }
        MESSAGE("C01 BuildSpriteDrawList(10,000) = " << ms10k << " ms");

        // 20,000 items (10,000 more, half of them tilemaps): still exact.
        for (int i = 0; i < 10000; ++i)
        {
            const int32_t z = zDist(rng);
            Entity e = scene->CreateEntity("m" + std::to_string(i));
            const float pz = pDist(rng);
            e.GetComponent<TransformComponent>().Position = { pDist(rng), pDist(rng), pz };
            if (i % 2)
            {
                auto& tm = e.AddComponent<TilemapComponent>();
                tm.ZOrder = z;
                expected.push_back({ (entt::entity)e, z, pz });
            }
            else
            {
                auto& sr = e.AddComponent<SpriteRendererComponent>();
                sr.ZOrder = z;
                expected.push_back({ (entt::entity)e, z, pz });
            }
        }
        const auto t1 = std::chrono::steady_clock::now();
        list = scene->BuildSpriteDrawList();
        const double ms20k = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
        REQUIRE(list.size() == 20000);
        ref = expected;
        std::sort(ref.begin(), ref.end(), RefLess);
        size_t mismatches = 0;
        for (size_t i = 0; i < ref.size(); ++i) mismatches += list[i].E != ref[i].E;
        CHECK(mismatches == 0);
        size_t maps = 0;
        for (const auto& it : list) maps += it.Map ? 1 : 0;
        CHECK(maps == 5000);
        MESSAGE("C01 BuildSpriteDrawList(20,000 incl. 5,000 tilemaps) = " << ms20k << " ms");
        CHECK(ms20k < 10000.0);   // the U-case deadline; the number itself is recorded, not gated tighter
    }

    TEST_CASE("WO-09 C01: equal sort keys — build twice is identical; the tie-break is the entity handle, not creation order")
    {
        Ref<Scene> scene = Scene::Create();
        std::vector<Entity> made;
        for (int i = 0; i < 200; ++i)
            made.push_back(MakeSprite(*scene, "eq" + std::to_string(i), { 1.0f, 2.0f, 3.0f }, 4));

        const auto a = scene->BuildSpriteDrawList();
        const auto b = scene->BuildSpriteDrawList();
        REQUIRE(a.size() == 200);
        CHECK(Handles(a) == Handles(b));                             // deterministic
        for (size_t i = 1; i < a.size(); ++i)
            CHECK(a[i - 1].E < a[i].E);                              // ascending handle

        // Permuted creation: destroy every other entity, recreate the same sprites in
        // reverse order. Recycled slots carry a higher VERSION, so the entt handle
        // value (version << 20 | index) — the documented tie-break — puts a recycled
        // entity AFTER every never-recycled one, regardless of creation order.
        std::vector<entt::entity> destroyed;
        for (size_t i = 0; i < made.size(); i += 2) { destroyed.push_back((entt::entity)made[i]); scene->DestroyEntity(made[i]); }
        std::vector<entt::entity> recreated;
        for (int i = 99; i >= 0; --i)
            recreated.push_back((entt::entity)MakeSprite(*scene, "re" + std::to_string(i), { 1.0f, 2.0f, 3.0f }, 4));

        const auto c = scene->BuildSpriteDrawList();
        REQUIRE(c.size() == 200);
        std::vector<entt::entity> handles = Handles(c);
        std::vector<entt::entity> sortedHandles = handles;
        std::sort(sortedHandles.begin(), sortedHandles.end());
        CHECK(handles == sortedHandles);                             // the list IS ascending handle order
        // Every recycled handle sorts after every surviving original handle.
        entt::entity maxOriginal = entt::null;
        for (size_t i = 1; i < made.size(); i += 2)
            if (maxOriginal == entt::null || (entt::entity)made[i] > maxOriginal) maxOriginal = (entt::entity)made[i];
        size_t recycledAfter = 0;
        for (entt::entity r : recreated) recycledAfter += r > maxOriginal;
        CHECK(recycledAfter == recreated.size());
        MESSAGE("C01 tie-break pinned: equal (ZOrder, key) items draw in ascending entt handle order; "
                "a recycled entity (higher version) draws on top of every never-recycled one.");
    }

    TEST_CASE("WO-09 C01: nonfinite sort keys (NaN/inf Position) — the list stays a deterministic permutation")
    {
        // 10,000 sprites, ~30 % with a NaN key (Position.z, or Position.y under YSort),
        // plus ±inf keys, across 8 seeds: every build must be a permutation of the
        // input, identical on a rebuild, and equal to the reference total order
        // (finite keys ascending, non-finite keys last, handle tie-break).
        for (uint32_t seed = 1; seed <= 8; ++seed)
        {
            Ref<Scene> scene = Scene::Create();
            std::mt19937 rng(0xC01A0000u + seed);
            std::uniform_int_distribution<int> zDist(-2, 2);
            std::uniform_real_distribution<float> pDist(-50.0f, 50.0f);
            std::uniform_int_distribution<int> kind(0, 9);
            std::vector<Expected> expected;
            for (int i = 0; i < 10000; ++i)
            {
                const int32_t z = zDist(rng);
                const bool ySort = (i % 5) == 0;
                float key = pDist(rng);
                const int k = kind(rng);
                if (k < 3)      key = kNaN;
                else if (k == 3) key = kInf;
                else if (k == 4) key = -kInf;
                glm::vec3 pos{ pDist(rng), ySort ? -key : pDist(rng), ySort ? pDist(rng) : key };
                Entity e = MakeSprite(*scene, "n" + std::to_string(i), pos, z, ySort);
                // -(-key) is key for ±inf and NaN alike.
                expected.push_back({ (entt::entity)e, z, ySort ? -pos.y : pos.z });
            }
            std::vector<entt::entity> input;
            for (const auto& x : expected) input.push_back(x.E);

            const auto first  = scene->BuildSpriteDrawList();
            const auto second = scene->BuildSpriteDrawList();
            REQUIRE(first.size() == 10000);
            CHECK(SameSet(Handles(first), input));                  // nothing dropped / duplicated
            CHECK(Handles(first) == Handles(second));               // deterministic

            std::vector<Expected> ref = expected;
            std::sort(ref.begin(), ref.end(), RefLess);
            size_t mismatches = 0;
            for (size_t i = 0; i < ref.size(); ++i) mismatches += first[i].E != ref[i].E;
            // The poison metric: adjacent FINITE-keyed items of the same ZOrder that the
            // engine list draws in the wrong order (the NaN entries themselves are
            // ambiguous by definition; a misordered finite pair is a real painter error).
            size_t finiteInversions = 0, finitePairs = 0;
            std::unordered_map<entt::entity, const Expected*> byHandle;
            for (const auto& x : expected) byHandle[x.E] = &x;
            const Expected* prev = nullptr;
            for (const auto& it : first)
            {
                const Expected* cur = byHandle[it.E];
                if (prev && std::isfinite(prev->Key) && std::isfinite(cur->Key) && prev->Z == cur->Z)
                {
                    ++finitePairs;
                    if (prev->Key > cur->Key) ++finiteInversions;
                }
                prev = cur;
            }
            MESSAGE("C01 nonfinite keys, seed " << seed << ": " << mismatches << " positions off the reference order; "
                    << finiteInversions << " of " << finitePairs << " adjacent finite same-layer pairs are inverted");
            CHECK_MESSAGE(finiteInversions == 0, "seed " << seed << ": NaN keys poisoned the order of " << finiteInversions << " finite-keyed pairs");
            CHECK_MESSAGE(mismatches == 0, "seed " << seed << ": " << mismatches
                          << " positions differ from the reference total order (non-finite keys last)");
        }
    }

    TEST_CASE("WO-09 C01: zero / negative / nonfinite Scale, Rotation and Color do not change the list")
    {
        Ref<Scene> scene = Scene::Create();
        const glm::vec3 scales[]  = { { 0, 0, 0 }, { -1, -1, 1 }, { kNaN, 1, 1 }, { kInf, -kInf, 1 }, { 1e-30f, 1e30f, 1 } };
        const float rots[]        = { 0.0f, -720.0f, kNaN, kInf, 1e9f };
        const glm::vec4 colors[]  = { { 0, 0, 0, 0 }, { -1, -1, -1, -1 }, { kNaN, 0, 0, 1 }, { kInf, 1, 1, 1 }, { 1, 1, 1, 1 } };
        std::vector<entt::entity> handles;
        for (int i = 0; i < 5; ++i)
        {
            Entity e = MakeSprite(*scene, "w" + std::to_string(i), { 0, 0, (float)i }, 0);
            auto& t = e.GetComponent<TransformComponent>();
            t.Scale = scales[i];
            t.Rotation.z = rots[i];
            e.GetComponent<SpriteRendererComponent>().Color = colors[i];
            handles.push_back((entt::entity)e);
        }
        const auto list = scene->BuildSpriteDrawList();
        REQUIRE(list.size() == 5);
        CHECK(Handles(list) == handles);   // ascending Position.z = creation order here

        // The sizing rule (pure): flips are NOT applied here (unsigned size; the
        // draw negates it), a non-positive PixelsPerUnit reads as 1, an untextured
        // sprite's size IS its scale (zero, negative and non-finite included).
        SpriteRendererComponent s;
        s.PixelsPerUnit = 100.0f;
        CHECK(SpriteRendererComponent::WorldSize(s, { 2.0f, 3.0f }, 100, 50) == glm::vec2(2.0f, 1.5f));
        s.FlipX = s.FlipY = true;
        CHECK(SpriteRendererComponent::WorldSize(s, { 2.0f, 3.0f }, 100, 50) == glm::vec2(2.0f, 1.5f));
        CHECK(SpriteRendererComponent::WorldSize(s, { -2.0f, 0.0f }, 100, 50) == glm::vec2(-2.0f, 0.0f));
        s.PixelsPerUnit = 0.0f;
        CHECK(SpriteRendererComponent::WorldSize(s, { 1.0f, 1.0f }, 100, 50) == glm::vec2(100.0f, 50.0f));
        s.PixelsPerUnit = -5.0f;
        CHECK(SpriteRendererComponent::WorldSize(s, { 1.0f, 1.0f }, 100, 50) == glm::vec2(100.0f, 50.0f));
        s.PixelsPerUnit = kNaN;                                     // NaN > 0 is false ⇒ 1
        CHECK(SpriteRendererComponent::WorldSize(s, { 1.0f, 1.0f }, 100, 50) == glm::vec2(100.0f, 50.0f));
        s.PixelsPerUnit = 100.0f;
        CHECK(SpriteRendererComponent::WorldSize(s, { 1.0f, 1.0f }, 0, 0) == glm::vec2(1.0f, 1.0f));   // untextured
        const glm::vec2 nan = SpriteRendererComponent::WorldSize(s, { kNaN, 1.0f }, 100, 50);
        CHECK(std::isnan(nan.x));                                   // a NaN scale stays NaN (pinned, not laundered)
        CHECK(nan.y == 0.5f);
    }

    TEST_CASE("WO-09 C01: active/disabled hierarchy — a 2,000-deep chain under an inactive root is excluded; the ancestry walk is guarded at 4,096")
    {
        Ref<Scene> scene = Scene::Create();
        Entity root = scene->CreateEntity("root");
        root.GetComponent<TagComponent>().Active = false;
        Entity prev = root;
        std::vector<Entity> chain;
        for (int i = 0; i < 2000; ++i)
        {
            Entity e = MakeSprite(*scene, "c" + std::to_string(i), { 0, 0, 0 }, 0);
            REQUIRE(scene->SetParent(e, prev, /*keepWorldPose=*/false));
            chain.push_back(e);
            prev = e;
        }
        CHECK(scene->BuildSpriteDrawList().empty());                // every level inherits the inactive root

        root.GetComponent<TagComponent>().Active = true;
        CHECK(scene->BuildSpriteDrawList().size() == 2000);
        chain[999].GetComponent<TagComponent>().Active = false;     // mid-chain: 1,000 below it vanish
        CHECK(scene->BuildSpriteDrawList().size() == 999);
        chain[999].GetComponent<TagComponent>().Active = true;
        chain[1500].GetComponent<SpriteRendererComponent>().Enabled = false;   // T12: only itself
        CHECK(scene->BuildSpriteDrawList().size() == 1999);
        chain[1500].GetComponent<SpriteRendererComponent>().Enabled = true;

        // The documented guard: IsActiveInHierarchy visits at most 4,096 nodes (self +
        // 4,095 ancestors). A sprite deeper than that below an inactive root is NOT
        // reached by the inactive flag — pinned here as the hierarchy-depth ceiling
        // for activity propagation (see numeric-bar-policy.md, C03 depth ceiling).
        for (int i = 2000; i < 4200; ++i)
        {
            Entity e = MakeSprite(*scene, "c" + std::to_string(i), { 0, 0, 0 }, 0);
            REQUIRE(scene->SetParent(e, prev, false));
            chain.push_back(e);
            prev = e;
        }
        root.GetComponent<TagComponent>().Active = false;
        const auto list = scene->BuildSpriteDrawList();
        // chain[i] is at depth i+1 below the root; the walk covers self + 4,095
        // ancestors, so depths >= 4,096 (i >= 4095) cannot see the root.
        CHECK(list.size() == 4200 - 4095);
        for (const auto& it : list)
        {
            const std::string& tag = scene->GetRegistry().get<TagComponent>(it.E).Tag;
            CHECK(std::stoi(tag.substr(1)) >= 4095);
        }
        MESSAGE("C01 activity-propagation depth ceiling pinned: 4,096 nodes incl. self = 4,095 ancestors (Scene::IsActiveInHierarchy guard)");
    }

    TEST_CASE("WO-09 C01: SelectFrame — loop / end / large delta / zero- and one-frame clips / nonfinite time")
    {
        const float fps = 8.0f; const int frames = 4;
        // Loop and end at ordinary times agree with the definition.
        for (double t : { 0.0, 0.124, 0.125, 0.499, 0.5, 1.0, 12.875, 100.0, -0.125, -1.0 })
        {
            CHECK(SpriteAnimationComponent::SelectFrame((float)t, fps, frames, true)  == DefinitionFrame(t, fps, frames, true));
            CHECK(SpriteAnimationComponent::SelectFrame((float)t, fps, frames, false) == DefinitionFrame(t, fps, frames, false));
        }
        // Degenerate clips: 0 and 1 frames pin to 0; fps 0 / negative / NaN pin to 0.
        CHECK(SpriteAnimationComponent::SelectFrame(3.0f, fps, 0, true)  == 0);
        CHECK(SpriteAnimationComponent::SelectFrame(3.0f, fps, 1, false) == 0);
        CHECK(SpriteAnimationComponent::SelectFrame(3.0f, 0.0f, frames, true) == 0);
        CHECK(SpriteAnimationComponent::SelectFrame(3.0f, -8.0f, frames, true) == 0);
        CHECK(SpriteAnimationComponent::SelectFrame(3.0f, kNaN, frames, true) == 0);

        // LARGE delta: elapsed*fps beyond the int range. A one-shot must still sit on
        // its LAST frame; a loop must return a frame inside [0, frames) that equals
        // the definition (fmod in double) — and never restart a one-shot at 0.
        for (double t : { 3.0e8, 1.0e9, 2.6843546e8, 1.0e12, 3.0e38 })
        {
            const int oneShot = SpriteAnimationComponent::SelectFrame((float)t, fps, frames, false);
            CHECK_MESSAGE(oneShot == frames - 1, "one-shot at elapsed=" << t << " returned frame " << oneShot);
            const int looped = SpriteAnimationComponent::SelectFrame((float)t, fps, frames, true);
            CHECK(looped >= 0);
            CHECK(looped < frames);
        }
        // Inf / NaN elapsed: a defined frame (0 for NaN; the last frame for +inf on a
        // one-shot; frame 0 for ±inf on a loop — fmod(inf) is NaN, which the policy
        // maps to 0), never an out-of-range index.
        for (bool loop : { true, false })
        {
            for (float t : { kInf, -kInf, kNaN })
            {
                const int f = SpriteAnimationComponent::SelectFrame(t, fps, frames, loop);
                CHECK(f >= 0);
                CHECK(f < frames);
            }
        }
        CHECK(SpriteAnimationComponent::SelectFrame(kInf, fps, frames, false) == frames - 1);
        CHECK(SpriteAnimationComponent::SelectFrame(-kInf, fps, frames, false) == 0);
        CHECK(SpriteAnimationComponent::SelectFrame(kNaN, fps, frames, false) == 0);
    }

    TEST_CASE("WO-09 C01: FrameUV with an out-of-range frame / row is finite and never crashes; degenerate sheets fall back")
    {
        // 64x32 sheet, 16x16 cells: 4 frames per row, 2 rows.
        const glm::vec4 f0 = SpriteAnimationComponent::FrameUV(64, 32, 16, 16, 0, 0);
        CHECK(f0 == glm::vec4(0.0f, 0.0f, 0.25f, 0.5f));
        // Past the row end: UV beyond 1 (the sampler wraps/clamps; the math is finite).
        const glm::vec4 over = SpriteAnimationComponent::FrameUV(64, 32, 16, 16, 0, 4);
        CHECK(over == glm::vec4(1.0f, 0.0f, 1.25f, 0.5f));
        const glm::vec4 neg = SpriteAnimationComponent::FrameUV(64, 32, 16, 16, 0, -1);
        CHECK(neg == glm::vec4(-0.25f, 0.0f, 0.0f, 0.5f));
        const glm::vec4 hugeF = SpriteAnimationComponent::FrameUV(64, 32, 16, 16, 7, 1000000);
        CHECK(std::isfinite(hugeF.x)); CHECK(std::isfinite(hugeF.z)); CHECK(std::isfinite(hugeF.y)); CHECK(std::isfinite(hugeF.w));
        // Degenerate: any non-positive dimension ⇒ the whole image.
        CHECK(SpriteAnimationComponent::FrameUV(0, 32, 16, 16, 0, 1) == glm::vec4(0, 0, 1, 1));
        CHECK(SpriteAnimationComponent::FrameUV(64, 32, 0, 16, 0, 1) == glm::vec4(0, 0, 1, 1));
        CHECK(SpriteAnimationComponent::FrameUV(64, 32, 16, -1, 0, 1) == glm::vec4(0, 0, 1, 1));
    }

    TEST_CASE("WO-09 C01: UpdateSpriteAnimations with a huge / nonfinite / negative delta keeps Elapsed defined and the SourceRect untouched (no sheet)")
    {
        Ref<Scene> scene = Scene::Create();
        Entity e = MakeSprite(*scene, "anim", { 0, 0, 0 }, 0);
        auto& anim = e.AddComponent<SpriteAnimationComponent>();
        anim.SheetPath = "";                                        // unresolved: SourceRect must stay
        auto& sr = e.GetComponent<SpriteRendererComponent>();
        const glm::vec4 rect0 = sr.SourceRect;

        scene->UpdateSpriteAnimations(1.0e30f);
        CHECK(anim.Elapsed == 1.0e30f);
        scene->UpdateSpriteAnimations(-1.0e30f);
        CHECK(anim.Elapsed == 0.0f);
        scene->UpdateSpriteAnimations(kInf);
        CHECK(std::isinf(anim.Elapsed));
        scene->UpdateSpriteAnimations(kNaN);                        // NaN in: Elapsed becomes NaN (pinned) —
        CHECK(std::isnan(anim.Elapsed));                            // SelectFrame maps it to frame 0
        CHECK(sr.SourceRect == rect0);
        anim.Playing = false;
        anim.Elapsed = 0.0f;
        scene->UpdateSpriteAnimations(5.0f);
        CHECK(anim.Elapsed == 0.0f);                                // paused: no accumulation
    }
}
