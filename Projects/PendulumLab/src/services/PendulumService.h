#pragma once
// PendulumService.h — the physics of PendulumLab (Cosmic App Platform / AP-04).
//
// The whole simulation lives here, in C++, as an AppService: one instance per run,
// constructed by the host after the module loads, stepped in OnFixedUpdate with the
// host's fixed step (project.cproj: fixed_dt_hz = 240), and torn down before the
// module unloads. It never touches an entity — it reads its parameters from the
// DataBus, writes its state back to the DataBus, and reacts to signals the UI
// emits. The scenes (authored in Starforge) bind widgets to those channels and the
// screen scripts move the rig sprites from them, so "visuals in the editor, logic
// in C++" is literal.
//
// Model (per unit mass, angle from the vertical, CCW positive):
//   theta'' = -(g / L) * f(theta) - c * theta'        f = sin (default) or identity
//   E       = 1/2 (L theta')^2 + g L (1 - cos theta)  (or 1/2 g L theta^2 linearised)
// integrated with the classic RK4 of math/Integrators.h at the fixed step.
//
// Bus channels READ every fixed step (seeded with defaults on attach when missing):
//   settings.length (1 m)   settings.gravity (9.80665 m/s^2)   settings.damping (0 1/s)
//   settings.theta0_deg (5 deg, the release angle Reset returns to)
//   settings.small_angle (false; true = the linearised model the F-PENDULUM reference solves)
// Bus channels PUBLISHED every fixed step:
//   pendulum.angle_deg  pendulum.omega (rad/s)  pendulum.energy (J/kg)
//   pendulum.period_est (s, upward zero-crossing estimate; 0 until two crossings)
//   pendulum.running (bool)  pendulum.phaseplot_draws (hosted-panel draw count)
// Signals handled (scene EventBus, fanned in by the ServiceHost):
//   pendulum.start  pendulum.stop  pendulum.reset  pendulum.nudge  startstop_clicked
//   settings.defaults
// Hosted panel: CS_PANEL("PhasePlot") — an ImPlot theta/omega scatter of the bus history.

#include <Cosmic.h>

#include <functional>
#include <vector>

class PendulumService : public Cosmic::AppService
{
public:
    // One-shot hook run at the END of the next PhasePlot hosted-panel draw, i.e. inside the host's
    // ImGui pass AFTER the scene render of that frame, with the viewport framebuffer complete. The
    // Y02 self-test uses it for its plot-ROI readback (a service's OnUpdate runs BEFORE the render,
    // when the host has just cleared the target). Test seam; nothing else calls it.
    static void SetAfterPhasePlotDrawOnce(std::function<void()> fn);
    struct State
    {
        double Theta = 0.0;   // rad
        double Omega = 0.0;   // rad/s
        State operator+(const State& o) const { return { Theta + o.Theta, Omega + o.Omega }; }
        State operator*(double k)       const { return { Theta * k, Omega * k }; }
    };

    struct Params
    {
        double Length     = 1.0;
        double Gravity    = 9.80665;
        double Damping    = 0.0;
        double Theta0Deg  = 5.0;
        bool   SmallAngle = false;
    };

    static constexpr double kPi           = 3.14159265358979323846;
    static constexpr double kNudgeOmega   = 1.0;    // rad/s added by pendulum.nudge
    static constexpr double kStopEnergy   = 0.01;   // the flow's `when` threshold (documentation only)
    static constexpr size_t kHistory      = 4096;   // ~17 s at 240 Hz for the plots

    // Pure pieces (the unit test drives these directly as well as through the host).
    static State  Step(const State& s, const Params& p, float dt);   // one RK4 step
    static double Energy(const State& s, const Params& p);
    static Params ReadParams(const Cosmic::DataBus& bus);

    const State& Current() const        { return m_State; }
    bool         Running() const        { return m_Running; }
    double       PeriodEstimate() const { return m_PeriodEst; }
    double       SimTime() const        { return m_Time; }
    int          PanelDraws() const     { return m_PanelDraws; }

protected:
    void OnAttach(Cosmic::AppContext& ctx) override;
    void OnFixedUpdate(float fixedDt) override;
    void OnSignal(const std::string& signal, Cosmic::Entity source) override;

private:
    void SeedDefaults(bool force);
    void Reset();
    void Publish();
    void DrawPhasePlot(const Cosmic::UiRect& rect);

    State  m_State;
    bool   m_Running   = false;
    double m_Time      = 0.0;    // simulated seconds since the last Reset
    double m_PeriodEst = 0.0;
    double m_LastCross = -1.0;   // time of the previous upward zero crossing (-1 = none)
    int    m_PanelDraws = 0;
    std::vector<Cosmic::DataSample> m_ScratchA, m_ScratchB;   // phase-plot history buffers
    std::vector<double>             m_PlotX, m_PlotY;
};
