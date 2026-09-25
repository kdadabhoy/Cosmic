// test_ap03_editor.cpp — AP-03 (App Platform): the headless halves of E03 and E08
// plus the ScreenScaffold text edits behind E02 / F01.
//
//   E03 U — UiRectGizmoMath: handle rects, hit-test priority, snap to 1/8 px and to the
//           16 px grid (grid wins), a move drag of (40, -20) px -> offsets change by exactly
//           that, a bottom-right resize changes OffsetMax only, the 1 px minimum size, the
//           canvas scale divides the offset delta. Bit-exact.
//   E08 U — SourceLocator against a scratch project tree: ForScriptClass finds the stub
//           file (+ line), ForService resolves an in-exe CS_SERVICE's file:line, ForPanel a
//           CS_PANEL's file:line through a PanelRegistry, ForChannel through Producer,
//           ForSignal lists every file with the quoted string, unresolved hits carry a
//           reason; the COSMIC_AP03_RECORD_SHELL seam records instead of launching.
//   E02 U — ScreenScaffold::InsertIntoModule: include + CS_SCRIPT between the markers,
//           idempotent, refused with the documented message when the markers are missing.
//
// The Starforge TUs under test are compiled from the Starforge tree (tests/CMakeLists.txt),
// not copied: the thing under test is the code the editor ships.

#include <doctest.h>

#include "../Projects/Starforge/src/UiRectGizmo.h"
#include "../Projects/Starforge/src/SourceLocator.h"
#include "../Projects/Starforge/src/ScreenScaffold.h"

#include "data/DataBus.h"
#include "scripting/AppService.h"
#include "scripting/ModuleRegistry.h"
#include "graphics/Gizmo.h"          // UX-02 ED01 — Gizmo::ApplyModel
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/ui/UiComponents.h"
#include "scene/SceneSerializer.h"
#include "../Projects/Starforge/src/EditorPrefs.h"   // UX-02 ED05 — the prefs round trip

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace Starforge;
using Cosmic::UiRect;

namespace
{
    fs::path ScratchRoot(const char* name)
    {
        std::error_code ec;
        fs::path root = fs::temp_directory_path(ec) / "cosmic-ap03" / name;
        fs::remove_all(root, ec);
        fs::create_directories(root / "src" / "screens", ec);
        fs::create_directories(root / "src" / "services", ec);
        return root;
    }
    void Put(const fs::path& p, const std::string& text)
    {
        std::error_code ec; fs::create_directories(p.parent_path(), ec);
        std::ofstream f(p, std::ios::binary | std::ios::trunc); f << text;
    }
    std::string Get(const fs::path& p)
    {
        std::ifstream f(p, std::ios::binary); std::string s((std::istreambuf_iterator<char>(f)), {}); return s;
    }
    UiRect R(float x0, float y0, float x1, float y1) { UiRect r; r.Min = { x0, y0 }; r.Max = { x1, y1 }; return r; }

    // An in-exe service so ForService / ForPanel / ForChannel resolve real registrations.
    class Ap03ProbeService final : public Cosmic::AppService
    {
    protected:
        void OnAttach(Cosmic::AppContext&) override {}
    };
}

