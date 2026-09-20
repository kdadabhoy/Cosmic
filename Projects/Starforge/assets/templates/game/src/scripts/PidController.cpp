// PidController.cpp — see PidController.h.

#include "PidController.h"

using namespace Cosmic;

void PidController::OnFixedUpdate(float dt)
{
    auto& t = GetComponent<TransformComponent>();

    const float error = Target - t.Position.y;
    m_Integral += error * dt;
    const float deriv = -m_VelY;                 // d(error)/dt = -d(altitude)/dt
    const float accel = Kp * error + Ki * m_Integral + Kd * deriv;

    m_VelY += accel * dt;
    t.Position.y += m_VelY * dt;

    // Telemetry (E20): sampled once per fixed step by the panel.
    Telemetry().Push("error",  error);
    Telemetry().Push("output", accel);
}
