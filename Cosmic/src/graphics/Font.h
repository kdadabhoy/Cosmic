#pragma once

// Font.h
// ============================================================================
// Cosmic::Font — a world-space text font (SDF glyph atlas).
// ============================================================================
//
// A Font bakes a TTF/OTF into a single-channel **signed distance field (SDF)**
// glyph atlas (via stb_truetype) so text drawn with Renderer2D::DrawString stays
// crisp at any camera zoom. The bake is cached to disk (atlas PNG + a small
// metrics file) and reused on subsequent runs — drop a font in the fonts folder
// and it "just works", baking once.
//
// This is the WORLD-SPACE text path. For ImGui panel/overlay text, use
// Cosmic::UI::Fonts (which feeds the same TTFs into ImGui's own atlas).
//
// Glyph metrics are stored in EM units (1 em = the font size), so a single
// Font renders at any size: the caller scales the transform by the desired
// world height.
// ============================================================================

#include "core/Core.h"
#include "graphics/Texture.h"

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

namespace Cosmic
{
	// One glyph's placement in the atlas and layout metrics (EM units).
	struct Glyph
	{
		glm::vec2 uv0{ 0.0f };    // atlas UV of the glyph's top-left
		glm::vec2 uv1{ 0.0f };    // atlas UV of the glyph's bottom-right
		glm::vec2 size{ 0.0f };   // quad size in em (1 em = font size)
		glm::vec2 offset{ 0.0f }; // baseline -> glyph top-left, em (offset.y up = positive)
		float     advance = 0.0f; // horizontal pen advance, em
	};

	class COSMIC_API Font
	{
	public:
		// Bake (or load from cache) a font from a TTF/OTF on disk. `atlasPixelSize`
		// is the rasterisation height of the SDF atlas (quality vs. memory). Returns
		// nullptr on failure.
		static Ref<Font> Create(const std::string& path, int atlasPixelSize = 64);

		// Library lookup by file stem (e.g. "Roboto-Bold"). Scans engine://fonts and
		// project://fonts on first use. Returns nullptr if the name isn't found.
		static Ref<Font> Get(const std::string& name);
		static Ref<Font> Default();

		// Rescan project://fonts for the mounted project. Called by the engine from
		// Application::LoadProjectDLL — the lazy first-use scan may have run before
		// any project was mounted (e.g. from the Launcher), missing project faces.
		// Idempotent (stems already in the library are kept, first wins).
		static void LoadProjectFonts();

		// ---- Per-font data ----
		const Ref<Texture2D>& GetAtlas() const { return m_Atlas; }
		const Glyph*          GetGlyph(uint32_t codepoint) const;
		float                 LineHeight() const { return m_LineHeight; }
		float                 Ascent()     const { return m_Ascent; }
		float                 Descent()    const { return m_Descent; }
		const std::string&    Name()       const { return m_Name; }
		bool                  IsValid()    const { return m_Atlas != nullptr; }

	private:
		// Bake from a TTF/OTF (rasterise SDF, upload atlas, write the disk cache).
		bool BakeFromTTF(const std::string& path, int atlasPixelSize);
		// Fast path: load a previously baked atlas + metrics if present and fresh.
		bool LoadFromCache(const std::string& sourcePath, int atlasPixelSize);

	private:
		Ref<Texture2D>                      m_Atlas;
		std::unordered_map<uint32_t, Glyph> m_Glyphs;
		float                               m_LineHeight = 1.0f;
		float                               m_Ascent     = 0.8f;
		float                               m_Descent    = -0.2f;
		std::string                         m_Name;
	};
}
