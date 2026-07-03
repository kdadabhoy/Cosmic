#pragma once

// TextureCube.h
// Last Modified: 7/3/2026

/**
 * ============================================================================
 * COSMIC ENGINE — TextureCube (cubemap GPU resource)  [S6.3 IBL]
 * ============================================================================
 *
 * A six-face cubemap used by the image-based-lighting pipeline: the environment
 * map, its diffuse-irradiance convolution, and the roughness-mip prefiltered
 * specular map are all TextureCubes (renderer/EnvironmentMap owns the bakes).
 *
 * Factory pattern (RendererAPI-dispatched, like Texture2D / UniformBuffer) — no
 * GL tokens in this header (§0 rule 2). A cube may be a plain sampled resource
 * OR a render target: BeginRenderToFace attaches one face+mip to an internal
 * FBO so the bake passes render into it a face at a time (the classic
 * render-to-cubemap path; the compute route is a later option).
 *
 * Face index order matches GL_TEXTURE_CUBE_MAP_POSITIVE_X + i:
 *   0 +X   1 -X   2 +Y   3 -Y   4 +Z   5 -Z
 * ============================================================================
 */

#include "core/Core.h"
#include <cstdint>

namespace Cosmic
{
	/**
	 * RGBA16F is the default and the only format guaranteed COLOR-RENDERABLE by
	 * the GL spec — the IBL bakes render into cube faces, and three-channel float
	 * formats (RGB16F) are "texture-only" on some drivers (Intel/AMD/ANGLE), which
	 * makes BeginRenderToFace's framebuffer incomplete there. Use RGB16F only for
	 * cubes that are SAMPLED but never rendered into.
	 */
	enum class TextureCubeFormat { RGBA16F = 0, RGB16F };

	struct TextureCubeSpecification
	{
		uint32_t          Size      = 512;                      // per-face edge length (mip 0)
		TextureCubeFormat Format    = TextureCubeFormat::RGBA16F;
		bool              Mipmapped = false;                    // allocate a mip chain (prefilter needs it)
	};

	class COSMIC_API TextureCube
	{
	public:
		virtual ~TextureCube() = default;

		virtual void     Bind(uint32_t slot = 0) const = 0;
		virtual uint32_t GetRendererID() const = 0;
		virtual uint32_t GetSize() const = 0;
		virtual uint32_t GetMipLevels() const = 0;

		/**
		 * Render-to-cube (IBL bake). Binds an internal framebuffer with `face`
		 * [0..5] at mip `mip` attached as color 0 and sets the GL viewport to that
		 * mip's pixel size. Issue draws afterward; call FinishRender() when the
		 * whole bake is done to restore the default framebuffer. The FBO is created
		 * lazily on first use (a sampled-only cube never allocates one).
		 */
		virtual void BeginRenderToFace(uint32_t face, uint32_t mip = 0) = 0;
		virtual void FinishRender() = 0;

		/** Generate the mip chain from mip 0 (call before sampling a mipmapped cube). */
		virtual void GenerateMips() = 0;

		static Ref<TextureCube> Create(const TextureCubeSpecification& spec);
	};
}
