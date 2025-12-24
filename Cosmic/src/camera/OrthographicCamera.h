#pragma once

#include <glm/glm.hpp>


namespace Cosmic
{
	class OrthographicCamera
	{
	public:
		OrthographicCamera(float left, float right, float bottom, float top);

		void SetProjection(float left, float right, float bottom, float top);

		const glm::vec3& GetPosition() const { return m_Position; }
		void SetPosition(const glm::vec3& position) { m_Position = position; UpdateViewMatrix(); }

		float GetRotation() const { return m_Rotation; }
		void SetRotation(float rotation) { m_Rotation = rotation; UpdateViewMatrix(); }

		const glm::mat4& GetProjectionMatrix() const { return m_ProjectionMatrix; }
		const glm::mat4& GetViewMatrix() const { return m_ViewMatrix; }
		const glm::mat4& GetViewProjectionMatrix() const { return m_ViewProjectionMatrix; }


	private:
		void UpdateViewMatrix();


	private:
		glm::mat4 m_ProjectionMatrix;
		glm::mat4 m_ViewMatrix;
		glm::mat4 m_ViewProjectionMatrix;

		glm::vec3 m_Position = { 0.0f, 0.0f, 0.0f };
		float m_Rotation = 0.0f; // Added rotation for more dynamic movement
	};

}











/*	Documentation:

	The OrthographicCamera serves as the eyes of the engine in the 2D world.
		- Orthographic projection, bc in 2D we don't want perspective distortion (object is same size whether it is close or far away)


	Two main matrices for 2D stuff (that this class manages)

		1. Projection Matrix
			- This determines the boundaries of the world (how far up, left, down, or right that we can see)

		2. View Matrix
			- This defines where the Camera is in the world (tracks the camera's x,y,z location)

	UpdateViewMatrix()
		- The camera doesnt actually move... the world moves around the camera
			- If we want the camera to move 5 units right, the engine actually has to move every object in the world 5 units left
			- So, we first transform every object (apply a rotational matrix and translation) and then we take the inverse of that... and that is our new ViewMatrix



	m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix


	... This needs to be reworked and rewritten... the ViewProjectionMatrix is updated once by CPU... and then used in the shader with the particular object's transform matrix...






*/