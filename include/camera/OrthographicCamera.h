// An orthographic camera (2D) camera is basically a parallel projection

#ifndef ORTHOGRAPHIC_CAMERA_H
#define ORTHOGRAPHIC_CAMERA_H

#include <glm/glm.hpp>


class OrthographicCamera {
public:
    OrthographicCamera(float left, float right, float bottom, float top);

    // Add this to update the projection without recreating the object
    void setProjection(float left, float right, float bottom, float top);

    void setPosition(const glm::vec3& position) { m_Position = position; updateViewMatrix(); }
    const glm::mat4& getProjectionMatrix() const { return m_ProjectionMatrix; }
    const glm::mat4& getViewMatrix() const { return m_ViewMatrix; }

private:
    void updateViewMatrix();

    glm::mat4 m_ProjectionMatrix;
    glm::mat4 m_ViewMatrix;
    glm::vec3 m_Position = { 0.0f, 0.0f, 0.0f };
};

#endif