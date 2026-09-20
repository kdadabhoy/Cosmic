// test_pendulumlab.cpp — PendulumLab's physics and flow, headless (App Platform / AP-04;
// catalog Y01 units + F02 U).
//
// The sample's REAL service TU (Projects/PendulumLab/src/services/PendulumService.cpp)
// is compiled into this exe and registered with the same CS_SERVICE macro its
// Module.cpp uses; a ServiceHost drives it on a DataBus at the project's fixed step
// (1/240 s) exactly as PlayerLayer does — no copy of the integrator, no private
// mutation.
//
//   Y01  undamped RK4 (small-angle model) vs the analytic F-PENDULUM reference within
//        1e-4 rad over 10 s; monotone energy decrease with c = 0.05 (and the damped
//        reference within the same bound); the zero-crossing period estimate within 1 %
//        of 2 pi sqrt(L / g) (both models); two runs bit-identical (FNV-1a over the
//        published angle series); parameters are read from the bus (L = 4 doubles T).
//   F02  the sample's own flows/Main.cflow + scenes + screen scripts, driven headlessly:
//        Home -(start_clicked)-> Lab -(key:Escape)-> Home; Lab -(settings_clicked)->
//        Settings -(back_clicked)-> Lab; `when pendulum.energy < 0.01` pushes Stopped and
//        resume_clicked pops it (the StoppedScreen script re-releases the bob so the guard
//        does not re-arm); a scripted 200-step sequence gives the same state trace twice.
//
// Non-vacuity: the reference comparison was run once with the RK4 step replaced by an
// explicit Euler step (see evidence/AP-04/report.md) and failed at 1.97e-2 rad (1055 energy increases under damping).

#include <doctest.h>

#include "services/PendulumService.h"
#include "screens/HomeScreen.h"
#include "screens/LabScreen.h"
#include "screens/SettingsScreen.h"
#include "screens/StoppedScreen.h"

#include "data/DataBus.h"
#include "scripting/AppService.h"
#include "scripting/ServiceHost.h"
#include "scripting/ModuleRegistry.h"
#include "scripting/ModuleMacros.h"
#include "scripting/ScriptHost.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/EventBus.h"
#include "scene/SceneSerializer.h"
#include "scene/FlowMachine.h"
#include "scene/FlowKeyBridge.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace Cosmic;

namespace
{
    constexpr float  kDt      = 1.0f / 240.0f;
    constexpr int    kSteps   = 2400;            // 10 s
    constexpr double kPi      = 3.14159265358979323846;
    constexpr double kL       = 1.0;
    constexpr double kG       = 9.80665;
    const char*      kModule  = "ap04test";

    // ---- module registration (once per process; the same macros Module.cpp uses) ----
    void RegisterOnce()
    {
        static bool done = false;
        if (done) return;
        done = true;
        auto& reg = ModuleRegistry::Get();
        reg.BeginModule(kModule);
        CS_SERVICE(PendulumService).Order(0) CS_END;
        CS_SCRIPT(HomeScreen)     CS_FIELD(SwingPx)   CS_END;
        CS_SCRIPT(LabScreen)      CS_FIELD(RodWidth)  CS_END;
        CS_SCRIPT(SettingsScreen) CS_FIELD(ChangesSeen) CS_END;
        CS_SCRIPT(StoppedScreen)  CS_FIELD(ResetOnResume) CS_END;
        reg.EndModule();
    }

    // ---- the F-PENDULUM reference -----------------------------------------------
    struct RefRow { double t, theta0, omega0, theta005, omega005; };

    std::vector<RefRow> LoadReference()
    {
        std::vector<RefRow> rows;
        std::ifstream f(std::string(COSMIC_AP04_FIXTURES) + "/pendulum_reference.csv");
        REQUIRE_MESSAGE(f.good(), "F-PENDULUM fixture missing: run tests/fixtures/ap04/Generate-PendulumReference.ps1");
        std::string line;
        while (std::getline(f, line))
        {
            if (line.empty() || line[0] == '#' || line[0] == 't') continue;
            RefRow r{};
            std::replace(line.begin(), line.end(), ',', ' ');
            std::istringstream ss(line);
            ss >> r.t >> r.theta0 >> r.omega0 >> r.theta005 >> r.omega005;
            rows.push_back(r);
        }
        return rows;
    }

