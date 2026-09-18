// test_wo09_c03_ui.cpp — WO-09 (2D stability) C03, the headless half: UI
// layout / hit-test, invalid (inverted / zero / NaN) rectangles, DEEP hierarchies
// (the measured depth ceiling), hierarchy CYCLES (must terminate), 1,000+
// controls and canvas-scale extremes.
//
// Depth ceiling method: the hierarchy walkers (UiSystem::CollectElements,
// Scene::GetWorldTransform, SceneSerializer::SavePrefab, Scene::DestroyEntity)
// are recursive or ancestor-walking, so the practical ceiling is a STACK
// question that differs between Debug and Release frames. The "depth ladder"
// case measures it honestly: it launches THIS executable as a child per rung
// (`--test-case="WO-09 C03: depth probe"` with COSMIC_WO09_DEPTH=<n>) and
// records the exit code — a crash (0xC00000FD stack overflow) is evidence, not
// a failure of the parent. The probe walks all four paths on a chain of n UI
// entities under one canvas. The ratified ceiling and the guards that make
// deeper data BOUNDED are recorded in numeric-bar-policy.md (C03).
//
// Cycles: RelationshipComponent is a public struct, so a script or plugin can
// author A.Children = [B], B.Children = [A] (Scene::SetParent and the serializer
// both refuse it). Every walker must terminate on such data.
//
// GPU half (rendered controls, lights, odd buffers): tests/render/render_wo09_ui_lights.cpp.

#include <doctest.h>

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/EventBus.h"
#include "scene/SceneSerializer.h"
#include "scene/ui/UiComponents.h"
#include "scene/ui/UiSystem.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using namespace Cosmic;

namespace
{
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    constexpr float kInf = std::numeric_limits<float>::infinity();

    Entity MakeCanvas(Scene& s, const char* name = "Canvas")
    {
        Entity c = s.CreateEntity(name);
        auto& cv = c.AddComponent<CanvasComponent>();
        cv.ScaleMode = UiScaleMode::ConstantPixel;
        return c;
    }

    // A button-bearing UI node with an absolute pixel rect inside its parent.
    Entity MakeButton(Scene& s, Entity parent, const char* name, glm::vec2 min, glm::vec2 max,
                      int32_t z = 0, const char* signal = "clicked")
    {
        Entity e = s.CreateEntity(name);
        auto& rt = e.AddComponent<RectTransformComponent>();
        rt.AnchorMin = { 0, 0 }; rt.AnchorMax = { 0, 0 };
        rt.OffsetMin = min; rt.OffsetMax = max;
        rt.ZOrder = z;
        e.AddComponent<UiButtonComponent>().Signal = signal;
        s.SetParent(e, parent, false);
        return e;
    }

    std::string ExePath()
    {
        char buf[MAX_PATH * 2] = {};
        GetModuleFileNameA(nullptr, buf, sizeof(buf));
        return buf;
    }

    std::string EvidenceDir()
    {
        if (const char* e = std::getenv("COSMIC_WO09_EVIDENCE_DIR")) return e[0] ? e : "";
        return "";
    }

    struct ChildResult { DWORD exit = 0; bool timedOut = false; double seconds = 0; };

    // Run this exe as a child with one env var, a deadline, and no window.
    ChildResult RunChild(const std::string& args, const char* envName, const std::string& envValue, DWORD timeoutMs)
    {
        SetEnvironmentVariableA(envName, envValue.c_str());
        std::string cmd = "\"" + ExePath() + "\" " + args;
        STARTUPINFOA si{}; si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        ChildResult r;
        const auto t0 = std::chrono::steady_clock::now();
        const BOOL ok = CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        SetEnvironmentVariableA(envName, nullptr);
        if (!ok) { r.exit = 0xFFFFFFFFu; return r; }
        if (WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_TIMEOUT)
        {
            TerminateProcess(pi.hProcess, 0xDEAD);
            WaitForSingleObject(pi.hProcess, 5000);
            r.timedOut = true;
        }
        GetExitCodeProcess(pi.hProcess, &r.exit);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return r;
    }

