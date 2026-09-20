#pragma once
// AppService.h — the @PROJECT_NAME@ app service (app template, App Platform / AP-04).
//
// An AppService is the app's LOGIC: one instance per run, constructed by the host
// (Starforge Play or the packaged player) after the module loads, ticked in the
// fixed frame order, and torn down before the module unloads. It never touches
// entities directly — it publishes values on the DataBus, which the bound
// widgets in scenes/*.cscene display (UiValueText / UiGauge / UiPlot), and it
// reacts to signals the UI emits (UiButton.Signal, UiToggle.Signal). State that
// must survive a live reload belongs on the bus, not in members (contract §6).
//
// Channels published every tick:
//   app.uptime   seconds since OnAttach
//   app.sine     settings.amplitude * sin(2 pi * settings.frequency * uptime)
//   app.counter  the counter (integer-valued)
// Channels read (written by the Settings screen's sliders/toggle):
//   settings.amplitude (1), settings.frequency (0.5 Hz), settings.counter_step (1),
//   settings.auto_increment (false)
// Signals handled: counter.increment, counter.reset, settings.defaults
// Hosted panel: CS_PANEL("Diagnostics") — three ImGui::Text lines drawn inside the
//   Dashboard's UiHostedPanel element (the host sizes the ImGui window to it).

#include <Cosmic.h>

class AppService : public Cosmic::AppService
{
public:
    // Defaults the service seeds onto the bus when the channels are missing, so
    // the Settings sliders show real values before anyone drags them.
    static constexpr double kDefaultAmplitude = 1.0;
    static constexpr double kDefaultFrequency = 0.5;    // Hz
    static constexpr double kDefaultStep      = 1.0;
    static constexpr double kAutoIncrementPeriod = 1.0;  // s between auto-increments

    double Uptime()  const { return m_Uptime; }
    double Counter() const { return m_Counter; }
    int    PanelDraws() const { return m_PanelDraws; }

protected:
    void OnAttach(Cosmic::AppContext& ctx) override;
    void OnDetach() override;
    void OnUpdate(float ts) override;
    void OnSignal(const std::string& signal, Cosmic::Entity source) override;

private:
    void SeedDefaults(bool force);
    void Publish();

    double m_Uptime   = 0.0;
    double m_Counter  = 0.0;
    double m_AutoAccum = 0.0;
    int    m_PanelDraws = 0;
};