    // ---- a headless host: bus + panels + ServiceHost around the real service --------
    struct Rig
    {
        DataBus       Bus;
        PanelRegistry Panels;
        ServiceHost   Host;
        Rig(bool smallAngle, double damping, double theta0Deg = 5.0, double length = kL)
        {
            RegisterOnce();
            // Settings live on the bus BEFORE the service attaches (the service seeds only
            // what is missing — the same precedence a live-reloaded bus gets).
            Bus.SetBool("settings.small_angle", smallAngle);
            Bus.Set("settings.damping", damping);
            Bus.Set("settings.theta0_deg", theta0Deg);
            Bus.Set("settings.length", length);
            Host.Instantiate(kModule, AppContext{ Bus, Panels, nullptr, nullptr, "PendulumLab", false });
        }
        ~Rig() { Host.Destroy(); }
        void Start() { Host.DispatchSignal("pendulum.start", Entity()); }
        void Step()  { Bus.Advance((double)kDt); Host.FixedTick(kDt); }
        double Angle()  const { return Bus.GetNumber("pendulum.angle_deg"); }
        double Omega()  const { return Bus.GetNumber("pendulum.omega"); }
        double Energy() const { return Bus.GetNumber("pendulum.energy"); }
    };

    uint64_t Fnv1a(const std::vector<double>& v)
    {
        uint64_t h = 1469598103934665603ull;
        for (double d : v)
        {
            unsigned char b[sizeof(double)]; std::memcpy(b, &d, sizeof b);
            for (unsigned char c : b) { h ^= c; h *= 1099511628211ull; }
        }
        return h;
    }
}

// =====================================================================================
// Y01 — physics
// =====================================================================================

TEST_CASE("AP-04 Y01: undamped small-angle RK4 at 1/240 matches F-PENDULUM within 1e-4 rad over 10 s")
{
    const auto ref = LoadReference();
    REQUIRE(ref.size() == (size_t)kSteps + 1);
    CHECK(ref[0].t == 0.0);
    CHECK(ref[kSteps].t == 10.0);

    Rig rig(/*smallAngle=*/true, /*damping=*/0.0);
    CHECK(rig.Bus.Producer("pendulum.angle_deg") == "PendulumService");   // the host bracketed the attach writes
    rig.Start();
    double maxTheta = 0.0, maxOmega = 0.0;
    for (int i = 0; i <= kSteps; ++i)
    {
        const double theta = rig.Angle() * kPi / 180.0;
        maxTheta = std::max(maxTheta, std::abs(theta - ref[(size_t)i].theta0));
        maxOmega = std::max(maxOmega, std::abs(rig.Omega() - ref[(size_t)i].omega0));
        if (i < kSteps) rig.Step();
    }
    INFO("max |theta - ref| = " << maxTheta << " rad, max |omega - ref| = " << maxOmega << " rad/s");
    CHECK(maxTheta < 1e-4);
    CHECK(maxOmega < 1e-3);
    CHECK(maxTheta > 0.0);   // a real integrator, not the reference read back
    CHECK(std::abs(rig.Bus.GetNumber("pendulum.period_est") - 2.0 * kPi * std::sqrt(kL / kG)) / (2.0 * kPi * std::sqrt(kL / kG)) < 0.01);
}

TEST_CASE("AP-04 Y01: damping 0.05 decreases the energy monotonically and tracks the damped reference")
{
    const auto ref = LoadReference();
    for (bool smallAngle : { true, false })
    {
        CAPTURE(smallAngle);
        Rig rig(smallAngle, 0.05);
        rig.Start();
        double prevE = rig.Energy(); const double e0 = prevE;
        int increases = 0; double maxTheta = 0.0;
        for (int i = 0; i <= kSteps; ++i)
        {
            const double e = rig.Energy();
            if (e > prevE) ++increases;
            prevE = e;
            if (smallAngle) maxTheta = std::max(maxTheta, std::abs(rig.Angle() * kPi / 180.0 - ref[(size_t)i].theta005));
            if (i < kSteps) rig.Step();
        }
        CHECK(increases == 0);
        CHECK(prevE < e0 * 0.65);                     // e^{-0.05 * 10} = 0.61 of the initial energy
        CHECK(prevE > e0 * 0.55);
        if (smallAngle) CHECK(maxTheta < 1e-4);
    }
}

