// OrbitCameraController.cpp
// Last Modified: 7/1/2026

#include "camera/OrbitCameraController.h"
#include "core/Input.h"
#include "codes/MouseButtonCodes.h"
#include "codes/KeyCodes.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	OrbitCameraController::OrbitCameraController(float aspectRatio)
		: m_Camera(45.0f, aspectRatio, 0.1f, 1000.0f)
	{
		RecalculateCamera();
	}

	/////////////////////////////////////////////////////////////////////////////////

	void OrbitCameraController::OnUpdate(float ts)
	{
		// SCREEN pixels — the same space as the viewport rect and the pivot probe
		// (drag deltas are space-invariant, but absolute positions must line up).
		const glm::vec2 mouse = Input::GetMouseScreenPosition();

		bool userInteracted = false;   // a live drag this frame → cancels pose blends

		if (m_ControlEnabled)
		{
			// Resolve which gesture each button maps to for the active nav style.
			bool orbitBtn = false, panBtn = false, dollyBtn = false;
			if (m_NavStyle == NavStyle::CAD)
			{
				// SolidWorks bindings: MMB orbit / Ctrl+MMB pan / Shift+MMB dolly.
				// LMB stays free for selection. Modifiers pick the mode.
				const bool mmb   = Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_MIDDLE);
				const bool ctrl  = Input::IsKeyPressed(CS_KEY_LEFT_CONTROL) || Input::IsKeyPressed(CS_KEY_RIGHT_CONTROL);
				const bool shift = Input::IsKeyPressed(CS_KEY_LEFT_SHIFT)   || Input::IsKeyPressed(CS_KEY_RIGHT_SHIFT);
				orbitBtn = mmb && !ctrl && !shift;
				panBtn   = mmb && ctrl;
				dollyBtn = mmb && shift && !ctrl;
			}
			else // Classic: LMB orbit / RMB pan (scroll zooms to center).
			{
				orbitBtn = Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_LEFT);
				panBtn   = Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_RIGHT);
			}

			const bool anyDrag = orbitBtn || panBtn || dollyBtn;
			if (anyDrag)
			{
				if (!m_Dragging)
				{
					// First frame of a drag: latch the mode + position, produce no
					// delta (else the camera jumps by the distance the mouse moved
					// since the previous drag ended).
					m_DragMode = orbitBtn ? DragMode::Orbit
					           : panBtn   ? DragMode::Pan
					                      : DragMode::Dolly;
					m_OrbitVelocity = { 0.0f, 0.0f };

					// CAD orbit-about-cursor: re-anchor the rig on the point under the
					// cursor without moving the camera, so the drag pivots there.
					if (m_NavStyle == NavStyle::CAD && m_DragMode == DragMode::Orbit)
					{
						glm::vec3 pivot;
						if (ComputeCursorPivot(pivot))
							ReanchorAround(pivot);
					}
				}
				else
				{
					const glm::vec2 delta = mouse - m_LastMousePos;
					switch (m_DragMode)
					{
					case DragMode::Orbit:
					{
						// Horizontal drag yaws around world +Y, vertical pitches.
						// Dragging right rotates the scene left — editor-standard.
						const float dYaw   = -delta.x * m_OrbitSpeed;
						const float dPitch =  delta.y * m_OrbitSpeed;
						m_YawDeg   += dYaw;
						m_PitchDeg += dPitch;
						m_PitchDeg  = std::clamp(m_PitchDeg, m_MinPitchDeg, m_MaxPitchDeg);
						m_OrbitVelocity = { dYaw, dPitch };
						break;
					}
					case DragMode::Pan:
					{
						// Pan in the camera's right/up plane. Scaling by distance
						// keeps the world pinned under the cursor at any zoom.
						const float scale = m_Distance * 0.0015f * m_PanSpeed;
						m_Target -= m_Camera.GetRight() * (delta.x * scale);
						m_Target += m_Camera.GetUp()    * (delta.y * scale);
						break;
					}
					case DragMode::Dolly:
					{
						// Vertical drag dollies (drag up = zoom in), exponential so
						// the feel is uniform at any scale — same law as scroll zoom.
						const float factor = std::pow(1.01f, delta.y * m_ZoomSpeed);
						m_TargetDistance = std::clamp(m_TargetDistance * factor, m_MinDistance, m_MaxDistance);
						m_Distance       = m_TargetDistance;   // dolly is immediate (no blend)
						break;
					}
					default: break;
					}
					userInteracted = true;
				}
				m_Dragging = true;
			}
			else
			{
				m_Dragging = false;
				m_DragMode = DragMode::None;
			}
		}
		else
		{
			m_Dragging = false;
			m_DragMode = DragMode::None;
		}

		m_LastMousePos = mouse;

		if (userInteracted)
			m_Animating = false;   // a live drag always wins over a snap/frame blend

		if (m_Animating && !m_Dragging)
		{
			// Smooth pose blend toward a SnapView/Frame target (yaw along the short
			// arc). Frame-rate independent: ~63% closed per 1/12 s.
			const float blend = 1.0f - std::exp(-ts * 12.0f);

			float yawErr = m_AnimYawDeg - m_YawDeg;
			while (yawErr >  180.0f) yawErr -= 360.0f;
			while (yawErr < -180.0f) yawErr += 360.0f;

			m_YawDeg   += yawErr * blend;
			m_PitchDeg += (m_AnimPitchDeg - m_PitchDeg) * blend;
			m_Distance += (m_AnimDistance - m_Distance) * blend;
			m_Target   += (m_AnimTarget   - m_Target)   * blend;
			m_TargetDistance = m_Distance;

			if (std::abs(yawErr) < 0.05f &&
			    std::abs(m_AnimPitchDeg - m_PitchDeg) < 0.05f &&
			    std::abs(m_AnimDistance - m_Distance) < 1e-3f &&
			    glm::length(m_AnimTarget - m_Target) < 1e-3f)
			{
				m_YawDeg   = m_AnimYawDeg;
				m_PitchDeg = m_AnimPitchDeg;
				m_Distance = m_TargetDistance = m_AnimDistance;
				m_Target   = m_AnimTarget;
				m_Animating = false;
			}
		}
		else
		{
			// Smooth asymptotic zoom blend (same feel as the ortho controller's
			// zoom). Converges ~63% per 0.1 s. CAD zoom-to-cursor snaps distance, so
			// this is a no-op there; Classic scroll rides it.
			if (std::abs(m_TargetDistance - m_Distance) > 1e-4f)
			{
				const float blend = 1.0f - std::exp(-ts * 10.0f);
				m_Distance += (m_TargetDistance - m_Distance) * blend;
			}
			else
			{
				m_Distance = m_TargetDistance;
			}

			// Optional inertial orbit drift after releasing an orbit drag (off by
			// default). Decays exponentially; purely cosmetic.
			if (m_InertiaEnabled && !m_Dragging &&
			    (std::abs(m_OrbitVelocity.x) > 1e-3f || std::abs(m_OrbitVelocity.y) > 1e-3f))
			{
				m_YawDeg   += m_OrbitVelocity.x;
				m_PitchDeg += m_OrbitVelocity.y;
				m_PitchDeg  = std::clamp(m_PitchDeg, m_MinPitchDeg, m_MaxPitchDeg);
				m_OrbitVelocity *= std::exp(-ts * 8.0f);
			}
		}

		RecalculateCamera();
	}

	/////////////////////////////////////////////////////////////////////////////////

	void OrbitCameraController::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<MouseScrolledEvent>([this](MouseScrolledEvent& event) { return OnMouseScrolled(event); });
		dispatcher.Dispatch<WindowResizeEvent>([this](WindowResizeEvent& event) { return OnWindowResized(event); });
	}

	void OrbitCameraController::OnResize(float width, float height)
	{
		m_Camera.SetViewportSize(width, height);
	}

	/////////////////////////////////////////////////////////////////////////////////

	void OrbitCameraController::SetDistance(float distance)
	{
		m_Animating      = false;   // explicit hard-set cancels any snap/frame blend
		m_Distance       = std::clamp(distance, m_MinDistance, m_MaxDistance);
		m_TargetDistance = m_Distance;
		RecalculateCamera();
	}

	void OrbitCameraController::SetTargetDistance(float distance)
	{
		m_TargetDistance = std::clamp(distance, m_MinDistance, m_MaxDistance);
	}

	void OrbitCameraController::SetYawPitch(float yawDeg, float pitchDeg)
	{
		m_Animating = false;        // explicit hard-set cancels any snap/frame blend
		m_YawDeg   = yawDeg;
		m_PitchDeg = std::clamp(pitchDeg, m_MinPitchDeg, m_MaxPitchDeg);
		RecalculateCamera();
	}

	/////////////////////////////////////////////////////////////////////////////////

	glm::vec3 OrbitCameraController::PoseToOffset(float yawDeg, float pitchDeg, float dist) const
	{
		// Spherical mount: yaw 0 / pitch 0 puts the camera on the target's +Z side.
		// Positive pitch raises the camera above the target (looking down).
		const float yaw   = glm::radians(yawDeg);
		const float pitch = glm::radians(pitchDeg);
		return glm::vec3(
			dist * std::cos(pitch) * std::sin(yaw),
			dist * std::sin(pitch),
			dist * std::cos(pitch) * std::cos(yaw));
	}

	void OrbitCameraController::RecalculateCamera()
	{
		m_Camera.LookAt(m_Target + PoseToOffset(m_YawDeg, m_PitchDeg, m_Distance), m_Target);
	}

	/////////////////////////////////////////////////////////////////////////////////

	bool OrbitCameraController::OnMouseScrolled(MouseScrolledEvent& e)
	{
		if (!m_ControlEnabled)
			return false;

		m_Animating = false;   // scrolling cancels a snap/frame blend

		// Exponential zoom: each notch scales the distance, so zoom speed is
		// proportional to how far out you are (uniform-feeling at every scale).
		const float factor = std::pow(1.15f, -e.GetYOffset() * m_ZoomSpeed);

		if (m_NavStyle == NavStyle::CAD)
		{
			// Zoom TOWARD the cursor: scale the whole rig about the world point P
			// under the cursor by `ratio`. Since (target - P) and the camera offset
			// both scale by ratio, the camera slides along the cursor ray and P
			// stays pinned to the same pixel. Distance is snapped (not blended) so
			// the point stays fixed even across rapid scrolls.
			const float newDist = std::clamp(m_Distance * factor, m_MinDistance, m_MaxDistance);
			const float ratio   = (m_Distance > 1e-4f) ? (newDist / m_Distance) : 1.0f;

			glm::vec3 pivot;
			if (ComputeCursorPivot(pivot))
				m_Target = pivot + (m_Target - pivot) * ratio;

			m_Distance = m_TargetDistance = newDist;
			RecalculateCamera();
		}
		else
		{
			// Classic: zoom toward the center with the smooth blend in OnUpdate.
			m_TargetDistance = std::clamp(m_TargetDistance * factor, m_MinDistance, m_MaxDistance);
		}

		return false; // never consume — see the event consumption contract
	}

	bool OrbitCameraController::OnWindowResized(WindowResizeEvent& e)
	{
		if (e.GetHeight() > 0)
			OnResize(static_cast<float>(e.GetWidth()), static_cast<float>(e.GetHeight()));

		return false; // never consume — resize must reach framebuffers/viewports
	}

	/////////////////////////////////////////////////////////////////////////////////
	// CAD Navigation (S5.1)
	/////////////////////////////////////////////////////////////////////////////////

	void OrbitCameraController::SetViewportRect(const glm::vec2& posPx, const glm::vec2& sizePx)
	{
		m_ViewportPos  = posPx;
		m_ViewportSize = sizePx;
		if (sizePx.x > 0.0f && sizePx.y > 0.0f)
			m_Camera.SetViewportSize(sizePx.x, sizePx.y);
	}

	bool OrbitCameraController::CursorRay(glm::vec3& outOrigin, glm::vec3& outDir) const
	{
		if (m_ViewportSize.x <= 0.0f || m_ViewportSize.y <= 0.0f)
			return false;

		// Screen px → viewport-local [0,1] → NDC [-1,1]. Screen y grows downward,
		// so it flips to NDC y (up-positive).
		const glm::vec2 mouse = Input::GetMouseScreenPosition();
		const float u = (mouse.x - m_ViewportPos.x) / m_ViewportSize.x;
		const float v = (mouse.y - m_ViewportPos.y) / m_ViewportSize.y;
		const glm::vec2 ndc(u * 2.0f - 1.0f, 1.0f - v * 2.0f);

		const glm::mat4 invVP = glm::inverse(m_Camera.GetViewProjectionMatrix());
		glm::vec4 pNear = invVP * glm::vec4(ndc, -1.0f, 1.0f);
		glm::vec4 pFar  = invVP * glm::vec4(ndc,  1.0f, 1.0f);
		if (std::abs(pNear.w) < 1e-8f || std::abs(pFar.w) < 1e-8f)
			return false;

		const glm::vec3 worldNear = glm::vec3(pNear) / pNear.w;
		const glm::vec3 worldFar  = glm::vec3(pFar)  / pFar.w;
		const glm::vec3 dir = worldFar - worldNear;
		if (glm::length(dir) < 1e-8f)
			return false;

		outOrigin = worldNear;
		outDir    = glm::normalize(dir);
		return true;
	}

	bool OrbitCameraController::ComputeCursorPivot(glm::vec3& outWorld) const
	{
		// 1) Precise: the app-supplied depth probe (surface point under the cursor).
		if (m_PivotProbe)
		{
			glm::vec3 hit;
			if (m_PivotProbe(Input::GetMouseScreenPosition(), hit))
			{
				outWorld = hit;
				return true;
			}
		}

		// 2) Fallback (needs only S1): cursor ray ∩ the plane through the target
		//    facing the camera. Good enough when geometry sits near the target.
		glm::vec3 ro, rd;
		if (!CursorRay(ro, rd))
			return false;

		const glm::vec3 n = m_Camera.GetForward();     // look direction, into the scene
		const float denom = glm::dot(rd, n);
		if (std::abs(denom) < 1e-6f)
			return false;

		const float t = glm::dot(m_Target - ro, n) / denom;
		if (t <= 0.0f)
			return false;

		outWorld = ro + rd * t;
		return true;
	}

	void OrbitCameraController::ReanchorAround(const glm::vec3& pivot)
	{
		// Re-derive (target, yaw, pitch, distance) so the rig orbits about `pivot`
		// while leaving the camera exactly where it is.
		const glm::vec3 camPos = m_Camera.GetPosition();
		const glm::vec3 off    = camPos - pivot;
		const float     dist   = glm::length(off);
		if (dist < 1e-3f)
			return;   // camera basically at the pivot — keep the current framing

		m_Target   = pivot;
		m_Distance = m_TargetDistance = std::clamp(dist, m_MinDistance, m_MaxDistance);

		// Invert PoseToOffset: off = dist * (cosP·sinY, sinP, cosP·cosY).
		m_PitchDeg = std::clamp(glm::degrees(std::asin(glm::clamp(off.y / dist, -1.0f, 1.0f))),
		                        m_MinPitchDeg, m_MaxPitchDeg);
		m_YawDeg   = glm::degrees(std::atan2(off.x, off.z));
		RecalculateCamera();
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Frame & Snap Views (S5.2)
	/////////////////////////////////////////////////////////////////////////////////

	void OrbitCameraController::BeginPoseAnimation(float yawDeg, float pitchDeg, float dist, const glm::vec3& target)
	{
		m_AnimYawDeg   = yawDeg;
		m_AnimPitchDeg = std::clamp(pitchDeg, m_MinPitchDeg, m_MaxPitchDeg);
		m_AnimDistance = std::clamp(dist, m_MinDistance, m_MaxDistance);
		m_AnimTarget   = target;
		m_Animating    = true;
	}

	void OrbitCameraController::SnapView(ViewPreset preset, bool animate)
	{
		float yaw = m_YawDeg, pitch = m_PitchDeg;
		switch (preset)
		{
		case ViewPreset::Front:  yaw =    0.0f; pitch =   0.0f; break;   // camera on +Z, looks -Z
		case ViewPreset::Back:   yaw =  180.0f; pitch =   0.0f; break;
		case ViewPreset::Right:  yaw =   90.0f; pitch =   0.0f; break;   // camera on +X
		case ViewPreset::Left:   yaw =  -90.0f; pitch =   0.0f; break;
		case ViewPreset::Top:    yaw =    0.0f; pitch =  89.0f; break;   // shy of the pole
		case ViewPreset::Bottom: yaw =    0.0f; pitch = -89.0f; break;
		case ViewPreset::Iso:    yaw =   45.0f; pitch =  30.0f; break;
		}

		if (animate)
			BeginPoseAnimation(yaw, pitch, m_TargetDistance, m_Target);
		else
			SetYawPitch(yaw, pitch);   // hard-set already cancels animation
	}

	void OrbitCameraController::FrameSphere(const glm::vec3& center, float radius, bool animate)
	{
		if (radius <= 0.0f)
			return;

		// Fit so the sphere fills ~70% of the viewport HALF-height (≈70% of the full
		// height diameter) at any aspect: half-height at distance d is d·tan(fovY/2).
		const float halfFov = glm::radians(m_Camera.GetFovY() * 0.5f);
		const float t = std::tan(halfFov);
		float dist = (t > 1e-4f) ? (radius / (0.7f * t)) : (radius * 3.0f);
		dist = std::clamp(dist, m_MinDistance, m_MaxDistance);

		if (animate)
			BeginPoseAnimation(m_YawDeg, m_PitchDeg, dist, center);
		else
		{
			m_Animating = false;
			m_Target    = center;
			SetDistance(dist);
		}
	}

	void OrbitCameraController::FrameBounds(const glm::vec3& worldMin, const glm::vec3& worldMax, bool animate)
	{
		const glm::vec3 center = 0.5f * (worldMin + worldMax);
		const float     radius = 0.5f * glm::length(worldMax - worldMin);   // bounding-sphere of the box
		if (radius <= 1e-6f)
			return;
		FrameSphere(center, radius, animate);
	}

	/////////////////////////////////////////////////////////////////////////////////
}
