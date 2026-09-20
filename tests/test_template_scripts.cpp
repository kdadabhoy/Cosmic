// test_template_scripts.cpp — compile smoke for every template script + the template
// scene validation (U8 / Q3; App Platform / AP-04).
//
// Template scripts only compile when a scaffolded project builds (the user's
// Ctrl+B) — so a template typo would surface on the USER's machine. Including them
// here builds them against the same SDK headers a project would, headless.
// `@PROJECT_NAME@` appears only in their comments, so the files compile verbatim.
//
// AP-04 adds:
//   * the game template's remaining scripts, the app template's three screen scripts
//     and its AppService (compiled in-exe from the real TU), driven on a DataBus;
//   * the scene-validation suite: every template / sample / PendulumLab scene loads
//     through SceneSerializer, carries the expected entities and component names
//     (contract §3 / §5 reflected names), and every widget block's field names are
//     the contract's. Components AP-02 has not registered yet are checked through the
//     serializer's verbatim-preservation path (OpaqueComponentsComponent); once they
//     are registered the same test checks them as live components AND validates the
//     field names against the reflection registry — a typo fails here before any
//     renderer sees it.

#include <doctest.h>

// ---- game template ---------------------------------------------------------------
#include "../Projects/Starforge/assets/templates/game/src/scripts/BouncingBall.h"
#include "../Projects/Starforge/assets/templates/game/src/scripts/PidController.h"
#include "../Projects/Starforge/assets/templates/game/src/scripts/PhysicsBall.h"
#include "../Projects/Starforge/assets/templates/game/src/scripts/PaddleController.h"
#include "../Projects/Starforge/assets/templates/game/src/scripts/PongBall.h"
#include "../Projects/Starforge/assets/templates/game/src/scripts/StoryUiBinding.h"   // Q3

// ---- app template ----------------------------------------------------------------
// The screen classes follow the §5 convention (<Name>Screen), so the app template's
// HomeScreen / SettingsScreen share their names with PendulumLab's (compiled into this
// exe by test_pendulumlab.cpp). The template copies are compile smoke only, so they are
// wrapped in a namespace here; every header they need is already included above
// (pragma once makes the nested <Cosmic.h> includes no-ops).
#include <Cosmic.h>
#include "scene/ui/UiComponents.h"
#include <cmath>
namespace AppTemplate
{
#include "../Projects/Starforge/assets/templates/app/src/screens/HomeScreen.h"
#include "../Projects/Starforge/assets/templates/app/src/screens/DashboardScreen.h"
#include "../Projects/Starforge/assets/templates/app/src/screens/SettingsScreen.h"
}
#include "services/AppService.h"   // templates/app/src on the include path; the .cpp is compiled in-exe

#include "data/DataBus.h"
#include "scripting/ServiceHost.h"
#include "scripting/ModuleRegistry.h"
#include "scripting/ModuleMacros.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/SceneSerializer.h"
#include "scene/FlowMachine.h"
#include "reflect/TypeRegistry.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// (No `using namespace Cosmic` until the scene suite: the template's AppService lives at
// global scope and would be ambiguous with Cosmic::AppService - exactly what a project's
// Module.cpp avoids by not opening the namespace.)

// =====================================================================================
// Script compile smoke + default fields
// =====================================================================================

TEST_CASE("U8: ForgePong template scripts compile and expose their tuned fields")
{
    PaddleController paddle;
    CHECK(paddle.Speed > 0.0f);
    CHECK(paddle.LimitY > 0.0f);
    CHECK_FALSE(paddle.UseArrows);   // left player default; the right one overrides

    PongBall ball;
    CHECK(ball.WinScore == 5);
    CHECK(ball.Speed > 0.0f);
    CHECK(ball.SpeedUp >= 1.0f);
    CHECK(ball.CourtHalfW > ball.CourtHalfH);   // a pong court is wide
}

TEST_CASE("Q3: the stock Story Graph UI binding template compiles")
{
    StoryUiBinding b;
    CHECK(StoryUiBinding::MaxOptions == 4);
    CHECK(b.TextTag == "StoryText");
    CHECK(b.OptionTagPrefix == "StoryOption");
}

TEST_CASE("AP-04: the game template's telemetry demo scripts compile with their defaults")
{
    BouncingBall ball;
    CHECK(ball.Gravity < 0.0f);
    CHECK(ball.Restitution > 0.0f); CHECK(ball.Restitution <= 1.0f);
    CHECK(ball.StartHeight > ball.FloorY);
    PidController pid;
    CHECK(pid.Target > 0.0f);
    CHECK(pid.Kp > 0.0f);
    PhysicsBall pb; (void)pb;
}

