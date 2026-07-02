// Font.cpp — see Font.h.
//
// windows.h (via the PCH) defines APIENTRY; undef it so glad can define its own
// cleanly, exactly like OpenGLTexture.cpp does.
#ifdef APIENTRY
	#undef APIENTRY
#endif
#include <glad/glad.h>

#include "graphics/Font.h"
#include "utils/FileSystem.h"
#include "core/Log.h"

#include <stb_truetype.h>
#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace Cosmic
{
	namespace
	{
		namespace fs = std::filesystem;

		// SDF bake parameters. padding = SDF spread in pixels around each glyph.
		constexpr int           k_Padding   = 5;
		constexpr unsigned char k_OnEdge    = 128;
		constexpr int           k_AtlasW    = 1024;
		constexpr uint32_t      k_CacheMagic = 0x544E4643; // 'CFNT' little-endian
		constexpr uint32_t      k_CacheVer   = 1;

		std::string Stem(const std::string& path)
		{
			return fs::path(path).stem().string();
		}

		std::string CacheBase(const std::string& sourcePath, int px)
		{
			return (fs::path("assets") / "cache" / "fonts"
				/ (Stem(sourcePath) + "_" + std::to_string(px))).generic_string();
		}

		bool ReadBinaryFile(const std::string& path, std::vector<unsigned char>& out)
		{
			std::ifstream f(path, std::ios::binary | std::ios::ate);
			if (!f) return false;
			const std::streamsize n = f.tellg();
			if (n <= 0) return false;
			out.resize((size_t)n);
			f.seekg(0);
			f.read(reinterpret_cast<char*>(out.data()), n);
			return (bool)f;
		}

		// Apply the SDF-friendly sampling state to a freshly created atlas texture.
		void SetAtlasFiltering(uint32_t rendererID)
		{
			glBindTexture(GL_TEXTURE_2D, rendererID);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glBindTexture(GL_TEXTURE_2D, 0);
		}

		// One glyph during baking.
		struct Baked
		{
			uint32_t       cp;
			unsigned char* sdf;
			int w, h, xoff, yoff, advance;
			int px, py;
		};
	}

	// =========================================================================
	// Glyph lookup
	// =========================================================================
	const Glyph* Font::GetGlyph(uint32_t codepoint) const
	{
		auto it = m_Glyphs.find(codepoint);
		return it != m_Glyphs.end() ? &it->second : nullptr;
	}

	// =========================================================================
	// Bake from TTF/OTF (+ write cache)
	// =========================================================================
	bool Font::BakeFromTTF(const std::string& path, int px)
	{
		if (px < 8) px = 8;

		std::vector<unsigned char> ttf;
		if (!ReadBinaryFile(path, ttf))
		{
			CS_CORE_ERROR("Font: cannot read '{0}'", path);
			return false;
		}

		stbtt_fontinfo info;
		const int offset = stbtt_GetFontOffsetForIndex(ttf.data(), 0);
		if (offset < 0 || !stbtt_InitFont(&info, ttf.data(), offset))
		{
			CS_CORE_ERROR("Font: invalid font file '{0}'", path);
			return false;
		}

		const float scale = stbtt_ScaleForPixelHeight(&info, (float)px);
		int ascent, descent, lineGap;
		stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
		m_Ascent     = ascent  * scale / px;
		m_Descent    = descent * scale / px;
		m_LineHeight = (ascent - descent + lineGap) * scale / px;

		const float pixelDistScale = (float)k_OnEdge / (float)k_Padding;

		// --- Rasterise each printable ASCII glyph to its own SDF bitmap ---
		std::vector<Baked> baked;
		baked.reserve(95);
		for (uint32_t cp = 32; cp < 127; ++cp)
		{
			int adv = 0, lsb = 0;
			stbtt_GetCodepointHMetrics(&info, (int)cp, &adv, &lsb);

			int w = 0, h = 0, xoff = 0, yoff = 0;
			unsigned char* sdf = stbtt_GetCodepointSDF(&info, scale, (int)cp,
				k_Padding, k_OnEdge, pixelDistScale, &w, &h, &xoff, &yoff);
			baked.push_back({ cp, sdf, w, h, xoff, yoff, adv, 0, 0 });
		}

		// --- Shelf-pack the bitmaps into a fixed-width atlas ---
		int penX = k_Padding, penY = k_Padding, rowH = 0;
		for (auto& b : baked)
		{
			if (!b.sdf || b.w <= 0 || b.h <= 0) continue;
			if (penX + b.w + k_Padding > k_AtlasW) { penX = k_Padding; penY += rowH + k_Padding; rowH = 0; }
			b.px = penX; b.py = penY;
			penX += b.w + k_Padding;
			rowH = std::max(rowH, b.h);
		}
		const int atlasW = k_AtlasW;
		int atlasH = penY + rowH + k_Padding;
		if (atlasH < 16) atlasH = 16;

		// --- Blit into a single-channel atlas ---
		std::vector<unsigned char> gray((size_t)atlasW * atlasH, 0);
		for (auto& b : baked)
		{
			if (!b.sdf || b.w <= 0 || b.h <= 0) continue;
			for (int row = 0; row < b.h; ++row)
				for (int col = 0; col < b.w; ++col)
					gray[(size_t)(b.py + row) * atlasW + (b.px + col)] = b.sdf[row * b.w + col];
		}

		// --- Build glyph metrics (em units) and free per-glyph SDF buffers ---
		m_Glyphs.clear();
		for (auto& b : baked)
		{
			Glyph g;
			g.advance = b.advance * scale / (float)px;
			if (b.sdf && b.w > 0 && b.h > 0)
			{
				g.uv0    = { (float)b.px / atlasW,          (float)b.py / atlasH };
				g.uv1    = { (float)(b.px + b.w) / atlasW,  (float)(b.py + b.h) / atlasH };
				g.size   = { (float)b.w / px,               (float)b.h / px };
				g.offset = { (float)b.xoff / px,            (float)(-b.yoff) / px };
			}
			m_Glyphs[b.cp] = g;
			if (b.sdf) stbtt_FreeSDF(b.sdf, nullptr);
		}

		// --- Expand to RGBA and upload ---
		std::vector<unsigned char> rgba((size_t)atlasW * atlasH * 4);
		for (size_t i = 0; i < gray.size(); ++i)
		{
			const unsigned char v = gray[i];
			rgba[i * 4 + 0] = v; rgba[i * 4 + 1] = v; rgba[i * 4 + 2] = v; rgba[i * 4 + 3] = v;
		}

		m_Atlas = Texture2D::Create((uint32_t)atlasW, (uint32_t)atlasH);
		if (!m_Atlas)
		{
			CS_CORE_ERROR("Font: failed to allocate atlas texture for '{0}'", path);
			return false;
		}
		m_Atlas->SetData(rgba.data(), (uint32_t)rgba.size());
		SetAtlasFiltering(m_Atlas->GetRendererID());

		// --- Write disk cache (atlas PNG + metrics) ---
		const std::string base = CacheBase(path, px);
		std::error_code ec;
		fs::create_directories(fs::path(base).parent_path(), ec);

		stbi_write_png((base + ".png").c_str(), atlasW, atlasH, 4, rgba.data(), atlasW * 4);

		std::ofstream meta(base + ".csmfont", std::ios::binary);
		if (meta)
		{
			auto w32 = [&](uint32_t v) { meta.write(reinterpret_cast<const char*>(&v), 4); };
			auto wf  = [&](float v)    { meta.write(reinterpret_cast<const char*>(&v), 4); };
			w32(k_CacheMagic); w32(k_CacheVer);
			w32((uint32_t)atlasW); w32((uint32_t)atlasH);
			wf(m_LineHeight); wf(m_Ascent); wf(m_Descent);
			w32((uint32_t)m_Glyphs.size());
			for (const auto& [cp, g] : m_Glyphs)
			{
				w32(cp);
				wf(g.uv0.x); wf(g.uv0.y); wf(g.uv1.x); wf(g.uv1.y);
				wf(g.size.x); wf(g.size.y); wf(g.offset.x); wf(g.offset.y);
				wf(g.advance);
			}
		}

		CS_CORE_INFO("Font: baked '{0}' ({1}x{2}, {3} glyphs)", m_Name, atlasW, atlasH, (int)m_Glyphs.size());
		return true;
	}

	// =========================================================================
	// Load from disk cache
	// =========================================================================
	bool Font::LoadFromCache(const std::string& sourcePath, int px)
	{
		const std::string base = CacheBase(sourcePath, px);
		const std::string pngPath  = base + ".png";
		const std::string metaPath = base + ".csmfont";

		std::error_code ec;
		if (!fs::exists(pngPath, ec) || !fs::exists(metaPath, ec))
			return false;

		// Re-bake if the source font is newer than the cache.
		if (fs::exists(sourcePath, ec))
		{
			const auto src = fs::last_write_time(sourcePath, ec);
			const auto cac = fs::last_write_time(pngPath, ec);
			if (!ec && src > cac) return false;
		}

		// --- Read metrics ---
		std::ifstream meta(metaPath, std::ios::binary);
		if (!meta) return false;

		auto r32 = [&](uint32_t& v) { meta.read(reinterpret_cast<char*>(&v), 4); };
		auto rf  = [&](float& v)    { meta.read(reinterpret_cast<char*>(&v), 4); };

		uint32_t magic = 0, ver = 0, aw = 0, ah = 0, count = 0;
		r32(magic); r32(ver);
		if (magic != k_CacheMagic || ver != k_CacheVer) return false;
		r32(aw); r32(ah);
		rf(m_LineHeight); rf(m_Ascent); rf(m_Descent);
		r32(count);

		std::unordered_map<uint32_t, Glyph> glyphs;
		glyphs.reserve(count);
		for (uint32_t i = 0; i < count; ++i)
		{
			uint32_t cp = 0; Glyph g;
			r32(cp);
			rf(g.uv0.x); rf(g.uv0.y); rf(g.uv1.x); rf(g.uv1.y);
			rf(g.size.x); rf(g.size.y); rf(g.offset.x); rf(g.offset.y);
			rf(g.advance);
			glyphs[cp] = g;
		}
		if (!meta) return false;

		// --- Load atlas PNG (no vertical flip — bake wrote it top-row-first) ---
		stbi_set_flip_vertically_on_load(0);
		int w = 0, h = 0, ch = 0;
		stbi_uc* pixels = stbi_load(pngPath.c_str(), &w, &h, &ch, 4);
		if (!pixels) return false;

		m_Atlas = Texture2D::Create((uint32_t)w, (uint32_t)h);
		if (!m_Atlas) { stbi_image_free(pixels); return false; }
		m_Atlas->SetData(pixels, (uint32_t)(w * h * 4));
		SetAtlasFiltering(m_Atlas->GetRendererID());
		stbi_image_free(pixels);

		m_Glyphs = std::move(glyphs);
		CS_CORE_INFO("Font: loaded '{0}' from cache ({1}x{2}, {3} glyphs)", m_Name, w, h, (int)m_Glyphs.size());
		return true;
	}

	// =========================================================================
	// Factory
	// =========================================================================
	Ref<Font> Font::Create(const std::string& path, int atlasPixelSize)
	{
		auto font = CreateRef<Font>();
		font->m_Name = Stem(path);

		if (font->LoadFromCache(path, atlasPixelSize))
			return font;
		if (font->BakeFromTTF(path, atlasPixelSize))
			return font;

		CS_CORE_ERROR("Font: failed to create font from '{0}'", path);
		return nullptr;
	}

	// =========================================================================
	// Library
	// =========================================================================
	namespace
	{
		std::unordered_map<std::string, Ref<Font>> s_Library;
		Ref<Font> s_Default;
		bool      s_LibraryInit = false;

		void LoadFolderInternal(const std::string& resolvedDir)
		{
			std::error_code ec;
			if (!fs::exists(resolvedDir, ec) || !fs::is_directory(resolvedDir, ec))
				return;

			for (const auto& de : fs::directory_iterator(resolvedDir, ec))
			{
				if (ec || !de.is_regular_file()) continue;
				std::string ext = de.path().extension().string();
				std::transform(ext.begin(), ext.end(), ext.begin(),
					[](unsigned char c) { return (char)std::tolower(c); });
				if (ext != ".ttf" && ext != ".otf") continue;

				const std::string name = de.path().stem().string();
				if (s_Library.count(name)) continue;

				if (auto f = Font::Create(de.path().string()))
					s_Library[name] = f;
			}
		}

		void EnsureLibrary()
		{
			if (s_LibraryInit) return;
			s_LibraryInit = true;

			LoadFolderInternal(FileSystem::Resolve("engine://fonts"));
			LoadFolderInternal(FileSystem::Resolve("project://fonts"));

			auto it = s_Library.find("Roboto-Regular");
			if (it != s_Library.end())      s_Default = it->second;
			else if (!s_Library.empty())    s_Default = s_Library.begin()->second;

			CS_CORE_INFO("Font: library initialised ({0} font(s))", (int)s_Library.size());
		}
	}

	Ref<Font> Font::Get(const std::string& name)
	{
		EnsureLibrary();
		auto it = s_Library.find(name);
		return it != s_Library.end() ? it->second : nullptr;
	}

	void Font::LoadProjectFonts()
	{
		// Project-mount rescan (Application::LoadProjectDLL). Only relevant when
		// the lazy EnsureLibrary already ran BEFORE the project was mounted (its
		// project:// scan resolved against no project and found nothing). If the
		// library hasn't initialised yet, stay lazy: the eventual first-use scan
		// now runs with the engine-side active project set, so it sees the
		// project faces itself — and we avoid eagerly SDF-baking fonts for apps
		// that never draw world-space text. LoadFolderInternal skips stems
		// already in the library, so this is idempotent. Baking creates GL
		// textures — the engine calls this on the main thread, context current.
		if (!s_LibraryInit)
			return;

		LoadFolderInternal(FileSystem::Resolve("project://fonts"));

		// The pre-mount init may have found no faces at all (empty engine
		// folder) — give the default another chance now that project faces exist.
		if (!s_Default && !s_Library.empty())
		{
			auto it = s_Library.find("Roboto-Regular");
			s_Default = (it != s_Library.end()) ? it->second : s_Library.begin()->second;
		}
	}

	Ref<Font> Font::Default()
	{
		EnsureLibrary();
		return s_Default;
	}
}