TEST_CASE("AP-04 Y01: the zero-crossing period estimate is within 1 % of 2 pi sqrt(L/g), and L comes from the bus")
{
    const double T0 = 2.0 * kPi * std::sqrt(kL / kG);
    for (bool smallAngle : { true, false })
    {
        CAPTURE(smallAngle);
        Rig rig(smallAngle, 0.0);
        rig.Start();
        CHECK(rig.Bus.GetNumber("pendulum.period_est") == 0.0);          // no crossing yet
        for (int i = 0; i < kSteps; ++i) rig.Step();
        const double est = rig.Bus.GetNumber("pendulum.period_est");
        INFO("period_est " << est << " s vs " << T0);
        CHECK(std::abs(est - T0) / T0 < 0.01);
        CHECK(std::abs(est - T0) > 0.0);
    }
    // L = 4 m: T doubles. The service reads settings.length every fixed step.
    {
        Rig rig(true, 0.0, 5.0, 4.0);
        rig.Start();
        for (int i = 0; i < kSteps; ++i) rig.Step();
        CHECK(std::abs(rig.Bus.GetNumber("pendulum.period_est") - 2.0 * T0) / (2.0 * T0) < 0.01);
    }
}

TEST_CASE("AP-04 Y01: two runs publish a bit-identical angle series")
{
    auto run = [](std::vector<double>& out)
    {
        Rig rig(false, 0.02);
        rig.Start();
        out.clear(); out.reserve(kSteps + 1);
        for (int i = 0; i <= kSteps; ++i) { out.push_back(rig.Angle()); if (i < kSteps) rig.Step(); }
    };
    std::vector<double> a, b;
    run(a); run(b);
    REQUIRE(a.size() == b.size());
    CHECK(Fnv1a(a) == Fnv1a(b));
    CHECK(std::memcmp(a.data(), b.data(), a.size() * sizeof(double)) == 0);
}

TEST_CASE("AP-04 Y01: start/stop/reset/nudge signals and the published channels")
{
    Rig rig(false, 0.0);
    CHECK_FALSE(rig.Bus.GetBool("pendulum.running"));
    CHECK(rig.Angle() == doctest::Approx(5.0));
    CHECK(rig.Bus.GetNumber("settings.gravity") == doctest::Approx(9.80665));   // seeded default
    // Not running: stepping does not move it.
    for (int i = 0; i < 24; ++i) rig.Step();
    CHECK(rig.Angle() == doctest::Approx(5.0));
    rig.Start();
    CHECK(rig.Bus.GetBool("pendulum.running"));
    for (int i = 0; i < 240; ++i) rig.Step();
    CHECK(rig.Angle() != doctest::Approx(5.0));
    rig.Host.DispatchSignal("pendulum.stop", Entity());
    const double held = rig.Angle();
    for (int i = 0; i < 24; ++i) rig.Step();
    CHECK(rig.Angle() == held);
    rig.Host.DispatchSignal("pendulum.nudge", Entity());
    CHECK(rig.Omega() == doctest::Approx(PendulumService::kNudgeOmega).epsilon(0.5));   // the previous omega plus the kick
    rig.Host.DispatchSignal("pendulum.reset", Entity());
    CHECK(rig.Angle() == doctest::Approx(5.0));
    CHECK(rig.Omega() == 0.0);
    CHECK(rig.Bus.GetNumber("pendulum.period_est") == 0.0);
    CHECK(rig.Bus.Has("pendulum.energy"));
    CHECK(rig.Bus.Has("pendulum.phaseplot_draws"));
    CHECK(rig.Panels.Has("PhasePlot"));
    CHECK(rig.Panels.SourceOf("PhasePlot").File.find("PendulumService.cpp") != std::string::npos);
}

// =====================================================================================
// F02 — the flow, headless, with the sample's real scenes and screen scripts
// =====================================================================================

namespace
{
    // A minimal PlayerLayer: the flow owns scene selection; scripts + services rebind
    // on a swap in the production order (services BindScene before scripts Instantiate).
    struct FlowRig
    {
        Rig          Svc{ false, 0.0 };
        FlowAsset    Asset;
        FlowMachine  Flow;
        ScriptHost   Scripts;
        FlowKeyBridge Keys;
        Ref<Scene>   Tracked;
        std::vector<std::string> Loads;
        bool EscapeDown = false;

