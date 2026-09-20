#pragma once
#include <Cosmic.h>
#include "scene/ui/UiComponents.h"
#include "ScreenCommon.h"

#include <cmath>

// Home screen — per-display logic for scenes/Home.cscene (PendulumLab, App Platform / AP-04).
// Values come from the app's services through Data(); commands go out as signals or bus writes.
// The title swings like the pendulum it advertises: its RectTransform offsets slide left/right
// on a slow sine and its colour breathes. Everything else on the screen is data (buttons emit
// start_clicked / settings_clicked / quit_clicked to the flow; the period readout is a UiValueText).
class HomeScreen : public Cosmic::ScriptableEntity
{
public:
    float SwingPx  = 40.0f;   // horizontal swing amplitude of the title, in canvas px
    float SwingHz  = 0.35f;   // swing rate
    float PulseMin = 0.7f;    // darkest title brightness

protected:
    void OnStart() override
    {
        m_Title = PendulumLab::FindByTag(GetScene(), "Title");
        if (m_Title && m_Title.HasComponent<Cosmic::RectTransformComponent>())
        {
            const auto& rt = m_Title.GetComponent<Cosmic::RectTransformComponent>();
            m_BaseMin = rt.OffsetMin;
            m_BaseMax = rt.OffsetMax;
        }
    }

    void OnUpdate(float ts) override
    {
        m_Time += ts;
        if (!m_Title) return;
        const float phase = 6.2831853f * SwingHz * m_Time;
        if (m_Title.HasComponent<Cosmic::RectTransformComponent>())
        {
            auto& rt = m_Title.GetComponent<Cosmic::RectTransformComponent>();
            const float dx = SwingPx * std::sin(phase);
            rt.OffsetMin = { m_BaseMin.x + dx, m_BaseMin.y };
            rt.OffsetMax = { m_BaseMax.x + dx, m_BaseMax.y };
        }
        if (m_Title.HasComponent<Cosmic::UiTextComponent>())
        {
            const float k = PulseMin + (1.0f - PulseMin) * (0.5f + 0.5f * std::cos(phase * 2.0f));
            m_Title.GetComponent<Cosmic::UiTextComponent>().Color = { 0.95f * k, 0.98f * k, 1.0f, 1.0f };
        }
    }

    void OnSignal(const std::string& signal, Cosmic::Entity source) override { (void)signal; (void)source; }

private:
    Cosmic::Entity m_Title;
    glm::vec2      m_BaseMin{ 0.0f }, m_BaseMax{ 0.0f };
    float          m_Time = 0.0f;
};