TEST_CASE("AP-04: the app template's screen scripts compile with their defaults")
{
    AppTemplate::HomeScreen home;
    CHECK(home.PulseHz > 0.0f);
    CHECK(home.PulseMin > 0.0f); CHECK(home.PulseMin < 1.0f);
    AppTemplate::DashboardScreen dash;
    CHECK(dash.HighlightAbove == doctest::Approx(10.0f));
    AppTemplate::SettingsScreen settings;
    CHECK(settings.ChangesSeen == 0);
}

TEST_CASE("AP-04: the app template's AppService publishes app.* channels and handles the counter signals")
{
    auto& reg = Cosmic::ModuleRegistry::Get();
    reg.UnregisterModule("ap04apptemplate");
    reg.BeginModule("ap04apptemplate");
    CS_SERVICE(AppService).Order(0) CS_END;
    reg.EndModule();

    Cosmic::DataBus bus; Cosmic::PanelRegistry panels; Cosmic::ServiceHost host;
    host.Instantiate("ap04apptemplate", Cosmic::AppContext{ bus, panels, nullptr, nullptr, "AppTemplate", false });
    REQUIRE(host.Count() == 1);
    CHECK(panels.Has("Diagnostics"));
    CHECK(panels.SourceOf("Diagnostics").File.find("AppService.cpp") != std::string::npos);
    // Seeded defaults + the published channels.
    CHECK(bus.GetNumber("settings.amplitude") == doctest::Approx(AppService::kDefaultAmplitude));
    CHECK(bus.GetNumber("settings.frequency") == doctest::Approx(AppService::kDefaultFrequency));
    CHECK(bus.Has("app.uptime")); CHECK(bus.Has("app.sine")); CHECK(bus.Has("app.counter"));
    CHECK(bus.GetNumber("app.counter") == 0.0);
    CHECK(bus.Producer("app.sine") == "AppService");
    // Tick: uptime advances, the sine follows amplitude * sin(2 pi f t).
    for (int i = 0; i < 60; ++i) { bus.Advance(1.0 / 60.0); host.Tick(1.0f / 60.0f); }
    const double up = bus.GetNumber("app.uptime");
    CHECK(up == doctest::Approx(1.0).epsilon(1e-3));
    CHECK(bus.GetNumber("app.sine") == doctest::Approx(std::sin(2.0 * 3.14159265358979323846 * AppService::kDefaultFrequency * up)).epsilon(1e-6));
    // Signals.
    host.DispatchSignal("counter.increment", Cosmic::Entity());
    host.DispatchSignal("counter.increment", Cosmic::Entity());
    CHECK(bus.GetNumber("app.counter") == 2.0);
    bus.Set("settings.counter_step", 5.0);
    host.DispatchSignal("counter.increment", Cosmic::Entity());
    CHECK(bus.GetNumber("app.counter") == 7.0);
    host.DispatchSignal("counter.reset", Cosmic::Entity());
    CHECK(bus.GetNumber("app.counter") == 0.0);
    // Auto-increment once per second when the toggle channel is on.
    bus.SetBool("settings.auto_increment", true);
    for (int i = 0; i < 130; ++i) { bus.Advance(1.0 / 60.0); host.Tick(1.0f / 60.0f); }
    CHECK(bus.GetNumber("app.counter") == 10.0);   // two seconds at step 5
    host.DispatchSignal("settings.defaults", Cosmic::Entity());
    CHECK(bus.GetNumber("settings.counter_step") == doctest::Approx(AppService::kDefaultStep));
    CHECK_FALSE(bus.GetBool("settings.auto_increment"));
    host.Destroy();
    reg.UnregisterModule("ap04apptemplate");
}

using namespace Cosmic;

// =====================================================================================
// Scene validation — every template scene through SceneSerializer
// =====================================================================================

namespace
{
    using json = nlohmann::json;