        FlowRig()
        {
            std::string err;
            REQUIRE_MESSAGE(FlowAsset::Load(Asset, std::string(COSMIC_PENDULUMLAB_DIR) + "/flows/Main.cflow", &err), err);
            REQUIRE(Asset.Validate().empty());
            Flow.SetDataBus(&Svc.Bus);
            Flow.SetSceneLoader([this](const std::string& p) -> Ref<Scene>
            {
                Loads.push_back(p);
                // "project://scenes/X.cscene" -> the sample's real file.
                const std::string rel = p.substr(p.find("scenes/"));
                Ref<Scene> s = Scene::Create();
                if (!SceneSerializer::Load(*s, std::string(COSMIC_PENDULUMLAB_DIR) + "/" + rel)) return nullptr;
                return s;
            });
            Scripts.SetDataBus(&Svc.Bus);
            Keys.Bind(Asset, [this](int key) { return key == FlowKeyBridge::KeyCodeFor("Escape") && EscapeDown; });
            Flow.Start(Asset);
            Rebind();
        }
        ~FlowRig()
        {
            // PlayerLayer::OnDetach order: scripts, then services unbind, before the scenes die.
            Scripts.Destroy();
            Svc.Host.BindScene(nullptr);
            Flow.Stop();
            Tracked.reset();
        }

        void Rebind()
        {
            Ref<Scene> active = Flow.ActiveScene();
            if (active == Tracked) return;
            Scripts.Destroy();
            Svc.Host.BindScene(active.get());
            Tracked = active;
            if (Tracked) Scripts.Instantiate(*Tracked);
        }
        // One frame == one fixed step at 240 Hz, in the Application's order: the fixed
        // tick (services, then scripts) precedes the variable tick (PlayerLayer::OnUpdate).
        void Frame()
        {
            Svc.Host.FixedTick(kDt);
            Scripts.FixedTick(kDt);
            Svc.Bus.Advance((double)kDt);
            Svc.Host.Tick(kDt);
            Keys.Poll(Flow);
            Flow.OnUpdate(kDt);
            Rebind();
            Scripts.Tick(kDt);
        }
        void Click(const char* signal) { if (Tracked) Tracked->Events().Emit(signal, Entity()); }   // what UiButton does
        const std::string& State() const { return Flow.CurrentState(); }
    };
}

TEST_CASE("AP-04 F02: Home -> Lab -> Home by Escape; Lab -> Settings -> Lab")
{
    FlowRig r;
    CHECK(r.State() == "Home");
    CHECK(r.Tracked != nullptr);
    CHECK(r.Scripts.LiveCount() == 1);            // HomeScreen on the canvas
    CHECK_FALSE(r.Svc.Bus.GetBool("pendulum.running"));
    r.Frame();
    r.Click("start_clicked"); r.Frame();
    CHECK(r.State() == "Lab");
    CHECK(r.Svc.Bus.GetBool("pendulum.running"));   // LabScreen::OnStart emitted pendulum.start
    const double a0 = r.Svc.Bus.GetNumber("pendulum.angle_deg");
    for (int i = 0; i < 120; ++i) r.Frame();
    CHECK(r.Svc.Bus.GetNumber("pendulum.angle_deg") != a0);
    // The rig follows the bus: the bob sits at pivot + L * (sin theta, -cos theta).
    {
        Entity bob, pivot, rod;
        auto& reg = r.Tracked->GetRegistry();
        for (auto e : reg.view<TagComponent>())
        {
            const auto& tag = reg.get<TagComponent>(e).Tag;
            if (tag == "Bob") bob = Entity(e, r.Tracked.get());
            if (tag == "Pivot") pivot = Entity(e, r.Tracked.get());
            if (tag == "Rod") rod = Entity(e, r.Tracked.get());
        }
        REQUIRE(bob); REQUIRE(pivot); REQUIRE(rod);
        const auto& pb = bob.GetComponent<TransformComponent>().Position;
        const auto& pp = pivot.GetComponent<TransformComponent>().Position;
        const float len = rod.GetComponent<TransformComponent>().Scale.y;
        const double th = r.Svc.Bus.GetNumber("pendulum.angle_deg") * kPi / 180.0;
        CHECK(pb.x == doctest::Approx(pp.x + len * std::sin(th)).epsilon(1e-4));
        CHECK(pb.y == doctest::Approx(pp.y - len * std::cos(th)).epsilon(1e-4));
        CHECK(rod.GetComponent<TransformComponent>().Rotation.z == doctest::Approx(th * 180.0 / kPi).epsilon(1e-4));
    }
    // key:Escape through the key bridge (rising edge) -> Home.
    r.EscapeDown = true; r.Frame(); r.EscapeDown = false; r.Frame();
    CHECK(r.State() == "Home");
    CHECK(r.Svc.Bus.GetBool("pendulum.running"));   // the service keeps running across screens
    // Lab -> Settings -> Lab.
    r.Click("start_clicked"); r.Frame();
    CHECK(r.State() == "Lab");
    r.Click("settings_clicked"); r.Frame();
    CHECK(r.State() == "Settings");
    CHECK(r.Scripts.LiveCount() == 1);            // SettingsScreen
    r.Click("back_clicked"); r.Frame();
    CHECK(r.State() == "Lab");
    CHECK(r.Loads.size() == 6);                   // Home, Lab, Home, Lab, Settings, Lab
}

