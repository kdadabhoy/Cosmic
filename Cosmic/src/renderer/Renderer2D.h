#pragma once
// Renderer2D.h
// Last Modified: 5/24/2026

/**
 * * General Description:
 * Renderer2D is a high-performance, 2D-specific hardware batch rendering system
 * designed to minimize draw call overhead. It manages separate internal vertex staging
 * buffers for Quads, Lines, and procedurally generated Circles. The system dynamically
 * executes automatic "flushes" to dispatch vertex arrays to the GPU when target buffer capacities
 * are saturated or when pipeline state transitions (such as switching Active Materials or Textures) occur.
 *
 * * Public Function Prototypes (Pre and Post Conditions):
 *
 * 1. void Init()
 * Pre:  None.
 * Post: Core batching geometry data pools, Vertex Array objects, layouts, procedural shaders,
 * and fallback color texture resources are fully allocated on the GPU.
 *
 * 2. void Shutdown()
 * Pre:  The subsystem was previously initialized.
 * Post: System allocations on the CPU are deleted; smart pointer tracking handles clean up graphics resources safely.
 *
 * 3. void BeginScene(const OrthographicCamera& camera)
 * Pre:  An active camera projection instance is provided.
 * Post: Clears geometry index/vertex tracking counters and caches the frame's View-Projection uniform matrix.
 *
 * 4. void EndScene()
 * Pre:  BeginScene() was called to establish a render block context.
 * Post: Finalizes scene layout graph entries and executes a mandatory Flush() pass to render remaining batched geometry.
 *
 * 5. void Flush()
 * Pre:  A scene pass is currently active.
 * Post: Submits all currently staged Quads, Lines, and procedural Circles to the GPU via independent pipeline draw calls.
 *
 * 6. void SetViewportSize(uint32_t width, uint32_t height)
 * Pre:  None.
 * Post: Updates internal viewport scale tracking vector variables.
 *
 * 7. void DrawQuad(...) [Multiple Overloads: Pure Color, Texture Assets, SubTexture Atlases, Materials]
 * Pre:  BeginScene() has been called.
 * Post: Appends 4 layout vertices to the Quad batch array; forces FlushAndReset() if index boundaries are exceeded.
 *
 * 8. void DrawRotatedQuad(...) [Multiple Overloads: Pure Color, Texture Assets, SubTexture Atlases, Materials]
 * Pre:  BeginScene() has been called.
 * Post: Applies a Z-axis rotation matrix transformation onto vertex coordinates before submitting them to the Quad batch.
 *
 * 9. void DrawCircle(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color, float thickness, float fade) [And 2D Vector Overload]
 * Pre:  BeginScene() has been called.
 * Post: Builds a quad boundary block passing explicit standard screen space offsets [-1, 1] as LocalPosition. Stages elements
 * into the specialized Circle buffer for procedural rendering via Signed Distance Fields (SDF).
 *
 * 10. void DrawLine(const glm::vec3& p0, const glm::vec3& p1, const glm::vec4& color)
 * Pre:  BeginScene() has been called.
 * Post: Appends two distinct point vertex elements directly into the debug Line batch array.
 *
 * 11. void DrawRect(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color)
 * Pre:  BeginScene() has been called.
 * Post: Coordinates 4 independent sequence calls to DrawLine() to render a wireframe rectangle boundary.
 *
 * 12. void SetStatsStatus(bool enabled)
 * Pre:  None.
 * Post: Toggles performance tracking engine calculations.
 *
 * 13. Statistics GetStats()
 * Pre:  None.
 * Post: Returns a local snapshot data payload containing current frame draw call metrics and element totals.
 *
 * 14. void ResetStats()
 * Pre:  None.
 * Post: Clears all tracking registers in the telemetry memory structure.
 */

#pragma once
#include "core/Core.h"
#include "camera/OrthographicCamera.h"
#include "graphics/Texture.h"
#include "graphics/Material.h"
#include <glm/glm.hpp>

namespace Cosmic
{
	// Forward Declaration
	class SubTexture2D;

	class COSMIC_API Renderer2D
	{
	public:
		static void Init();
		static void Shutdown();

		static void BeginScene(const OrthographicCamera& camera);
		static void EndScene();
		static void Flush();

		// Viewport handling
		static void SetViewportSize(uint32_t width, uint32_t height);

		// SubTexture2D Drawing Overloads
		static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const Ref<SubTexture2D>& subTexture, const glm::vec4& tintColor = glm::vec4(1.0f));
		static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<SubTexture2D>& subTexture, const glm::vec4& tintColor = glm::vec4(1.0f));
		static void DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const Ref<SubTexture2D>& subTexture, const glm::vec4& tintColor = glm::vec4(1.0f));
		static void DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const Ref<SubTexture2D>& subTexture, const glm::vec4& tintColor = glm::vec4(1.0f));

		// Material-Based Drawing Overloads
		static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const Ref<Material>& material);
		static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Material>& material);
		static void DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const Ref<Material>& material);

		// Primitive Drawing (Quads - Tint/Texture)
		static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color);
		static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color);
		static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const Ref<Texture>& texture, float tilingFactor = 1.0f, const glm::vec4& tintColor = glm::vec4(1.0f));
		static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Texture>& texture, float tilingFactor = 1.0f, const glm::vec4& tintColor = glm::vec4(1.0f));

		// Rotated Quads (Tint/Texture)
		static void DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const glm::vec4& color);
		static void DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const glm::vec4& color);
		static void DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const Ref<Texture>& texture, float tilingFactor = 1.0f, const glm::vec4& tintColor = glm::vec4(1.0f));
		static void DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const Ref<Texture>& texture, float tilingFactor = 1.0f, const glm::vec4& tintColor = glm::vec4(1.0f));

		// Specialized Math Primitives
		static void DrawCircle(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color, float thickness = 1.0f, float fade = 0.005f);
		inline static void DrawCircle(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color, float thickness = 1.0f, float fade = 0.005f) { DrawCircle({ position.x, position.y, 0.0f }, size, color, thickness, fade); }

		// Utilities
		static void DrawLine(const glm::vec3& p0, const glm::vec3& p1, const glm::vec4& color);
		static void DrawRect(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color);

		// Telemetry / Statistics
		struct Statistics
		{
			uint32_t DrawCalls = 0;
			uint32_t QuadCount = 0;
			uint32_t LineCount = 0;

			uint32_t GetTotalVertexCount() const { return QuadCount * 4 + LineCount * 2; }
			uint32_t GetTotalIndexCount() const { return QuadCount * 6; }
		};
		static void ResetStats();
		static Statistics GetStats();
		static void SetStatsStatus(bool enabled);

	private:
		static void FlushAndReset();
	};
} // namespace Cosmic