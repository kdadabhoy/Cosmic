#pragma once
#include <Cosmic.h>
#include "scene/ui/UiComponents.h"

// Home screen — per-display logic for scenes/Home.cscene (app template, App Platform / AP-04).
// Values come from the app's services through Data(); commands go out as signals or bus writes.
// This one gently pulses the title's colour so the screen reads as alive; the buttons are
// pure data (UiButton.Signal -> the flow), and the uptime readout is a bound UiValueText.
class HomeScreen : public Cosmic::ScriptableEntity
{
public:
    float PulseHz  = 0.5f;   // title pulse rate
    float PulseMin = 0.75f;  // darkest title brightness

protected:
    void OnStart() override
    {
        m_Title = FindByTag("Title");
    }

    void OnUpdate(float ts) override
    {
        m_Time += ts;
        if (m_Title && m_Title.HasComponent<Cosmic::UiTextComponent>())
        {
            const float k = PulseMin + (1.0f - PulseMin) * (0.5f + 0.5f * std::sin(6.2831853f * PulseHz * m_Time));
            auto& txt = m_Title.GetComponent<Cosmic::UiTextComponent>();
            txt.Color = { 0.95f * k, 0.98f * k, 1.0f * k, 1.0f };
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

    Cosmic::Entity m_Title;
    float          m_Time = 0.0f;
};