TEST_CASE("AP-04 F02: `when pendulum.energy < 0.01` pushes Stopped; resume_clicked pops and re-releases")
{
    FlowRig r;
    r.Click("start_clicked"); r.Frame();
    REQUIRE(r.State() == "Lab");
    for (int i = 0; i < 24; ++i) r.Frame();
    CHECK(r.Flow.StackDepth() == 1);
    // Re-release from 2 degrees: E = g L (1 - cos 2 deg) = 0.006 < 0.01.
    r.Svc.Bus.Set("settings.theta0_deg", 2.0);
    r.Click("pendulum.reset"); r.Frame();
    CHECK(r.Svc.Bus.GetNumber("pendulum.energy") < 0.01);
    r.Frame();
    CHECK(r.State() == "Stopped");
    CHECK(r.Flow.StackDepth() == 2);
    CHECK(r.Scripts.LiveCount() == 1);            // StoppedScreen on the overlay's canvas
    for (int i = 0; i < 12; ++i) r.Frame();
    CHECK(r.State() == "Stopped");                // the guard fires at most once per update and the state holds
    // Resume: the StoppedScreen turns the click into pendulum.reset (release angle back at 5 deg first).
    r.Svc.Bus.Set("settings.theta0_deg", 5.0);
    r.Click("resume_clicked"); r.Frame();
    CHECK(r.State() == "Lab");
    CHECK(r.Flow.StackDepth() == 1);
    CHECK(r.Svc.Bus.GetNumber("pendulum.energy") > 0.01);
    for (int i = 0; i < 24; ++i) r.Frame();
    CHECK(r.State() == "Lab");                    // the guard did not re-arm
}

TEST_CASE("AP-04 F02: a scripted 200-step sequence gives the same state trace on two runs")
{
    auto run = [](std::vector<std::string>& trace)
    {
        FlowRig r;
        trace.clear();
        for (int step = 0; step < 200; ++step)
        {
            switch (step)
            {
            case 5:   r.Click("start_clicked"); break;
            case 40:  r.Click("settings_clicked"); break;
            case 60:  r.Svc.Bus.Set("settings.damping", 0.3); r.Click("back_clicked"); break;
            case 90:  r.Svc.Bus.Set("settings.theta0_deg", 2.0); r.Click("pendulum.reset"); break;
            case 120: r.Svc.Bus.Set("settings.theta0_deg", 5.0); r.Click("resume_clicked"); break;
            case 150: r.EscapeDown = true; break;
            case 151: r.EscapeDown = false; break;
            case 170: r.Click("start_clicked"); break;
            case 190: r.Click("home_clicked"); break;
            default: break;
            }
            r.Frame();
            char buf[96];
            std::snprintf(buf, sizeof buf, "%s/%zu/%.9f", r.State().c_str(), r.Flow.StackDepth(), r.Svc.Bus.GetNumber("pendulum.angle_deg"));
            trace.emplace_back(buf);
        }
    };
    std::vector<std::string> a, b;
    run(a); run(b);
    REQUIRE(a.size() == 200);
    CHECK(a == b);
    // The trace visited what the script asked for, in order.
    auto stateAt = [&](int i) { return a[(size_t)i].substr(0, a[(size_t)i].find('/')); };
    CHECK(stateAt(4) == "Home");
    CHECK(stateAt(10) == "Lab");
    CHECK(stateAt(45) == "Settings");
    CHECK(stateAt(65) == "Lab");
    CHECK(stateAt(95) == "Stopped");
    CHECK(stateAt(125) == "Lab");
    CHECK(stateAt(155) == "Home");
    CHECK(stateAt(175) == "Lab");
    CHECK(stateAt(195) == "Home");
}