TEST_SUITE("AP-03 editor units")
{
    TEST_CASE("E03 U UiRectGizmoMath: handle rects + hit-test priority")
    {
        const UiRect r = R(100, 50, 300, 150);
        const float h = UiRectGizmoMath::kHandleHalf;
        CHECK(UiRectGizmoMath::HandleRect(r, RectHandle::NW).Min == glm::vec2(100 - h, 50 - h));
        CHECK(UiRectGizmoMath::HandleRect(r, RectHandle::SE).Max == glm::vec2(300 + h, 150 + h));
        CHECK(UiRectGizmoMath::HandleRect(r, RectHandle::N).Center() == glm::vec2(200, 50));
        CHECK(UiRectGizmoMath::HandleRect(r, RectHandle::E).Center() == glm::vec2(300, 100));
        CHECK(UiRectGizmoMath::HandleRect(r, RectHandle::Move).Min == r.Min);

        CHECK(UiRectGizmoMath::HitTest(r, { 300, 150 }) == RectHandle::SE);   // corner beats Move
        CHECK(UiRectGizmoMath::HitTest(r, { 100, 100 }) == RectHandle::W);
        CHECK(UiRectGizmoMath::HitTest(r, { 200, 50 })  == RectHandle::N);
        CHECK(UiRectGizmoMath::HitTest(r, { 200, 100 }) == RectHandle::Move);
        CHECK(UiRectGizmoMath::HitTest(r, { 10, 10 })   == RectHandle::None);
        CHECK(UiRectGizmoMath::HitTest(r, { 300 + h + 0.5f, 100 }) == RectHandle::None);
    }

    TEST_CASE("E03 U UiRectGizmoMath: snap 1/8 px, 16 px grid (grid wins), identity")
    {
        RectGizmoSnap none;
        CHECK(UiRectGizmoMath::Snap(13.3f, none) == 13.3f);
        RectGizmoSnap eighth; eighth.PixelEighth = true;
        CHECK(UiRectGizmoMath::Snap(13.3f, eighth) == 13.25f);
        CHECK(UiRectGizmoMath::Snap(13.44f, eighth) == 13.5f);
        CHECK(UiRectGizmoMath::Snap(-0.06f, eighth) == 0.0f);
        RectGizmoSnap grid; grid.Grid16 = true;
        CHECK(UiRectGizmoMath::Snap(23.0f, grid) == 16.0f);
        CHECK(UiRectGizmoMath::Snap(25.0f, grid) == 32.0f);
        RectGizmoSnap both; both.Grid16 = true; both.PixelEighth = true;
        CHECK(UiRectGizmoMath::Snap(23.3f, both) == 16.0f);   // grid wins
    }

    TEST_CASE("E03 U UiRectGizmoMath: move drag (40,-20) -> offsets exactly, size kept, anchors untouched by construction")
    {
        UiRectGizmoMath::DragInput in;
        in.Handle = RectHandle::Move;
        in.StartRect = R(100, 50, 300, 150);
        in.StartOffsetMin = { 10, 20 }; in.StartOffsetMax = { 210, 120 };
        in.Delta = { 40, -20 };
        const auto out = UiRectGizmoMath::ApplyDrag(in);
        CHECK(out.OffsetMin == glm::vec2(50, 0));
        CHECK(out.OffsetMax == glm::vec2(250, 100));
        CHECK(out.Rect.Min == glm::vec2(140, 30));
        CHECK(out.Rect.Size() == in.StartRect.Size());

        // canvas scale 2: a 40 px screen move is 20 offset px
        in.Scale = 2.0f;
        const auto scaled = UiRectGizmoMath::ApplyDrag(in);
        CHECK(scaled.OffsetMin == glm::vec2(30, 10));
        CHECK(scaled.OffsetMax == glm::vec2(230, 110));

        // snapped move: the top-left lands on the grid, the size is unchanged
        in.Scale = 1.0f; in.Snap.Grid16 = true; in.Delta = { 5, 5 };
        const auto snapped = UiRectGizmoMath::ApplyDrag(in);
        CHECK(snapped.Rect.Min == glm::vec2(112, 48));
        CHECK(snapped.Rect.Size() == glm::vec2(200, 100));
        CHECK(snapped.OffsetMin == glm::vec2(22, 18));
        CHECK(snapped.OffsetMax == glm::vec2(222, 118));
    }

    TEST_CASE("E03 U UiRectGizmoMath: bottom-right resize changes OffsetMax only; min size 1 px; left edge yields")
    {
        UiRectGizmoMath::DragInput in;
        in.Handle = RectHandle::SE;
        in.StartRect = R(100, 50, 300, 150);
        in.StartOffsetMin = { 10, 20 }; in.StartOffsetMax = { 210, 120 };
        in.Delta = { 15, 7 };
        const auto out = UiRectGizmoMath::ApplyDrag(in);
        CHECK(out.OffsetMin == in.StartOffsetMin);
        CHECK(out.OffsetMax == glm::vec2(225, 127));
        CHECK(out.Rect.Max == glm::vec2(315, 157));

        // collapse past the opposite edge: clamps to 1 px, the dragged edge yields
        in.Delta = { -500, -500 };
        const auto tiny = UiRectGizmoMath::ApplyDrag(in);
        CHECK(tiny.Rect.Size() == glm::vec2(1, 1));
        CHECK(tiny.Rect.Min == in.StartRect.Min);
        CHECK(tiny.OffsetMax == glm::vec2(11, 21));

        // west handle: only OffsetMin.x moves
        in.Handle = RectHandle::W; in.Delta = { 30, 999 };
        const auto west = UiRectGizmoMath::ApplyDrag(in);
        CHECK(west.OffsetMin == glm::vec2(40, 20));
        CHECK(west.OffsetMax == in.StartOffsetMax);
        // north-west past the far corner: both min edges clamp to 1 px from max
        in.Handle = RectHandle::NW; in.Delta = { 1000, 1000 };
        const auto nw = UiRectGizmoMath::ApplyDrag(in);
        CHECK(nw.Rect.Min == glm::vec2(299, 149));
        CHECK(nw.OffsetMax == in.StartOffsetMax);
    }

    TEST_CASE("E02 U ScreenScaffold::InsertIntoModule: include + CS_SCRIPT between the markers, idempotent, refused without markers")
    {
        std::string mod =
            "#include <Cosmic.h>\n\n#include \"services/AppService.h\"\n\nCS_MODULE_BEGIN(Demo)\n"
            "    CS_SERVICE(AppService).Order(0) CS_END;\n\n"
            "    // CS_SCREENS_BEGIN — managed by Starforge\n"
            "    // CS_SCREENS_END\n"
            "CS_MODULE_END()\n";
        std::string err;
        REQUIRE(ScreenScaffold::InsertIntoModule(mod, "Telemetry", &err));
        CHECK(mod.find("#include \"services/AppService.h\"\n#include \"screens/TelemetryScreen.h\"\n") != std::string::npos);
        const size_t b = mod.find("CS_SCREENS_BEGIN"), scr = mod.find("CS_SCRIPT(TelemetryScreen)"), e = mod.find("CS_SCREENS_END");
        REQUIRE(scr != std::string::npos);
        CHECK(b < scr); CHECK(scr < e);
        CHECK(mod.find("    CS_SCRIPT(TelemetryScreen)\n        CS_FIELD(ExampleField)\n    CS_END;\n    // CS_SCREENS_END") != std::string::npos);
        CHECK(ScreenScaffold::ModuleHasScript(mod, "Telemetry"));
        // idempotent
        const std::string once = mod;
        REQUIRE(ScreenScaffold::InsertIntoModule(mod, "Telemetry", &err));
        CHECK(mod == once);
        // the include lands before CS_MODULE_BEGIN, never inside the module block
        CHECK(mod.find("#include \"screens/TelemetryScreen.h\"") < mod.find("CS_MODULE_BEGIN"));

        std::string bare = "#include <Cosmic.h>\nCS_MODULE_BEGIN(Demo)\nCS_MODULE_END()\n";
        const std::string before = bare;
        CHECK_FALSE(ScreenScaffold::InsertIntoModule(bare, "Telemetry", &err));
        CHECK(err == ScreenScaffold::kNoMarkersMessage);
        CHECK(err == "Module.cpp has no CS_SCREENS markers");
        CHECK(bare == before);

        CHECK(ScreenScaffold::RenderStub("class @SCREEN@Screen // @PROJECT_NAME@", "Lab", "Demo") == "class LabScreen // Demo");
        CHECK(ScreenScaffold::ValidName("Telemetry"));
        CHECK_FALSE(ScreenScaffold::ValidName("2Fast"));
        CHECK_FALSE(ScreenScaffold::ValidName("a b"));
        CHECK_FALSE(ScreenScaffold::ValidName(""));
    }

    TEST_CASE("E02 U ScreenScaffold::CreateScript on disk: refused without markers changes nothing; stub -> header + module")
    {
        const fs::path root = ScratchRoot("scaffold");
        const fs::path stub = root / "ScreenScript.h.in";
        Put(stub, "#pragma once\n// @SCREEN@ screen of @PROJECT_NAME@\nclass @SCREEN@Screen : public Cosmic::ScriptableEntity {};\n");
        Put(root / "src" / "Module.cpp", "#include <Cosmic.h>\nCS_MODULE_BEGIN(P)\nCS_MODULE_END()\n");
        const std::string moduleBefore = Get(root / "src" / "Module.cpp");
        auto r = ScreenScaffold::CreateScript(root.generic_string(), "Telemetry", "P", stub.generic_string());
        CHECK_FALSE(r.Ok);
        CHECK(r.Message == ScreenScaffold::kNoMarkersMessage);
        CHECK(Get(root / "src" / "Module.cpp") == moduleBefore);
        CHECK_FALSE(fs::exists(root / "src" / "screens" / "TelemetryScreen.h"));

        Put(root / "src" / "Module.cpp", "#include <Cosmic.h>\nCS_MODULE_BEGIN(P)\n    // CS_SCREENS_BEGIN\n    // CS_SCREENS_END\nCS_MODULE_END()\n");
        r = ScreenScaffold::CreateScript(root.generic_string(), "Telemetry", "P", stub.generic_string());
        REQUIRE(r.Ok);
        const std::string header = Get(root / "src" / "screens" / "TelemetryScreen.h");
        CHECK(header.find("class TelemetryScreen") != std::string::npos);
        CHECK(header.find("of P\n") != std::string::npos);
        CHECK(header.find('@') == std::string::npos);
        const std::string mod = Get(root / "src" / "Module.cpp");
        CHECK(mod.find("#include \"screens/TelemetryScreen.h\"") != std::string::npos);
        CHECK(mod.find("CS_SCRIPT(TelemetryScreen)") != std::string::npos);

        // E08 through the scaffolded tree: ForScreen / ForScriptClass find the header + line
        const SourceLocator loc(root.generic_string());
        const SourceHit hs = loc.ForScreen("Telemetry");
        REQUIRE(hs.Resolved());
        CHECK(hs.Path == SourceLocator::Normalize((root / "src" / "screens" / "TelemetryScreen.h").generic_string()));
        CHECK(hs.Line == 3);
        CHECK(loc.ForScriptClass("TelemetryScreen").Path == hs.Path);
        CHECK(loc.ForScriptClass("TelemetryScreen").Line == 3);
        std::error_code ec; fs::remove_all(root, ec);
    }

    TEST_CASE("E08 U SourceLocator: service / panel / channel / signal / unresolved reasons / record seam")
    {
        const fs::path root = ScratchRoot("locator");
        // Register an in-exe service whose File is THIS test file (CS_SERVICE records __FILE__/__LINE__).
        const int regLine = __LINE__ + 1;
        Cosmic::ModuleRegistry::Get().AddService<Ap03ProbeService>("Ap03ProbeService", __FILE__, regLine);

        Put(root / "src" / "services" / "Ap03ProbeService.h", "#pragma once\nclass Ap03ProbeService;\n\nclass Ap03ProbeService : public Cosmic::AppService {};\n");
        Put(root / "src" / "screens" / "HomeScreen.h", "class HomeScreen {\n  void f() { Emit(\"counter.increment\"); }\n};\n");
        Put(root / "src" / "services" / "Handlers.cpp", "// handlers\nif (signal == \"counter.increment\") {}\n");
        Put(root / "src" / "Other.cpp", "// nothing here\n");

        const SourceLocator loc(root.generic_string());

        // ForService: the registration site wins (file:line of the AddService call)
        const SourceHit svc = loc.ForService("Ap03ProbeService");
        REQUIRE(svc.Resolved());
        CHECK(svc.Path == SourceLocator::Normalize(__FILE__));
        CHECK(svc.Line == regLine);
        // an unregistered service falls back to the class scan (skipping the forward declaration)
        const SourceHit scan = loc.ForService("NotRegisteredButDeclared");
        CHECK_FALSE(scan.Resolved());
        CHECK_FALSE(scan.Reason.empty());
        Put(root / "src" / "services" / "Late.h", "class NotRegisteredButDeclared;\nclass NotRegisteredButDeclared : public X {};\n");
        const SourceHit scan2 = loc.ForService("NotRegisteredButDeclared");
        REQUIRE(scan2.Resolved());
        CHECK(scan2.Line == 2);

        // ForPanel via a PanelRegistry (what CS_PANEL expands to: Register(name, fn, __FILE__, __LINE__))
        Cosmic::DataBus bus;
        Cosmic::PanelRegistry panels;
        const int panelLine = __LINE__ + 1;
        panels.Register("Ap03Probe", [](const Cosmic::UiRect&) {}, __FILE__, panelLine);
        REQUIRE(panels.Has("Ap03Probe"));
        const SourceHit pan = loc.ForPanel("Ap03Probe", panels);
        REQUIRE(pan.Resolved());
        CHECK(pan.Path == SourceLocator::Normalize(__FILE__));
        CHECK(pan.Line == panelLine);
        const SourceHit nopan = loc.ForPanel("Nope", panels);
        CHECK_FALSE(nopan.Resolved());
        CHECK(nopan.Reason.find("not registered") != std::string::npos);

        // ForChannel: Producer -> ForService
        bus.SetProducer("Ap03ProbeService");
        bus.Set("probe.value", 1.0);
        bus.SetProducer("");
        bus.Set("orphan.value", 2.0);
        const SourceHit ch = loc.ForChannel("probe.value", bus);
        REQUIRE(ch.Resolved());
        CHECK(ch.Path == svc.Path);
        CHECK(ch.Line == svc.Line);
        CHECK_FALSE(loc.ForChannel("orphan.value", bus).Resolved());
        CHECK(loc.ForChannel("orphan.value", bus).Reason.find("producer") != std::string::npos);
        CHECK(loc.ForChannel("missing.value", bus).Reason.find("no value") != std::string::npos);

        // ForSignal: every file containing the quoted string, none for a stranger
        const auto hits = loc.ForSignal("counter.increment");
        REQUIRE(hits.size() == 2);
        CHECK(hits[0].Path.find("HomeScreen.h") != std::string::npos);   // sorted: screens/ before services/
        CHECK(hits[0].Line == 2);
        CHECK(hits[1].Path.find("Handlers.cpp") != std::string::npos);
        CHECK(hits[1].Line == 2);
        CHECK(loc.ForSignal("no.such.signal").empty());

        // unresolved reasons
        CHECK_FALSE(loc.ForScriptClass("Ghost").Resolved());
        CHECK(loc.ForScriptClass("Ghost").Reason.find("Ghost") != std::string::npos);
        CHECK(loc.ForScreen("Ghost").Reason.find("Create script") != std::string::npos);
        CHECK_FALSE(SourceLocator::Open(SourceHit{}));
        CHECK_FALSE(SourceLocator::Reveal(SourceHit{}));

        // the record seam: nothing launches, the file + list get the absolute path
        const fs::path rec = root / "shell.txt";
        _putenv_s("COSMIC_AP03_RECORD_SHELL", rec.generic_string().c_str());
        SourceLocator::ClearRecorded();
        CHECK(SourceLocator::Recording());
        CHECK(SourceLocator::Open(svc));
        CHECK(SourceLocator::Reveal(pan));
        REQUIRE(SourceLocator::Recorded().size() == 2);
        CHECK(SourceLocator::Recorded()[0].Kind == "open");
        CHECK(SourceLocator::Recorded()[0].Path == svc.Path);
        CHECK(SourceLocator::Recorded()[0].Line == svc.Line);
        CHECK(SourceLocator::Recorded()[1].Kind == "reveal");
        const std::string recorded = Get(rec);
        CHECK(recorded.find("open\t" + svc.Path + "\t" + std::to_string(svc.Line)) != std::string::npos);
        CHECK(recorded.find("reveal\t" + pan.Path) != std::string::npos);
        _putenv_s("COSMIC_AP03_RECORD_SHELL", "");
        CHECK_FALSE(SourceLocator::Recording());
        SourceLocator::ClearRecorded();

        std::error_code ec; fs::remove_all(root, ec);
    }
}

