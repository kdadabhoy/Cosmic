#pragma once
#include <Cosmic.h>
#include "scene/ui/UiComponents.h"
#include "ScreenCommon.h"

#include <cmath>

// Lab screen — per-display logic for scenes/Lab.cscene (PendulumLab, App Platform / AP-04).
// Values come from the app's services through Data(); commands go out as signals or bus writes.
//
// The rig is three sprites authored in the editor — "Pivot", "Rod", "Bob" — and this script
// places the rod and the bob from the bus every frame: pendulum.angle_deg is the only input.
// The rod length in world units is taken from the authored rod sprite (its Scale.y), so an
// artist resizes the rig in Starforge without touching C++. On its first frame the screen asks
// the service to run if it is not already (pendulum.start), so entering the Lab starts the swing
// and returning from Settings leaves a running pendulum alone.
class LabScreen : public Cosmic::ScriptableEntity
{
public:
    float RodWidth = 0.05f;   // world units; the rod sprite's x scale

protected:
    void OnStart() override
    {
        m_Pivot = PendulumLab::FindByTag(GetScene(), "Pivot");
        m_Rod   = PendulumLab::FindByTag(GetScene(), "Rod");
        m_Bob   = PendulumLab::FindByTag(GetScene(), "Bob");
        if (m_Rod && m_Rod.HasComponent<Cosmic::TransformComponent>())
            m_RodLength = m_Rod.GetComponent<Cosmic::TransformComponent>().Scale.y;
        if (!Data().GetBool("pendulum.running", false))
            Signals().Emit("pendulum.start");
        Place();
    }

    void OnUpdate(float ts) override
    {
        (void)ts;
        Place();
    }

    void OnSignal(const std::string& signal, Cosmic::Entity source) override { (void)signal; (void)source; }

private:
    void Place()
    {
        if (!m_Pivot || !m_Pivot.HasComponent<Cosmic::TransformComponent>()) return;
        const glm::vec3 pivot = m_Pivot.GetComponent<Cosmic::TransformComponent>().Position;
        const float thetaDeg = (float)Data().GetNumber("pendulum.angle_deg", 0.0);
        const float theta    = thetaDeg * 3.14159265f / 180.0f;
        const glm::vec2 dir{ std::sin(theta), -std::cos(theta) };   // hanging down at theta = 0, CCW positive

        if (m_Rod && m_Rod.HasComponent<Cosmic::TransformComponent>())
        {
            auto& t = m_Rod.GetComponent<Cosmic::TransformComponent>();
            t.Position = { pivot.x + dir.x * m_RodLength * 0.5f, pivot.y + dir.y * m_RodLength * 0.5f, t.Position.z };
            t.Rotation = { 0.0f, 0.0f, thetaDeg };
            t.Scale    = { RodWidth, m_RodLength, 1.0f };
        }
        if (m_Bob && m_Bob.HasComponent<Cosmic::TransformComponent>())
        {
            auto& t = m_Bob.GetComponent<Cosmic::TransformComponent>();
            t.Position = { pivot.x + dir.x * m_RodLength, pivot.y + dir.y * m_RodLength, t.Position.z };
        }
    }

    Cosmic::Entity m_Pivot, m_Rod, m_Bob;
    float          m_RodLength = 4.0f;
};
