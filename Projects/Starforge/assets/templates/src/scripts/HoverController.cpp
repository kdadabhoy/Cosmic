// HoverController.cpp — see HoverController.h.

#include "HoverController.h"

using namespace Cosmic;

void HoverController::OnUpdate(float ts)
{
    // Simple PD lift toward TargetAltitude (edit these gains live in the Inspector,
    // press Ctrl+B to rebuild, then Play to feel the difference).
    auto& t = GetComponent<TransformComponent>();
    const float error = TargetAltitude - t.Position.y;
    const float accel = Kp * error - Kd * m_VelY;
    m_VelY += accel * ts;
    t.Position.y += m_VelY * ts;
}
