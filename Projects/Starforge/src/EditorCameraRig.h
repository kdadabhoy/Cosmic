#pragma once

// EditorCameraRig.h — the editor camera modes (Phase 22 / K7, gap §2.2).
//
// One rig owning both engine controllers plus a possess pose:
//
//   Orbit   — the default CAD navigation (OrbitCameraController, H1 pose-based:
//             MMB orbit, Ctrl+MMB pan, scroll zoom).
//   Fly     — FlyCameraController (F1, Frontier-proven): RMB mouse-look,
//             WASD+QE move, LShift boost, scroll scales speed.
//   Possess — renders from a scene CameraComponent's pose, READ-ONLY (nothing
//             writes back to the entity).
//
// Seamless transitions (no pose jumps): the two controllers' angle conventions
// mirror each other exactly — fly(yaw,pitch) == (-orbitYaw, -orbitPitch) with
// the same look direction — so Orbit->Fly seeds the fly pose at the orbit EYE
// and Fly->Orbit re-targets the orbit pivot `distance` metres along the fly
// look direction.
//
// RMB-hold in the viewport = TEMPORARY fly (the Unreal idiom): entering on the
// press edge seeds fly from the current camera, the hold drives mouse-look +
// WASD, and the release commits the pose back into the base mode. Scroll while
// flying scales the fly speed (the K6 strip shows it); scroll otherwise zooms
// the orbit rig as before.

#include "EditorContext.h"

#include <Cosmic.h>

namespace Starforge
{
    // Fixed-pose camera for Possess (fed per frame from the possessed entity).
    class PossessCamera : public Cosmic::Camera
    {
    public:
        void Set(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& pos)
        {
            m_View = view; m_Proj = proj; m_ViewProj = proj * view; m_Pos = pos;
        }
        const glm::mat4& GetViewMatrix() const override           { return m_View; }
        const glm::mat4& GetProjectionMatrix() const override     { return m_Proj; }
        const glm::mat4& GetViewProjectionMatrix() const override { return m_ViewProj; }
        const glm::vec3& GetPosition() const override             { return m_Pos; }
    private:
        glm::mat4 m_View{ 1.0f }, m_Proj{ 1.0f }, m_ViewProj{ 1.0f };
        glm::vec3 m_Pos{ 0.0f };
    };

    class EditorCameraRig
    {
    public:
        enum class Mode : int { Orbit = 0, Fly, Possess };

        EditorCameraRig();

        // The owned controllers (bookmarks, pivot probes, adopt-camera, and the
        // nav cube keep talking to Orbit directly).
        Cosmic::OrbitCameraController& Orbit()             { return m_Orbit; }
        const Cosmic::OrbitCameraController& Orbit() const { return m_Orbit; }
        Cosmic::FlyCameraController&   Fly()               { return m_Fly; }

        Mode GetMode() const { return m_Mode; }
        bool IsFlying() const { return m_Mode == Mode::Fly || m_TempFly; }

        // Mode switches with pose continuity. Possess needs the entity's UUID
        // (resolved against the live scene each frame; a vanished entity drops
        // the rig back to Orbit).
        void SetMode(Mode mode);
        void Possess(Cosmic::UUID entity);
        Cosmic::UUID PossessedEntity() const { return m_PossessId; }

        // Per-frame drive. `controlOk` = viewport hovered (or a drag already in
        // progress) and the gizmo idle — the H1 gating rule. `allowTempFly`
        // gates the RMB-hold fly (off while playing / in 2D mode / gizmo busy).
        void OnUpdate(EditorContext& ctx, float ts,
                      const glm::vec2& vpPos, const glm::vec2& vpSize,
                      bool controlOk, bool allowTempFly);

        // Route discrete events (scroll) to the active controller.
        void OnEvent(Cosmic::Event& e);

        // The camera the viewport should render with THIS frame.
        const Cosmic::Camera& ActiveCamera() const;

        // Frame helpers that stay seamless in every mode: they drive the orbit
        // rig, then re-seed the fly pose when flying.
        void FrameBounds(const glm::vec3& mn, const glm::vec3& mx, bool animate = true);
        void SnapView(Cosmic::ViewPreset preset);
        void RecallPose(const glm::vec3& target, float yaw, float pitch, float dist);

        // Pose continuity primitives (public for the strip/tests).
        void SyncFlyFromOrbit();    // fly pose = orbit eye + mirrored angles
        void SyncOrbitFromFly();    // orbit target = fly pos + dir * distance

    private:
        Cosmic::OrbitCameraController m_Orbit{ 16.0f / 9.0f };
        Cosmic::FlyCameraController   m_Fly{ 16.0f / 9.0f };
        PossessCamera                 m_Possess;

        Mode         m_Mode = Mode::Orbit;
        Cosmic::UUID m_PossessId{ 0 };
        bool         m_PossessValid = false;   // resolved this frame

        bool m_TempFly    = false;   // RMB-hold fly overlay on the Orbit mode
        bool m_RmbWasDown = false;
    };
}
