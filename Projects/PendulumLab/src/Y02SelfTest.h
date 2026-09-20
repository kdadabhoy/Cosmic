#pragma once
// Y02SelfTest.h — PendulumLab's in-app acceptance harness (App Platform catalog Y02;
// the Y03 soak drives the same exe from outside). X01SelfTest pattern: armed by the
// environment, otherwise inert; driven from the SAME frame the app runs (a service
// OnUpdate), feeding the real FlowMachine the signals the buttons would emit and
// reading the real DataBus — never a second app or a fake scheduler.
//
//   COSMIC_Y02_SELFTEST=<result.json>   arm; write the verdict JSON there
//   COSMIC_Y02_OUTPUT=<dir>             where the plot-ROI PNG lands (default: the result's folder)
//
// Sequence (each phase bounded to 60 s wall):
//   Boot     3 frames, the flow must be running at "Home"
//   ToLab    start_clicked -> "Lab"; pendulum.running becomes true (LabScreen starts it)
//   Sample   ~1.5 s: pendulum.angle_deg must change (max - min > 0.05 deg), monotone bus clock
//   ToSettings / BackToLab   settings_clicked -> "Settings", back_clicked -> "Lab"
//   Verify   ~1 s on Lab: PhasePlot hosted-panel draws > 0 (pendulum.phaseplot_draws),
//            the UiPlot element's rect read back from the framebuffer contains line-colour pixels
//   Escape   key:Escape -> "Home" (the key bridge's signal, fed directly)
//   Finish   verdict JSON; PASS closes the app (exit 0), FAIL quick_exit(1)

#include <Cosmic.h>

#include <string>
#include <vector>

class Y02SelfTestService : public Cosmic::AppService
{
public:
    static const char* kResultEnv;   // "COSMIC_Y02_SELFTEST"
    static const char* kOutputEnv;   // "COSMIC_Y02_OUTPUT"

protected:
    void OnAttach(Cosmic::AppContext& ctx) override;
    void OnDetach() override;
    void OnUpdate(float ts) override;

private:
    struct Impl;
    Impl* m_Impl = nullptr;
};
