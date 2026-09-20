#pragma once
#include <Cosmic.h>
#include "scene/ui/UiComponents.h"

// Settings screen — per-display logic for scenes/Settings.cscene (app template, App Platform / AP-04).
// Values come from the app's services through Data(); commands go out as signals or bus writes.
// The sliders and the toggle write settings.* channels directly (UiSlider / UiToggle); the service
// re-reads them every tick, so there is nothing to "apply". This script only counts the
// settings_changed signals the toggle emits (a hook for per-screen feedback).
class SettingsScreen : public Cosmic::ScriptableEntity
{
public:
    int ChangesSeen = 0;   // how many settings_changed signals this screen received

protected:
    void OnStart() override {}
    void OnUpdate(float ts) override { (void)ts; }
    void OnSignal(const std::string& signal, Cosmic::Entity source) override
    {
        (void)source;
        if (signal == "settings_changed")
            ++ChangesSeen;
    }
};
