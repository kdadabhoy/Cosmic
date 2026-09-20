// AP03AuthoringSelfTest.cpp — AP-03 (App Platform): the E01 -> E08 (+ F01, V05 editor
// half) authoring sequence, driven inside the REAL Starforge editor.
//
// Armed by COSMIC_AP03_SELFTEST=<result.json>, COSMIC_AP03_ROOT=<temp root> and
// COSMIC_AP03_RECORD_SHELL=<file> (SourceLocator records Open/Reveal instead of
// launching). Everything goes through the editor's own commands and panels — the
// only test-side seams are Dear ImGui's input queue (the injected pointer drag of
// the L05 pattern) and the file system (E07 edits the scaffolded service on disk).
// The WO-07 ImGui stack oracle (tests/WO07UiOracle.h) is judged after EVERY step.
//
//   E01  New Project (kind app) -> tree == templates/app with the token replaced (F-APP)
//        -> the open scene is the flow's start screen (Home) -> viewport non-blank ->
//        BuildScripts through the real BuildRunner -> Play -> flow state Home, services
//        instantiated -> Stop.
//   E02  Screens ▸ New Screen "Telemetry" + Create script: scene (canvas + camera +
//        NativeScript{TelemetryScreen}), flow state (byte-stable apart from the addition),
//        stub header, include + CS_SCRIPT between the markers, rebuild ok, Validate()
//        empty; a marker-less module is refused with the documented message and no change.
//   F01  rename + set-start round-trip the .cflow byte-stable (inverse ops restore the bytes);
//        a when + channel-guard transition round-trips and validates.
//   E03  injected pointer drag on the rect gizmo: (40, -20) px -> one CommandStack entry,
//        undo / redo, anchors + pivot untouched; a SE resize changes OffsetMax only.
//   E04  UiImage.TexturePath replaced with an imported PNG through the field command (what
//        the Inspector slot / Content Browser drop commit) -> saved -> reopened -> the
//        sentinel colour is present in the viewport readback.
//   E06  homescreen: templates App/Game/Blank + samples FlowDemo/ForgePong listed; each
//        scaffolds, opens, builds, plays; LoadProjects calls over 600 homescreen frames <= 3.
//   E07  live loop: rewrite AppService.cpp while playing (amplitude x2) -> back in Play on
//        the same flow state, services re-instantiated, amplitude changed, uptime history
//        older than the rebuild kept, Console has the build line; a compile error -> stays
//        stopped, chip "Build failed"; the fix -> resumes.
//   E08  recorded Open/Reveal for: a screen script (Screens panel), a service (Inspector /
//        DataBus panel producer), a panel, a bound widget channel and a button signal
//        (the viewport context-menu resolution), all equal to the expected absolute paths.
//   V05  editor half: the Diagnostics CS_PANEL is drawn at the element's rect (viewport
//        offset, also under a letterbox preset), an unknown name is not drawn (placeholder),
//        never in edit mode, and the hosted block leaves the ImGui stacks balanced.
//
// Verdict -> exit code: PASS -> graceful close; FAIL -> quick_exit(1). JSON always written.

#include "StarforgeApp.h"
#include "commands/EditorCommands.h"

#include <Cosmic.h>
#include "layers/WorkspaceLayer.h"
#include "scene/SceneSerializer.h"
#include "scene/ui/UiComponents.h"
#include "scene/ui/UiSystem.h"
#include "utils/ImageIO.h"
#include "../../../tests/WO07UiOracle.h"

#include <imgui.h>
#include <imgui_internal.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#if defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace fs = std::filesystem;

namespace Starforge
{
    namespace
    {
        std::string Esc(const std::string& s)
        {
            std::string o;
            for (char c : s) { if (c == '"' || c == '\\') o += '\\'; if (c == '\n') { o += "\\n"; continue; } if (c == '\t') { o += "\\t"; continue; } if (c == '\r') continue; o += c; }
            return o;
        }
        std::string ReadAll(const fs::path& p)
        {
            std::ifstream in(p, std::ios::binary); std::stringstream ss; ss << in.rdbuf(); return ss.str();
        }
        bool WriteAll(const fs::path& p, const std::string& text)
        {
            std::error_code ec; fs::create_directories(p.parent_path(), ec);
            std::ofstream out(p, std::ios::binary | std::ios::trunc); out << text; return (bool)out;
        }
        std::string ReplaceAll(std::string s, const std::string& from, const std::string& to)
        {
            size_t pos = 0;
            while ((pos = s.find(from, pos)) != std::string::npos) { s.replace(pos, from.size(), to); pos += to.size(); }
            return s;
        }
        uint64_t Fnv1a(uint64_t h, const std::string& s)
        {
            for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
            return h;
        }
        // Every file under templates/app, with the token replaced, must equal the scaffold.
        // Returns the number of mismatches and fills a tree hash (paths + bytes) for the record.
        int CompareTree(const fs::path& templ, const fs::path& proj, const std::string& name, uint64_t& hash, std::string& firstBad)
        {
            int bad = 0; hash = 1469598103934665603ull;
            std::vector<fs::path> files;
            std::error_code ec;
            for (auto it = fs::recursive_directory_iterator(templ, ec); it != fs::recursive_directory_iterator(); it.increment(ec))
                if (it->is_regular_file(ec)) files.push_back(fs::relative(it->path(), templ, ec));
            std::sort(files.begin(), files.end());
            for (const fs::path& rel : files)
            {
                const std::string expected = ReplaceAll(ReadAll(templ / rel), "@PROJECT_NAME@", name);
                const std::string actual = ReadAll(proj / rel);
                hash = Fnv1a(hash, rel.generic_string());
                hash = Fnv1a(hash, actual);
                if (expected != actual) { ++bad; if (firstBad.empty()) firstBad = rel.generic_string(); }
            }
            return bad;
        }
    }

    struct StarforgeApp::AP03SelfTest
    {
        std::string resultPath, root, shellFile;
        std::string projectName = "Ap03App", projectDir;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point stepStart = std::chrono::steady_clock::now();
        CosmicTest::UiOracle oracle;
        int frame = 0, wait = 0;
        int step = 0;                              // index into the plan
        int e06Phase = 0;
        bool finished = false;
        std::vector<std::function<bool()>> plan;   // each returns true when done (advance); false = wait a frame
        std::vector<std::string> planNames;
        double deadlineSec = 240.0;                // per step

        // injected pointer (L05 pattern) — consumed in FrameEnd, one per frame
        struct Inject { float x = 0, y = 0; int button = -1; bool down = false; };
        std::vector<Inject> injects;

        // results
        int failures = 0;
        std::vector<std::string> log, checks, shellTable;
        std::map<std::string, std::string> idStatus;   // E01..E08, F01, V05 -> PASS/FAIL

        // scratch across steps
        std::string flowBytesBefore, flowBytesAfterE02;
        uint64_t treeHash = 0;
        int buildsSeen = 0;
        double rebuildBusTime = 0.0;
        glm::vec2 e03StartMin{}, e03StartMax{}, e03AnchorMin{}, e03AnchorMax{}, e03Pivot{};
        float e03Scale = 1.0f;
        size_t e03UndoBefore = 0;
        Cosmic::UUID e03Uuid;
        int loadCallsAtHome = 0, homeFrames = 0;
        std::vector<std::string> e06Kinds; size_t e06Index = 0;
        double sineMaxAfter = 0.0; int sineSamples = 0;
        std::string e07State;
        bool hostedSeen = false, hostedLetterboxSeen = false, hostedUnknownPlaceholder = false;
        int liveBuilds = 0, liveResumes = 0, liveFailures = 0;   // snapshot before CloseProject resets m_Live

