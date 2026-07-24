#pragma once
// PlayerController.h — Forge Isle player (Z1 greybox tier: capsule walker).
//
// v0 (Z1): mouse-look + view-relative WASD over the engine character controller
// (Jolt CharacterVirtual, J6), Space jump, Shift run. Z2 upgrades this to the
// rigged third-person character (camera follow + M6 crossfades + interact
// raycast) — the input/velocity core below carries over.
//
// Entity contract (Island.cscene):
//   * this entity: TransformComponent + CharacterControllerComponent (+ this script)
//   * one CHILD entity tagged `PlayerCamera` with the Primary CameraComponent
//     (local position = eye height; this script writes its pitch)
//
// Cursor: captured on start / on a left-click; Esc releases it (and the flow's
// `key:Escape` pause-push destroys this script, whose OnDestroy releases too —
// so the pause menu always gets a live cursor). Design doc §7.5. In the editor
// game view, hold RMB to look instead (the U7 Capture checkbox owns capture
// there and forces it off each frame unless ticked).

#include <Cosmic.h>

#include <cmath>

class PlayerController : public Cosmic::ScriptableEntity
{
public:
    float MoveSpeed       = 4.5f;    // metres / second (walk)
    float RunMultiplier   = 1.8f;    // Shift
    float JumpSpeed       = 6.0f;    // launch velocity
    float LookSensitivity = 0.12f;   // degrees per mouse pixel
    float PitchMin        = -75.0f;  // camera pitch clamp (degrees)
    float PitchMax        = 80.0f;

protected:
    void OnStart() override
    {
        using namespace Cosmic;

        // Idempotent re-entry (flow push/pop re-runs OnStart — design doc §7.2):
        // derive every bit of state from the live components.
        m_Camera = FindChildByTag("PlayerCamera");
        auto& t  = GetComponent<TransformComponent>();
        t.UseQuatRotation = false;             // this script authors Euler yaw
        m_Yaw = t.Rotation.y;
        if (m_Camera && m_Camera.HasComponent<TransformComponent>())
            m_Pitch = m_Camera.GetComponent<TransformComponent>().Rotation.x;

        Application::Get().GetWindow().SetCursorCaptured(true);
        m_HaveMouse = false;                   // swallow the first delta
    }

    void OnDestroy() override
    {
        // Never leak capture into a pause menu / the title screen (U7 contract).
        Cosmic::Application::Get().GetWindow().SetCursorCaptured(false);
    }

    void OnUpdate(float /*ts*/) override
    {
        using namespace Cosmic;
        auto& win = Application::Get().GetWindow();

        // Capture lifecycle (standalone player; the editor checkbox overrides).
        const bool lmb = Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_LEFT);
        if (Input::IsKeyPressed(CS_KEY_ESCAPE))
            win.SetCursorCaptured(false);
        else if (lmb && !m_PrevLmb && !win.IsCursorCaptured())
            win.SetCursorCaptured(true);
        m_PrevLmb = lmb;

        // Look while captured (shipped) or while RMB is held (editor game view).
        const glm::vec2 mouse = Input::GetMousePosition();
        const bool looking = win.IsCursorCaptured()
                          || Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_RIGHT);
        if (m_HaveMouse && looking)
        {
            const glm::vec2 d = mouse - m_PrevMouse;
            m_Yaw   -= d.x * LookSensitivity;
            m_Pitch -= d.y * LookSensitivity;
            m_Pitch  = glm::clamp(m_Pitch, PitchMin, PitchMax);

            // Gamepad right stick adds on top.
            const float rx = Input::GetGamepadAxis(CS_GAMEPAD_AXIS_RIGHT_X);
            const float ry = Input::GetGamepadAxis(CS_GAMEPAD_AXIS_RIGHT_Y);
            if (std::fabs(rx) > 0.2f) m_Yaw   -= rx * 2.4f;
            if (std::fabs(ry) > 0.2f) m_Pitch -= ry * 1.6f;

            GetComponent<TransformComponent>().Rotation.y = m_Yaw;
            if (m_Camera && m_Camera.HasComponent<TransformComponent>())
                m_Camera.GetComponent<TransformComponent>().Rotation.x = m_Pitch;
        }
        m_PrevMouse = mouse;
        m_HaveMouse = true;
    }

    void OnFixedUpdate(float /*dt*/) override
    {
        using namespace Cosmic;

        // View-relative planar move from the current yaw.
        const float yawRad = glm::radians(m_Yaw);
        const glm::vec3 fwd  { -std::sin(yawRad), 0.0f, -std::cos(yawRad) };
        const glm::vec3 right{  std::cos(yawRad), 0.0f, -std::sin(yawRad) };

        glm::vec3 dir(0.0f);
        if (Input::IsKeyPressed(CS_KEY_W)) dir += fwd;
        if (Input::IsKeyPressed(CS_KEY_S)) dir -= fwd;
        if (Input::IsKeyPressed(CS_KEY_D)) dir += right;
        if (Input::IsKeyPressed(CS_KEY_A)) dir -= right;

        const float gx = Input::GetGamepadAxis(CS_GAMEPAD_AXIS_LEFT_X);
        const float gy = Input::GetGamepadAxis(CS_GAMEPAD_AXIS_LEFT_Y);
        if (std::fabs(gx) > 0.2f) dir += right * gx;
        if (std::fabs(gy) > 0.2f) dir -= fwd * gy;

        if (glm::length(dir) > 1.0f)
            dir = glm::normalize(dir);

        const bool run = Input::IsKeyPressed(CS_KEY_LEFT_SHIFT);
        Character().Move(dir * MoveSpeed * (run ? RunMultiplier : 1.0f));

        if (Input::IsKeyPressed(CS_KEY_SPACE) && Character().IsGrounded())
            Character().Jump(JumpSpeed);
    }

private:
    Cosmic::Entity FindChildByTag(const std::string& tag)
    {
        using namespace Cosmic;
        auto& reg = GetScene().GetRegistry();
        if (auto* rel = reg.try_get<RelationshipComponent>(GetEntity()))
            for (const UUID& c : rel->Children)
                if (Entity child = GetScene().FindByUUID(c))
                    if (child.HasComponent<TagComponent>()
                        && child.GetComponent<TagComponent>().Tag == tag)
                        return child;
        return {};
    }

    Cosmic::Entity m_Camera;
    glm::vec2 m_PrevMouse{ 0.0f };
    float m_Yaw = 0.0f, m_Pitch = 0.0f;
    bool  m_HaveMouse = false;
    bool  m_PrevLmb   = false;
};
