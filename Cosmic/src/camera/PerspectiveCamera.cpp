// PerspectiveCamera.cpp
// Last Modified: 7/1/2026

#include "camera/PerspectiveCamera.h"
#include "core/Log.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	PerspectiveCamera::PerspectiveCamera(float fovYDegrees, float aspect, float nearClip, float farClip)
		: m_FovYDegrees(fovYDegrees), m_Aspect(aspect), m_NearClip(nearClip), m_FarClip(farClip)
	{
		UpdateProjectionMatrix();
		UpdateViewMatrix();
	}

	/////////////////////////////////////////////////////////////////////////////////

	void PerspectiveCamera::SetProjection(float fovYDegrees, float aspect, float nearClip, float farClip)
	{
		m_FovYDegrees = fovYDegrees;
		m_Aspect      = aspect;
		m_NearClip    = nearClip;
		m_FarClip     = farClip;
		UpdateProjectionMatrix();
	}

	void PerspectiveCamera::SetViewportSize(float width, float height)
	{
		// A zero-sized viewport happens transiently while docking/minimizing —
		// keep the previous aspect instead of dividing by zero.
		if (width <= 0.0f || height <= 0.0f)
			return;

		m_Aspect = width / height;
		UpdateProjectionMatrix();
	}

	/////////////////////////////////////////////////////////////////////////////////

	glm::vec3 PerspectiveCamera::GetForward() const
	{
		// Local -Z rotated into world space (OpenGL look direction).
		return m_Orientation * glm::vec3(0.0f, 0.0f, -1.0f);
	}

	glm::vec3 PerspectiveCamera::GetRight() const
	{
		return m_Orientation * glm::vec3(1.0f, 0.0f, 0.0f);
	}

	glm::vec3 PerspectiveCamera::GetUp() const
	{
		return m_Orientation * glm::vec3(0.0f, 1.0f, 0.0f);
	}

	/////////////////////////////////////////////////////////////////////////////////

	void PerspectiveCamera::LookAt(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up)
	{
		const glm::vec3 toTarget = target - eye;
		if (glm::dot(toTarget, toTarget) < 1e-12f)
		{
			CS_CORE_WARN("PerspectiveCamera::LookAt: eye == target — orientation unchanged.");
			m_Position = eye;
			UpdateViewMatrix();
			return;
		}

		m_Position = eye;

		// glm::lookAt builds the VIEW matrix (world -> camera); the camera's world
		// orientation is the inverse (conjugate) of that rotation.
		const glm::mat4 view = glm::lookAt(eye, target, up);
		m_Orientation = glm::normalize(glm::conjugate(glm::quat_cast(view)));

		// Reuse the exact lookAt result rather than rebuilding from the quaternion —
		// avoids one round of float error between the two representations.
		m_ViewMatrix = view;
		m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
	}

	/////////////////////////////////////////////////////////////////////////////////

	void PerspectiveCamera::UpdateProjectionMatrix()
	{
		m_ProjectionMatrix = glm::perspective(glm::radians(m_FovYDegrees), m_Aspect, m_NearClip, m_FarClip);
		m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
	}

	void PerspectiveCamera::UpdateViewMatrix()
	{
		// View = inverse of the camera's rigid transform. For rotation+translation
		// the inverse is (R^-1, -R^-1 * t) — cheaper and more numerically stable
		// than a general 4x4 inverse.
		const glm::mat4 rotation = glm::mat4_cast(glm::conjugate(m_Orientation));
		m_ViewMatrix = rotation * glm::translate(glm::mat4(1.0f), -m_Position);
		m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
	}

	/////////////////////////////////////////////////////////////////////////////////
}
