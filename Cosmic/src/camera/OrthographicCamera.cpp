#include "camera/OrthographicCamera.h"
#include <glm/gtc/matrix_transform.hpp>



namespace Cosmic {
    OrthographicCamera::OrthographicCamera(float left, float right, float bottom, float top)
        : m_ProjectionMatrix(glm::ortho(left, right, bottom, top, -1.0f, 1.0f)), m_ViewMatrix(1.0f)
    {
    }

    void OrthographicCamera::setProjection(float left, float right, float bottom, float top)
    {
        m_ProjectionMatrix = glm::ortho(left, right, bottom, top, -1.0f, 1.0f);
    }

    void OrthographicCamera::updateViewMatrix()
    {
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), m_Position);
        m_ViewMatrix = glm::inverse(transform);
    }

}