    // Contract §3 reflected names -> field names (AP-02 registers exactly these).
    const std::map<std::string, std::set<std::string>>& WidgetFields()
    {
        static const std::map<std::string, std::set<std::string>> f = {
            { "UiValueText",   { "Channel", "Format", "Prefix", "Suffix", "Placeholder", "StaleAfter", "StaleColor", "PreviewValue" } },
            { "UiGauge",       { "Channel", "Min", "Max", "Style", "Direction", "FillColor", "TrackColor", "Thickness", "PreviewValue" } },
            { "UiIndicator",   { "Channel", "Op", "Threshold", "OnTint", "OffTint", "OnTexture", "OffTexture", "PreviewOn" } },
            { "UiPlot",        { "Channel", "Channel2", "Channel3", "Channel4", "WindowSeconds", "AutoScaleY", "YMin", "YMax",
                                 "LineColor", "LineColor2", "LineColor3", "LineColor4", "GridColor", "BackgroundColor",
                                 "GridDivisions", "LineWidth", "ShowLabels", "PreviewAmplitude" } },
            { "UiSlider",      { "Channel", "Min", "Max", "Step", "Signal", "Orientation", "TrackColor", "FillColor", "KnobColor",
                                 "KnobSize", "Interactable", "PreviewValue" } },
            { "UiToggle",      { "Channel", "Signal", "OnTint", "OffTint", "OnTexture", "OffTexture", "Interactable", "PreviewOn" } },
            { "UiHostedPanel", { "PanelName", "ShowFrame", "FrameColor", "PlaceholderText" } },
        };
        return f;
    }

    struct Expect { std::string Tag; std::vector<std::string> Components; };
    struct SceneCase
    {
        std::string Path;                 // absolute
        std::vector<Expect> Entities;     // must exist with at least these components
        std::set<std::string> Scripts;    // allowed NativeScript ClassNames
        bool Screen = true;               // §5 screen convention: a "Canvas" entity + an ORTHOGRAPHIC primary camera
                                          // (false for the game template's world scene and for the two captured
                                          // samples, which are what BuildFlowDemo / BuildForgePong generate:
                                          // FlowDemo's menu screens carry a perspective camera and its game
                                          // scenes name their canvas "HUD")
    };

    std::string Templates() { return COSMIC_AP04_TEMPLATES; }
    std::string Pendulum()  { return COSMIC_PENDULUMLAB_DIR; }

