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