    // Build a chain of `depth` UI nodes under a canvas; returns the leaf.
    Entity BuildChain(Scene& s, int depth, std::vector<Entity>* out = nullptr)
    {
        Entity canvas = MakeCanvas(s);
        Entity prev = canvas;
        for (int i = 0; i < depth; ++i)
        {
            Entity e = s.CreateEntity("n" + std::to_string(i));
            auto& rt = e.AddComponent<RectTransformComponent>();
            rt.AnchorMin = { 0, 0 }; rt.AnchorMax = { 1, 1 };
            rt.OffsetMin = { 1, 1 }; rt.OffsetMax = { -1, -1 };   // inset by one pixel per level
            e.AddComponent<UiImageComponent>();
            e.GetComponent<TransformComponent>().Position = { 1.0f, 0.0f, 0.0f };
            REQUIRE(s.SetParent(e, prev, false));
            if (out) out->push_back(e);
            prev = e;
        }
        return prev;
    }
}

TEST_SUITE("WO-09 C03 UI (headless)")
{
    TEST_CASE("WO-09 C03: inverted / zero / NaN / inf rectangles resolve without crashing and are never hit")
    {
        const UiRect parent{ { 0, 0 }, { 800, 600 } };
        RectTransformComponent rt;
        // Inverted: min past max.
        rt.AnchorMin = { 0.8f, 0.8f }; rt.AnchorMax = { 0.2f, 0.2f };
        rt.OffsetMin = { 10, 10 };     rt.OffsetMax = { -10, -10 };
        UiRect inv = UiSystem::ResolveRect(parent, rt);
        CHECK(inv.Min.x > inv.Max.x); CHECK(inv.Min.y > inv.Max.y);
        CHECK(inv.Width() < 0.0f);
        for (glm::vec2 p : { glm::vec2{ 400, 300 }, glm::vec2{ 0, 0 }, glm::vec2{ 170, 130 }, glm::vec2{ 630, 470 } })
            CHECK_FALSE(inv.Contains(p));                          // an inverted rect contains nothing
        // Zero-size: only its own point is inside (Contains is inclusive).
        rt.AnchorMin = rt.AnchorMax = { 0.5f, 0.5f }; rt.OffsetMin = rt.OffsetMax = { 0, 0 };
        UiRect zero = UiSystem::ResolveRect(parent, rt);
        CHECK(zero.Size() == glm::vec2(0.0f));
        CHECK(zero.Contains({ 400, 300 }));
        CHECK_FALSE(zero.Contains({ 400.5f, 300 }));
        // NaN / inf: propagate, never hit, never throw.
        rt.OffsetMin = { kNaN, 0 }; rt.OffsetMax = { 10, kInf };
        UiRect bad = UiSystem::ResolveRect(parent, rt);
        CHECK(std::isnan(bad.Min.x)); CHECK(std::isinf(bad.Max.y));
        CHECK_FALSE(bad.Contains({ 400, 300 }));
        CHECK_FALSE(bad.Contains({ kNaN, kNaN }));
        CHECK(std::isnan(UiSystem::PivotPoint(bad, { 0.5f, 0.5f }).x));

        // Through the scene: an inverted button is not hit at its own centre, a
        // sibling normal button still is (the bad rect does not poison the walk).
        Ref<Scene> s = Scene::Create();
        Entity canvas = MakeCanvas(*s);
        Entity good = MakeButton(*s, canvas, "good", { 10, 10 }, { 110, 60 });
        Entity flip = MakeButton(*s, canvas, "flip", { 300, 300 }, { 200, 200 }, 5);
        Entity nanB = MakeButton(*s, canvas, "nan",  { kNaN, 0 }, { kNaN, 100 }, 9);
        std::vector<UiElement> els;
        UiSystem::CollectElements(*s, parent, els);
        CHECK(els.size() == 3);
        uint32_t hit = 0;
        CHECK(UiSystem::HitTest(*s, parent, { 50, 30 }, hit));
        CHECK(hit == (uint32_t)(entt::entity)good);
        CHECK_FALSE(UiSystem::HitTest(*s, parent, { 250, 250 }, hit));   // inside the inverted box's span: no hit
        CHECK_FALSE(UiSystem::HitTest(*s, parent, { 5, 50 }, hit));
        UiPointer ptr; ptr.Position = { 250, 250 }; ptr.Down = true; ptr.PressedEdge = true;
        CHECK_FALSE(UiSystem::Update(*s, parent, ptr));
        CHECK(flip.GetComponent<UiButtonComponent>().State == UiButtonState::Normal);
        CHECK(nanB.GetComponent<UiButtonComponent>().State == UiButtonState::Normal);
    }

    TEST_CASE("WO-09 C03: 2,000 controls — exact element count, topmost hit, one emit per click, timing")
    {
        Ref<Scene> s = Scene::Create();
        Entity canvas = MakeCanvas(*s);
        const UiRect viewport{ { 0, 0 }, { 1920, 1080 } };
        // A 50 x 40 grid of 30x20 buttons at 38x27 pitch (they do not overlap).
        std::vector<Entity> buttons;
        for (int y = 0; y < 40; ++y)
            for (int x = 0; x < 50; ++x)
            {
                const glm::vec2 min{ 2.0f + x * 38.0f, 2.0f + y * 27.0f };
                buttons.push_back(MakeButton(*s, canvas, "b", min, min + glm::vec2{ 30, 20 }, 0,
                                             ("sig" + std::to_string(y * 50 + x)).c_str()));
            }
        // One overlapping "modal" button on top of the grid centre (higher ZOrder).
        Entity modal = MakeButton(*s, canvas, "modal", { 900, 500 }, { 1000, 560 }, 100, "modal");
        REQUIRE(buttons.size() == 2000);

        std::vector<UiElement> els;
        const auto t0 = std::chrono::steady_clock::now();
        UiSystem::CollectElements(*s, viewport, els);
        const double msCollect = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(els.size() == 2001);
        // Back-to-front: the modal (ZOrder 100) is last.
        CHECK(els.back().Handle == (uint32_t)(entt::entity)modal);

        uint32_t hit = 0;
        CHECK(UiSystem::HitTest(*s, viewport, { 17, 12 }, hit));
        CHECK(hit == (uint32_t)(entt::entity)buttons[0]);
        CHECK(UiSystem::HitTest(*s, viewport, { 2.0f + 49 * 38.0f + 15, 2.0f + 39 * 27.0f + 10 }, hit));
        CHECK(hit == (uint32_t)(entt::entity)buttons[1999]);
        CHECK(UiSystem::HitTest(*s, viewport, { 950, 530 }, hit));   // over grid cell AND the modal
        CHECK(hit == (uint32_t)(entt::entity)modal);
        CHECK_FALSE(UiSystem::HitTest(*s, viewport, { 0.5f, 0.5f }, hit));   // the 2 px gutter

        // Exactly one signal per click: press + release over button 1234.
        int emits = 0, others = 0;
        s->Events().Connect("sig1234", [&](Entity) { ++emits; });
        s->Events().ConnectAny([&](const std::string& sig, Entity) { if (sig != "sig1234") ++others; });
        const glm::vec2 p{ 2.0f + 34 * 38.0f + 10, 2.0f + 24 * 27.0f + 10 };   // index 24*50+34 = 1234
        UiPointer down; down.Position = p; down.Down = true; down.PressedEdge = true;
        const auto t1 = std::chrono::steady_clock::now();
        CHECK(UiSystem::Update(*s, viewport, down));
        UiPointer up; up.Position = p; up.Down = false; up.ReleasedEdge = true;
        CHECK(UiSystem::Update(*s, viewport, up));
        const double msUpdate = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count() / 2.0;
        CHECK(emits == 1);
        CHECK(others == 0);
        int hovering = 0, pressed = 0;
        for (Entity b : buttons)
        {
            const auto& bc = b.GetComponent<UiButtonComponent>();
            hovering += bc.State == UiButtonState::Hover;
            pressed  += bc.State == UiButtonState::Pressed;
        }
        CHECK(hovering == 1);                                       // only 1234 (released over it)
        CHECK(pressed == 0);
        MESSAGE("C03 2,001 controls: CollectElements " << msCollect << " ms, Update " << msUpdate << " ms per frame");
        CHECK(msCollect < 10000.0);
    }

    TEST_CASE("WO-09 C03: canvas-scale extremes — ReferenceHeight 0 / 1e-30 / 1e30 / NaN / inf, viewport 0x0 / 1x1 / inverted")
    {
        CanvasComponent c;
        c.ScaleMode = UiScaleMode::ScaleWithHeight;
        const UiRect vp{ { 0, 0 }, { 1920, 1080 } };
        c.ReferenceHeight = 1080.0f; CHECK(UiSystem::CanvasScale(c, vp) == doctest::Approx(1.0f));
        c.ReferenceHeight = 0.0f;    CHECK(UiSystem::CanvasScale(c, vp) == 1080.0f);      // <= 1 reads as 1
        c.ReferenceHeight = -5.0f;   CHECK(UiSystem::CanvasScale(c, vp) == 1080.0f);
        c.ReferenceHeight = 1e-30f;  CHECK(UiSystem::CanvasScale(c, vp) == 1080.0f);
        c.ReferenceHeight = 1e30f;   CHECK(UiSystem::CanvasScale(c, vp) == doctest::Approx(1080.0f / 1e30f));
        c.ReferenceHeight = kNaN;    CHECK(UiSystem::CanvasScale(c, vp) == 1080.0f);      // NaN > 1 is false ⇒ 1
        c.ReferenceHeight = kInf;    CHECK(UiSystem::CanvasScale(c, vp) == 0.0f);
        c.ReferenceHeight = 1080.0f;
        CHECK(UiSystem::CanvasScale(c, UiRect{ { 0, 0 }, { 0, 0 } }) == 0.0f);            // 0x0 viewport ⇒ scale 0
        CHECK(UiSystem::CanvasScale(c, UiRect{ { 0, 0 }, { 1, 1 } }) == doctest::Approx(1.0f / 1080.0f));
        CHECK(UiSystem::CanvasScale(c, UiRect{ { 0, 0 }, { -1920, -1080 } }) == doctest::Approx(-1.0f)); // inverted ⇒ negative
        c.ScaleMode = UiScaleMode::ConstantPixel;
        CHECK(UiSystem::CanvasScale(c, UiRect{ { 0, 0 }, { 0, 0 } }) == 1.0f);

        // Through the scene at a 0x0 and a 1x1 viewport: collect + hit-test are finite.
        Ref<Scene> s = Scene::Create();
        Entity canvas = MakeCanvas(*s);
        canvas.GetComponent<CanvasComponent>().ScaleMode = UiScaleMode::ScaleWithHeight;
        Entity b = MakeButton(*s, canvas, "b", { 0, 0 }, { 100, 50 });
        std::vector<UiElement> els;
        UiSystem::CollectElements(*s, UiRect{ { 0, 0 }, { 0, 0 } }, els);
        REQUIRE(els.size() == 1);
        CHECK(els[0].Rect.Size() == glm::vec2(0.0f));               // scale 0 collapses the offsets
        uint32_t hit = 0;
        CHECK(UiSystem::HitTest(*s, UiRect{ { 0, 0 }, { 0, 0 } }, { 0, 0 }, hit));   // inclusive on the point
        CHECK(hit == (uint32_t)(entt::entity)b);
        UiSystem::CollectElements(*s, UiRect{ { 0, 0 }, { 1, 1 } }, els);
        REQUIRE(els.size() == 1);
        CHECK(els[0].Rect.Max.x == doctest::Approx(100.0f / 1080.0f));
    }

    TEST_CASE("WO-09 C03: depth probe")
    {
        // Runs alone as a CHILD of the depth ladder (COSMIC_WO09_DEPTH=<n>), or as
        // a tiny self-check (depth 8) inside the ordinary suite. Walks every
        // hierarchy path on a chain of n UI nodes; a stack overflow kills the child
        // and the ladder records it.
        int depth = 8;
        if (const char* d = std::getenv("COSMIC_WO09_DEPTH")) depth = std::atoi(d);
        REQUIRE(depth >= 1);
        Ref<Scene> s = Scene::Create();
        std::vector<Entity> nodes;
        std::printf("WO09_DEPTH_PROBE step=build\n"); std::fflush(stdout);
        Entity leaf = BuildChain(*s, depth, &nodes);
        const UiRect vp{ { 0, 0 }, { 4096, 4096 } };

        // COSMIC_WO09_PROBE_STEPS (optional, e.g. "world,destroy") limits the walkers so
        // each one's own ceiling can be measured; the ladder runs all of them.
        const std::string only = std::getenv("COSMIC_WO09_PROBE_STEPS") ? std::getenv("COSMIC_WO09_PROBE_STEPS") : "";
        auto step = [&only](const char* what) -> bool
        {
            if (!only.empty() && only.find(what) == std::string::npos) return false;
            std::printf("WO09_DEPTH_PROBE step=%s\n", what); std::fflush(stdout);
            return true;
        };
        // Beyond the ratified ceiling (Scene::kMaxHierarchyDepth = 4,096 nodes per
        // path) every walker must STOP, not crash: the UI walk lays out the first
        // 4,095 chain nodes (the canvas is node 0), the transform chain multiplies the
        // leaf's nearest 4,096 nodes, the prefab gather saves 4,096 entities and the
        // subtree destroy removes 4,096 and orphans the rest.
        const int cap = Scene::kMaxHierarchyDepth;
        std::vector<UiElement> els;
        if (step("collect"))
        {
            UiSystem::CollectElements(*s, vp, els);                   // 1) the UI walk
            CHECK(els.size() == (size_t)std::min(depth, cap - 1));
            uint32_t hit = 0;
            CHECK(UiSystem::HitTest(*s, vp, { 2048, 2048 }, hit));
        }
        if (step("world"))
        {
            const glm::mat4 world = s->GetWorldTransform(leaf);       // 2) the transform chain
            CHECK(world[3].x == doctest::Approx((float)std::min(depth, cap)));   // one unit per level
            CHECK(s->IsActiveInHierarchy(leaf));                      // 3) the guarded ancestor walk
        }
        if (step("prefab"))
        {
            const std::string prefab = (std::filesystem::temp_directory_path() / ("wo09-depth-" + std::to_string(depth) + ".cprefab")).string();
            CHECK(SceneSerializer::SavePrefab(*s, nodes.front(), prefab)); // 4) the prefab subtree gather
            const std::string text = [&] { std::ifstream in(prefab, std::ios::binary); return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()); }();
            size_t saved = 0;
            for (size_t p = text.find("\"id\":"); p != std::string::npos; p = text.find("\"id\":", p + 5)) ++saved;
            CHECK(saved == (size_t)std::min(depth, cap));
            std::error_code ec; std::filesystem::remove(prefab, ec);
        }
        if (step("destroy"))
        {
            s->DestroyEntity(nodes.front(), true);                    // 5) subtree destroy of the whole chain
            CHECK(s->GetRegistry().view<RectTransformComponent>().size() == (size_t)std::max(0, depth - cap));
            if (depth > cap)   // the first survivor was orphaned, not left with a dangling parent
                CHECK_FALSE(nodes[cap].GetComponent<RelationshipComponent>().Parent.IsValid());
        }
        std::printf("WO09_DEPTH_PROBE depth=%d ok\n", depth);
        std::fflush(stdout);
    }

    TEST_CASE("WO-09 C03: depth ladder — measure the hierarchy depth ceiling in a child per rung (evidence, not a gate)")
    {
        // Each child gets 120 s; a crash exit code is recorded as the ceiling evidence.
        // 4,095 / 4,096 / 4,097 straddle the ratified ceiling (Scene::kMaxHierarchyDepth);
        // 8,192 proves "beyond the ceiling is bounded" (SetParent is O(depth) per link,
        // so building a 16k+ chain alone exceeds a Debug child's budget — not a walker).
        const int rungs[] = { 64, 256, 1024, 4095, 4096, 4097, 8192 };
        std::string report;
        int deepestOk = 0, shallowestBad = 0;
        for (int n : rungs)
        {
            const ChildResult r = RunChild("--test-case=\"WO-09 C03: depth probe\" --no-intro --no-colors",
                                           "COSMIC_WO09_DEPTH", std::to_string(n), 120000);
            char line[256];
            std::snprintf(line, sizeof(line), "depth=%d exit=0x%08X%s seconds=%.2f\n", n, (unsigned)r.exit,
                          r.timedOut ? " TIMEOUT" : "", r.seconds);
            report += line;
            MESSAGE("C03 depth ladder: " << std::string(line));
            if (r.exit == 0 && !r.timedOut) deepestOk = n;
            else if (!shallowestBad) shallowestBad = n;
        }
        // Evidence file (when the runner points at an evidence dir).
        if (!EvidenceDir().empty())
        {
#if defined(NDEBUG)
            const char* cfg = "Release";
#else
            const char* cfg = "Debug";
#endif
            std::ofstream f(std::filesystem::path(EvidenceDir()) / (std::string("c03-depth-ladder-") + cfg + ".txt"), std::ios::trunc);
            f << "# WO-09 C03 hierarchy depth ladder (" << cfg << ") — exit 0 = every walker survived; 0xC00000FD = stack overflow\n" << report;
        }
        // The catalog's documented guard depth must survive in BOTH configurations.
        // Every rung must survive now: at and past the ceiling the walkers stop instead
        // of crashing (the probe asserts the truncation counts itself).
        CHECK_MESSAGE(deepestOk >= 8192, "a rung did not survive every hierarchy walker (deepest ok = " << deepestOk << ", first failing = " << shallowestBad << ")");
        MESSAGE("C03 depth ladder: deepest surviving rung = " << deepestOk << ", first failing rung = " << shallowestBad);
    }

    TEST_CASE("WO-09 C03: hierarchy CYCLES terminate in every walker (UI collect, world transform, ancestry, prefab gather, destroy)")
    {
        Ref<Scene> s = Scene::Create();
        Entity canvas = MakeCanvas(*s);
        Entity a = MakeButton(*s, canvas, "A", { 0, 0 }, { 100, 100 });
        Entity b = MakeButton(*s, a, "B", { 0, 0 }, { 50, 50 });
        // SetParent refuses the cycle (documented); author it directly like a rogue
        // script would: A -> B -> A through both Children and Parent links.
        CHECK_FALSE(s->SetParent(a, b, false));
        auto& ra = a.GetComponent<RelationshipComponent>();
        auto& rb = b.GetComponent<RelationshipComponent>();
        rb.Children.push_back(a.GetComponent<IDComponent>().ID);
        ra.Parent = b.GetComponent<IDComponent>().ID;              // A's parent is now B (and canvas lost A)
        // Also a self-cycle on a third node.
        Entity c = MakeButton(*s, canvas, "C", { 0, 0 }, { 10, 10 });
        c.GetComponent<RelationshipComponent>().Children.push_back(c.GetComponent<IDComponent>().ID);

        const UiRect vp{ { 0, 0 }, { 800, 600 } };
        std::vector<UiElement> els;
        const auto t0 = std::chrono::steady_clock::now();
        UiSystem::CollectElements(*s, vp, els);                       // must return
        CHECK(els.size() <= 4u * 4096u);                              // bounded, not unbounded
        uint32_t hit = 0;
        UiSystem::HitTest(*s, vp, { 5, 5 }, hit);
        (void)s->GetWorldTransform(a);                                // parent cycle A <-> B
        (void)s->GetWorldTransform(b);
        CHECK(s->IsActiveInHierarchy(a));                             // guarded walk
        CHECK_FALSE(s->IsAncestor(canvas, a));                        // must terminate: A's chain never reaches the canvas
        const std::string prefab = (std::filesystem::temp_directory_path() / "wo09-cycle.cprefab").string();
        CHECK(SceneSerializer::SavePrefab(*s, a, prefab));
        std::error_code ec; std::filesystem::remove(prefab, ec);
        s->DestroyEntity(a, true);                                    // A -> B -> A
        CHECK_FALSE(s->GetRegistry().valid((entt::entity)a));
        CHECK_FALSE(s->GetRegistry().valid((entt::entity)b));
        s->DestroyEntity(c, true);                                    // self-cycle
        CHECK_FALSE(s->GetRegistry().valid((entt::entity)c));
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(ms < 10000.0);
        MESSAGE("C03 cycles: every walker returned in " << ms << " ms");
    }
}