    const std::vector<SceneCase>& Cases()
    {
        static const std::vector<SceneCase> c = {
            { Templates() + "/game/scenes/Main.cscene",
              { { "Camera", { "Camera" } }, { "Sprite", { "SpriteRenderer" } } }, {}, /*Screen=*/false },
            { Templates() + "/blank/scenes/Main.cscene",
              { { "Camera", { "Camera" } }, { "Canvas", { "Canvas" } } }, {} },
            { Templates() + "/app/scenes/Home.cscene",
              { { "Canvas", { "Canvas", "UiImage", "NativeScript" } }, { "Logo", { "UiImage" } }, { "Title", { "UiText" } },
                { "Uptime", { "UiText", "UiValueText" } }, { "DashboardButton", { "UiImage", "UiButton", "UiText" } },
                { "SettingsButton", { "UiButton" } }, { "QuitButton", { "UiButton" } } },
              { "HomeScreen" } },
            { Templates() + "/app/scenes/Dashboard.cscene",
              { { "Canvas", { "Canvas", "NativeScript" } }, { "HeaderUptime", { "UiText", "UiValueText" } },
                { "PlotFrame", { "UiImage" } }, { "Plot", { "UiPlot" } }, { "SineGauge", { "UiGauge" } },
                { "CounterGauge", { "UiGauge" } }, { "CounterValue", { "UiText", "UiValueText" } },
                { "CounterHigh", { "UiImage", "UiIndicator" } }, { "IncrementButton", { "UiButton" } },
                { "ResetButton", { "UiButton" } }, { "Diagnostics", { "UiHostedPanel" } }, { "BackButton", { "UiButton" } } },
              { "DashboardScreen" } },
            { Templates() + "/app/scenes/Settings.cscene",
              { { "Canvas", { "Canvas", "NativeScript" } }, { "AmplitudeSlider", { "UiSlider" } }, { "AmplitudeSliderValue", { "UiText", "UiValueText" } },
                { "FrequencySlider", { "UiSlider" } }, { "StepSlider", { "UiSlider" } }, { "AutoToggle", { "UiImage", "UiToggle" } },
                { "DefaultsButton", { "UiButton" } }, { "BackButton", { "UiButton" } } },
              { "SettingsScreen" } },
            { Templates() + "/samples/FlowDemo/scenes/MainMenu.cscene", { { "Canvas", { "Canvas" } }, { "PlayButton", { "UiButton", "UiImage", "UiText" } } }, {}, false },
            { Templates() + "/samples/FlowDemo/scenes/Game.cscene",     { { "HUD", { "Canvas" } }, { "Monument", { "SpriteRenderer" } } }, {}, false },
            { Templates() + "/samples/FlowDemo/scenes/Pause.cscene",    { { "Canvas", { "Canvas" } }, { "ResumeButton", { "UiButton" } } }, {}, false },
            { Templates() + "/samples/ForgePong/scenes/Menu.cscene",    { { "Canvas", { "Canvas" } }, { "PlayButton", { "UiButton" } } }, {}, false },
            { Templates() + "/samples/ForgePong/scenes/Game.cscene",    { { "HUD", { "Canvas" } }, { "Ball", { "SpriteRenderer", "NativeScript" } }, { "HitFx", { "SpriteAnimation" } } }, { "PaddleController", "PongBall" }, false },
            { Templates() + "/samples/ForgePong/scenes/Win.cscene",     { { "Canvas", { "Canvas" } }, { "RematchButton", { "UiButton" } } }, {}, false },
            { Pendulum() + "/scenes/Home.cscene",
              { { "Canvas", { "Canvas", "NativeScript" } }, { "Title", { "UiText" } }, { "Period", { "UiText", "UiValueText" } },
                { "StartButton", { "UiButton" } }, { "SettingsButton", { "UiButton" } }, { "QuitButton", { "UiButton" } } },
              { "HomeScreen" } },
            { Pendulum() + "/scenes/Lab.cscene",
              { { "Canvas", { "Canvas", "NativeScript" } }, { "Pivot", { "SpriteRenderer" } }, { "Rod", { "SpriteRenderer" } }, { "Bob", { "SpriteRenderer" } },
                { "Running", { "UiImage", "UiIndicator" } }, { "AngleValue", { "UiText", "UiValueText" } }, { "OmegaValue", { "UiText", "UiValueText" } },
                { "EnergyGauge", { "UiGauge" } }, { "Plot", { "UiPlot" } }, { "PhasePlot", { "UiHostedPanel" } },
                { "StartStopButton", { "UiButton" } }, { "ResetButton", { "UiButton" } }, { "NudgeButton", { "UiButton" } },
                { "SettingsButton", { "UiButton" } }, { "HomeButton", { "UiButton" } } },
              { "LabScreen" } },
            { Pendulum() + "/scenes/Settings.cscene",
              { { "Canvas", { "Canvas", "NativeScript" } }, { "LengthSlider", { "UiSlider" } }, { "GravitySlider", { "UiSlider" } },
                { "DampingSlider", { "UiSlider" } }, { "Theta0Slider", { "UiSlider" } }, { "SmallAngleToggle", { "UiImage", "UiToggle" } },
                { "Note", { "UiText" } }, { "DefaultsButton", { "UiButton" } }, { "BackButton", { "UiButton" } } },
              { "SettingsScreen" } },
            { Pendulum() + "/scenes/Stopped.cscene",
              { { "Canvas", { "Canvas", "UiImage", "NativeScript" } }, { "Energy", { "UiText", "UiValueText" } }, { "ResumeButton", { "UiButton" } } },
              { "StoppedScreen" } },
        };
        return c;
    }

    // Component presence through the production loader: registered -> a live component;
    // unregistered (AP-02 pending) -> preserved verbatim in OpaqueComponentsComponent.
    bool HasComponentNamed(Scene& scene, entt::entity e, const std::string& name, bool& live)
    {
        auto& reg = scene.GetRegistry();
        if (const auto* d = Reflect::GetRegistry().FindByName(name))
        {
            live = true;
            return d->Has(reg, e);
        }
        live = false;
        if (const auto* opaque = reg.try_get<OpaqueComponentsComponent>(e))
            for (const auto& b : opaque->Blocks) if (b.first == name) return true;
        return false;
    }

    entt::entity FindTag(Scene& scene, const std::string& tag)
    {
        auto& reg = scene.GetRegistry();
        for (auto e : reg.view<TagComponent>())
            if (reg.get<TagComponent>(e).Tag == tag) return e;
        return entt::null;
    }
}

