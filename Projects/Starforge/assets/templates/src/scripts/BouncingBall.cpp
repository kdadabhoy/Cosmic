// BouncingBall.cpp — see BouncingBall.h.

#include "BouncingBall.h"

using namespace Cosmic;

void BouncingBall::OnFixedUpdate(float dt)
{
    auto& t = GetComponent<TransformComponent>();

    m_VelY += Gravity * dt;
    t.Position.y += m_VelY * dt;

    if (t.Position.y < 0.0f)              // hit the ground — bounce
    {
        t.Position.y = 0.0f;
        m_VelY = -m_VelY * Restitution;
    }

    // Telemetry (E20): sampled once per fixed step by the panel.
    Telemetry().Push("height", t.Position.y);
    Telemetry().Push("velY",   m_VelY);
}
