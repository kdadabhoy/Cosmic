// AppService.cpp — see AppService.h.

#include "AppService.h"

#include "scene/ui/UiComponents.h"   // UiRect (the hosted panel's canvas rect)

#include <imgui.h>

#include <cmath>

void AppService::OnAttach(Cosmic::AppContext& ctx)
{
    (void)ctx;
    // Recover what a previous instance left on the bus (live reload keeps the bus,
    // contract §6); a cold start finds nothing and begins at zero.
    m_Uptime  = Bus().GetNumber("app.uptime", 0.0);
    m_Counter = Bus().GetNumber("app.counter", 0.0);
    SeedDefaults(/*force=*/false);
    Bus().SetHistoryCapacity("app.sine", 4096);      // ~10 s of a 240 Hz tick for UiPlot
    Bus().SetHistoryCapacity("app.counter", 4096);
    Publish();

    // The Diagnostics hosted panel: plain ImGui, drawn by the host inside a window
    // sized to the Dashboard's "Diagnostics" UiHostedPanel element.
    CS_PANEL("Diagnostics", [this](const Cosmic::UiRect& rect)
    {
        ++m_PanelDraws;
        ImGui::Text("uptime  %.1f s   (panel %.0fx%.0f px)", m_Uptime, rect.Width(), rect.Height());
        ImGui::Text("sine    %+.3f   amplitude %.2f  frequency %.2f Hz", Bus().GetNumber("app.sine"),
                    Bus().GetNumber("settings.amplitude"), Bus().GetNumber("settings.frequency"));
        ImGui::Text("counter %.0f     step %.0f  auto %s", m_Counter, Bus().GetNumber("settings.counter_step"),
                    Bus().GetBool("settings.auto_increment") ? "on" : "off");
    });
}

void AppService::OnDetach()
{
    // Nothing to release: the bus and the panel registry belong to the host (the
    // host clears the registry after OnDetach).
}

void AppService::OnUpdate(float ts)
{
    m_Uptime += (double)ts;
    if (Bus().GetBool("settings.auto_increment", false))
    {
        m_AutoAccum += (double)ts;
        while (m_AutoAccum >= kAutoIncrementPeriod)
        {
            m_AutoAccum -= kAutoIncrementPeriod;
            m_Counter += Bus().GetNumber("settings.counter_step", kDefaultStep);
        }
    }
    else
        m_AutoAccum = 0.0;
    Publish();
}

void AppService::OnSignal(const std::string& signal, Cosmic::Entity source)
{
    (void)source;
    if (signal == "counter.increment")
        m_Counter += Bus().GetNumber("settings.counter_step", kDefaultStep);
    else if (signal == "counter.reset")
        m_Counter = 0.0;
    else if (signal == "settings.defaults")
        SeedDefaults(/*force=*/true);
    else
        return;
    Publish();
}

void AppService::SeedDefaults(bool force)
{
    auto& bus = Bus();
    if (force || !bus.Has("settings.amplitude"))      bus.Set("settings.amplitude", kDefaultAmplitude);
    if (force || !bus.Has("settings.frequency"))      bus.Set("settings.frequency", kDefaultFrequency);
    if (force || !bus.Has("settings.counter_step"))   bus.Set("settings.counter_step", kDefaultStep);
    if (force || !bus.Has("settings.auto_increment")) bus.SetBool("settings.auto_increment", false);
}

void AppService::Publish()
{
    auto& bus = Bus();
    const double amplitude = bus.GetNumber("settings.amplitude", kDefaultAmplitude);
    const double frequency = bus.GetNumber("settings.frequency", kDefaultFrequency);
    bus.Set("app.uptime", m_Uptime);
    bus.Set("app.sine", amplitude * std::sin(2.0 * 3.14159265358979323846 * frequency * m_Uptime));
    bus.Set("app.counter", m_Counter);
}