TEST_CASE("AP-04: every template / sample / PendulumLab scene loads with the expected entities and component names")
{
    int scenes = 0, widgets = 0, liveWidgets = 0;
    for (const SceneCase& c : Cases())
    {
        CAPTURE(c.Path);
        Scene scene;
        REQUIRE_MESSAGE(SceneSerializer::Load(scene, c.Path), "scene failed to load: " << c.Path);
        ++scenes;
        auto& reg = scene.GetRegistry();

        // Exactly one primary camera everywhere; §5 screens: ORTHOGRAPHIC, plus a "Canvas".
        {
            int primary = 0, primaryOrtho = 0, cameras = 0;
            for (auto e : reg.view<CameraComponent>())
            {
                ++cameras;
                const auto& cam = reg.get<CameraComponent>(e);
                if (cam.Primary) ++primary;
                if (cam.Primary && cam.ProjectionType == CameraComponent::Projection::Orthographic) ++primaryOrtho;
                if (c.Screen) CHECK(cam.ProjectionType == CameraComponent::Projection::Orthographic);
            }
            CHECK(cameras == 1);
            CHECK(primary == 1);
            if (c.Screen)
            {
                CHECK(primaryOrtho == 1);
                const entt::entity canvas = FindTag(scene, "Canvas");
                REQUIRE(canvas != entt::null);
                CHECK(reg.all_of<CanvasComponent>(canvas));
            }
        }
        // No 3D leftovers anywhere: the only blocks the loader may not know are the AP-02 widgets.
        for (auto e : reg.view<TagComponent>())
        {
            if (const auto* opaque = reg.try_get<OpaqueComponentsComponent>(e))
                for (const auto& b : opaque->Blocks)
                {
                    CAPTURE(b.first);
                    CHECK(WidgetFields().count(b.first) == 1);
                }
        }
        // Expected entities + components.
        for (const Expect& ex : c.Entities)
        {
            CAPTURE(ex.Tag);
            const entt::entity e = FindTag(scene, ex.Tag);
            REQUIRE_MESSAGE(e != entt::null, "entity missing: " << ex.Tag);
            for (const std::string& comp : ex.Components)
            {
                CAPTURE(comp);
                bool live = false;
                CHECK_MESSAGE(HasComponentNamed(scene, e, comp, live), "component missing on " << ex.Tag << ": " << comp);
            }
        }
        // Scripts: every NativeScript names a class this template registers.
        for (auto e : reg.view<NativeScriptComponent>())
        {
            const std::string& cls = reg.get<NativeScriptComponent>(e).ClassName;
            CAPTURE(cls);
            CHECK(c.Scripts.count(cls) == 1);
        }

        // Widget blocks: every field name is the contract's (and, once AP-02 registers the
        // struct, a reflected field); sibling rules (§3): UiValueText needs UiText,
        // UiToggle / UiIndicator draw through a sibling UiImage.
        std::ifstream f(c.Path);
        const json doc = json::parse(f, nullptr, false);
        REQUIRE(doc.is_object());
        for (const auto& ent : doc["entities"])
        {
            const auto& comps = ent["components"];
            const std::string tag = comps.contains("Tag") ? comps["Tag"].value("Tag", "") : "";
            CAPTURE(tag);
            for (const auto& item : comps.items())
            {
                const auto it = WidgetFields().find(item.key());
                if (it == WidgetFields().end()) continue;
                ++widgets;
                const auto* d = Reflect::GetRegistry().FindByName(item.key());
                if (d) ++liveWidgets;
                for (const auto& field : item.value().items())
                {
                    CAPTURE(field.key());
                    CHECK_MESSAGE(it->second.count(field.key()) == 1, item.key() << " has a field the contract does not name: " << field.key());
                    if (d) CHECK_MESSAGE(d->FindField(field.key()) != nullptr, item.key() << "." << field.key() << " is not a reflected field");
                }
                CHECK(item.value().contains(item.key() == "UiHostedPanel" ? "PanelName" : "Channel"));
                if (item.key() == "UiValueText") CHECK(comps.contains("UiText"));
                if (item.key() == "UiToggle" || item.key() == "UiIndicator") CHECK(comps.contains("UiImage"));
            }
            if (comps.contains("UiButton"))
                CHECK_FALSE(comps["UiButton"].value("Signal", "").empty());
        }
    }
    CHECK(scenes == (int)Cases().size());
    CHECK(widgets >= 30);   // 35 authored blocks across the app template + PendulumLab
    MESSAGE("validated " << scenes << " scenes, " << widgets << " widget blocks (" << liveWidgets
            << " through registered components, " << (widgets - liveWidgets) << " via verbatim preservation pending AP-02)");
}

