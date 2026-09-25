// Gizmo.cpp
// See Gizmo.h — engine wrapper over vendored ImGuizmo (S5.5).

#include "graphics/Gizmo.h"
#include "camera/Camera.h"
#include "scene/Components.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace Cosmic
{
	namespace
	{
		ImGuizmo::OPERATION ToOp(Gizmo::Operation op)
		{
			switch (op)
			{
			case Gizmo::Operation::Rotate:    return ImGuizmo::ROTATE;
			case Gizmo::Operation::Scale:     return ImGuizmo::SCALE;
			case Gizmo::Operation::Universal: return ImGuizmo::UNIVERSAL;   // K11
			case Gizmo::Operation::Translate:
			default:                          return ImGuizmo::TRANSLATE;
			}
		}

		ImGuizmo::MODE ToMode(Gizmo::Space space)
		{
			return space == Gizmo::Space::World ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
		}

		// UX-02 (KI-74): the 2D editor rotates about Z only — the X/Y rings would
		// author a tilt the sprite pass never draws.
		ImGuizmo::OPERATION ToOp2D(Gizmo::Operation op)
		{
			switch (op)
			{
			case Gizmo::Operation::Rotate:    return ImGuizmo::ROTATE_Z;
			case Gizmo::Operation::Universal:
				return static_cast<ImGuizmo::OPERATION>(static_cast<int>(ImGuizmo::UNIVERSAL) &
					~static_cast<int>(ImGuizmo::ROTATE_X | ImGuizmo::ROTATE_Y));
			default:                          return ToOp(op);
			}
		}

		bool ManipulateOp(const Camera& camera, glm::mat4& model, ImGuizmo::OPERATION op,
		                  Gizmo::Space space, float snap)
		{
			// Draw into (and hover-test against) the CURRENT window — the caller is in
			// the viewport window per the frame protocol. ImGuizmo resolves hover by the
			// draw list's owner window, so a window-less list (foreground/background)
			// yields a gizmo that renders but never activates.
			ImGuizmo::SetDrawlist();

			// GL-style projections carry [3][3] == 1 for orthographic, 0 for perspective.
			ImGuizmo::SetOrthographic(camera.GetProjectionMatrix()[3][3] > 0.5f);

			const float snapArr[3] = { snap, snap, snap };
			const float* snapPtr = (snap > 0.0f) ? snapArr : nullptr;

			return ImGuizmo::Manipulate(
				glm::value_ptr(camera.GetViewMatrix()),
				glm::value_ptr(camera.GetProjectionMatrix()),
				op, ToMode(space),
				glm::value_ptr(model), nullptr, snapPtr);
		}
	}

	void Gizmo::SetRect(float x, float y, float width, float height)
	{
		ImGuizmo::SetRect(x, y, width, height);
	}

	void Gizmo::SetEnabled(bool enabled) { ImGuizmo::Enable(enabled); }
	bool Gizmo::IsUsing() { return ImGuizmo::IsUsing(); }
	bool Gizmo::IsOver()  { return ImGuizmo::IsOver(); }

	bool Gizmo::Manipulate(const Camera& camera, glm::mat4& model,
	                       Operation op, Space space, float snap)
	{
		return ManipulateOp(camera, model, ToOp(op), space, snap);
	}

	bool Gizmo::Manipulate(const Camera& camera, TransformComponent& transform,
	                       Operation op, Space space, float snap, bool mode2D)
	{
		glm::mat4 model = transform.GetTransform();
		if (!ManipulateOp(camera, model, mode2D ? ToOp2D(op) : ToOp(op), space, snap))
			return false;
		ApplyModel(transform, model, mode2D);
		return true;
	}

	void Gizmo::ApplyModel(TransformComponent& transform, const glm::mat4& model, bool mode2D)
	{
		// Decompose the edited matrix: translation = 4th column; per-axis scale =
		// basis-column lengths; rotation = normalized basis columns. Extracted
		// manually (no glm::decompose) to dodge the quaternion-sign pitfall some
		// glm versions have.
		const glm::vec3 t  = glm::vec3(model[3]);
		const glm::vec3 c0 = glm::vec3(model[0]);
		const glm::vec3 c1 = glm::vec3(model[1]);
		const glm::vec3 c2 = glm::vec3(model[2]);
		const glm::vec3 s  = { glm::length(c0), glm::length(c1), glm::length(c2) };

		glm::mat3 rot;
		rot[0] = s.x > 1e-6f ? c0 / s.x : glm::vec3(1.0f, 0.0f, 0.0f);
		rot[1] = s.y > 1e-6f ? c1 / s.y : glm::vec3(0.0f, 1.0f, 0.0f);
		rot[2] = s.z > 1e-6f ? c2 / s.z : glm::vec3(0.0f, 0.0f, 1.0f);

		transform.Position = t;
		transform.Scale    = s;

		if (mode2D)
		{
			// UX-02 (KI-74): the 2D sprite pass draws the Euler Z angle
			// (Scene.cpp: radians(Rotation.z)), so the edit must land there. The
			// Euler product is Rx * Ry * Rz: divide the kept X/Y factors out and
			// read the remaining Z rotation's angle from its first column.
			const glm::mat3 rxy = glm::mat3(
				glm::rotate(glm::mat4(1.0f), glm::radians(transform.Rotation.x), { 1.0f, 0.0f, 0.0f }) *
				glm::rotate(glm::mat4(1.0f), glm::radians(transform.Rotation.y), { 0.0f, 1.0f, 0.0f }));
			const glm::mat3 rz = glm::transpose(rxy) * rot;
			float z = glm::degrees(std::atan2(rz[0][1], rz[0][0]));
			// Unwrap to the turn nearest the previous angle: a drag through 180
			// (or an authored 350 turned by +20) never jumps by 360.
			const float prev = transform.Rotation.z;
			z = prev + std::remainder(z - prev, 360.0f);
			transform.Rotation.z      = z;
			transform.UseQuatRotation = false;   // the Euler path; RotationQuat is left untouched
			return;
		}

		transform.RotationQuat    = glm::normalize(glm::quat_cast(rot));
		transform.UseQuatRotation = true;   // gizmo drives the quaternion path
	}
}
