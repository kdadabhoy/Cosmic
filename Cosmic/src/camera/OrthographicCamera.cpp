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
		glm::mat4 transform = glm::translate(glm::mat4(1.0f), m_Position) *
			glm::rotate(glm::mat4(1.0f), glm::radians(m_Rotation), glm::vec3(0, 0, 1));

		m_ViewMatrix = glm::inverse(transform);
		m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
	}

	/////////////////////////////////////////////////////////////////////////////////
}