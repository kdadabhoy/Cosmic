#pragma once

// OrthographicCamera.h
// Last Modified 5/14/2026

/**
 * General Description:
 * The OrthographicCamera serves as the eyes of the engine in the 2D world.
 * It utilizes an orthographic projection to ensure objects maintain a consistent
 * size regardless of their distance (depth) from the camera, eliminating perspective
 * distortion. The class manages the View, Projection, and combined View-Projection
 * matrices, allowing for camera movement and rotation by internally inverting
 * world transformations.
 *
 * Documentation Notes:
 * - Projection Matrix: Determines the world boundaries (left, right, bottom, top).
 * - View Matrix: Defines the camera's location. The camera doesn't "move"; the
 *   world moves in the opposite direction (Inverse Transform).
 * - ViewProjectionMatrix: Updated on the CPU and passed to shaders to be
 *   multiplied by object transform matrices.
 *
 * Public Function Prototypes (Pre and Post Conditions):
 *
 * 1. OrthographicCamera(float left, float right, float bottom, float top)
 *    Pre:  Valid boundary values provided for the frustum.
 *    Post: Projection matrix is initialized and View-Projection is calculated.
 *
 * 2. ~OrthographicCamera()
 *    Pre:  The camera instance exists.
 *    Post: Camera resources are released.
 *
 * 3. void SetProjection(float left, float right, float bottom, float top)
 *    Pre:  None.
 *    Post: Re-calculates the orthographic projection matrix and updates the
 *          combined View-Projection matrix.
 *
 * 4. const glm::vec3& GetPosition()
 *    Pre:  None.
 *    Post: Returns the current 3D world position of the camera.
 *
 * 5. float GetRotation()
 *    Pre:  None.
 *    Post: Returns the camera's rotation around the Z-axis in degrees.
 *
 * 6. const glm::mat4& GetProjectionMatrix()
 *    Pre:  None.
 *    Post: Returns the 4x4 orthographic projection matrix.
 *
 * 7. const glm::mat4& GetViewMatrix()
 *    Pre:  None.
 *    Post: Returns the current view matrix (the inverse of the camera's transform).
 *
 * 8. const glm::mat4& GetViewProjectionMatrix()
 *    Pre:  None.
 *    Post: Returns the pre-multiplied Projection * View matrix for shader use.
 *
 * 9. void SetPosition(const glm::vec3& position)
 *    Pre:  None.
 *    Post: Updates the camera position and re-calculates the View and VP matrices.
 *
 * 10. void SetRotation(float rotation)
 *     Pre:  None.
 *     Post: Updates the Z-axis rotation and re-calculates the View and VP matrices.
 */

#include "core/Core.h"
#include "camera/Camera.h"
#include <glm/glm.hpp>

namespace Cosmic
{
	class COSMIC_API OrthographicCamera : public Camera
	{
	public:
		////////////////////////////////
		// Constructor & Destructor
		///////////////////////////////

		OrthographicCamera(float left, float right, float bottom, float top);
		~OrthographicCamera();

		////////////////////////////////
		// Projection & Bounds
		///////////////////////////////

		void					SetProjection(float left, float right, float bottom, float top);

		// Depth-ranged overload (U3): the 4-arg form keeps the legacy -1..1 clip
		// range; pass explicit near/far when the ortho view must see world depth
		// (the editor 2D mode looks down +Z at sprites spread across Z/ZOrder).
		void					SetProjection(float left, float right, float bottom, float top,
		                                      float nearZ, float farZ);

		////////////////////////////////
		// Getters (Matrices & State)
		///////////////////////////////

		const glm::vec3&		GetPosition() const override		{ return m_Position; }
		float					GetRotation() const					{ return m_Rotation; }

		const glm::mat4&		GetProjectionMatrix() const override		{ return m_ProjectionMatrix; }
		const glm::mat4&		GetViewMatrix() const override				{ return m_ViewMatrix; }
		const glm::mat4&		GetViewProjectionMatrix() const override	{ return m_ViewProjectionMatrix; }


		////////////////////////////////
		// Setters (Transformation)
		///////////////////////////////

		void					SetPosition(const glm::vec3& position) { m_Position = position; UpdateViewMatrix(); }
		void					SetRotation(float rotation) { m_Rotation = rotation; UpdateViewMatrix(); }


	private:
		////////////////////////////////
		// Internal Matrix Math
		///////////////////////////////

		void					UpdateViewMatrix();


	private:
		////////////////////////////////
		// Matrix Storage
		///////////////////////////////

		glm::mat4				m_ProjectionMatrix;
		glm::mat4				m_ViewMatrix;
		glm::mat4				m_ViewProjectionMatrix;

		////////////////////////////////
		// Camera State
		///////////////////////////////

		glm::vec3				m_Position = { 0.0f, 0.0f, 0.0f };
		float					m_Rotation = 0.0f; // z-axis rotation
	};

}