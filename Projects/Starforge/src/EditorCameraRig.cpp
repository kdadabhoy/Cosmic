// EditorCameraRig.cpp — orbit + fly + possess editor camera (K7). See header.

#include "EditorCameraRig.h"

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"

#include <algorithm>

using namespace Cosmic;

namespace Starforge
{
    EditorCameraRig::EditorCameraRig()
    {
        m_Orbit.SetNavigationStyle(NavStyle::CAD);
        m_Fly.SetControlEnabled(false);
    }

    void EditorCameraRig::SetMode(Mode mode)
    {
        if (mode == m_Mode)
            return;

        // Pose continuity on the way IN.
        if (mode == Mode::Fly)
            SyncFlyFromOrbit();
        else if (mode == Mode::Orbit && (m_Mode == Mode::Fly || m_TempFly))
            SyncOrbitFromFly();
        // -> Possess needs no seed (it renders the entity's pose); leaving it
        //    returns to whatever the orbit/fly pose already was.

        m_TempFly = false;
        m_Mode    = mode;
        if (mode != Mode::Possess)
        {
            m_PossessId    = UUID(0);
            m_PossessValid = false;
        }
    }

    void EditorCameraRig::Possess(Cosmic::UUID entity)
    {
        m_PossessId = entity;
        m_TempFly   = false;
        m_Mode      = Mode::Possess;
    }

    void EditorCameraRig::SyncFlyFromOrbit()
    {
        // Same look direction: fly(yaw,pitch) == (-orbitYaw, -orbitPitch); the
        // fly position is the orbit EYE (target + spherical offset) — read off
        // the live camera so any in-flight snap/frame blend is honored.
        m_Fly.SetPose(m_Orbit.GetCamera().GetPosition(),
                      -m_Orbit.GetYaw(), -m_Orbit.GetPitch());
    }

    void EditorCameraRig::SyncOrbitFromFly()
    {
        const float dist = std::max(1.0f, m_Orbit.GetDistance());
        const glm::vec3 dir = FlyCameraController::DirectionFromYawPitch(
            m_Fly.GetYaw(), m_Fly.GetPitch());
        m_Orbit.SetTarget(m_Fly.GetPosition() + dir * dist);
        m_Orbit.SetYawPitch(-m_Fly.GetYaw(), -m_Fly.GetPitch());
        m_Orbit.SetDistance(dist);
    }

    void EditorCameraRig::OnUpdate(EditorContext& ctx, float ts,
                                   const glm::vec2& vpPos, const glm::vec2& vpSize,
                                   bool controlOk, bool allowTempFly)
    {
        // --- RMB-hold temporary fly (Orbit mode only) ------------------------
        const bool rmb = Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_RIGHT);
        if (m_Mode == Mode::Orbit && allowTempFly)
        {
            if (rmb && !m_RmbWasDown && controlOk)
            {
                m_TempFly = true;
                SyncFlyFromOrbit();
            }
            if (!rmb && m_TempFly)
            {
                m_TempFly = false;
                SyncOrbitFromFly();
            }
        }
        else if (m_TempFly && !rmb)
        {
            m_TempFly = false;
            SyncOrbitFromFly();
        }
        m_RmbWasDown = rmb;

        // --- Drive the active controller -------------------------------------
        const bool flying = IsFlying();

        m_Orbit.SetViewportRect(vpPos, vpSize);
        m_Fly.SetViewportRect(vpPos, vpSize);
        if (vpSize.x > 0.0f && vpSize.y > 0.0f)
        {
            m_Orbit.OnResize(vpSize.x, vpSize.y);
            m_Fly.OnResize(vpSize.x, vpSize.y);
        }

        m_Orbit.SetControlEnabled(!flying && m_Mode != Mode::Possess && controlOk);
        m_Fly.SetControlEnabled(flying && (controlOk || m_Fly.IsLooking()));

        if (m_Mode != Mode::Possess)
        {
            if (flying) m_Fly.OnUpdate(ts);
            else        m_Orbit.OnUpdate(ts);
        }
        else
        {
            // --- Possess: read the entity's pose (READ-ONLY) -----------------
            m_PossessValid = false;
            if (ctx.Scene && (uint64_t)m_PossessId != 0)
            {
                if (Entity e = ctx.Scene->FindByUUID(m_PossessId);
                    e && e.HasComponent<CameraComponent>())
                {
                    const auto& cc = e.GetComponent<CameraComponent>();
                    const glm::mat4 world = ctx.Scene->GetWorldTransform(e);
                    const float aspect = vpSize.y > 0.0f ? vpSize.x / vpSize.y : 1.0f;
                    m_Possess.Set(glm::inverse(world), cc.GetProjection(aspect),
                                  glm::vec3(world[3]));
                    m_PossessValid = true;
                }
            }
            if (!m_PossessValid)
                SetMode(Mode::Orbit);   // entity gone -> fall back cleanly
        }
    }

    void EditorCameraRig::OnEvent(Cosmic::Event& e)
    {
        // Scroll: fly-speed while flying, orbit zoom otherwise (Possess ignores).
        if (IsFlying())            m_Fly.OnEvent(e);
        else if (m_Mode == Mode::Orbit) m_Orbit.OnEvent(e);
    }

    const Cosmic::Camera& EditorCameraRig::ActiveCamera() const
    {
        if (m_Mode == Mode::Possess && m_PossessValid)
            return m_Possess;
        if (IsFlying())
            return m_Fly.GetCamera();
        return m_Orbit.GetCamera();
    }

    void EditorCameraRig::FrameBounds(const glm::vec3& mn, const glm::vec3& mx, bool animate)
    {
        m_Orbit.FrameBounds(mn, mx, animate && !IsFlying());
        if (IsFlying())
            SyncFlyFromOrbit();   // F frames seamlessly while flying too
    }

    void EditorCameraRig::SnapView(Cosmic::ViewPreset preset)
    {
        m_Orbit.SnapView(preset, /*animate=*/!IsFlying());
        if (IsFlying())
            SyncFlyFromOrbit();
    }

    void EditorCameraRig::RecallPose(const glm::vec3& target, float yaw, float pitch, float dist)
    {
        m_Orbit.SetTarget(target);
        m_Orbit.SetYawPitch(yaw, pitch);
        m_Orbit.SetDistance(dist);
        if (IsFlying())
            SyncFlyFromOrbit();
    }
}
