#pragma once

// wo08_common.h — shared helpers for the WO-08 (2D stability) render cases R01–R07.
//
// Everything here is built from engine verbs only (no gl* / GL_* tokens — the
// conformance scanner covers tests/). The helpers are deliberately small and
// independent of the code under test:
//   * pixel probes read the Image the GoldenImage harness captured (top-left
//     origin RGBA8) — the oracle for every sentinel/ROI assertion;
//   * index<->colour encoding lets a grid of N primitives prove "nothing dropped,
//     nothing duplicated, nothing shifted" without a golden;
//   * a pixel-space ortho (1 world unit == 1 target pixel, bottom-left origin)
//     makes sentinel positions exact instead of approximate;
//   * FboTexture is the documented client-side adapter (docs/guide/game-ui.md)
//     that lets a UiImage show a FrameBuffer attachment (R05);
//   * StatsScope arms Renderer2D::Statistics — they default OFF, and an unarmed
//     counter reads zero and passes every assertion vacuously.

#include "GoldenImage.h"

#include "graphics/FrameBuffer.h"
#include "graphics/Texture.h"
#include "renderer/RenderCommand.h"
#include "renderer/Renderer2D.h"
#include "utils/ImageIO.h"

#include <doctest.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace Wo08
{
    using namespace Cosmic;
    using namespace CosmicRender;

    // A dark, deliberately non-black, non-grey clear so "nothing rendered" and
    // "rendered black" are both visible, and so index-coded sentinels (which
    // always carry blue >= 0x80) never collide with it.
    inline constexpr glm::vec4 kClear{ 0.07f, 0.09f, 0.13f, 1.0f };
    inline constexpr glm::u8vec4 kClearU8{ 18, 23, 33, 255 };

    // ------------------------------------------------------------------------
    // Targets + cameras
    // ------------------------------------------------------------------------

    inline Ref<FrameBuffer> MakeRgba8Target(uint32_t w, uint32_t h)
    {
        FramebufferSpecification spec;
        spec.Width       = w;
        spec.Height      = h;
        spec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::DEPTH24STENCIL8 };
        return FrameBuffer::Create(spec);
    }

    inline Ref<FrameBuffer> MakeHdrTarget(uint32_t w, uint32_t h)
    {
        FramebufferSpecification spec;
        spec.Width       = w;
        spec.Height      = h;
        spec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::DEPTH24STENCIL8 };
        return FrameBuffer::Create(spec);
    }

    // Bind + clear a target, set the viewport to cover it, and put the render
    // state where the engine's own 2D pass puts it (Scene::OnRenderSprites,
    // Scene.cpp:620-622): depth test ON, depth WRITE OFF, alpha blending. With
    // depth write on, a later primitive at the same z fails GL_LESS and painter
    // order is silently lost — exactly what the overlap sentinels must see.
    inline void BeginFrame(const Ref<FrameBuffer>& fbo, const glm::vec4& clear = kClear)
    {
        fbo->Bind();
        RenderCommand::SetViewport(0, 0, fbo->GetWidth(), fbo->GetHeight());
        RenderCommand::SetDepthTest(true);
        RenderCommand::SetDepthWrite(true);   // so Clear() clears depth too
        RenderCommand::SetClearColor(clear);
        RenderCommand::Clear();
        RenderCommand::SetDepthWrite(false);
        RenderCommand::SetBlendMode(RendererAPI::BlendMode::Alpha);
    }

    // Pixel-space camera: world (x, y) == target pixel (x, y) with a BOTTOM-LEFT
    // origin (GL's), so pixel (i, j) has its centre at world (i + 0.5, j + 0.5).
    // Captured images are top-left origin: image row = h - 1 - j.
    inline glm::mat4 PixelOrtho(uint32_t w, uint32_t h)
    {
        return glm::ortho(0.0f, (float)w, 0.0f, (float)h, -1.0f, 1.0f);
    }

    // The classic 2D camera: `halfHeight` world units above/below `center`,
    // aspect from the target.
    inline glm::mat4 Ortho2DFor(uint32_t w, uint32_t h, const glm::vec2& center, float halfHeight)
    {
        const float aspect = (float)w / (float)h;
        const float hw = halfHeight * aspect;
        const glm::mat4 proj = glm::ortho(-hw, hw, -halfHeight, halfHeight, -100.0f, 100.0f);
        const glm::mat4 view = glm::translate(glm::mat4(1.0f), { -center.x, -center.y, 0.0f });
        return proj * view;
    }

    // World -> image pixel (top-left origin) for Ortho2DFor(w, h, center, halfH).
    // Returns the CONTINUOUS pixel coordinate; callers floor() it to sample.
    inline glm::vec2 WorldToImagePx(uint32_t w, uint32_t h, const glm::vec2& center, float halfH,
                                    const glm::vec2& world)
    {
        const float aspect = (float)w / (float)h;
        const float nx = (world.x - center.x) / (halfH * aspect);   // -1..1
        const float ny = (world.y - center.y) / halfH;              // -1..1
        return { (nx + 1.0f) * 0.5f * (float)w, (1.0f - ny) * 0.5f * (float)h };
    }

    // ------------------------------------------------------------------------
    // Pixel probes (image space, top-left origin)
    // ------------------------------------------------------------------------

    inline glm::u8vec4 PixelAt(const Image& img, uint32_t x, uint32_t y)
    {
        REQUIRE(x < img.Width);
        REQUIRE(y < img.Height);
        const size_t i = ((size_t)y * img.Width + x) * 4;
        return { img.Rgba[i + 0], img.Rgba[i + 1], img.Rgba[i + 2], img.Rgba[i + 3] };
    }

    // Read the pixel whose GL (bottom-left) coordinates are (x, y).
    inline glm::u8vec4 PixelAtGl(const Image& img, uint32_t x, uint32_t yGl)
    {
        REQUIRE(yGl < img.Height);
        return PixelAt(img, x, img.Height - 1 - yGl);
    }

    inline int MaxChannelDelta(const glm::u8vec4& a, const glm::u8vec4& b, bool includeAlpha = true)
    {
        int d = 0;
        for (int c = 0; c < (includeAlpha ? 4 : 3); ++c)
            d = std::max(d, std::abs((int)a[c] - (int)b[c]));
        return d;
    }

    inline bool Near(const glm::u8vec4& a, const glm::u8vec4& b, int tol = 2, bool includeAlpha = true)
    {
        return MaxChannelDelta(a, b, includeAlpha) <= tol;
    }

    inline glm::u8vec4 ToU8(const glm::vec4& c)
    {
        auto q = [](float v) { return (uint8_t)std::lround(std::fmin(std::fmax(v, 0.0f), 1.0f) * 255.0f); };
        return { q(c.r), q(c.g), q(c.b), q(c.a) };
    }

    inline std::string Describe(const glm::u8vec4& c)
    {
        return "(" + std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b) + "," + std::to_string(c.a) + ")";
    }

    // Does the WxH region starting at (x, y) contain at least one pixel whose
    // RGB differs from `background` by more than `tol` on some channel?
    inline int CountInk(const Image& img, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        const glm::u8vec4& background, int tol = 8)
    {
        int ink = 0;
        for (uint32_t yy = y; yy < y + h && yy < img.Height; ++yy)
            for (uint32_t xx = x; xx < x + w && xx < img.Width; ++xx)
                if (MaxChannelDelta(PixelAt(img, xx, yy), background, false) > tol)
                    ++ink;
        return ink;
    }

    // Count the pixels in a region that are within `tol` of `color` (RGB).
    inline int CountColor(const Image& img, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                          const glm::u8vec4& color, int tol = 2)
    {
        int n = 0;
        for (uint32_t yy = y; yy < y + h && yy < img.Height; ++yy)
            for (uint32_t xx = x; xx < x + w && xx < img.Width; ++xx)
                if (Near(PixelAt(img, xx, yy), color, tol, false))
                    ++n;
        return n;
    }

    // ------------------------------------------------------------------------
    // Index <-> colour sentinels
    // ------------------------------------------------------------------------
    // r = low byte, g = next byte, b = 0x80 | high nibble. Every encoded colour
    // has b >= 0x80, so it can never be confused with the clear colour (b = 33).
    // 2^20 distinct indices is far beyond any batch limit under test.

    inline glm::vec4 EncodeIndex(uint32_t i)
    {
        REQUIRE(i < (1u << 20));
        const uint8_t r = (uint8_t)(i & 0xFF);
        const uint8_t g = (uint8_t)((i >> 8) & 0xFF);
        const uint8_t b = (uint8_t)(0x80 | ((i >> 16) & 0x0F));
        return { r / 255.0f, g / 255.0f, b / 255.0f, 1.0f };
    }

    inline glm::u8vec4 EncodeIndexU8(uint32_t i)
    {
        return ToU8(EncodeIndex(i));
    }

    // -1 when the pixel is not an encoded index (e.g. the clear colour).
    inline int64_t DecodeIndex(const glm::u8vec4& c)
    {
        if ((c.b & 0xF0) != 0x80)
            return -1;
        return (int64_t)c.r | ((int64_t)c.g << 8) | ((int64_t)(c.b & 0x0F) << 16);
    }

    // ------------------------------------------------------------------------
    // Procedural textures (F-2D)
    // ------------------------------------------------------------------------

    inline Ref<Texture2D> MakeFlatTexture(uint32_t w, uint32_t h, glm::u8vec4 c)
    {
        std::vector<uint8_t> px((size_t)w * h * 4);
        for (size_t i = 0; i < (size_t)w * h; ++i)
        {
            px[i * 4 + 0] = c.r; px[i * 4 + 1] = c.g; px[i * 4 + 2] = c.b; px[i * 4 + 3] = c.a;
        }
        Ref<Texture2D> t = Texture2D::Create(w, h);
        t->SetData(px.data(), (uint32_t)px.size());
        return t;
    }

    inline Ref<Texture2D> MakeChecker(uint32_t size, uint32_t cell, glm::u8vec4 a, glm::u8vec4 b)
    {
        std::vector<uint8_t> px((size_t)size * size * 4);
        for (uint32_t y = 0; y < size; ++y)
            for (uint32_t x = 0; x < size; ++x)
            {
                const bool odd = ((x / cell) + (y / cell)) % 2 == 1;
                const glm::u8vec4 c = odd ? b : a;
                const size_t i = ((size_t)y * size + x) * 4;
                px[i + 0] = c.r; px[i + 1] = c.g; px[i + 2] = c.b; px[i + 3] = c.a;
            }
        Ref<Texture2D> t = Texture2D::Create(size, size);
        t->SetData(px.data(), (uint32_t)px.size());
        return t;
    }

    // A 4x4 atlas of 16 flat, distinct colours, `tile` texels per tile, NO
    // borders (so a sub-texture quad samples one exact colour everywhere).
    inline const glm::u8vec4* AtlasPalette()
    {
        static const glm::u8vec4 palette[16] = {
            { 220,  70,  70, 255 }, {  70, 200,  90, 255 }, {  80, 120, 235, 255 }, { 235, 200,  60, 255 },
            { 200,  90, 210, 255 }, {  60, 205, 205, 255 }, { 240, 140,  50, 255 }, { 150, 150, 150, 255 },
            { 120,  40,  40, 255 }, {  40, 110,  55, 255 }, {  45,  70, 140, 255 }, { 140, 120,  40, 255 },
            { 110,  50, 120, 255 }, {  35, 120, 120, 255 }, { 140,  85,  30, 255 }, {  70,  70,  70, 255 },
        };
        return palette;
    }

    inline Ref<Texture2D> MakeAtlas4x4(uint32_t tile = 16)
    {
        const uint32_t size = tile * 4;
        std::vector<uint8_t> px((size_t)size * size * 4);
        for (uint32_t y = 0; y < size; ++y)
            for (uint32_t x = 0; x < size; ++x)
            {
                // Texel row 0 is the texture's BOTTOM row (GL); tile ids count
                // from the bottom-left so id == row * 4 + col in UV space.
                const uint32_t idx = (y / tile) * 4 + (x / tile);
                const glm::u8vec4 c = AtlasPalette()[idx];
                const size_t i = ((size_t)y * size + x) * 4;
                px[i + 0] = c.r; px[i + 1] = c.g; px[i + 2] = c.b; px[i + 3] = c.a;
            }
        Ref<Texture2D> t = Texture2D::Create(size, size);
        t->SetData(px.data(), (uint32_t)px.size());
        return t;
    }

    // ------------------------------------------------------------------------
    // FboTexture — the documented client-side adapter (docs/guide/game-ui.md)
    // ------------------------------------------------------------------------

    class FboTexture : public Texture2D
    {
    public:
        explicit FboTexture(const Ref<FrameBuffer>& fbo) : m_Fbo(fbo) {}

        uint32_t GetWidth()      const override { return m_Fbo->GetWidth(); }
        uint32_t GetHeight()     const override { return m_Fbo->GetHeight(); }
        uint64_t GetGpuBytes()   const override { return 0; }
        uint32_t GetRendererID() const override { return m_Fbo->GetColorAttachmentRendererID(0); }

        void Bind(uint32_t slot = 0) const override { RenderCommand::BindTextureSlot(slot, GetRendererID()); }

        void SetData(void*, uint32_t) override {}
        void SetSampling(TextureFilter, TextureWrap) override {}
        bool operator==(const Texture& o) const override { return GetRendererID() == o.GetRendererID(); }

    private:
        Ref<FrameBuffer> m_Fbo;
    };

    // ------------------------------------------------------------------------
    // Statistics
    // ------------------------------------------------------------------------

    struct StatsScope
    {
        StatsScope()  { Renderer2D::SetStatsStatus(true); Renderer2D::ResetStats(); }
        ~StatsScope() { Renderer2D::SetStatsStatus(false); }
        Renderer2D::Statistics Get() const { return Renderer2D::GetStats(); }
        void Reset() { Renderer2D::ResetStats(); }
    };

    // ------------------------------------------------------------------------
    // Evidence + fixtures
    // ------------------------------------------------------------------------

    // Where reviewable captures go (COSMIC_WO08_EVIDENCE_DIR, set by the runner
    // wrapper). Empty => captures are not written (plain local runs).
    inline std::string EvidenceDir()
    {
        if (const char* e = std::getenv("COSMIC_WO08_EVIDENCE_DIR"))
            return e[0] ? std::string(e) : std::string();
        return {};
    }

    inline void WriteEvidence(const std::string& name, const Image& img)
    {
        const std::string dir = EvidenceDir();
        if (dir.empty() || !img.Valid())
            return;
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(dir), ec);
        ImageIO::WritePNG((std::filesystem::path(dir) / (name + ".png")).string(),
                          (int)img.Width, (int)img.Height, 4, img.Rgba.data());
    }

    // Absolute path of tests/render/fixtures/wo08 (custom shaders), baked by CMake.
    inline std::string FixtureDir()
    {
#ifdef COSMIC_WO08_FIXTURE_DIR
        return COSMIC_WO08_FIXTURE_DIR;
#else
        return {};
#endif
    }

    inline std::string FixturePath(const char* name)
    {
        return (std::filesystem::path(FixtureDir()) / name).string();
    }

    // The pinned world-space text font: engine://fonts/Roboto-Regular.ttf, baked
    // at the default 64 px SDF atlas (assets/ is synced next to the exe and the
    // harness chdirs there).
    inline const char* kPinnedFontPath = "assets/fonts/Roboto-Regular.ttf";
    inline constexpr int kPinnedFontAtlasPx = 64;
}
