// PendulumService.cpp — see PendulumService.h.

#include "PendulumService.h"

#include "scene/ui/UiComponents.h"   // UiRect
#include "math/Integrators.h"         // IntegrateRK4

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cmath>

// ---- pure pieces ------------------------------------------------------------------

PendulumService::State PendulumService::Step(const State& s, const Params& p, float dt)
{
    const double gl = p.Gravity / p.Length;
    const double c  = p.Damping;
    const bool   lin = p.SmallAngle;
    auto deriv = [gl, c, lin](const State& x, float /*t*/) -> State
    {
        const double restoring = lin ? x.Theta : std::sin(x.Theta);
        return { x.Omega, -gl * restoring - c * x.Omega };
    };
    return Cosmic::IntegrateRK4(s, deriv, 0.0f, dt);
}

double PendulumService::Energy(const State& s, const Params& p)
{
    const double v = p.Length * s.Omega;
    const double potential = p.SmallAngle ? 0.5 * p.Gravity * p.Length * s.Theta * s.Theta
                                          : p.Gravity * p.Length * (1.0 - std::cos(s.Theta));
    return 0.5 * v * v + potential;
}

PendulumService::Params PendulumService::ReadParams(const Cosmic::DataBus& bus)
{
    Params d;   // defaults
    Params p;
    p.Length     = bus.GetNumber("settings.length",     d.Length);
    p.Gravity    = bus.GetNumber("settings.gravity",    d.Gravity);
    p.Damping    = bus.GetNumber("settings.damping",    d.Damping);
    p.Theta0Deg  = bus.GetNumber("settings.theta0_deg", d.Theta0Deg);
    p.SmallAngle = bus.GetBool  ("settings.small_angle", d.SmallAngle);
    // Guard against a slider (or a stray write) driving the model singular.
    if (!(p.Length  > 1e-6) || !std::isfinite(p.Length))  p.Length  = d.Length;
    if (!(p.Gravity > 0.0)  || !std::isfinite(p.Gravity)) p.Gravity = d.Gravity;
    if (!(p.Damping >= 0.0) || !std::isfinite(p.Damping)) p.Damping = d.Damping;
    if (!std::isfinite(p.Theta0Deg)) p.Theta0Deg = d.Theta0Deg;
    return p;
}

// ---- service lifecycle --------------------------------------------------------------

void PendulumService::OnAttach(Cosmic::AppContext& ctx)
{
    (void)ctx;
    SeedDefaults(/*force=*/false);
    for (const char* ch : { "pendulum.angle_deg", "pendulum.omega", "pendulum.energy" })
        Bus().SetHistoryCapacity(ch, kHistory);
    Reset();
    Publish();
    CS_PANEL("PhasePlot", [this](const Cosmic::UiRect& rect) { DrawPhasePlot(rect); });
}

void PendulumService::OnFixedUpdate(float fixedDt)
{
    const Params p = ReadParams(Bus());
    if (m_Running && fixedDt > 0.0f)
    {
        const State prev = m_State;
        m_State = Step(m_State, p, fixedDt);
        const double tPrev = m_Time;
        m_Time += (double)fixedDt;

        // Upward zero crossing (theta: negative -> non-negative), linearly interpolated.
        if (prev.Theta < 0.0 && m_State.Theta >= 0.0)
        {
            const double frac  = prev.Theta / (prev.Theta - m_State.Theta);   // in [0, 1]
            const double cross = tPrev + frac * (double)fixedDt;
            if (m_LastCross >= 0.0)
                m_PeriodEst = cross - m_LastCross;
            m_LastCross = cross;
        }
    }
    Publish();
}

void PendulumService::OnSignal(const std::string& signal, Cosmic::Entity source)
{
    (void)source;
    if      (signal == "pendulum.start")    m_Running = true;
    else if (signal == "pendulum.stop")     m_Running = false;
    else if (signal == "startstop_clicked") m_Running = !m_Running;
    else if (signal == "pendulum.reset")    Reset();
    else if (signal == "pendulum.nudge")    m_State.Omega += kNudgeOmega;
    else if (signal == "settings.defaults") SeedDefaults(/*force=*/true);
    else return;
    Publish();
}

