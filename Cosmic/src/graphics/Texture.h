#pragma once

// Texture.h
// Last Modified 5/14/2026

/**
 * General Description:
 * 
 * Texture.h defines the abstract interface for GPU-resident image data. In the
 * Cosmic Engine, a Texture represents a spatial map of data (usually RGBA color)
 * that can be sampled by a Shader during the rendering process.
 * * The system supports both loading image files from disk (PNG, JPG) and
 * creating "procedural" textures in memory. It provides a unified binding
 * mechanism to support the multi-slot texture system used in Batch Rendering.
 * 
 * 
 * Architecture Components:
 * 
 * 1. Texture (Base): The primary interface for all texture types. It defines
 * core functionality like binding, data uploading, and equality comparison.
 * 
 * 2. Texture2D (Derived): A specialization for two-dimensional images. This is
 * the standard format for sprites, backgrounds, and UI elements.
 * 
 * 3. Texture Slots: The 'Bind' function takes a 'slot' parameter, allowing
 * the GPU to have multiple textures active simultaneously for a single draw call.
 * 
 * 
 * Public Function Prototypes (Pre and Post Conditions):
 * 
 * 
 * 1. virtual uint32_t GetWidth() / GetHeight()
 * Pre:  The texture has been successfully created.
 * Post: Returns the dimensions of the image in pixels.
 * 
 * 2. virtual void SetData(void* data, uint32_t size)
 * Pre:  The provided data size must match the texture's internal format
 * (Width * Height * BytesPerPixel).
 * Post: Replaces the current texture data on the GPU with the provided buffer.
 * 
 * 3. virtual void Bind(uint32_t slot = 0)
 * Pre:  None.
 * Post: Activates the texture in the specified hardware texture slot.
 * Shaders use this slot index to sample the correct image.
 * 
 * 4. virtual bool operator==(const Texture& other)
 * Pre:  None.
 * Post: Returns true if both texture objects refer to the same
 * underlying GPU resource ID.
 * 
 * 5. static Ref<Texture2D> Create(uint32_t width, uint32_t height)
 * Pre:  Width and height are greater than zero.
 * Post: Returns an empty (procedural) texture that can be filled via SetData().
 * 
 * 6. static Ref<Texture2D> Create(const std::string& path)
 * Pre:  A valid file path is provided.
 * Post: Returns a reference-counted Texture2D loaded from the disk.
 */

#include "core/Core.h"
#include <string>

namespace Cosmic
{
	// Engine-side sampling state enums (translated to API values in the platform
	// layer — no GL tokens in public headers, doc 05 §0 rule 2).
	enum class TextureFilter { Nearest = 0, Linear };
	enum class TextureWrap   { Repeat  = 0, ClampToEdge };

	class COSMIC_API Texture
	{
	public:
		////////////////////////////////
		// Destructor
		///////////////////////////////
		virtual ~Texture() = default;

		////////////////////////////////
		// Metadata Accessors
		///////////////////////////////
		virtual uint32_t		GetWidth() const	= 0;
		virtual uint32_t		GetHeight() const	= 0;

		////////////////////////////////
		// GPU Data Operations
		///////////////////////////////
		virtual void		SetData(void* data, uint32_t size)	= 0;
		virtual void		Bind(uint32_t slot = 0) const		= 0;

		/**
		 * @brief Overrides the texture's min/mag filtering and wrap mode.
		 * Factories create textures with their own defaults (file textures:
		 * mipmapped linear; procedural: nearest) — call this when a use case
		 * needs different sampling (e.g. the SDF font atlas wants Linear +
		 * ClampToEdge).
		 */
		virtual void		SetSampling(TextureFilter filter, TextureWrap wrap) = 0;

		////////////////////////////////
		// Utility & Identification
		///////////////////////////////
		virtual bool			operator==(const Texture& other) const		= 0;
		virtual uint32_t		GetRendererID() const						= 0;

		////////////////////////////////
		// Factory Pattern (Generic)
		///////////////////////////////
		static Ref<Texture>		Create(const std::string& path);
	};

	///////////////////////////////////////////////
	///////////////////////////////////////////////
	class COSMIC_API Texture2D : public Texture
	{
	public:
		////////////////////////////////
		// Factory Pattern (Specialized)
		///////////////////////////////
		static Ref<Texture2D>		Create(uint32_t width, uint32_t height);
		static Ref<Texture2D>		Create(const std::string& path);

		/**
		 * @brief Decode an ENCODED image (PNG/JPG bytes) already in memory.
		 * Pre:  `data` points to `size` bytes of a compressed image file.
		 * Post: Returns an uploaded, mipmapped Texture2D, or nullptr on decode
		 *       failure. Used for glTF/.glb embedded images (S6.2) — no temp file.
		 */
		static Ref<Texture2D>		Create(const uint8_t* data, uint32_t size);
	};
}