// ============================================================================
// UX-02 (UX & Shipping) — the headless halves of ED01, ED03, ED04, ED05.
// ============================================================================
TEST_SUITE("UX-02 editor units")
{
    TEST_CASE("UX-02 ED01 Gizmo::ApplyModel 2D: 30 deg Z -> Rotation.z = 30 +- 1e-4, UseQuatRotation false, RotationQuat untouched")
    {
        Cosmic::TransformComponent t;
        t.Position = { 1.0f, 2.0f, 0.5f };
        t.Scale    = { 2.0f, 3.0f, 1.0f };
        t.RotationQuat = glm::quat(0.8f, 0.1f, 0.2f, 0.3f);   // a sentinel the 2D write must not touch
        const glm::quat sentinel = t.RotationQuat;

        // The matrix the Z ring hands back after a 30 degree turn (translation + scale kept).
        Cosmic::TransformComponent edited = t;
        edited.Rotation.z = 30.0f;
        Cosmic::Gizmo::ApplyModel(t, edited.GetTransform(), /*mode2D=*/true);
        CHECK(std::abs(t.Rotation.z - 30.0f) <= 1e-4f);
        CHECK(t.Rotation.x == 0.0f);
        CHECK(t.Rotation.y == 0.0f);
        CHECK_FALSE(t.UseQuatRotation);
        CHECK(t.RotationQuat.w == sentinel.w);
        CHECK(t.RotationQuat.x == sentinel.x);
        CHECK(t.RotationQuat.y == sentinel.y);
        CHECK(t.RotationQuat.z == sentinel.z);
        CHECK(std::abs(t.Position.x - 1.0f) <= 1e-5f);
        CHECK(std::abs(t.Position.y - 2.0f) <= 1e-5f);
        CHECK(std::abs(t.Position.z - 0.5f) <= 1e-5f);
        CHECK(std::abs(t.Scale.x - 2.0f) <= 1e-5f);
        CHECK(std::abs(t.Scale.y - 3.0f) <= 1e-5f);

        // Continuity: 350 deg turned by +20 lands on 370, not on 10 (no 360 jump mid-drag).
        Cosmic::TransformComponent w; w.Rotation.z = 350.0f;
        Cosmic::TransformComponent w2 = w; w2.Rotation.z = 370.0f;
        Cosmic::Gizmo::ApplyModel(w, w2.GetTransform(), true);
        CHECK(std::abs(w.Rotation.z - 370.0f) <= 1e-3f);

        // Kept X/Y Euler factors are divided out before the Z angle is read.
        Cosmic::TransformComponent k; k.Rotation = { 20.0f, -10.0f, 5.0f };
        Cosmic::TransformComponent k2 = k; k2.Rotation.z = 50.0f;
        Cosmic::Gizmo::ApplyModel(k, k2.GetTransform(), true);
        CHECK(std::abs(k.Rotation.z - 50.0f) <= 1e-3f);
        CHECK(k.Rotation.x == 20.0f);
        CHECK(k.Rotation.y == -10.0f);

        // 3D keeps the quaternion path (unchanged behaviour).
        Cosmic::TransformComponent q;
        Cosmic::Gizmo::ApplyModel(q, edited.GetTransform(), /*mode2D=*/false);
        CHECK(q.UseQuatRotation);
        const glm::quat expect = glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        CHECK(std::abs(std::abs(glm::dot(q.RotationQuat, expect)) - 1.0f) <= 1e-5f);
        CHECK(q.Rotation.z == 0.0f);
    }

    TEST_CASE("UX-02 ED01 KI-73 UiRectGizmoMath::CapturesMove: the centre square captures whether or not the selection is topmost; the rest of the rect only when topmost")
    {
        const UiRect r = R(100.0f, 100.0f, 300.0f, 200.0f);   // centre (200, 150)
        const UiRect sq = UiRectGizmoMath::MoveHandleRect(r);
        CHECK(sq.Min.x == 195.0f); CHECK(sq.Min.y == 145.0f);
        CHECK(sq.Max.x == 205.0f); CHECK(sq.Max.y == 155.0f);
        // the centre square: captured even when another element is drawn over it (the KI-73 case)
        CHECK(UiRectGizmoMath::CapturesMove(r, { 200.0f, 150.0f }, false));
        CHECK(UiRectGizmoMath::CapturesMove(r, { 204.0f, 154.0f }, false));
        CHECK(UiRectGizmoMath::CapturesMove(r, { 200.0f, 150.0f }, true));
        // elsewhere inside the rect: only when topmost (clicking an overlapping element still selects it)
        CHECK_FALSE(UiRectGizmoMath::CapturesMove(r, { 207.0f, 150.0f }, false));
        CHECK(UiRectGizmoMath::CapturesMove(r, { 207.0f, 150.0f }, true));
        CHECK_FALSE(UiRectGizmoMath::CapturesMove(r, { 120.0f, 120.0f }, false));
        CHECK(UiRectGizmoMath::CapturesMove(r, { 120.0f, 120.0f }, true));
        // outside the rect: never the Move surface
        CHECK_FALSE(UiRectGizmoMath::CapturesMove(r, { 50.0f, 50.0f }, true));
        // the centre is still the Move surface in HitTest (resize squares win only on themselves)
        CHECK(UiRectGizmoMath::HitTest(r, { 200.0f, 150.0f }) == RectHandle::Move);
    }

    TEST_CASE("UX-02 ED01 UiRectGizmo::Owns: RectTransform or Canvas -> the rect gizmo; a world sprite -> the transform gizmo")
    {
        Cosmic::Scene s;
        Cosmic::Entity canvas = s.CreateEntity("Canvas");
        canvas.AddComponent<Cosmic::CanvasComponent>();
        Cosmic::Entity el = s.CreateEntity("Plot");
        el.AddComponent<Cosmic::RectTransformComponent>();
        el.AddComponent<Cosmic::UiPlotComponent>();
        Cosmic::Entity sprite = s.CreateEntity("Sprite");
        sprite.AddComponent<Cosmic::SpriteRendererComponent>();
        CHECK(UiRectGizmo::Owns(canvas));
        CHECK(UiRectGizmo::Owns(el));
        CHECK_FALSE(UiRectGizmo::Owns(sprite));
        CHECK_FALSE(UiRectGizmo::Owns(Cosmic::Entity{}));
    }

    TEST_CASE("UX-02 ED03 SourceLocator::ForSignal(back_clicked) on PendulumLab: src hits, then the Flow hit Settings -> Lab with state/target/indices; @quit/@pop/push shown as written")
    {
        const std::string pl = COSMIC_PENDULUMLAB_DIR;
        const SourceLocator loc(pl);
        CHECK(loc.StartupFlowRel() == "flows/Main.cflow");
        const auto hits = loc.ForSignal("back_clicked");
        size_t src = 0, flow = 0; bool srcAfterFlow = false;
        for (const SourceHit& h : hits)
        {
            if (h.IsFlow()) ++flow;
            else { ++src; if (flow) srcAfterFlow = true; }
        }
        CHECK(src >= 1);                 // src/Y02SelfTest.cpp feeds "back_clicked"
        CHECK_FALSE(srcAfterFlow);       // the Flow hits come after every src/ hit
        REQUIRE(flow == 1);
        const SourceHit& f = hits.back();
        CHECK(f.Kind == SourceHitKind::Flow);
        CHECK(f.State == "Settings");
        CHECK(f.Target == "Lab");
        CHECK(f.Signal == "back_clicked");
        CHECK(f.Line == 0);
        CHECK(f.Path == SourceLocator::Normalize(pl + "/flows/Main.cflow"));
        CHECK(f.FlowVfs == "project://flows/Main.cflow");
        CHECK(f.StateIndex == 2);
        CHECK(f.TransitionIndex == 0);
        CHECK(f.FlowLine() == "Flow: Settings \xE2\x80\x94" "back_clicked\xE2\x86\x92 Lab");
        CHECK(f.Reason.find(f.FlowLine()) != std::string::npos);
        // every src hit is still a Source hit with a 1-based line (E08's contract)
        for (const SourceHit& h : hits) if (!h.IsFlow()) { CHECK(h.Line >= 1); CHECK(h.Kind == SourceHitKind::Source); }

        // targets as written
        const auto q = loc.FlowHitsForSignal("quit_clicked");
        REQUIRE(q.size() == 1);
        CHECK(q[0].State == "Home"); CHECK(q[0].Target == "@quit");
        const auto r = loc.FlowHitsForSignal("resume_clicked");
        REQUIRE(r.size() == 1);
        CHECK(r[0].State == "Stopped"); CHECK(r[0].Target == "@pop");
        const auto w = loc.FlowHitsForSignal("when");
        REQUIRE(w.size() == 1);
        CHECK(w[0].State == "Lab"); CHECK(w[0].Target == "push Stopped");
        CHECK(loc.FlowHitsForSignal("settings_clicked").size() == 2);   // Home + Lab
        CHECK(loc.FlowHitsForSignal("no.such.signal").empty());
        CHECK(loc.FlowHitsForSignal("").empty());

        // a project without project.cproj / startup_flow contributes no Flow hits (E08's trees)
        const fs::path bare = ScratchRoot("ux02-noflow");
        Put(bare / "src" / "A.cpp", "Emit(\"back_clicked\");\n");
        const auto bh = SourceLocator(bare.generic_string()).ForSignal("back_clicked");
        REQUIRE(bh.size() == 1);
        CHECK_FALSE(bh[0].IsFlow());
        Put(bare / "project.cproj", "name = \"Bare\"\n");
        CHECK(SourceLocator(bare.generic_string()).ForSignal("back_clicked").size() == 1);
        std::error_code ec; fs::remove_all(bare, ec);
    }

    TEST_CASE("UX-02 ED04 SourceLocator::ProjectScenes: the ux02 fixture lists exactly Main, overlays/Pause, SpriteUnderUi (recursive, sorted, *.bak never)")
    {
        const std::string fx = COSMIC_UX02_FIXTURE_DIR;
        const auto v = SourceLocator::ProjectScenes(fx);
        REQUIRE(v.size() == 3);
        CHECK(v[0] == "project://scenes/Main.cscene");
        CHECK(v[1] == "project://scenes/overlays/Pause.cscene");
        CHECK(v[2] == "project://scenes/SpriteUnderUi.cscene");
        std::error_code ec;
        CHECK(fs::exists(fs::path(fx) / "scenes" / "Main.cscene.bak", ec));   // the negative case is real
        for (const std::string& s : v) CHECK(s.find(".bak") == std::string::npos);

        // the fixture's scenes load, and the sprite scene is what ED01 needs: one sprite under a
        // full-screen opaque UiImage
        for (const std::string& s : v)
        {
            Cosmic::Ref<Cosmic::Scene> sc = Cosmic::Scene::Create();
            CHECK_MESSAGE(Cosmic::SceneSerializer::Load(*sc, fx + "/" + s.substr(10)), s);
        }
        {
            Cosmic::Ref<Cosmic::Scene> sc = Cosmic::Scene::Create();
            REQUIRE(Cosmic::SceneSerializer::Load(*sc, fx + "/scenes/SpriteUnderUi.cscene"));
            auto& reg = sc->GetRegistry();
            int sprites = 0; bool opaqueFull = false;
            for (auto e : reg.view<Cosmic::SpriteRendererComponent>()) { (void)e; ++sprites; }
            for (auto e : reg.view<Cosmic::UiImageComponent, Cosmic::RectTransformComponent>())
            {
                const auto& rt = reg.get<Cosmic::RectTransformComponent>(e);
                const auto& im = reg.get<Cosmic::UiImageComponent>(e);
                opaqueFull = opaqueFull || (rt.AnchorMin == glm::vec2(0.0f) && rt.AnchorMax == glm::vec2(1.0f) && im.Tint.a == 1.0f);
            }
            CHECK(sprites == 1);
            CHECK(opaqueFull);
        }

        // a scratch tree: nesting, other extensions, a backup, case-insensitive order
        const fs::path root = ScratchRoot("ux02-scenes");
        Put(root / "scenes" / "b.cscene", "{}");
        Put(root / "scenes" / "A" / "z.cscene", "{}");
        Put(root / "scenes" / "deep" / "er" / "y.cscene", "{}");
        Put(root / "scenes" / "notes.txt", "x");
        Put(root / "scenes" / "b.cscene.bak", "{}");
        const auto s2 = SourceLocator::ProjectScenes(root.generic_string());
        REQUIRE(s2.size() == 3);
        CHECK(s2[0] == "project://scenes/A/z.cscene");
        CHECK(s2[1] == "project://scenes/b.cscene");
        CHECK(s2[2] == "project://scenes/deep/er/y.cscene");
        CHECK(SourceLocator::ProjectScenes("").empty());
        CHECK(SourceLocator::ProjectScenes((root / "missing").generic_string()).empty());
        fs::remove_all(root, ec);
    }

    TEST_CASE("UX-02 ED05 EditorPrefs: editor.toml round trip of autosave_enabled / autosave_minutes / prompt_unsaved; defaults when absent; legacy float minutes")
    {
        const fs::path root = ScratchRoot("ux02-prefs");
        const std::string p = (root / "starforge" / "editor.toml").generic_string();
        CHECK(p != Prefs::PrefsPath());   // never the real editor.toml

        const Prefs::EditorSettings d = Prefs::LoadSettingsFrom(p);   // absent -> defaults
        CHECK(d.AutosaveEnabled);
        CHECK(d.PromptUnsaved);
        CHECK(d.AutosaveMinutes == 5);

        Prefs::EditorSettings s;
        s.AutosaveEnabled = false; s.PromptUnsaved = false; s.AutosaveMinutes = 17;
        s.SnapMove = 0.5f; s.AutoResumePlay = false;   // the other keys keep round-tripping
        REQUIRE(Prefs::SaveSettingsTo(s, p));
        std::string text = Get(p);
        text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());   // text-mode CRLF on Windows
        CHECK(text.find("autosave_minutes = 17\n") != std::string::npos);   // whole minutes, same key
        CHECK(text.find("autosave_enabled = false\n") != std::string::npos);
        CHECK(text.find("prompt_unsaved = false\n") != std::string::npos);
        const Prefs::EditorSettings r = Prefs::LoadSettingsFrom(p);
        CHECK_FALSE(r.AutosaveEnabled);
        CHECK_FALSE(r.PromptUnsaved);
        CHECK(r.AutosaveMinutes == 17);
        CHECK(r.SnapMove == 0.5f);
        CHECK_FALSE(r.AutoResumePlay);

        s.AutosaveEnabled = true; s.PromptUnsaved = true; s.AutosaveMinutes = 60;
        REQUIRE(Prefs::SaveSettingsTo(s, p));
        const Prefs::EditorSettings r2 = Prefs::LoadSettingsFrom(p);
        CHECK(r2.AutosaveEnabled); CHECK(r2.PromptUnsaved); CHECK(r2.AutosaveMinutes == 60);

        // legacy float values and the 1-60 range
        Put(p, "autosave_minutes = 2.6\n");
        CHECK(Prefs::LoadSettingsFrom(p).AutosaveMinutes == 3);
        CHECK(Prefs::LoadSettingsFrom(p).AutosaveEnabled);
        Put(p, "autosave_minutes = 5.0\ncamera_speed = 1.0\n");
        CHECK(Prefs::LoadSettingsFrom(p).AutosaveMinutes == 5);
        Put(p, "autosave_minutes = 500\n");
        CHECK(Prefs::LoadSettingsFrom(p).AutosaveMinutes == 60);
        Put(p, "autosave_minutes = 0.0\n");                       // the old "0 = off"
        CHECK(Prefs::LoadSettingsFrom(p).AutosaveMinutes == 5);
        CHECK_FALSE(Prefs::LoadSettingsFrom(p).AutosaveEnabled);
        Put(p, "autosave_minutes = 0.0\nautosave_enabled = true\n"); // an explicit flag wins
        CHECK(Prefs::LoadSettingsFrom(p).AutosaveEnabled);
        s.AutosaveMinutes = 0;                                      // clamped on save
        REQUIRE(Prefs::SaveSettingsTo(s, p));
        text = Get(p);
        text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
        CHECK(text.find("autosave_minutes = 1\n") != std::string::npos);
        Put(p, "this is = = not toml [");                          // unparsable -> defaults
        CHECK(Prefs::LoadSettingsFrom(p).AutosaveMinutes == 5);
        std::error_code ec; fs::remove_all(root, ec);
    }
}
