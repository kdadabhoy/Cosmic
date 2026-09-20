#pragma once
#include <Cosmic.h>
#include "scene/ui/UiComponents.h"
#include "ScreenCommon.h"

#include <cmath>
#include <cstdio>

// Settings screen — per-display logic for scenes/Settings.cscene (PendulumLab, App Platform / AP-04).
// Values come from the app's services through Data(); commands go out as signals or bus writes.
// The sliders write settings.length / gravity / damping / theta0_deg and the toggle writes
// settings.small_angle straight onto the bus; PendulumService re-reads them every fixed step, so
// there is nothing to apply. This script only keeps the header's subtitle honest: it shows the
// analytic small-angle period 2 pi sqrt(L / g) for the current settings, next to what the
// service measured.
class SettingsScreen : public Cosmic::ScriptableEntity
{
public:
    int ChangesSeen = 0;   // settings_changed signals (the toggle) received by this screen

protected:
    void OnStart() override
    {
        m_Note = PendulumLab::FindByTag(GetScene(), "Note");
    }

    void OnUpdate(float ts) override
    {
        (void)ts;
        if (!m_Note || !m_Note.HasComponent<Cosmic::UiTextComponent>()) return;
        const double L = Data().GetNumber("settings.length", 1.0);
        const double g = Data().GetNumber("settings.gravity", 9.80665);
        const double analytic = (L > 0.0 && g > 0.0) ? 2.0 * 3.14159265358979323846 * std::sqrt(L / g) : 0.0;
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "Small-angle period 2 pi sqrt(L/g) = %.3f s   measured %.3f s\nChanges apply live; Reset re-releases from the release angle.",
                      analytic, Data().GetNumber("pendulum.period_est", 0.0));
        m_Note.GetComponent<Cosmic::UiTextComponent>().Text = buf;
    }

    void OnSignal(const std::string& signal, Cosmic::Entity source) override
    {
        (void)source;
        if (signal == "settings_changed")
            ++ChangesSeen;
    }

private:
    Cosmic::Entity m_Note;
};
