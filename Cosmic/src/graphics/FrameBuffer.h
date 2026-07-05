#pragma once

// FrameBuffer.h
// Last Modified 5/14/2026

/**
 * General Description:
 * 
 * FrameBuffer.h defines the abstract interface for off-screen rendering targets.
 * In the Cosmic Engine, a FrameBuffer (FBO) allows the Renderer to redirect its
 * output into a GPU-resident texture rather than the system's back buffer.
 * 
 * This is the cornerstone of the engine's Editor/Sandbox architecture, enabling
 * the game scene to be rendered into an ImGui window, facilitating post-processing,
 * and allowing for dynamic viewport scaling independent of the native window resolution.
 * 
 * 
 * Architecture Components:
 * 
 * 1. FramebufferSpecification: A configuration structure defining the physical
 * properties of the buffer, including dimensions, multi-sampling (MSAA) levels,
 * and whether it targets the application's swap chain.
 * 
 * 2. FrameBuffer (Base): The abstract interface providing methods to Bind/Unbind
 * the target, handle dynamic resizing, and retrieve the underlying texture IDs
 * for UI consumption.
 * 
 * 
 * Public Function Prototypes (Pre and Post Conditions):
 * 
 * 1. virtual void Bind()
 * Pre:  The FrameBuffer has been successfully initialized on the GPU.
 * Post: All subsequent draw calls are directed to this FrameBuffer's attachments.
 * 
 * 2. virtual void Unbind()
 * Pre:  None.
 * Post: Rendering target is reset to the default system framebuffer (Screen).
 * 
 * 3. virtual void Resize(uint32_t width, uint32_t height)
 * Pre:  Width and height are greater than zero.
 * Post: The internal GPU textures are destroyed and re-allocated at the new
 * dimensions to prevent sampling distortion.
 * 
 * 4. virtual uint32_t GetColorAttachmentRendererID()
 * Pre:  None.
 * Post: Returns the API-specific handle (e.g., OpenGL ID) for the texture
 * representing the color buffer. Used for ImGui::Image calls.
 * 
 * 5. static Ref<FrameBuffer> Create(const FramebufferSpecification& spec)
 * Pre:  A valid specification is provided.
 * Post: Returns a reference-counted, platform-specific FrameBuffer instance
 * based on the active RendererAPI.
 */

#include "core/Core.h"
#include <vector>
#include <cstdint>
#include <initializer_list>

namespace Cosmic
{
	/**
	 * FramebufferTextureFormat — engine-side attachment formats (no GL enums in
	 * public headers; the platform layer translates). RED_INTEGER backs entity-ID
	 * picking (S4.6); RGBA16F is the HDR target (S6). DEPTH24STENCIL8 is the depth
	 * attachment. (§0 rule 2 — portability.)
	 */
	enum class FramebufferTextureFormat
	{
		None = 0,
		RGBA8,
		RGBA16F,
		RED_INTEGER,
		DEPTH24STENCIL8
	};

	struct FramebufferTextureSpecification
	{
		FramebufferTextureSpecification() = default;
		// Implicit on purpose so an attachment list can be written as {RGBA8, RED_INTEGER}.
		FramebufferTextureSpecification(FramebufferTextureFormat format) : TextureFormat(format) {}

		FramebufferTextureFormat TextureFormat = FramebufferTextureFormat::None;
	};

	struct FramebufferAttachmentSpecification
	{
		FramebufferAttachmentSpecification() = default;
		FramebufferAttachmentSpecification(std::initializer_list<FramebufferTextureSpecification> attachments)
			: Attachments(attachments) {}

		std::vector<FramebufferTextureSpecification> Attachments;
	};

