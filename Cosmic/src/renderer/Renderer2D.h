#pragma once

// Renderer2D.h
// Last Modified 5/14/2026

/**
 * General Description:
 * Renderer2D is a high-performance, 2D-specific rendering system that utilizes
 * batch rendering to minimize draw calls. It manages internal vertex buffers for
 * quads and lines, automatically "flushing" data to the GPU when buffers are full
 * or when a state change (like a new Material or Texture) occurs.
 *
 * Public Function Prototypes (Pre and Post Conditions):
 *
 * 1. void Init()
 *    Pre:  None.
 *    Post: Internal batching buffers, shaders, and white texture resources are allocated.
 *
 * 2. void Shutdown()
 *    Pre:  The system was previously initialized.
 *    Post: All allocated CPU and GPU resources are safely released.
 *
 * 3. void BeginScene(const OrthographicCamera& camera)
 *    Pre:  None.
 *    Post: Resets batch counters and caches the camera's View-Projection matrix for the frame.
 *
 * 4. void EndScene()
 *    Pre:  BeginScene() has been called.
 *    Post: Finalizes the scene and triggers a Flush() to render remaining batched data.
 *
 * 5. void Flush()
 *    Pre:  A scene is currently active.
 *    Post: Submits all currently batched vertices to the GPU and executes the draw call.
 *
 * 6. void DrawQuad(...) [Multiple Overloads]
 *    Pre:  BeginScene() has been called.
 *    Post: Adds vertex data for a quad to the batch; triggers FlushAndReset() if capacity is reached.
 *
 * 7. void DrawRotatedQuad(...) [Multiple Overloads]
 *    Pre:  BeginScene() has been called.
 *    Post: Transforms quad vertices by the given rotation and adds them to the batch.
 *
 * 8. void DrawLine(const glm::vec3& p0, const glm::vec3& p1, const glm::vec4& color)
 *    Pre:  BeginScene() has been called.
 *    Post: Adds two vertices to the line batch for rendering.
 *
 * 9. void DrawRect(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color)
 *    Pre:  BeginScene() has been called.
 *    Post: Submits four DrawLine() calls to form a wireframe rectangle.
 *
 * 10. void SetStatsStatus(bool enabled)
 *    Pre:  None.
 *    Post: Toggles the internal recording of draw calls and quad counts.
 *
 * 11. Statistics GetStats()
 *    Pre:  None.
 *    Post: Returns a copy of the current performance telemetry data.
 *
 * 12. void ResetStats()
 *    Pre:  None.
 *    Post: Zeroes out all performance counters.
 */

#include "core/Core.h"
#include "camera/OrthographicCamera.h"
#include "graphics/Texture.h"
#include "graphics/Material.h"
#include <glm/glm.hpp>

namespace Cosmic
{
	class COSMIC_API Renderer2D
	{
	public:
		////////////////////////////////
		// Lifecycle & Scene Control
		///////////////////////////////

		static void Init();
		static void Shutdown();

		static void BeginScene(const OrthographicCamera& camera);
		static void EndScene();
		static void Flush();

		////////////////////////////////
		// Standard Primitive Drawing
		///////////////////////////////

		static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color);
		static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color);
		static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const Ref<Texture>& texture, float tilingFactor = 1.0f, const glm::vec4& tintColor = glm::vec4(1.0f));
		static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Texture>& texture, float tilingFactor = 1.0f, const glm::vec4& tintColor = glm::vec4(1.0f));

		////////////////////////////////
		// Material-Based Drawing
		///////////////////////////////

		static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Material>& material);
		static void DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const Ref<Material>& material);

		////////////////////////////////
		// Debug & Line Drawing
		///////////////////////////////

		static void DrawLine(const glm::vec3& p0, const glm::vec3& p1, const glm::vec4& color);
		static void DrawRect(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color);

		////////////////////////////////
		// Rotation Overloads
		///////////////////////////////

		static void DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const glm::vec4& color);
		static void DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const glm::vec4& color);
		static void DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const Ref<Texture>& texture, float tilingFactor = 1.0f, const glm::vec4& tintColor = glm::vec4(1.0f));
		static void DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const Ref<Texture>& texture, float tilingFactor = 1.0f, const glm::vec4& tintColor = glm::vec4(1.0f));


		////////////////////////////////
		// Telemetry & Statistics
		///////////////////////////////
		struct Statistics
		{
			uint32_t DrawCalls = 0;
			uint32_t QuadCount = 0;
			uint32_t GetTotalVertexCount() const { return QuadCount * 4; }
			uint32_t GetTotalIndexCount() const { return QuadCount * 6; }
		};

		static void SetStatsStatus(bool enabled);
		static Statistics GetStats();
		static void ResetStats();

	private:
		static void FlushAndReset();
	};
}