#include "camera/OrthographicCamera.h"
#include <glm/gtc/matrix_transform.hpp>

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Constructor
	 *
	 * Initializes the camera with an orthographic projection matrix based on the
	 * provided boundaries. The View matrix is initialized to identity (origin),
	 * and the initial View-Projection matrix is calculated.
	 */
	OrthographicCamera::OrthographicCamera(float left, float right, float bottom, float top)
		: m_ProjectionMatrix(glm::ortho(left, right, bottom, top, -1.0f, 1.0f)), m_ViewMatrix(1.0f)
	{
		m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Destructor
	 *
	 * Standard cleanup for the OrthographicCamera instance.
	 */
	OrthographicCamera::~OrthographicCamera()
	{
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * SetProjection
	 *
	 * Redefines the orthographic frustum (visible area). This is typically called
	 * by the CameraController during window resize events to ensure the aspect
	 * ratio remains consistent and objects do not appear stretched.
	 */
	void OrthographicCamera::SetProjection(float left, float right, float bottom, float top)
	{
		m_ProjectionMatrix = glm::ortho(left, right, bottom, top, -1.0f, 1.0f);
		m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
	}

	/**
	 * SetProjection (depth-ranged overload, U3)
	 *
	 * Same as above with an explicit near/far clip range, for ortho views that
	 * must see real world depth (the editor 2D mode's Z-facing camera).
	 */
	void OrthographicCamera::SetProjection(float left, float right, float bottom, float top,
	                                       float nearZ, float farZ)
	{
		m_ProjectionMatrix = glm::ortho(left, right, bottom, top, nearZ, farZ);
		m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * UpdateViewMatrix
	 *
	 * INTERNAL MATH LOGIC: Calculates the View Matrix based on Position and Rotation.
	 *
	 * The "View" is the inverse of the camera's world transform. To simulate the
	 * camera moving right, we must move the world to the left. We calculate a
	 * standard transform matrix (Translation * Rotation) and then apply
	 * glm::inverse() to produce the final View Matrix.
	 *
	 * The View-Projection matrix is updated at the end to be ready for shader upload.
	 */
	void OrthographicCamera::UpdateViewMatrix()
	{
		// For a pure translate * rotate transform, (T*R)^-1 == R^T * T(-pos).
		// transpose(R) == inverse(R) because rotation matrices have orthonormal columns.
		// This avoids the general-case glm::inverse() (Cramer's rule, ~80 ops) in favour
		// of the closed-form (~10 ops), which matters since this is called on every
		// SetPosition and SetRotation mutation.
		glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), glm::radians(m_Rotation), { 0, 0, 1 });
		m_ViewMatrix = glm::transpose(rotation) *
		               glm::translate(glm::mat4(1.0f), -m_Position);

		m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
	}

	/////////////////////////////////////////////////////////////////////////////////
}