	/**
	 * FramebufferSpecification
	 * Configuration data for creating a new FrameBuffer.
	 *
	 * Attachments EMPTY ⇒ the default {RGBA8, DEPTH24STENCIL8} — so every existing
	 * call site (workspace FBO, FPV inset) keeps byte-for-byte identical behavior.
	 */
	struct FramebufferSpecification
	{
		uint32_t Width = 0, Height = 0;
		uint32_t Samples = 1;             // Reserved — MSAA not yet implemented; always renders single-sample
		bool SwapChainTarget = false;     // Reserved — not yet implemented
		FramebufferAttachmentSpecification Attachments;   // empty ⇒ {RGBA8, DEPTH24STENCIL8}
	};

	///////////////////////////////////////////////
	///////////////////////////////////////////////

	class COSMIC_API FrameBuffer
	{
	public:
		////////////////////////////////
		// Destructor
		///////////////////////////////
		virtual ~FrameBuffer() = default;

		////////////////////////////////
		// Pipeline State
		///////////////////////////////
		virtual void Bind() = 0;
		virtual void Unbind() = 0;

		////////////////////////////////
		// Dynamic Transformation
		///////////////////////////////
		virtual void Resize(uint32_t width, uint32_t height) = 0;

		////////////////////////////////
		// Metadata Accessors
		///////////////////////////////
		virtual uint32_t GetWidth() const = 0;
		virtual uint32_t GetHeight() const = 0;
		virtual const FramebufferSpecification& GetSpecification() const = 0;

		////////////////////////////////
		// GPU Resource Accessors
		///////////////////////////////

		// Color attachment texture handle. `index` selects the attachment for MRT
		// FBOs; the default 0 keeps every single-attachment ImGui::Image caller
		// source-compatible.
		virtual uint32_t GetColorAttachmentRendererID(uint32_t index = 0) const = 0;

		// Depth/stencil attachment handle (DEPTH24_STENCIL8 texture). Exposed for 3D
		// work: depth read-back, debug visualization, and future post-processing.
		virtual uint32_t GetDepthAttachmentRendererID() const = 0;

		////////////////////////////////
		// Integer Attachment I/O (S4.6 — entity-ID picking)
		///////////////////////////////

		// Read one integer texel from a RED_INTEGER attachment. The FBO must be
		// bound. GL's origin is bottom-left, so the CALLER flips y
		// (glY = height - 1 - mouseY).
		virtual int ReadPixel(uint32_t attachmentIndex, int x, int y) = 0;

		// Clear a RED_INTEGER attachment to `value` (glClearBufferiv). glClear does
		// NOT reliably clear integer attachments, so callers clear the ID attachment
		// every frame after Bind(). The FBO must be bound.
		virtual void ClearAttachment(uint32_t attachmentIndex, int value) = 0;

		////////////////////////////////
		// Depth Read-back (S5.1 — cursor-pivot navigation)
		///////////////////////////////

		// Read one window-space depth texel in [0, 1] from the depth attachment.
		// The FBO must be bound. GL's origin is bottom-left, so the CALLER flips y
		// (glY = height - 1 - mouseY), same convention as ReadPixel. Returns 1.0
		// (the far plane — "nothing was drawn here") when there is no depth
		// attachment or the read misses geometry. Used to reconstruct the world
		// point under the cursor for orbit-about-cursor / zoom-to-cursor.
		virtual float ReadDepth(int x, int y) = 0;

		////////////////////////////////
		// Full-image Read-back (S7 — thumbnail / screenshot capture)
		///////////////////////////////

		// Read a whole color attachment back into 8-bit RGBA, ROW-MAJOR with a
		// TOP-LEFT origin (GL's bottom-left rows are flipped for you, so the buffer
		// is ready for stb_image_write). HDR (RGBA16F) attachments are converted +
		// clamped to 8-bit. The FBO must be bound. Returns false (and leaves outputs
		// untouched) when the attachment index is out of range.
		virtual bool ReadPixels(uint32_t attachmentIndex, std::vector<uint8_t>& outRGBA,
		                        uint32_t& outWidth, uint32_t& outHeight) = 0;

		////////////////////////////////
		// Factory Pattern
		///////////////////////////////
		static Ref<FrameBuffer> Create(const FramebufferSpecification& spec);
	};
}