        // viewport capture (taken inside RenderViewport while the FBO is the bound target)
        bool wantCapture = false, captured = false;
        std::vector<uint8_t> cap; uint32_t capW = 0, capH = 0;

        void note(const char* fmt, ...)
        {
            char buf[1024]; va_list ap; va_start(ap, fmt); _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap); va_end(ap);
            log.emplace_back(buf); std::printf("[AP03] %s\n", buf); std::fflush(stdout);
        }
        void fail(const char* id, const char* fmt, ...)
        {
            char buf[1024]; va_list ap; va_start(ap, fmt); _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap); va_end(ap);
            ++failures; checks.emplace_back(std::string(id) + ": " + buf);
            idStatus[id] = "FAIL";
            log.emplace_back(std::string("FAIL ") + id + ": " + buf); std::printf("[AP03] FAIL %s: %s\n", id, buf); std::fflush(stdout);
        }
        void pass(const char* id) { if (idStatus.find(id) == idStatus.end()) idStatus[id] = "PASS"; }
        void check(const char* id, bool ok, const char* what) { if (!ok) fail(id, "%s", what); }
        void check(const char* id, bool ok, const std::string& what) { if (!ok) fail(id, "%s", what.c_str()); }
        double secondsInStep() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - stepStart).count(); }
    };

    // =========================================================================
    void StarforgeApp::AP03SelfTestInit()
    {
        const char* rp = std::getenv("COSMIC_AP03_SELFTEST");
        if (!rp || !*rp) return;
        m_AP03 = new AP03SelfTest();
        auto& t = *m_AP03;
        t.resultPath = rp;
        if (const char* r = std::getenv("COSMIC_AP03_ROOT")) t.root = r;
        if (t.root.empty()) t.root = (fs::current_path() / "ap03-root").generic_string();
        if (const char* s = std::getenv("COSMIC_AP03_RECORD_SHELL")) t.shellFile = s;
        std::error_code ec; fs::create_directories(t.root, ec);
#if defined(_DEBUG)
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
        SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
        m_OpenFirstRun = false;
        Cosmic::Application::Get().GetWindow().SetVSync(false);
        Cosmic::Application::Get().SetPauseOnMinimize(false);
        ImGui::GetIO().BackendFlags &= ~ImGuiBackendFlags_HasMouseHoveredViewport;
        t.oracle.Install();
        t.note("armed: result=%s root=%s shell=%s", rp, t.root.c_str(), t.shellFile.c_str());
    }

    void StarforgeApp::AP03SelfTestShutdown()
    {
        if (!m_AP03) return;
        m_AP03->oracle.Uninstall();
        delete m_AP03; m_AP03 = nullptr;
    }

    void StarforgeApp::AP03SelfTestAfterRender()
    {
        if (!m_AP03 || !m_AP03->wantCapture) return;
        auto fb = Cosmic::Application::Get().GetFrameBuffer();
        if (!fb) return;
        m_AP03->captured = fb->ReadPixels(0, m_AP03->cap, m_AP03->capW, m_AP03->capH);
        m_AP03->wantCapture = false;
    }

    // Injected pointer + per-frame oracle bookkeeping, after the whole UI ran.
    void StarforgeApp::AP03SelfTestFrameEnd()
    {
        if (!m_AP03) return;
        auto& t = *m_AP03;
        t.oracle.CheckContexts();
        if (!t.injects.empty())
        {
            const auto in = t.injects.front();
            t.injects.erase(t.injects.begin());
            ImGuiIO& io = ImGui::GetIO();
            io.AddMouseViewportEvent(ImGui::GetMainViewport()->ID);
            io.AddMousePosEvent(in.x, in.y);
            if (in.button >= 0) io.AddMouseButtonEvent(in.button, in.down);
        }
    }

    // =========================================================================
    void StarforgeApp::AP03SelfTestTick()
    {
        if (!m_AP03) return;
        auto& t = *m_AP03;
        if (t.finished) return;
        ++t.frame;
        auto& app = Cosmic::Application::Get();

        auto readback = [&](std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h) -> bool
        {
            if (!t.captured || t.capW == 0 || t.capH == 0) return false;
            rgba = t.cap; w = t.capW; h = t.capH;
            return true;
        };
        auto requestCapture = [&]() { t.wantCapture = true; t.captured = false; };
        auto viewportNonBlank = [&](double& fractionOut) -> bool
        {
            std::vector<uint8_t> rgba; uint32_t w = 0, h = 0;
            if (!readback(rgba, w, h)) return false;
            const int cr = (int)(0.086f * 255.0f + 0.5f), cg = (int)(0.098f * 255.0f + 0.5f), cb = (int)(0.129f * 255.0f + 0.5f);
            size_t differ = 0;
            for (size_t i = 0; i + 3 < rgba.size(); i += 4)
                if (std::abs((int)rgba[i] - cr) > 8 || std::abs((int)rgba[i + 1] - cg) > 8 || std::abs((int)rgba[i + 2] - cb) > 8) ++differ;
            fractionOut = (double)differ / (double)((size_t)w * h);
            return true;
        };
        auto sentinelPresent = [&](int r, int g, int b) -> int
        {
            std::vector<uint8_t> rgba; uint32_t w = 0, h = 0;
            if (!readback(rgba, w, h)) return -1;
            int n = 0;
            for (size_t i = 0; i + 3 < rgba.size(); i += 4)
                if (std::abs((int)rgba[i] - r) <= 6 && std::abs((int)rgba[i + 1] - g) <= 6 && std::abs((int)rgba[i + 2] - b) <= 6) ++n;
            return n;
        };
        auto expectPath = [&](const char* id, const std::string& what, const std::string& expected)
        {
            const auto& recs = SourceLocator::Recorded();
            const SourceLocator::ShellRecord rec = recs.empty() ? SourceLocator::ShellRecord{} : recs.back();
            const std::string exp = SourceLocator::Normalize(expected);
            t.shellTable.push_back(what + "\t" + rec.Kind + "\t" + rec.Path + ":" + std::to_string(rec.Line) + "\t" + (rec.Path == exp ? "OK" : ("EXPECTED " + exp)));
            if (rec.Path != exp) t.fail(id, "%s recorded '%s', expected '%s'", what.c_str(), rec.Path.c_str(), exp.c_str());
        };
        auto firstCanvas = [&]() -> Cosmic::Entity
        {
            if (!m_Ctx.Scene) return {};
            for (auto e : m_Ctx.Scene->GetRegistry().view<Cosmic::CanvasComponent>()) return Cosmic::Entity(e, m_Ctx.Scene.get());
            return {};
        };
        auto waitFrames = [&](int n) { t.wait = n; };

        // ---- the plan (built once) ----------------------------------------------
        if (t.plan.empty())
        {
            auto add = [&](const char* name, std::function<bool()> fn) { t.planNames.push_back(name); t.plan.push_back(std::move(fn)); };
            const fs::path templ = fs::path("assets") / "projects" / "Starforge" / "templates" / "app";

            add("boot", [&] { return t.frame > 10; });

            // ---------------- E01 ----------------
            add("E01 new project (app)", [&, templ]
            {
                const bool ok = NewProjectAt(t.projectName, t.root, "app");
                t.projectDir = (fs::path(t.root) / t.projectName).generic_string();
                t.check("E01", ok, "NewProjectAt(app) failed");
                t.check("E01", m_Ctx.ProjectOpen, "project did not open");
                std::string bad;
                const int mismatches = CompareTree(templ, t.projectDir, t.projectName, t.treeHash, bad);
                if (mismatches) t.fail("E01", "F-APP: %d file(s) differ from templates/app (first: %s)", mismatches, bad.c_str());
                t.note("F-APP tree hash (FNV-1a over paths+bytes): %016llx", (unsigned long long)t.treeHash);
                t.check("E01", m_ProjectKind == "app", "manifest kind not read as app");
                t.check("E01", m_AutoBuild, "AutoBuild not ON for kind app");
                t.check("E01", m_Ctx.SceneName == "Home", "open scene is '" + m_Ctx.SceneName + "', expected the flow start 'Home'");
                waitFrames(6);
                return true;
            });
            add("E01 request capture", [&] { requestCapture(); waitFrames(3); return true; });
            add("E01 viewport non-blank", [&]
            {
                double frac = 0.0;
                const bool ok = viewportNonBlank(frac);
                t.check("E01", ok, "viewport readback failed");
                if (ok && frac < 0.01) t.fail("E01", "viewport blank: %.3f%% of pixels differ from the clear colour", frac * 100.0);
                t.note("E01 viewport: %.1f%% non-clear pixels", frac * 100.0);
                return true;
            });
            add("E01 build", [&] { BuildScripts(); t.check("E01", m_Builder.IsBuilding(), "BuildScripts did not start"); waitFrames(2); return true; });
            add("E01 wait build", [&] { if (m_Builder.IsBuilding()) return false; waitFrames(2); return true; });
            add("E01 wait: watcher + builder idle (late scaffold events may queue an auto-build)", [&]
            {
                return !m_Builder.IsBuilding() && m_Live.Debounce < 0.0f;
            });
            add("E01 module loaded + play", [&]
            {
                t.check("E01", m_Builder.GetStatus() == BuildRunner::Status::Success, "the app template's module build failed");
                t.check("E01", m_Module.IsLoaded(), "module not loaded after the build");
                PlayScene();
                t.check("E01", IsPlaying(), "PlayScene did not enter Play");
                waitFrames(20);
                return true;
            });
            add("E01 play checks + stop", [&]
            {
                t.check("E01", m_PlayFlowActive && m_PlayFlow.CurrentState() == "Home", "flow state '" + m_PlayFlow.CurrentState() + "' != Home");
                t.check("E01", m_PlayServices.Count() >= 1, "no service instantiated");
                t.check("E01", m_PlayBus.Has("app.uptime"), "app.uptime not published");
                StopScene();
                t.check("E01", !IsPlaying(), "StopScene did not stop");
                t.pass("E01");
                waitFrames(2);
                return true;
            });

            // ---------------- E02 + F01 ----------------
            add("E02 new screen Telemetry (+script) + F01 round trips", [&]
            {
                const fs::path flow = fs::path(t.projectDir) / "flows" / "Main.cflow";
                // The template's hand-authored .cflow is not in the serializer's canonical form;
                // "byte-stable" is asserted against the canonical (Load -> Save) text, which is
                // what every editor write produces from then on.
                { Cosmic::FlowAsset a; std::string err; Cosmic::FlowAsset::LoadFromString(a, ReadAll(flow), &err);
                  t.flowBytesBefore = a.SaveToString();
                  Cosmic::FlowAsset b; Cosmic::FlowAsset::LoadFromString(b, t.flowBytesBefore, &err);
                  t.check("F01", b.SaveToString() == t.flowBytesBefore, "flows/Main.cflow canonical text does not round-trip byte-stable"); }
                const auto r = m_Screens.NewScreen(m_Ctx, ScreensHost(), "Telemetry", true);
                t.check("E02", r.Ok, "NewScreen failed: " + r.Message);
                std::error_code ec;
                const fs::path scene = fs::path(t.projectDir) / "scenes" / "Telemetry.cscene";
                t.check("E02", fs::exists(scene, ec), "scenes/Telemetry.cscene missing");
                {
                    Cosmic::Ref<Cosmic::Scene> s = Cosmic::Scene::Create();
                    t.check("E02", Cosmic::SceneSerializer::Load(*s, scene.generic_string()), "Telemetry.cscene does not load");
                    auto& reg = s->GetRegistry();
                    bool canvas = false, camera = false, script = false;
                    for (auto e : reg.view<Cosmic::CanvasComponent>()) { canvas = true; if (auto* ns = reg.try_get<Cosmic::NativeScriptComponent>(e)) script = ns->ClassName == "TelemetryScreen"; }
                    for (auto e : reg.view<Cosmic::CameraComponent>()) { const auto& c = reg.get<Cosmic::CameraComponent>(e); camera = c.Primary && c.ProjectionType == Cosmic::CameraComponent::Projection::Orthographic; }
                    t.check("E02", canvas, "no Canvas entity"); t.check("E02", camera, "no primary ortho camera"); t.check("E02", script, "canvas lacks NativeScript{TelemetryScreen}");
                }
                t.flowBytesAfterE02 = ReadAll(flow);
                {
                    Cosmic::FlowAsset a; std::string err;
                    t.check("E02", Cosmic::FlowAsset::LoadFromString(a, t.flowBytesAfterE02, &err), "flow does not load after the edit");
                    const Cosmic::FlowState* st = a.Find("Telemetry");
                    t.check("E02", st != nullptr, "flow lacks the Telemetry state");
                    if (st) t.check("E02", st->Scene == "project://scenes/Telemetry.cscene", "state scene path wrong");
                    t.check("E02", a.Validate().empty(), "FlowAsset::Validate() not empty");
                    a.States.erase(std::remove_if(a.States.begin(), a.States.end(), [](const Cosmic::FlowState& s) { return s.Name == "Telemetry"; }), a.States.end());
                    t.check("E02", a.SaveToString() == t.flowBytesBefore, "flow bytes changed beyond the added state");
                }
                const std::string header = ReadAll(fs::path(t.projectDir) / "src" / "screens" / "TelemetryScreen.h");
                t.check("E02", header.find("class TelemetryScreen : public Cosmic::ScriptableEntity") != std::string::npos, "TelemetryScreen.h not from the stub");
                t.check("E02", header.find('@') == std::string::npos, "stub tokens left in TelemetryScreen.h");
                const std::string mod = ReadAll(fs::path(t.projectDir) / "src" / "Module.cpp");
                const size_t b = mod.find("CS_SCREENS_BEGIN"), sidx = mod.find("CS_SCRIPT(TelemetryScreen)"), e = mod.find("CS_SCREENS_END");
                t.check("E02", mod.find("#include \"screens/TelemetryScreen.h\"") != std::string::npos, "Module.cpp lacks the include");
                t.check("E02", sidx != std::string::npos && b < sidx && sidx < e, "CS_SCRIPT(TelemetryScreen) not between the markers");
                // the marker-less refusal on a throwaway project
                const fs::path bare = fs::path(t.root) / "Ap03NoMarkers";
                WriteAll(bare / "src" / "Module.cpp", "#include <Cosmic.h>\nCS_MODULE_BEGIN(Ap03NoMarkers)\nCS_MODULE_END()\n");
                const std::string before = ReadAll(bare / "src" / "Module.cpp");
                const auto rr = ScreenScaffold::CreateScript(bare.generic_string(), "Telemetry", "Ap03NoMarkers");
                t.check("E02", !rr.Ok && rr.Message == "Module.cpp has no CS_SCREENS markers", "refusal message: '" + rr.Message + "'");
                t.check("E02", ReadAll(bare / "src" / "Module.cpp") == before, "marker-less Module.cpp was modified");
                t.check("E02", !fs::exists(bare / "src" / "screens" / "TelemetryScreen.h", ec), "marker-less project got a header");
                // F01: rename + set-start round trips
                const auto r1 = m_Screens.Rename(m_Ctx, ScreensHost(), "Telemetry", "Telem2");
                const auto r2 = m_Screens.Rename(m_Ctx, ScreensHost(), "Telem2", "Telemetry");
                const auto r3 = m_Screens.SetAsStart(m_Ctx, ScreensHost(), "Dashboard");
                const auto r4 = m_Screens.SetAsStart(m_Ctx, ScreensHost(), "Home");
                t.check("F01", r1.Ok && r2.Ok && r3.Ok && r4.Ok, "rename / set-start op failed");
                t.check("F01", ReadAll(flow) == t.flowBytesAfterE02, "rename + set-start inverse ops did not restore the .cflow bytes");
                {
                    std::string err;
                    Cosmic::FlowAsset a; Cosmic::FlowAsset::LoadFromString(a, ReadAll(flow), &err);
                    m_Screens.SetAsStart(m_Ctx, ScreensHost(), "Dashboard");
                    Cosmic::FlowAsset b2; Cosmic::FlowAsset::LoadFromString(b2, ReadAll(flow), &err);
                    t.check("F01", b2.Start == "Dashboard", "set-start did not write start");
                    a.Start = "Dashboard";
                    t.check("F01", a.SaveToString() == ReadAll(flow), "set-start changed bytes beyond the start key");
                    m_Screens.SetAsStart(m_Ctx, ScreensHost(), "Home");
                    // the transition inspector's fields: a channel guard + when transition round-trips and validates
                    Cosmic::FlowAsset w; Cosmic::FlowAsset::LoadFromString(w, ReadAll(flow), &err);
                    Cosmic::FlowTransition tr; tr.On = "when"; tr.To = "Home"; tr.HasGuard = true;
                    tr.Guard.Channel = "app.uptime"; tr.Guard.Op = ">"; tr.Guard.Value = Cosmic::FlowValue::MakeNumber(3.0);
                    w.States.back().Transitions.push_back(tr);
                    Cosmic::FlowAsset w2; t.check("F01", Cosmic::FlowAsset::LoadFromString(w2, w.SaveToString(), &err), "when/channel flow does not reload");
                    t.check("F01", w2.Validate().empty(), "when + channel guard fails Validate()");
                    const auto& tt = w2.States.back().Transitions.back();
                    t.check("F01", tt.On == "when" && tt.Guard.Channel == "app.uptime" && tt.Guard.Op == ">", "when/channel fields not written verbatim");
                    Cosmic::FlowTransition noGuard; noGuard.On = "when"; noGuard.To = "Home";
                    w2.States.back().Transitions.push_back(noGuard);
                    t.check("F01", !w2.Validate().empty(), "Validate() accepts a when transition without a guard");
                }
                t.pass("F01");
                waitFrames(2);
                return true;
            });
            add("E02 rebuild", [&] { BuildScripts(); waitFrames(2); return true; });
            add("E02 wait rebuild", [&]
            {
                if (m_Builder.IsBuilding()) return false;
                t.check("E02", m_Builder.GetStatus() == BuildRunner::Status::Success, "rebuild with TelemetryScreen failed");
                waitFrames(2);
                return true;
            });
            add("E02 telemetry registered", [&]
            {
                t.check("E02", Cosmic::ModuleRegistry::Get().FindScript("TelemetryScreen") != nullptr, "TelemetryScreen not registered after the rebuild");
                t.pass("E02");
                return true;
            });

            // ---------------- E03 ----------------
            add("E03 open Home", [&] { OpenScene("project://scenes/Home.cscene"); m_Aspect = GameAspect::Free; waitFrames(4); return true; });
            add("E03 pick target", [&]
            {
                const glm::vec2 vpSize = app.GetViewportSize();
                const Cosmic::UiRect band{ { 0.0f, 0.0f }, vpSize };
                std::vector<Cosmic::UiElement> els;
                Cosmic::UiSystem::CollectElements(*m_Ctx.Scene, band, els);
                Cosmic::Entity target;
                for (auto it = els.rbegin(); it != els.rend(); ++it)   // front to back
                {
                    Cosmic::Entity e(static_cast<entt::entity>(it->Handle), m_Ctx.Scene.get());
                    if (!e.HasComponent<Cosmic::RectTransformComponent>()) continue;
                    uint32_t hit = 0;
                    if (Cosmic::UiSystem::HitTest(*m_Ctx.Scene, band, it->Rect.Center(), hit) && hit == it->Handle &&
                        it->Rect.Width() > 40.0f && it->Rect.Height() > 20.0f)
                    { target = e; t.e03Scale = it->Scale > 0.0f ? it->Scale : 1.0f; break; }
                }
                if (!target) { t.fail("E03", "no hit-testable RectTransform element in Home"); return true; }
                m_Ctx.SelectOnly(target);
                const auto& rt = target.GetComponent<Cosmic::RectTransformComponent>();
                t.e03StartMin = rt.OffsetMin; t.e03StartMax = rt.OffsetMax; t.e03AnchorMin = rt.AnchorMin; t.e03AnchorMax = rt.AnchorMax; t.e03Pivot = rt.Pivot;
                t.e03Uuid = target.GetComponent<Cosmic::IDComponent>().ID;
                t.e03UndoBefore = m_Ctx.Commands.UndoCount();
                t.note("E03 target '%s' scale %.3f offsets (%g,%g)-(%g,%g)", target.GetComponent<Cosmic::TagComponent>().Tag.c_str(), t.e03Scale,
                       rt.OffsetMin.x, rt.OffsetMin.y, rt.OffsetMax.x, rt.OffsetMax.y);
                waitFrames(3);
                return true;
            });
            add("E03 inject move drag (40,-20)", [&]
            {
                if (!m_RectGizmo.HasTarget()) { t.fail("E03", "rect gizmo has no target after selection"); return true; }
                const glm::vec2 c = m_RectGizmo.HandleScreenRect(RectHandle::Move).Center();
                t.injects.push_back({ c.x, c.y, -1, false });
                t.injects.push_back({ c.x, c.y, -1, false });
                t.injects.push_back({ c.x, c.y, 0, true });
                for (int i = 1; i <= 8; ++i) t.injects.push_back({ c.x + 5.0f * i, c.y - 2.5f * i, -1, false });
                t.injects.push_back({ c.x + 40.0f, c.y - 20.0f, -1, false });
                t.injects.push_back({ c.x + 40.0f, c.y - 20.0f, 0, false });
                t.injects.push_back({ c.x + 40.0f, c.y - 20.0f, -1, false });
                waitFrames((int)t.injects.size() + 4);
                return true;
            });
            add("E03 verify move + undo/redo", [&]
            {
                Cosmic::Entity e = m_Ctx.Scene->FindByUUID(t.e03Uuid);
                if (!e) { t.fail("E03", "target lost"); return true; }
                const auto& rt = e.GetComponent<Cosmic::RectTransformComponent>();
                const glm::vec2 d = glm::vec2(40.0f, -20.0f) / t.e03Scale;
                const bool moved = glm::all(glm::epsilonEqual(rt.OffsetMin, t.e03StartMin + d, 0.01f)) && glm::all(glm::epsilonEqual(rt.OffsetMax, t.e03StartMax + d, 0.01f));
                if (!moved) t.fail("E03", "offsets after drag (%g,%g)-(%g,%g), expected +(%g,%g)", rt.OffsetMin.x, rt.OffsetMin.y, rt.OffsetMax.x, rt.OffsetMax.y, d.x, d.y);
                t.check("E03", rt.AnchorMin == t.e03AnchorMin && rt.AnchorMax == t.e03AnchorMax && rt.Pivot == t.e03Pivot, "anchors/pivot changed");
                const size_t entries = m_Ctx.Commands.UndoCount() - t.e03UndoBefore;
                if (entries != 1) t.fail("E03", "%zu CommandStack entries for one gesture (expected 1)", entries);
                t.check("E03", m_Ctx.Commands.UndoName() == "Move UI Rect", "undo label '" + m_Ctx.Commands.UndoName() + "'");
                m_Ctx.Commands.Undo();
                t.check("E03", rt.OffsetMin == t.e03StartMin && rt.OffsetMax == t.e03StartMax, "undo did not restore the offsets");
                m_Ctx.Commands.Redo();
                t.check("E03", glm::all(glm::epsilonEqual(rt.OffsetMin, t.e03StartMin + d, 0.01f)), "redo did not re-apply");
                m_Ctx.Commands.Undo();
                t.e03UndoBefore = m_Ctx.Commands.UndoCount();
                waitFrames(3);
                return true;
            });
            add("E03 inject SE resize", [&]
            {
                if (!m_RectGizmo.HasTarget()) { t.fail("E03", "rect gizmo lost its target"); return true; }
                const glm::vec2 c = m_RectGizmo.HandleScreenRect(RectHandle::SE).Center();
                t.injects.push_back({ c.x, c.y, -1, false });
                t.injects.push_back({ c.x, c.y, -1, false });
                t.injects.push_back({ c.x, c.y, 0, true });
                for (int i = 1; i <= 4; ++i) t.injects.push_back({ c.x + 5.0f * i, c.y + 3.0f * i, -1, false });
                t.injects.push_back({ c.x + 20.0f, c.y + 12.0f, -1, false });
                t.injects.push_back({ c.x + 20.0f, c.y + 12.0f, 0, false });
                t.injects.push_back({ c.x + 20.0f, c.y + 12.0f, -1, false });
                waitFrames((int)t.injects.size() + 4);
                return true;
            });
            add("E03 verify resize", [&]
            {
                Cosmic::Entity e = m_Ctx.Scene->FindByUUID(t.e03Uuid);
                if (!e) { t.fail("E03", "target lost"); return true; }
                const auto& rt = e.GetComponent<Cosmic::RectTransformComponent>();
                const glm::vec2 d = glm::vec2(20.0f, 12.0f) / t.e03Scale;
                t.check("E03", rt.OffsetMin == t.e03StartMin, "SE resize changed OffsetMin");
                if (!glm::all(glm::epsilonEqual(rt.OffsetMax, t.e03StartMax + d, 0.01f)))
                    t.fail("E03", "OffsetMax after SE resize (%g,%g), expected (%g,%g)", rt.OffsetMax.x, rt.OffsetMax.y, (t.e03StartMax + d).x, (t.e03StartMax + d).y);
                t.check("E03", m_Ctx.Commands.UndoCount() - t.e03UndoBefore == 1, "resize gesture != 1 undo entry");
                t.check("E03", m_Ctx.Commands.UndoName() == "Resize UI Rect", "resize undo label");
                m_Ctx.Commands.Undo();
                t.check("E03", rt.OffsetMax == t.e03StartMax, "resize undo did not restore OffsetMax");
                m_Ctx.ClearSelection();
                t.pass("E03");
                return true;
            });

            // ---------------- E04 ----------------
            add("E04 texture replace + save", [&]
            {
                std::vector<uint8_t> px(16 * 16 * 4);
                for (size_t i = 0; i < px.size(); i += 4) { px[i] = 250; px[i + 1] = 20; px[i + 2] = 200; px[i + 3] = 255; }
                const fs::path tex = fs::path(t.projectDir) / "textures" / "ap03_sentinel.png";
                std::error_code ec; fs::create_directories(tex.parent_path(), ec);
                t.check("E04", Cosmic::ImageIO::WritePNG(tex.generic_string(), 16, 16, 4, px.data()), "sentinel PNG write failed");
                Cosmic::Entity canvas = firstCanvas();
                if (!canvas) { t.fail("E04", "Home has no canvas"); return true; }
                Cosmic::Entity img = Commands::Create(m_Ctx, "Ap03Sentinel", canvas, [](Cosmic::Entity e)
                {
                    auto& rt = e.AddComponent<Cosmic::RectTransformComponent>();
                    rt.AnchorMin = { 0.25f, 0.25f }; rt.AnchorMax = { 0.75f, 0.75f }; rt.OffsetMin = { 0, 0 }; rt.OffsetMax = { 0, 0 }; rt.ZOrder = 500;
                    e.AddComponent<Cosmic::UiImageComponent>();
                });
                const auto* d = Cosmic::Reflect::GetRegistry().FindByName("UiImage");
                if (!d) { t.fail("E04", "UiImage not reflected"); return true; }
                Commands::SetField(m_Ctx, img, d->TypeId, "TexturePath", Cosmic::Reflect::FieldValue{ std::string("project://textures/ap03_sentinel.png") });
                t.check("E04", img.GetComponent<Cosmic::UiImageComponent>().TexturePath == "project://textures/ap03_sentinel.png", "TexturePath not applied");
                t.check("E04", SaveScene(), "SaveScene failed");
                const std::string saved = ReadAll(fs::path(t.projectDir) / "scenes" / "Home.cscene");
                t.check("E04", saved.find("ap03_sentinel.png") != std::string::npos, "saved scene lacks the new texture path");
                waitFrames(2);
                return true;
            });
            add("E04 reopen", [&] { OpenScene("project://scenes/Dashboard.cscene"); OpenScene("project://scenes/Home.cscene"); waitFrames(8); return true; });
            add("E04 request capture", [&] { requestCapture(); waitFrames(3); return true; });
            add("E04 sentinel present", [&]
            {
                const int n = sentinelPresent(250, 20, 200);
                if (n < 100) t.fail("E04", "sentinel pixels after reopen: %d (expected >= 100)", n);
                t.note("E04 sentinel pixels: %d", n);
                t.pass("E04");
                return true;
            });

            // ---------------- E08 (edit-mode links) + V05 prep ----------------
            add("E08 edit-mode links (Screens panel, Inspector script, viewport ctx)", [&]
            {
                SourceLocator::ClearRecorded();
                const std::string root = t.projectDir;
                t.check("E08", m_Screens.OpenScript(ScreensHost(), "Home"), "Screens panel Open script failed");
                expectPath("E08", "screens-panel open script Home", root + "/src/screens/HomeScreen.h");
                t.check("E08", m_Screens.RevealScript(ScreensHost(), "Home"), "Screens panel Reveal failed");
                expectPath("E08", "screens-panel reveal Home", root + "/src/screens/HomeScreen.h");
                Cosmic::Entity canvas = firstCanvas();
                const std::string cls = canvas && canvas.HasComponent<Cosmic::NativeScriptComponent>() ? canvas.GetComponent<Cosmic::NativeScriptComponent>().ClassName : "";
                t.check("E08", cls == "HomeScreen", "Home canvas script is '" + cls + "'");
                m_Ctx.SelectOnly(canvas);
                t.check("E08", SourceLocator::Open(SourceLocator(root).ForScriptClass(cls)), "Inspector Open source (script) failed");
                expectPath("E08", "inspector open source NativeScript", root + "/src/screens/HomeScreen.h");
                std::string what;
                const SourceHit vh = ResolveLogicSource(canvas, &what);
                t.check("E08", vh.Resolved() && what == "script", "viewport resolution (edit mode, canvas) did not fall through to the script");
                t.check("E08", SourceLocator::Open(vh), "viewport Open logic source failed");
                expectPath("E08", "viewport ctx open logic source (script)", root + "/src/screens/HomeScreen.h");
                m_Ctx.ClearSelection();
                // V05: an unknown hosted panel on the Dashboard, authored through the real create command
                OpenScene("project://scenes/Dashboard.cscene");
                Cosmic::Entity dcanvas = firstCanvas();
                Commands::Create(m_Ctx, "Ap03UnknownPanel", dcanvas, [](Cosmic::Entity e)
                {
                    auto& rt = e.AddComponent<Cosmic::RectTransformComponent>();
                    rt.AnchorMin = { 0.02f, 0.80f }; rt.AnchorMax = { 0.02f, 0.80f }; rt.OffsetMin = { 0, 0 }; rt.OffsetMax = { 120, 40 };
                    e.AddComponent<Cosmic::UiHostedPanelComponent>().PanelName = "Ap03Nope";
                });
                t.check("V05", SaveScene(), "Dashboard save failed");
                waitFrames(2);
                return true;
            });

            // ---------------- E07 + V05 + E08 (play-time links) ----------------
            add("E07 wait: builder idle (the Screens writes queued an auto-build)", [&]
            {
                return !m_Builder.IsBuilding() && m_Live.Debounce < 0.0f;
            });
            add("E07 play + navigate to Dashboard", [&]
            {
                t.check("V05", m_HostedDraws.empty(), "hosted panels drawn in edit mode");
                m_HostedImbalance = 0;
                OpenScene("project://scenes/Home.cscene");
                PlayScene();
                t.check("E07", IsPlaying() && m_PlayFlowActive, "Play with flow did not start");
                m_PlayFlow.FeedSignal("dashboard_clicked");
                waitFrames(30);
                return true;
            });
            add("V05 hosted panel at rect + unknown placeholder", [&]
            {
                t.check("E07", m_PlayFlow.CurrentState() == "Dashboard", "flow state '" + m_PlayFlow.CurrentState() + "' != Dashboard");
                const glm::vec2 vpPos = app.GetViewportPos(), vpSize = app.GetViewportSize();
                for (const auto& h : m_HostedDraws)
                {
                    if (h.Name == "Diagnostics")
                    {
                        t.hostedSeen = true;
                        t.check("V05", h.Drawn, "Diagnostics CS_PANEL not drawn in Play");
                        t.check("V05", std::abs(h.ScreenMin.x - (vpPos.x + h.Rect.Min.x)) < 0.5f && std::abs(h.ScreenMin.y - (vpPos.y + h.Rect.Min.y)) < 0.5f, "hosted window not at viewport-offset coordinates");
                        t.check("V05", h.Rect.Min.x >= -0.5f && h.Rect.Max.x <= vpSize.x + 0.5f, "hosted rect outside the viewport");
                    }
                    if (h.Name == "Ap03Nope") t.hostedUnknownPlaceholder = !h.Drawn;
                }
                t.check("V05", t.hostedSeen, "no Diagnostics hosted draw recorded");
                t.check("V05", t.hostedUnknownPlaceholder, "unknown panel name reported as drawn (placeholder expected)");
                bool drawnFlag = false, unknownFlag = true;
                for (auto e : m_Ctx.Scene->GetRegistry().view<Cosmic::UiHostedPanelComponent>())
                {
                    const auto& hp = m_Ctx.Scene->GetRegistry().get<Cosmic::UiHostedPanelComponent>(e);
                    if (hp.PanelName == "Diagnostics") drawnFlag = hp.DrawnThisFrame;
                    if (hp.PanelName == "Ap03Nope") unknownFlag = hp.DrawnThisFrame;
                }
                t.check("V05", drawnFlag, "UiHostedPanel.DrawnThisFrame not set for Diagnostics");
                t.check("V05", !unknownFlag, "UiHostedPanel.DrawnThisFrame set for the unknown panel");
                m_Aspect = GameAspect::W16H9;   // letterbox preset
                waitFrames(6);
                return true;
            });
            add("V05 letterboxed hosted position + E08 play-time links + E07 edit #1", [&]
            {
                const glm::vec2 vpPos = app.GetViewportPos(), vpSize = app.GetViewportSize();
                const bool banded = m_GameBandUv.x > 0.0001f || m_GameBandUv.y > 0.0001f || m_GameBandUv.z < 0.9999f || m_GameBandUv.w < 0.9999f;
                for (const auto& h : m_HostedDraws)
                    if (h.Name == "Diagnostics")
                    {
                        t.hostedLetterboxSeen = true;
                        t.check("V05", std::abs(h.ScreenMin.x - (vpPos.x + h.Rect.Min.x)) < 0.5f, "letterboxed hosted window x not viewport-offset");
                        const float bx0 = m_GameBandUv.x * vpSize.x, bx1 = (m_GameBandUv.x + m_GameBandUv.z) * vpSize.x;
                        const float by0 = m_GameBandUv.y * vpSize.y, by1 = (m_GameBandUv.y + m_GameBandUv.w) * vpSize.y;
                        t.check("V05", h.Rect.Min.x >= bx0 - 0.5f && h.Rect.Max.x <= bx1 + 0.5f && h.Rect.Min.y >= by0 - 0.5f && h.Rect.Max.y <= by1 + 0.5f, "letterboxed hosted rect outside the band");
                    }
                t.check("V05", t.hostedLetterboxSeen, "no hosted draw under the letterbox preset");
                t.note("V05 band uv (%.3f,%.3f,%.3f,%.3f) banded=%d", m_GameBandUv.x, m_GameBandUv.y, m_GameBandUv.z, m_GameBandUv.w, (int)banded);
                t.check("V05", m_HostedImbalance == 0, "ImGui stack imbalance across the hosted block");
                m_Aspect = GameAspect::Free;
                t.pass("V05");

                const std::string root = t.projectDir;
                const SourceHit prod = DataBusPanel::ProducerHit(root, "app.uptime", m_PlayBus);
                t.check("E08", prod.Resolved(), "DataBus panel producer for app.uptime unresolved: " + prod.Reason);
                t.check("E08", SourceLocator::Open(prod), "DataBus panel Open failed");
                expectPath("E08", "databus-panel open producer app.uptime (CS_SERVICE site)", root + "/src/Module.cpp");
                t.check("E08", SourceLocator::Open(SourceLocator(root).ForChannel("app.sine", m_PlayBus)), "Inspector Open producer failed");
                expectPath("E08", "inspector open producer app.sine", root + "/src/Module.cpp");
                Cosmic::Entity panelEnt, widgetEnt, buttonEnt;
                auto& reg = m_Ctx.Scene->GetRegistry();
                for (auto e : reg.view<Cosmic::UiHostedPanelComponent>()) if (reg.get<Cosmic::UiHostedPanelComponent>(e).PanelName == "Diagnostics") panelEnt = Cosmic::Entity(e, m_Ctx.Scene.get());
                for (auto e : reg.view<Cosmic::UiValueTextComponent>()) { widgetEnt = Cosmic::Entity(e, m_Ctx.Scene.get()); break; }
                for (auto e : reg.view<Cosmic::UiButtonComponent>()) if (reg.get<Cosmic::UiButtonComponent>(e).Signal == "counter.increment") buttonEnt = Cosmic::Entity(e, m_Ctx.Scene.get());
                std::string what;
                if (panelEnt)
                {
                    const SourceHit ph = ResolveLogicSource(panelEnt, &what);
                    t.check("E08", ph.Resolved() && what == "panel", "viewport resolution for the hosted panel: " + ph.Reason);
                    t.check("E08", SourceLocator::Open(ph), "viewport Open (panel) failed");
                    expectPath("E08", "viewport ctx open logic source (CS_PANEL Diagnostics)", root + "/src/services/AppService.cpp");
                    t.check("E08", SourceLocator::Reveal(ph), "viewport Reveal (panel) failed");
                    expectPath("E08", "viewport ctx reveal (CS_PANEL Diagnostics)", root + "/src/services/AppService.cpp");
                    t.check("E08", SourceLocator::Open(SourceLocator(root).ForPanel("Diagnostics", m_PlayPanels)), "Inspector hosted-panel Open failed");
                    expectPath("E08", "inspector open source UiHostedPanel", root + "/src/services/AppService.cpp");
                }
                else t.fail("E08", "Dashboard has no Diagnostics hosted panel entity");
                if (widgetEnt)
                {
                    const SourceHit wh = ResolveLogicSource(widgetEnt, &what);
                    t.check("E08", wh.Resolved() && what == "channel", "viewport resolution for a bound widget: " + wh.Reason);
                    t.check("E08", SourceLocator::Open(wh), "viewport Open (bound widget) failed");
                    expectPath("E08", "viewport ctx open logic source (bound widget channel -> CS_SERVICE)", root + "/src/Module.cpp");
                }
                else t.fail("E08", "Dashboard has no UiValueText");
                if (buttonEnt)
                {
                    const auto hits = SourceLocator(root).ForSignal("counter.increment");
                    t.check("E08", !hits.empty(), "Find handlers found nothing for counter.increment");
                    bool svc = false;
                    for (const auto& h : hits)
                        if (h.Path.find("services/AppService.cpp") != std::string::npos)
                        {
                            svc = true;
                            t.check("E08", SourceLocator::Open(h), "Find handlers Open failed");
                            expectPath("E08", "inspector find handlers counter.increment", root + "/src/services/AppService.cpp");
                        }
                    t.check("E08", svc, "Find handlers did not list services/AppService.cpp");
                    const SourceHit bh = ResolveLogicSource(buttonEnt, &what);
                    t.check("E08", bh.Resolved() && what == "signal", "viewport resolution for a button: " + bh.Reason);
                }
                else t.fail("E08", "Dashboard has no counter.increment button");
                t.pass("E08");

                // E07 part 1: rewrite AppService.cpp (amplitude x2) while playing
                t.e07State = m_PlayFlow.CurrentState();
                t.rebuildBusTime = m_PlayBus.Now();
                t.buildsSeen = m_Live.Builds;
                const fs::path svcFile = fs::path(t.projectDir) / "src" / "services" / "AppService.cpp";
                std::string src = ReadAll(svcFile);
                const std::string needle = "bus.Set(\"app.sine\", amplitude * std::sin(";
                t.check("E07", src.find(needle) != std::string::npos, "AppService.cpp publish line not found");
                src = ReplaceAll(src, needle, "bus.Set(\"app.sine\", 2.0 * amplitude * std::sin(");
                WriteAll(svcFile, src);
                t.note("E07 rewrote AppService.cpp (amplitude x2) at bus t=%.2f on state %s", t.rebuildBusTime, t.e07State.c_str());
                return true;
            });
            add("E07 wait: stop + build started", [&]
            {
                if (m_Live.Builds > t.buildsSeen && m_Builder.IsBuilding()) { t.check("E07", !IsPlaying(), "still playing while building"); return true; }
                if (t.secondsInStep() > 30.0) { t.fail("E07", "auto-build did not start within 30 s of the edit (debounce 0.5 s)"); return true; }
                return false;
            });
            add("E07 wait: rebuilt", [&] { if (m_Builder.IsBuilding()) return false; waitFrames(30); return true; });
            add("E07 verify resume", [&]
            {
                t.check("E07", IsPlaying(), "not back in Play after the rebuild");
                t.check("E07", m_PlayFlow.CurrentState() == t.e07State, "resumed on '" + m_PlayFlow.CurrentState() + "', expected '" + t.e07State + "'");
                t.check("E07", m_PlayServices.Count() >= 1, "services not re-instantiated");
                std::vector<Cosmic::DataSample> hist;
                m_PlayBus.History("app.uptime", hist);
                bool older = false;
                for (const auto& s : hist) if (s.Time < t.rebuildBusTime) { older = true; break; }
                t.check("E07", older, "app.uptime history lost the samples from before the rebuild");
                bool buildLine = false;
                for (const auto& l : m_Ctx.ConsoleLines) if (l.Text.find("[Build] Building") != std::string::npos) buildLine = true;
                t.check("E07", buildLine, "Console has no build line");
                t.sineMaxAfter = 0.0; t.sineSamples = 0;
                return true;
            });
            add("E07 sample app.sine amplitude, then break the build", [&]
            {
                t.sineMaxAfter = std::max(t.sineMaxAfter, std::abs(m_PlayBus.GetNumber("app.sine")));
                ++t.sineSamples;
                if (t.secondsInStep() < 2.5) return false;   // > one period at 0.5 Hz
                if (t.sineMaxAfter < 1.5) t.fail("E07", "app.sine amplitude after the rebuild %.3f (expected ~2.0 over %d samples)", t.sineMaxAfter, t.sineSamples);
                t.note("E07 app.sine |max| after rebuild = %.3f (%d samples)", t.sineMaxAfter, t.sineSamples);
                const fs::path svcFile = fs::path(t.projectDir) / "src" / "services" / "AppService.cpp";
                WriteAll(svcFile, ReadAll(svcFile) + "\nthis is not C++ ;\n");
                t.buildsSeen = m_Live.Builds;
                return true;
            });
            add("E07 wait: error build started", [&]
            {
                if (m_Live.Builds > t.buildsSeen && m_Builder.IsBuilding()) return true;
                if (t.secondsInStep() > 30.0) { t.fail("E07", "auto-build (error) did not start within 30 s"); return true; }
                return false;
            });
            add("E07 wait: error build done", [&] { if (m_Builder.IsBuilding()) return false; waitFrames(10); return true; });
            add("E07 verify failed state, then fix", [&]
            {
                t.check("E07", !IsPlaying(), "Play resumed after a failed build");
                ImVec4 col; const char* chip = LiveChipText(col);
                t.check("E07", chip && std::string(chip) == "Build failed", "status chip '" + std::string(chip ? chip : "(none)") + "' != 'Build failed'");
                bool err = false;
                for (const auto& l : m_Ctx.ConsoleLines) if (l.Severity == LogSeverity::Error && l.Text.find("Build") != std::string::npos) err = true;
                t.check("E07", err, "Console has no build error line");
                const fs::path svcFile = fs::path(t.projectDir) / "src" / "services" / "AppService.cpp";
                WriteAll(svcFile, ReplaceAll(ReadAll(svcFile), "\nthis is not C++ ;\n", "\n"));
                t.buildsSeen = m_Live.Builds;
                return true;
            });
            add("E07 wait: fix build started", [&]
            {
                if (m_Builder.IsBuilding()) return true;
                if (t.secondsInStep() > 30.0) { t.fail("E07", "auto-build (fix) did not start within 30 s"); return true; }
                return false;
            });
            add("E07 wait: fix built", [&] { if (m_Builder.IsBuilding()) return false; waitFrames(20); return true; });
            add("E07 verify resumed after fix + stop", [&]
            {
                t.check("E07", IsPlaying(), "not back in Play after the fix");
                t.check("E07", m_PlayFlow.CurrentState() == t.e07State, "fix resume landed on another state");
                ImVec4 col; const char* chip = LiveChipText(col);
                t.check("E07", chip && (std::string(chip) == "Live" || std::string(chip) == "Reloading"), "chip after the fix is not Live/Reloading");
                t.liveBuilds = m_Live.Builds; t.liveResumes = m_Live.Resumes; t.liveFailures = m_Live.Failures;
                StopScene();
                t.pass("E07");
                waitFrames(2);
                return true;
            });

            // ---------------- E06 ----------------
            add("E06 close + homescreen lists", [&]
            {
                CloseProject();
                const auto tpl = ListTemplates();
                std::string names;
                for (const auto& x : tpl) names += x.Display + ",";
                t.check("E06", names == "App,Game,Blank,", "templates listed: " + names);
                for (const auto& x : tpl) t.check("E06", !x.Description.empty(), "template without a description");
                const auto smp = ListSamples();
                std::string sn; for (const auto& x : smp) sn += x + ",";
                t.check("E06", sn == "FlowDemo,ForgePong,", "samples listed: " + sn);
                t.e06Kinds = { "game", "blank", "sample:FlowDemo", "sample:ForgePong" };   // app already proven by E01
                t.e06Index = 0; t.e06Phase = 0;
                t.loadCallsAtHome = m_LoadProjectsCalls; t.homeFrames = 0;
                waitFrames(2);
                return true;
            });
            add("E06 600 homescreen frames: LoadProjects <= 3", [&]
            {
                if (++t.homeFrames < 600) return false;
                const int calls = m_LoadProjectsCalls - t.loadCallsAtHome;
                if (calls > 3) t.fail("E06", "Prefs::LoadProjects called %d times over 600 homescreen frames (max 3)", calls);
                t.note("E06 LoadProjects calls over 600 frames: %d", calls);
                return true;
            });
            add("E06 each kind: scaffold + open + build + play", [&]
            {
                if (t.e06Index >= t.e06Kinds.size()) { t.pass("E06"); return true; }
                const std::string kind = t.e06Kinds[t.e06Index];
                const bool sample = kind.rfind("sample:", 0) == 0;
                const std::string name = sample ? kind.substr(7) : ("Ap03" + kind);
                switch (t.e06Phase)
                {
                case 0:
                {
                    const bool ok = sample ? OpenSample(name) : NewProjectAt(name, t.root, kind);
                    t.check("E06", ok, kind + ": scaffold/open failed");
                    if (sample) { std::error_code ec; t.check("E06", fs::exists(fs::path(SamplePath(name)) / "project.cproj", ec), kind + ": not scaffolded at SamplePath"); }
                    t.check("E06", m_Ctx.ProjectOpen, kind + ": project not open");
                    t.check("E06", m_Ctx.Scene != nullptr, kind + ": no scene");
                    BuildScripts();
                    t.stepStart = std::chrono::steady_clock::now();
                    t.e06Phase = 1; return false;
                }
                case 1:
                    if (m_Builder.IsBuilding()) return false;
                    t.check("E06", m_Builder.GetStatus() == BuildRunner::Status::Success, kind + ": build failed");
                    PlayScene();
                    t.check("E06", IsPlaying(), kind + ": Play failed");
                    t.e06Phase = 2; t.wait = 15; return false;
                default:
                    StopScene();
                    CloseProject();
                    t.note("E06 %s: scaffold + open + build + play ok", kind.c_str());
                    ++t.e06Index; t.e06Phase = 0; t.stepStart = std::chrono::steady_clock::now(); return false;
                }
            });
            add("finish", [&] { return true; });
        }

        // ---- run the plan ------------------------------------------------------
        if (t.wait > 0) { --t.wait; return; }
        if (t.step < (int)t.plan.size())
        {
            const auto before = t.oracle.Take();
            const bool advance = t.plan[t.step]();
            const std::string why = t.oracle.Judge(before);
            if (!why.empty()) t.fail("ORACLE", "%s during '%s'", why.c_str(), t.planNames[t.step].c_str());
            if (advance)
            {
                t.note("step %d/%zu done: %s (%.1f s)", t.step + 1, t.plan.size(), t.planNames[t.step].c_str(), t.secondsInStep());
                ++t.step; t.stepStart = std::chrono::steady_clock::now();
            }
            else if (t.secondsInStep() > t.deadlineSec)
            {
                t.fail("DEADLINE", "step '%s' exceeded %.0f s", t.planNames[t.step].c_str(), t.deadlineSec);
                ++t.step; t.stepStart = std::chrono::steady_clock::now();
            }
            if (t.step < (int)t.plan.size()) return;
        }

        // ---- verdict + JSON ------------------------------------------------------
        t.finished = true;
        const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - t.start).count();
        const bool oracleOk = t.oracle.recoveredErrors == 0 && t.oracle.endFrameLeaks == 0 && t.oracle.contextDrift == 0;
        const bool pass = t.failures == 0 && oracleOk;