void PendulumService::SeedDefaults(bool force)
{
    const Params d;
    auto& bus = Bus();
    if (force || !bus.Has("settings.length"))      bus.Set("settings.length", d.Length);
    if (force || !bus.Has("settings.gravity"))     bus.Set("settings.gravity", d.Gravity);
    if (force || !bus.Has("settings.damping"))     bus.Set("settings.damping", d.Damping);
    if (force || !bus.Has("settings.theta0_deg"))  bus.Set("settings.theta0_deg", d.Theta0Deg);
    if (force || !bus.Has("settings.small_angle")) bus.SetBool("settings.small_angle", d.SmallAngle);
}

void PendulumService::Reset()
{
    const Params p = ReadParams(Bus());
    m_State     = { p.Theta0Deg * kPi / 180.0, 0.0 };
    m_Time      = 0.0;
    m_PeriodEst = 0.0;
    m_LastCross = -1.0;
}

void PendulumService::Publish()
{
    const Params p = ReadParams(Bus());
    auto& bus = Bus();
    bus.Set("pendulum.angle_deg", m_State.Theta * 180.0 / kPi);
    bus.Set("pendulum.omega", m_State.Omega);
    bus.Set("pendulum.energy", Energy(m_State, p));
    bus.Set("pendulum.period_est", m_PeriodEst);
    bus.SetBool("pendulum.running", m_Running);
    bus.Set("pendulum.phaseplot_draws", (double)m_PanelDraws);
}

// ---- hosted panel -------------------------------------------------------------------

static std::function<void()> s_AfterPhasePlotDrawOnce;
void PendulumService::SetAfterPhasePlotDrawOnce(std::function<void()> fn) { s_AfterPhasePlotDrawOnce = std::move(fn); }

void PendulumService::DrawPhasePlot(const Cosmic::UiRect& rect)
{
    ++m_PanelDraws;
    struct AfterDraw { ~AfterDraw() { if (s_AfterPhasePlotDrawOnce) { auto fn = std::move(s_AfterPhasePlotDrawOnce); s_AfterPhasePlotDrawOnce = nullptr; fn(); } } } afterDraw;
    // Pair the two histories from the tail: both channels are written in the same
    // fixed step, so the newest N samples of each line up.
    const size_t na = Bus().History("pendulum.angle_deg", m_ScratchA, 10.0);
    const size_t nb = Bus().History("pendulum.omega",     m_ScratchB, 10.0);
    const size_t n  = std::min(na, nb);
    m_PlotX.resize(n); m_PlotY.resize(n);
    for (size_t i = 0; i < n; ++i)
    {
        m_PlotX[i] = m_ScratchA[na - n + i].Value;
        m_PlotY[i] = m_ScratchB[nb - n + i].Value;
    }
    const ImVec2 size(std::max(rect.Width() - 8.0f, 32.0f), std::max(rect.Height() - 8.0f, 32.0f));
    if (ImPlot::BeginPlot("Phase space##pendulum", size, ImPlotFlags_NoLegend | ImPlotFlags_NoMenus))
    {
        ImPlot::SetupAxes("theta (deg)", "omega (rad/s)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        if (n > 0)
        {
            ImPlotSpec spec;
            spec.Marker = ImPlotMarker_Circle;
            spec.MarkerSize = 2.0f;
            ImPlot::PlotScatter("state", m_PlotX.data(), m_PlotY.data(), (int)n, spec);
        }
        ImPlot::EndPlot();
    }
    ImGui::Text("%zu samples  E = %.4f J/kg  T ~ %.3f s", n, Bus().GetNumber("pendulum.energy"), m_PeriodEst);
}