TEST_CASE("AP-04: the app template's and PendulumLab's flows load, validate and name only screens that exist")
{
    for (const std::string& root : { Templates() + "/app", Pendulum() })
    {
        CAPTURE(root);
        FlowAsset a; std::string err;
        REQUIRE_MESSAGE(FlowAsset::Load(a, root + "/flows/Main.cflow", &err), err);
        CHECK(a.Validate().empty());
        CHECK(a.Start == "Home");
        std::set<std::string> names;
        for (const auto& s : a.States)
        {
            names.insert(s.Name);
            // "project://scenes/<Name>.cscene" -> the file exists and the §5 name matches.
            CHECK(s.Scene == "project://scenes/" + s.Name + ".cscene");
            std::ifstream f(root + "/scenes/" + s.Name + ".cscene");
            CHECK_MESSAGE(f.good(), "missing screen scene for state " << s.Name);
        }
        bool escape = false, quit = false;
        for (const auto& s : a.States)
            for (const auto& t : s.Transitions)
            {
                if (t.On == "key:Escape") escape = true;
                if (t.To == "@quit") quit = true;
                if (t.To != "@quit" && t.To != "@pop") CHECK(names.count(t.To) == 1);
            }
        CHECK(escape); CHECK(quit);
        const auto keys = FlowMachine::KeySignals(a);
        CHECK(std::find(keys.begin(), keys.end(), "key:Escape") != keys.end());
    }
    // PendulumLab's channel-guarded `when` transition (F02).
    FlowAsset p; REQUIRE(FlowAsset::Load(p, Pendulum() + "/flows/Main.cflow"));
    bool when = false;
    for (const auto& s : p.States) if (s.Name == "Lab")
        for (const auto& t : s.Transitions)
            if (t.On == "when") { when = true; CHECK(t.Push); CHECK(t.To == "Stopped"); CHECK(t.HasGuard); CHECK(t.Guard.Channel == "pendulum.energy"); CHECK(t.Guard.Op == "<"); }
    CHECK(when);
}

TEST_CASE("AP-04: every template carries kind, markers and the token only where it belongs")
{
    struct T { std::string Dir, Kind; };
    for (const T& t : { T{ "game", "game" }, T{ "blank", "blank" }, T{ "app", "app" }, T{ "samples/FlowDemo", "game" }, T{ "samples/ForgePong", "game" } })
    {
        CAPTURE(t.Dir);
        std::ifstream m(Templates() + "/" + t.Dir + "/project.cproj");
        REQUIRE(m.good());
        std::stringstream ms; ms << m.rdbuf();
        CHECK(ms.str().find("kind          = \"" + t.Kind + "\"") != std::string::npos);
        CHECK(ms.str().find("name          = \"@PROJECT_NAME@\"") != std::string::npos);
        std::ifstream mod(Templates() + "/" + t.Dir + "/src/Module.cpp");
        REQUIRE(mod.good());
        std::stringstream mods; mods << mod.rdbuf();
        CHECK(mods.str().find("// CS_SCREENS_BEGIN") != std::string::npos);
        CHECK(mods.str().find("// CS_SCREENS_END") != std::string::npos);
        CHECK(mods.str().find("CS_MODULE_BEGIN(@PROJECT_NAME@)") != std::string::npos);
        std::ifstream cm(Templates() + "/" + t.Dir + "/CMakeLists.txt");
        CHECK(cm.good());
    }
    // The app template's binaries carry no token (ScaffoldProjectTo replaces bytes blindly).
    for (const char* png : { "panel.png", "logo.png" })
    {
        std::ifstream f(Templates() + "/app/assets/ui/" + png, std::ios::binary);
        REQUIRE(f.good());
        std::stringstream s; s << f.rdbuf();
        CHECK(s.str().size() > 60);
        CHECK(s.str().substr(0, 8) == std::string("\x89PNG\r\n\x1a\n", 8));
        CHECK(s.str().find("@PROJECT_NAME@") == std::string::npos);
    }
    // PendulumLab is an app with markers too.
    std::ifstream pm(Pendulum() + "/project.cproj"); std::stringstream pms; pms << pm.rdbuf();
    CHECK(pms.str().find("kind          = \"app\"") != std::string::npos);
    CHECK(pms.str().find("fixed_dt_hz   = 240") != std::string::npos);
    std::ifstream pmod(Pendulum() + "/src/Module.cpp"); std::stringstream pmods; pmods << pmod.rdbuf();
    CHECK(pmods.str().find("CS_SERVICE(PendulumService)") != std::string::npos);
    CHECK(pmods.str().find("// CS_SCREENS_BEGIN") != std::string::npos);
}