#if defined(NDEBUG)
        const char* cfg = "Release";
#else
        const char* cfg = "Debug";
#endif
        std::ofstream f(t.resultPath, std::ios::trunc);
        if (f)
        {
            char hb[24]; std::snprintf(hb, sizeof(hb), "%016llx", (unsigned long long)t.treeHash);
            f << "{\n  \"work_order\": \"AP-03\",\n  \"case\": \"E01-E08 + F01 + V05 editor authoring\",\n  \"config\": \"" << cfg << "\",\n";
            f << "  \"project\": \"" << Esc(t.projectDir) << "\",\n  \"fapp_tree_hash_fnv1a64\": \"" << hb << "\",\n";
            f << "  \"failed_checks\": " << t.failures << ",\n  \"total_seconds\": " << total << ",\n";
            f << "  \"live\": { \"builds\": " << t.liveBuilds << ", \"resumes\": " << t.liveResumes << ", \"failures\": " << t.liveFailures << " },\n";
            f << "  \"oracle\": { \"recovered_errors\": " << t.oracle.recoveredErrors << ", \"end_frame_leaks\": " << t.oracle.endFrameLeaks
              << ", \"context_drift\": " << t.oracle.contextDrift << ", \"hosted_imbalance\": " << m_HostedImbalance << " },\n";
            f << "  \"ids\": {";
            bool first = true;
            for (const char* id : { "E01", "E02", "E03", "E04", "E06", "E07", "E08", "F01", "V05" })
            {
                auto it = t.idStatus.find(id);
                f << (first ? " " : ", ") << "\"" << id << "\": \"" << (it == t.idStatus.end() ? "NOT_RUN" : it->second) << "\"";
                first = false;
            }
            f << " },\n  \"verdict\": \"" << (pass ? "PASS" : "FAIL") << "\",\n  \"shell_invocations\": [\n";
            for (size_t i = 0; i < t.shellTable.size(); ++i) f << "    \"" << Esc(t.shellTable[i]) << "\"" << (i + 1 < t.shellTable.size() ? "," : "") << "\n";
            f << "  ],\n  \"checks_failed\": [\n";
            for (size_t i = 0; i < t.checks.size(); ++i) f << "    \"" << Esc(t.checks[i]) << "\"" << (i + 1 < t.checks.size() ? "," : "") << "\n";
            f << "  ],\n  \"log\": [\n";
            for (size_t i = 0; i < t.log.size(); ++i) f << "    \"" << Esc(t.log[i]) << "\"" << (i + 1 < t.log.size() ? "," : "") << "\n";
            f << "  ]\n}\n";
        }
        {
            std::ofstream c(fs::path(t.resultPath).parent_path() / "ap03-editor-console.txt", std::ios::trunc);
            for (const auto& l : m_Ctx.ConsoleLines) c << l.Text << "\n";
        }
        std::printf("AP03_SELFTEST_RESULT=%s config=%s failedChecks=%d seconds=%.1f errors=%d leaks=%d drift=%d hosted=%d\n",
                    pass ? "PASS" : "FAIL", cfg, t.failures, total, t.oracle.recoveredErrors.load(), t.oracle.endFrameLeaks.load(),
                    t.oracle.contextDrift.load(), m_HostedImbalance);
        std::fflush(stdout);
        if (pass) Cosmic::Application::Get().Close();
        else      std::quick_exit(1);
    }
}
