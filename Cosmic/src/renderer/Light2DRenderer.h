#pragma once

// Light2DRenderer.h — Phase 27 X5 (gap §12.1).
//
// ============================================================================
// COSMIC ENGINE — 2D lighting composite
// ============================================================================
//
// A screen-space light pass for the 2D sprite path. The scene draws lit=1.0,
// then this service accumulates additive radial lights (from Light2DComponent)
// into a HALF-RES HDR buffer cleared to the Ambient2D color, and MULTIPLIES that
// buffer over the currently-bound target — darkening the scene between lights.
//
// COMPAT: with no lights and a WHITE ambient the multiply is identity, so the
// caller (Scene::OnRender2DLights) skips the pass entirely ⇒ the 2D output is
// byte-identical. Normal-mapped 2D lights are out of scope v1.
//
// GL state contract: on return the transparent-phase state is restored (depth
// test ON, straight-alpha blend). Needs a live GL context; a no-op headless.
// ============================================================================

#include "core/Core.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace Cosmic
{
	class Shader;
	class FrameBuffer;

	class COSMIC_API Light2DRenderer
	{
	public:
		struct Light
		{
			glm::vec2 Center{ 0.0f };   // world XY
			float     Radius = 1.0f;
			glm::vec3 Color{ 1.0f };
			float     Intensity = 1.0f;
			float     Falloff = 2.0f;
		};

		/**
		 * Render `lights` into the half-res light buffer (cleared to `ambient`),
		 * then multiply it over the bound target. `viewProjection` is the 2D
		 * camera's; `targetW/H` the bound target's pixel size. No-op if the
		 * shaders/FBO cannot be created (headless).
		 */
		static void Composite(const std::vector<Light>& lights, const glm::vec3& ambient,
		                      const glm::mat4& viewProjection, uint32_t targetW, uint32_t targetH);

		/** Release the FBO + shaders (call while the GL context is current). */
		static void Shutdown();
	};
}
