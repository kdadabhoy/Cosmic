#pragma once
#include <Cosmic.h>
#include "scene/ui/UiComponents.h"

// Telemetry screen — per-display logic for scenes/Telemetry.cscene (created by Starforge ▸ Screens ▸ New Screen).
// Project: Ap03App. Values come from the app's services through Data(); commands go out as signals or bus writes.
class TelemetryScreen : public Cosmic::ScriptableEntity
{
public:
    float ExampleField = 1.0f;
protected:
    void OnStart() override {}
    void OnUpdate(float ts) override { (void)ts; }
    void OnSignal(const std::string& signal, Cosmic::Entity source) override { (void)signal; (void)source; }
};
