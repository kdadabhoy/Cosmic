#pragma once
#include <Cosmic.h>
#include "scene/ui/UiComponents.h"
#include "ScreenCommon.h"

// Stopped overlay — per-display logic for scenes/Stopped.cscene (PendulumLab, App Platform / AP-04).
// Values come from the app's services through Data(); commands go out as signals or bus writes.
//
// The flow pushes this overlay from Lab through a channel-guarded `when` transition
// (pendulum.energy < 0.01) and pops it on resume_clicked. Popping alone would re-arm the
// guard on the next update (the energy is still below the threshold), so this script turns
// the Resume click into a pendulum.reset as well: the service re-releases the bob from the
// release angle, the energy jumps above the threshold, and the Lab resumes cleanly.
class StoppedScreen : public Cosmic::ScriptableEntity
{
public:
    bool ResetOnResume = true;

protected:
    void OnStart() override {}
    void OnUpdate(float ts) override { (void)ts; }
    void OnSignal(const std::string& signal, Cosmic::Entity source) override
    {
        (void)source;
        if (signal == "resume_clicked" && ResetOnResume)
            Signals().Emit("pendulum.reset");
    }
};
