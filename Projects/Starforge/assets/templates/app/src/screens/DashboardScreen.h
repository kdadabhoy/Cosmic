#pragma once
#include <Cosmic.h>
#include "scene/ui/UiComponents.h"

// Dashboard screen — per-display logic for scenes/Dashboard.cscene (app template, App Platform / AP-04).
// Values come from the app's services through Data(); commands go out as signals or bus writes.
// Every readout on this screen is a bound widget reading the bus, so the script has little to do:
// it tints the counter readout when the counter crosses HighlightAbove (a per-display touch that
// does not belong in the service).
class DashboardScreen : public Cosmic::ScriptableEntity
{
public:
    float HighlightAbove = 10.0f;   // counter value that turns the readout warm

protected:
    void OnStart() override
    {
        m_CounterValue = FindByTag("CounterValue");
    }

    void OnUpdate(float ts) override
    {
        (void)ts;
        if (m_CounterValue && m_CounterValue.HasComponent<Cosmic::UiTextComponent>())
        {
            const bool high = Data().GetNumber("app.counter", 0.0) > (double)HighlightAbove;
            m_CounterValue.GetComponent<Cosmic::UiTextComponent>().Color =
                high ? glm::vec4{ 1.0f, 0.75f, 0.35f, 1.0f } : glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
        }
    }

    void OnSignal(const std::string& signal, Cosmic::Entity source) override { (void)signal; (void)source; }

private:
    Cosmic::Entity FindByTag(const std::string& tag)
    {
        auto& reg = GetScene().GetRegistry();
        for (auto e : reg.view<Cosmic::TagComponent>())
            if (reg.get<Cosmic::TagComponent>(e).Tag == tag)
                return Cosmic::Entity(e, &GetScene());
        return {};
    }

    Cosmic::Entity m_CounterValue;
};
