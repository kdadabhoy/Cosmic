// Gizmo.cpp
// See Gizmo.h — engine wrapper over vendored ImGuizmo (S5.5).

#include "graphics/Gizmo.h"
#include "camera/Camera.h"
#include "scene/Components.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>

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
			ToOp(op), ToMode(space),
			glm::value_ptr(model), nullptr, snapPtr);
	}

	bool Gizmo::Manipulate(const Camera& camera, TransformComponent& transform,
	                       Operation op, Space space, float snap)
	{
		glm::mat4 model = transform.GetTransform();
		if (!Manipulate(camera, model, op, space, snap))
			return false;

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

		transform.Position        = t;
		transform.Scale           = s;
		transform.RotationQuat    = glm::normalize(glm::quat_cast(rot));
		transform.UseQuatRotation = true;   // gizmo drives the quaternion path
		return true;
	}
}
