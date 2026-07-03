#pragma once

// Camera.h
// Last Modified: 7/2/2026

/**
 * General Description:
 *
 * Camera is the pure-interface base that unifies OrthographicCamera (2D) and
 * PerspectiveCamera (3D) behind a single type. It exists so renderer entry
 * points can accept ANY camera by const-reference — Renderer2D::BeginScene,
 * Renderer3D::BeginScene, and RenderPass all take `const Camera&` and call only
 * the four getters below. Concrete cameras keep their own rich, type-specific
 * APIs (SetProjection, LookAt, orbit controls, …); this base is deliberately
 * minimal so the forward-compatibility contract (doc 05 §1 rule 1 —
 * camera-agnostic core) holds for every future camera type without touching a
 * single existing call site.
 *
 * Design notes:
 * - PURE INTERFACE: no data members, no stored matrices. Each derived camera
 *   already caches these values; the getters just expose the cache. Adding a
 *   vtable to the exported camera classes is an ABI change (build_all).
 * - All getters return `const&` to the derived camera's cached member — no
 *   per-call recomputation, matching the existing inline getters.
 *
 * Public Function Prototypes (Pre and Post Conditions):
 *
 * 1. virtual const glm::mat4& GetViewMatrix() const
 *    Post: Returns the camera's cached world->view matrix.
 *
 * 2. virtual const glm::mat4& GetProjectionMatrix() const
 *    Post: Returns the camera's cached projection matrix.
 *
 * 3. virtual const glm::mat4& GetViewProjectionMatrix() const
 *    Post: Returns the cached Projection * View matrix (the value shaders consume).
 *
 * 4. virtual const glm::vec3& GetPosition() const
 *    Post: Returns the camera's world-space position.
 */

#include "core/Core.h"
#include <glm/glm.hpp>

namespace Cosmic
{
	class COSMIC_API Camera
	{
	public:
		virtual ~Camera() = default;

		virtual const glm::mat4& GetViewMatrix() const           = 0;
		virtual const glm::mat4& GetProjectionMatrix() const     = 0;
		virtual const glm::mat4& GetViewProjectionMatrix() const = 0;
		virtual const glm::vec3& GetPosition() const             = 0;
	};
}
