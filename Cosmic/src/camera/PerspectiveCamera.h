#pragma once

// PerspectiveCamera.h
// Last Modified: 7/1/2026

/**
 * General Description:
 * The PerspectiveCamera is the 3D counterpart of OrthographicCamera: a pinhole
 * projection (vertical FOV / aspect / near / far) plus a rigid transform stored
 * as position + quaternion orientation. Distant objects shrink — this is the
 * camera for the 3D viewport (Renderer3D), simulators, and any world where
 * depth perception matters.
 *
 * Frame convention (see math/Spatial.h): the RENDER frame is right-handed,
 * Y-up. The camera looks down its LOCAL -Z axis (OpenGL convention); +X is
 * right, +Y is up. Simulation code working in NED converts through
 * Math::NedToRender before driving this camera.
 *
 * Documentation Notes:
 * - Projection Matrix: glm::perspective(fovY, aspect, near, far).
 * - View Matrix: inverse of the camera's world transform. The world moves
 *   opposite to the camera, exactly like OrthographicCamera.
 * - ViewProjectionMatrix: cached Projection * View, recomputed on any change.
 *
 * Public Function Prototypes (Pre and Post Conditions):
 *
 * 1. PerspectiveCamera(float fovYDegrees, float aspect, float nearClip, float farClip)
 *    Pre:  fovY in (0, 180); aspect > 0; 0 < near < far.
 *    Post: Projection and View-Projection matrices are initialized.
 *
 * 2. void SetProjection(float fovYDegrees, float aspect, float nearClip, float farClip)
 *    Pre:  Same as constructor.
 *    Post: Projection matrix is recomputed; View-Projection refreshed.
 *
 * 3. void SetViewportSize(float width, float height)
 *    Pre:  width/height > 0 (a zero height is ignored).
 *    Post: Aspect ratio updated, projection recomputed. Call on viewport resize.
 *
 * 4. void SetPosition(const glm::vec3&) / SetOrientation(const glm::quat&)
 *    Pre:  Orientation must be (close to) unit length.
 *    Post: View and View-Projection matrices are recomputed.
 *
 * 5. void LookAt(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up)
 *    Pre:  eye != target; up not parallel to (target - eye).
 *    Post: Position = eye; orientation faces target; matrices recomputed.
 *
 * 6. GetForward() / GetRight() / GetUp()
 *    Pre:  None.
 *    Post: Returns the camera's world-space basis vectors (unit length).
 */

#include "core/Core.h"
#include "camera/Camera.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Cosmic
{
	class COSMIC_API PerspectiveCamera : public Camera
	{
	public:
		////////////////////////////////
		// Constructor & Destructor
		///////////////////////////////

		PerspectiveCamera(float fovYDegrees = 45.0f, float aspect = 16.0f / 9.0f,
		                  float nearClip = 0.1f, float farClip = 1000.0f);
		~PerspectiveCamera() = default;

		////////////////////////////////
		// Projection
		///////////////////////////////

		void					SetProjection(float fovYDegrees, float aspect, float nearClip, float farClip);
		void					SetViewportSize(float width, float height);   // updates aspect only

		float					GetFovY() const						{ return m_FovYDegrees; }
		float					GetAspect() const					{ return m_Aspect; }
		float					GetNearClip() const					{ return m_NearClip; }
		float					GetFarClip() const					{ return m_FarClip; }

		////////////////////////////////
		// Getters (Matrices & State)
		///////////////////////////////

		const glm::vec3&		GetPosition() const override		{ return m_Position; }
		const glm::quat&		GetOrientation() const				{ return m_Orientation; }

		const glm::mat4&		GetProjectionMatrix() const override		{ return m_ProjectionMatrix; }
		const glm::mat4&		GetViewMatrix() const override				{ return m_ViewMatrix; }
		const glm::mat4&		GetViewProjectionMatrix() const override	{ return m_ViewProjectionMatrix; }

		// World-space camera basis (unit vectors). Forward is the LOOK direction
		// (local -Z rotated into world space), not the +Z axis.
		glm::vec3				GetForward() const;
		glm::vec3				GetRight() const;
		glm::vec3				GetUp() const;

		////////////////////////////////
		// Setters (Transformation)
		///////////////////////////////

		void					SetPosition(const glm::vec3& position)		{ m_Position = position; UpdateViewMatrix(); }
		void					SetOrientation(const glm::quat& orientation){ m_Orientation = orientation; UpdateViewMatrix(); }

		// Place the camera at eye, looking at target. Up defaults to world +Y.
		void					LookAt(const glm::vec3& eye, const glm::vec3& target,
		                               const glm::vec3& up = { 0.0f, 1.0f, 0.0f });


	private:
		////////////////////////////////
		// Internal Matrix Math
		///////////////////////////////

		void					UpdateProjectionMatrix();
		void					UpdateViewMatrix();


	private:
		////////////////////////////////
		// Matrix Storage
		///////////////////////////////

		glm::mat4				m_ProjectionMatrix{ 1.0f };
		glm::mat4				m_ViewMatrix{ 1.0f };
		glm::mat4				m_ViewProjectionMatrix{ 1.0f };

		////////////////////////////////
		// Camera State
		///////////////////////////////

		glm::vec3				m_Position    = { 0.0f, 0.0f, 0.0f };
		glm::quat				m_Orientation = { 1.0f, 0.0f, 0.0f, 0.0f }; // identity (w, x, y, z)

		float					m_FovYDegrees = 45.0f;
		float					m_Aspect      = 16.0f / 9.0f;
		float					m_NearClip    = 0.1f;
		float					m_FarClip     = 1000.0f;
	};
}
