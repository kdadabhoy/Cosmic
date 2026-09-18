// render_wo08_primitives.cpp — WO-08 R01: every Renderer2D primitive + SDF text.
//
// Two reviewed goldens (320x180, the harness baseline: 2/255 channel tolerance,
// 0.1% pixel budget) PLUS exact sentinel pixels / ROIs, because a whole-frame
// tolerance can hide one absent glyph or one wrong tile. The fixture is F-2D:
// procedural textures, the pinned Roboto-Regular SDF font, overlapping colour
// sentinels, and a scene whose draw order deliberately crosses the documented
// per-flush primitive grouping (quads -> lines -> circles -> text).
//
//   wo08_primitives   flat / alpha / rotated / textured / tiled / atlas quads,
//                     SDF disc + ring + ellipse, dashed lines + rect, a Material
//                     quad (its own batch), and the grouping sentinel.
//   wo08_text         SDF text at two sizes, a multi-line string, kerning, and
//                     the '?' fallback for bytes outside the baked ASCII range.

#include "wo08_common.h"

#include "graphics/Font.h"
#include "graphics/Material.h"
#include "graphics/Shader.h"
#include "graphics/SubTexture2D.h"

#include <string>

using namespace Wo08;

namespace
{
    constexpr uint32_t kW = kGoldenWidth;    // 320
    constexpr uint32_t kH = kGoldenHeight;   // 180
    constexpr float    kHalfH = 5.0f;        // world half-height => 18 px per unit
    const glm::vec2    kCenter{ 0.0f, 0.0f };

    // World -> the image pixel to sample (floor of the continuous coordinate).
    glm::uvec2 Px(const glm::vec2& world)
    {
        const glm::vec2 p = WorldToImagePx(kW, kH, kCenter, kHalfH, world);
        return { (uint32_t)std::floor(p.x), (uint32_t)std::floor(p.y) };
    }

    glm::u8vec4 Sample(const Image& img, const glm::vec2& world)
    {
        const glm::uvec2 p = Px(world);
        return PixelAt(img, p.x, p.y);
    }

    // Alpha-over, the engine's default blend: src*a + dst*(1-a) on RGB, and the
    // same equation on alpha (GL applies the blend func to all four channels).
    glm::u8vec4 Over(const glm::vec4& src, const glm::u8vec4& dst)
    {
        const glm::vec4 d{ dst.r / 255.0f, dst.g / 255.0f, dst.b / 255.0f, dst.a / 255.0f };
        const glm::vec4 out = src * src.a + d * (1.0f - src.a);
        return ToU8(out);
    }

    // Independent glyph-box layout (the same em-unit pen model DrawString
    // documents), so each glyph's ROI is known without asking the renderer.
    struct GlyphBox { unsigned char c; glm::vec2 min, max; bool visible; };

    std::vector<GlyphBox> LayoutBoxes(const std::string& text, const Font& font,
                                      const glm::vec2& position, float size, float kerning = 0.0f)
    {
        std::vector<GlyphBox> out;
        float x = 0.0f, y = 0.0f;
        for (unsigned char c : text)
        {
            if (c == '\n') { x = 0.0f; y -= font.LineHeight(); continue; }
            if (c == '\r') continue;
            const Glyph* g = font.GetGlyph(c);
            if (!g) g = font.GetGlyph('?');
            REQUIRE(g != nullptr);
            GlyphBox b;
            b.c = c;
            b.visible = g->size.x > 0.0f && g->size.y > 0.0f;
            const float x0 = x + g->offset.x, y0 = y + g->offset.y;
            b.min = position + glm::vec2(x0, y0 - g->size.y) * size;
            b.max = position + glm::vec2(x0 + g->size.x, y0) * size;
            out.push_back(b);
            x += g->advance + kerning;
        }
        return out;
    }

    // Image ROI (top-left origin) for a world box.
    struct Roi { uint32_t x, y, w, h; };
    Roi RoiFor(const glm::vec2& wmin, const glm::vec2& wmax)
    {
        const glm::vec2 a = WorldToImagePx(kW, kH, kCenter, kHalfH, { wmin.x, wmax.y });   // top-left
        const glm::vec2 b = WorldToImagePx(kW, kH, kCenter, kHalfH, { wmax.x, wmin.y });   // bottom-right
        const uint32_t x0 = (uint32_t)std::floor(std::max(a.x, 0.0f));
        const uint32_t y0 = (uint32_t)std::floor(std::max(a.y, 0.0f));
        const uint32_t x1 = (uint32_t)std::ceil(std::min(b.x, (float)kW));
        const uint32_t y1 = (uint32_t)std::ceil(std::min(b.y, (float)kH));
        return { x0, y0, x1 > x0 ? x1 - x0 : 0u, y1 > y0 ? y1 - y0 : 0u };
    }
}

TEST_SUITE("WO-08 R01")
{
    TEST_CASE("R01 primitives — every quad/SDF/line path, alpha, UVs, per-flush grouping (golden + sentinels)")
    {
        Ref<Texture2D> checker = MakeChecker(32, 8, { 235, 235, 235, 255 }, { 60, 70, 90, 255 });
        Ref<Texture2D> atlas   = MakeAtlas4x4(16);
        // Atlas tile (col 2, row 1) == id 6 in the bottom-left-origin numbering.
        Ref<SubTexture2D> tile6 = SubTexture2D::CreateFromCoords(atlas, { 2.0f, 1.0f }, { 16.0f, 16.0f }, { 1.0f, 1.0f });

        // A Material on the engine's own batch shader, coloured through u_Color
        // (what DrawQuad(material) bakes into the vertex colour).
        Ref<Shader>   texShader = Shader::Create("assets/shaders/Texture.glsl");
        REQUIRE(texShader != nullptr);
        Ref<Material> material  = Material::Create(texShader, "wo08_r01_material");
        material->Set("u_Color", glm::vec4(0.90f, 0.30f, 0.90f, 1.0f));

        Ref<FrameBuffer> fbo = MakeTarget();
        REQUIRE(fbo != nullptr);

        StatsScope stats;
        BeginFrame(fbo);
        Renderer2D::PushRenderPass(Ortho2DFor(kW, kH, kCenter, kHalfH), { 0.0f, 0.0f, (float)kW, (float)kH });

        // Row 1 (y = 3.5): quads.
        Renderer2D::DrawQuad(glm::vec2{ -7.0f, 3.5f }, { 2.0f, 2.0f }, { 0.90f, 0.15f, 0.15f, 1.0f });   // red
        Renderer2D::DrawQuad(glm::vec2{ -4.5f, 3.5f }, { 2.0f, 2.0f }, { 0.15f, 0.80f, 0.20f, 1.0f });   // green
        Renderer2D::DrawQuad(glm::vec2{ -2.0f, 3.5f }, { 2.0f, 2.0f }, { 0.10f, 0.10f, 1.00f, 1.0f });   // blue
        Renderer2D::DrawQuad(glm::vec2{ -1.0f, 3.5f }, { 2.0f, 2.0f }, { 1.00f, 1.00f, 1.00f, 0.5f });   // 50% white over blue+clear
        Renderer2D::DrawRotatedQuad(glm::vec2{ 1.5f, 3.5f }, { 2.0f, 2.0f }, glm::radians(45.0f), { 1.0f, 0.85f, 0.10f, 1.0f });
        Renderer2D::DrawQuad(glm::vec2{ 4.5f, 3.5f }, { 2.0f, 2.0f }, checker);
        Renderer2D::DrawQuad(glm::vec2{ 7.0f, 3.5f }, { 2.0f, 2.0f }, tile6);

        // Row 2 (y = 0.5): tiling, SDF disc / ring / ellipse, lines.
        Renderer2D::DrawQuad(glm::vec2{ -7.0f, 0.5f }, { 2.0f, 2.0f }, checker, 2.0f);
        Renderer2D::DrawCircle(glm::vec2{ -4.5f, 0.5f }, { 2.0f, 2.0f }, { 0.20f, 0.75f, 0.95f, 1.0f }, 1.0f, 0.005f);   // disc
        Renderer2D::DrawCircle(glm::vec2{ -2.0f, 0.5f }, { 2.0f, 2.0f }, { 0.95f, 0.55f, 0.20f, 1.0f }, 0.2f, 0.005f);   // ring
        Renderer2D::DrawCircle(glm::vec2{  1.5f, 0.5f }, { 3.0f, 1.5f }, { 0.70f, 0.30f, 0.90f, 1.0f }, 1.0f, 0.005f);   // ellipse
        Renderer2D::DrawLine({ 4.0f, -0.5f, 0.0f }, { 8.0f, 1.5f, 0.0f }, { 0.20f, 0.95f, 0.95f, 1.0f });
        Renderer2D::DrawRect({ 6.0f, 0.5f, 0.0f }, { 3.0f, 2.0f }, { 1.0f, 1.0f, 1.0f, 1.0f });

        // Row 3 (y = -2.5): the grouping sentinel — a circle SUBMITTED BEFORE a
        // quad at the same spot still ends up on top, because a flush draws all
        // quads, then lines, then circles, then text (documented order).
        Renderer2D::DrawCircle(glm::vec2{ 4.5f, -2.5f }, { 2.0f, 2.0f }, { 0.95f, 0.20f, 0.80f, 1.0f }, 1.0f, 0.005f);
        Renderer2D::DrawQuad(glm::vec2{ 4.5f, -2.5f }, { 1.0f, 1.0f }, { 0.05f, 0.05f, 0.05f, 1.0f });
        // ...and a line submitted before a quad stays visible over it.
        Renderer2D::DrawLine({ -4.0f, -2.5f, 0.0f }, { 1.0f, -2.5f, 0.0f }, { 1.0f, 0.95f, 0.20f, 1.0f });
        Renderer2D::DrawQuad(glm::vec2{ -1.5f, -2.5f }, { 3.0f, 2.0f }, { 0.25f, 0.30f, 0.45f, 1.0f });

        // A Material quad LAST: it breaks the batch (its own draw call).
        Renderer2D::DrawQuad(glm::vec2{ -7.0f, -2.5f }, { 2.0f, 2.0f }, material);

        Renderer2D::PopRenderPass();

        const Renderer2D::Statistics st = stats.Get();
        // Default batch (quads + lines + circles) flushed at the material switch,
        // then the material batch at the pop: 2 flushes, 4 draws.
        CHECK(st.Flushes == 2);
        CHECK(st.DrawCalls == 4);
        CHECK(st.QuadCount == 11);
        CHECK(st.CircleCount == 4);
        CHECK(st.LineCount == 6);   // 1 + 4 (rect) + 1
        CHECK(st.GlyphCount == 0);
        CHECK(st.InstanceDrawCalls == 0);

        Image frame;
        REQUIRE(Capture(fbo, frame));
        WriteEvidence("r01-primitives", frame);

        // --- Sentinels (exact interior colours) ---------------------------------
        CHECK_MESSAGE(Near(Sample(frame, { -7.0f, 3.5f }), ToU8({ 0.90f, 0.15f, 0.15f, 1.0f })), "red quad centre");
        CHECK_MESSAGE(Near(Sample(frame, { -4.5f, 3.5f }), ToU8({ 0.15f, 0.80f, 0.20f, 1.0f })), "green quad centre");
        // Alpha: white@0.5 over blue where they overlap, over the clear where not.
        CHECK_MESSAGE(Near(Sample(frame, { -1.5f, 3.5f }), Over({ 1, 1, 1, 0.5f }, ToU8({ 0.10f, 0.10f, 1.00f, 1.0f }))), "50% white over blue");
        CHECK_MESSAGE(Near(Sample(frame, {  0.0f - 0.35f, 3.5f }), Over({ 1, 1, 1, 0.5f }, kClearU8)), "50% white over clear");
        CHECK_MESSAGE(Near(Sample(frame, { -2.7f, 3.5f }), ToU8({ 0.10f, 0.10f, 1.00f, 1.0f })), "blue outside the overlap");
        // Rotated: the centre is yellow; the unrotated corner is OUTSIDE the diamond.
        CHECK_MESSAGE(Near(Sample(frame, { 1.5f, 3.5f }), ToU8({ 1.0f, 0.85f, 0.10f, 1.0f })), "rotated quad centre");
        CHECK_MESSAGE(Near(Sample(frame, { 1.5f + 0.85f, 3.5f + 0.85f }), kClearU8), "rotated quad: unrotated corner is empty");
        CHECK_MESSAGE(Near(Sample(frame, { 1.5f + 0.9f, 3.5f }), ToU8({ 1.0f, 0.85f, 0.10f, 1.0f })), "rotated quad: diamond tip on the x axis");
        // Textured (UVs): checker cell (1,1) of the 4x4 grid is colour `a`; cell (0,0) too.
        CHECK_MESSAGE(Near(Sample(frame, { 4.5f - 0.25f, 3.5f - 0.25f }), glm::u8vec4{ 235, 235, 235, 255 }), "checker: cell (1,1) [UV 0.375,0.375]");
        CHECK_MESSAGE(Near(Sample(frame, { 4.5f + 0.25f, 3.5f - 0.25f }), glm::u8vec4{ 60, 70, 90, 255 }), "checker: cell (2,1) [UV 0.625,0.375]");
        // Atlas: the whole quad is tile 6.
        CHECK_MESSAGE(Near(Sample(frame, { 7.0f, 3.5f }), AtlasPalette()[6]), "atlas sub-texture centre == tile 6");
        CHECK_MESSAGE(Near(Sample(frame, { 7.0f - 0.8f, 3.5f + 0.8f }), AtlasPalette()[6]), "atlas sub-texture corner == tile 6");
        // Tiling x2 doubles the checker frequency: 8 cells of 0.25 world units
        // across the quad (the texture REPEATs, so tiled cell k samples texel cell
        // k mod 4 and the parity rule (k+m)%2 still holds). Sample tiled cell
        // CENTRES — the minified checker is bilinear, so a cell boundary is a
        // 50/50 blend.
        auto tiledCenter = [](int k, int m) { return glm::vec2{ -8.0f + 0.25f * k + 0.125f, -0.5f + 0.25f * m + 0.125f }; };
        CHECK_MESSAGE(Near(Sample(frame, tiledCenter(2, 2)), glm::u8vec4{ 235, 235, 235, 255 }, 4), "tiling: cell (2,2) -> colour a: ", Describe(Sample(frame, tiledCenter(2, 2))));
        CHECK_MESSAGE(Near(Sample(frame, tiledCenter(3, 2)), glm::u8vec4{ 60, 70, 90, 255 }, 4), "tiling: cell (3,2) -> colour b: ", Describe(Sample(frame, tiledCenter(3, 2))));
        CHECK_MESSAGE(Near(Sample(frame, tiledCenter(5, 5)), glm::u8vec4{ 235, 235, 235, 255 }, 4), "tiling: cell (5,5) -> colour a (wrapped)");
        CHECK_MESSAGE(Near(Sample(frame, tiledCenter(6, 1)), glm::u8vec4{ 60, 70, 90, 255 }, 4), "tiling: cell (6,1) -> colour b (wrapped)");
        // SDF disc / ring / ellipse.
        CHECK_MESSAGE(Near(Sample(frame, { -4.5f, 0.5f }), ToU8({ 0.20f, 0.75f, 0.95f, 1.0f })), "disc centre");
        CHECK_MESSAGE(Near(Sample(frame, { -4.5f + 1.2f, 0.5f }), kClearU8), "disc: outside the radius is clear");
        CHECK_MESSAGE(Near(Sample(frame, { -2.0f, 0.5f }), kClearU8), "ring centre is empty");
        CHECK_MESSAGE(Near(Sample(frame, { -2.0f + 0.9f, 0.5f }), ToU8({ 0.95f, 0.55f, 0.20f, 1.0f })), "ring at r=0.9");
        CHECK_MESSAGE(Near(Sample(frame, { 1.5f, 0.5f }), ToU8({ 0.70f, 0.30f, 0.90f, 1.0f })), "ellipse centre");
        CHECK_MESSAGE(Near(Sample(frame, { 1.5f + 1.3f, 0.5f }), ToU8({ 0.70f, 0.30f, 0.90f, 1.0f })), "ellipse: inside on the long axis");
        CHECK_MESSAGE(Near(Sample(frame, { 1.5f + 1.4f, 0.5f + 0.4f }), kClearU8), "ellipse: outside the normalised radius");
        // Lines are dashed by Line.glsl (a 0.05-unit world-space pattern), so the
        // oracle is "ink of the line colour exists along the line" not a solid run.
        {
            const Roi r = RoiFor({ 4.0f, -0.5f }, { 8.0f, 1.5f });
            CHECK_MESSAGE(CountColor(frame, r.x, r.y, r.w, r.h, ToU8({ 0.20f, 0.95f, 0.95f, 1.0f }), 12) >= 10, "diagonal line has cyan ink");
            const Roi top = RoiFor({ 4.5f, 1.5f - 0.05f }, { 7.5f, 1.5f + 0.05f });
            CHECK_MESSAGE(CountColor(frame, top.x, top.y, top.w, top.h, glm::u8vec4{ 255, 255, 255, 255 }, 12) >= 10, "rect top edge has white ink");
            const Roi left = RoiFor({ 4.5f - 0.05f, -0.5f }, { 4.5f + 0.05f, 1.5f });
            CHECK_MESSAGE(CountColor(frame, left.x, left.y, left.w, left.h, glm::u8vec4{ 255, 255, 255, 255 }, 12) >= 6, "rect left edge has white ink");
        }
        // Per-flush grouping: circle over the later quad; line over the later quad.
        CHECK_MESSAGE(Near(Sample(frame, { 4.5f, -2.5f }), ToU8({ 0.95f, 0.20f, 0.80f, 1.0f })), "circle submitted first still draws over the quad (quads flush before circles)");
        {
            const Roi r = RoiFor({ -2.9f, -2.5f - 0.05f }, { -0.1f, -2.5f + 0.05f });
            CHECK_MESSAGE(CountColor(frame, r.x, r.y, r.w, r.h, ToU8({ 1.0f, 0.95f, 0.20f, 1.0f }), 12) >= 8, "line submitted first still draws over the quad (quads flush before lines)");
        }
        // Material quad carries u_Color.
        CHECK_MESSAGE(Near(Sample(frame, { -7.0f, -2.5f }), ToU8({ 0.90f, 0.30f, 0.90f, 1.0f })), "material quad == u_Color");

        CHECK(CheckGolden("wo08_primitives", frame));
    }

    TEST_CASE("R01 text — SDF glyphs, sizes, multi-line, kerning and the '?' fallback (golden + glyph ROIs)")
    {
        Ref<Font> font = Font::Create(kPinnedFontPath, kPinnedFontAtlasPx);
        REQUIRE(font != nullptr);
        REQUIRE(font->IsValid());
        REQUIRE(font->GetGlyph('?') != nullptr);
        // Bytes outside the baked printable-ASCII range have no glyph and must
        // fall back to '?': the two UTF-8 bytes of "é" become "??".
        CHECK(font->GetGlyph(0xC3) == nullptr);
        CHECK(font->GetGlyph(0xA9) == nullptr);
        CHECK(font->GetGlyph(0x7F) == nullptr);

        const glm::vec4 ink{ 0.96f, 0.96f, 0.90f, 1.0f };
        const glm::vec4 ink2{ 0.40f, 0.85f, 1.00f, 1.0f };

        auto drawScene = [&](const std::string& fallbackText)
        {
            Renderer2D::DrawString("Cosmic 2D", font, glm::vec2{ -8.0f, 2.2f }, 1.6f, ink);
            Renderer2D::DrawString("Batch\nText", font, glm::vec2{ -8.0f, -1.0f }, 1.0f, ink2);
            Renderer2D::DrawString("AV", font, glm::vec2{ 3.0f, -1.0f }, 1.0f, ink, 0.25f);   // kerning: gap widened
            Renderer2D::DrawString(fallbackText, font, glm::vec2{ 3.0f, 2.2f }, 1.2f, ink);
        };

        Ref<FrameBuffer> fbo = MakeTarget();
        REQUIRE(fbo != nullptr);

        StatsScope stats;
        BeginFrame(fbo);
        Renderer2D::PushRenderPass(Ortho2DFor(kW, kH, kCenter, kHalfH), { 0.0f, 0.0f, (float)kW, (float)kH });
        drawScene("A\xC3\xA9" "B");   // "AéB" in UTF-8
        Renderer2D::PopRenderPass();

        const Renderer2D::Statistics st = stats.Get();
        // "Cosmic 2D" = 8 visible, "Batch\nText" = 9, "AV" = 2, "A??B" = 4 -> 23 glyphs, one text draw.
        CHECK(st.GlyphCount == 23);
        CHECK(st.QuadCount == 23);   // historical: glyphs also count as quads
        CHECK(st.DrawCalls == 1);
        CHECK(st.Flushes == 1);

        Image frame;
        REQUIRE(Capture(fbo, frame));
        WriteEvidence("r01-text", frame);

        // --- Glyph ROIs: every visible glyph box has ink; every space has none. ---
        auto checkString = [&](const std::string& text, const glm::vec2& pos, float size, float kerning, const glm::vec4& colour)
        {
            const std::vector<GlyphBox> boxes = LayoutBoxes(text, *font, pos, size, kerning);
            for (const GlyphBox& b : boxes)
            {
                const Roi r = RoiFor(b.min, b.max);
                if (b.c == '\n') continue;
                const int n = CountColor(frame, r.x, r.y, r.w, r.h, ToU8(colour), 24);
                if (b.visible)
                    CHECK_MESSAGE(n > 0, "glyph '", (char)b.c, "' of \"", text, "\" has no ink in its box");
                else
                    CHECK_MESSAGE(n == 0, "whitespace of \"", text, "\" has ink");
            }
        };
        checkString("Cosmic 2D", { -8.0f, 2.2f }, 1.6f, 0.0f, ink);
        checkString("Batch\nText", { -8.0f, -1.0f }, 1.0f, 0.0f, ink2);
        checkString("AV", { 3.0f, -1.0f }, 1.0f, 0.25f, ink);
        checkString("A??B", { 3.0f, 2.2f }, 1.2f, 0.0f, ink);   // the fallback lays out as '?'

        // Multi-line: the second line sits one LineHeight below the first.
        {
            const std::vector<GlyphBox> l = LayoutBoxes("Batch\nText", *font, { -8.0f, -1.0f }, 1.0f);
            const GlyphBox& B = l[0];
            const GlyphBox& T = l[5];   // the newline pushes no box
            CHECK(T.max.y < B.min.y);
            // Glyph boxes carry their own SDF padding, so compare the pen rows:
            // one LineHeight apart, i.e. the box tops differ by LineHeight plus
            // the two glyphs' own top offsets.
            const float penDelta = (B.max.y - font->GetGlyph('B')->offset.y * 1.0f) - (T.max.y - font->GetGlyph('T')->offset.y * 1.0f);
            CHECK(penDelta == doctest::Approx(font->LineHeight()));
        }

        // Kerning widens the gap: with +0.25 em the 'V' starts 0.25 world units
        // further right than with 0 — the ROI between them at kerning 0.25 is empty
        // where the unkerned 'V' would have started.
        {
            const std::vector<GlyphBox> k0 = LayoutBoxes("AV", *font, { 3.0f, -1.0f }, 1.0f, 0.0f);
            const std::vector<GlyphBox> k1 = LayoutBoxes("AV", *font, { 3.0f, -1.0f }, 1.0f, 0.25f);
            CHECK(k1[1].min.x - k0[1].min.x == doctest::Approx(0.25f));
        }

        // Font fallback A/B: "AéB" and "A??B" must be BYTE-identical frames.
        {
            Ref<FrameBuffer> fboB = MakeTarget();
            REQUIRE(fboB != nullptr);
            BeginFrame(fboB);
            Renderer2D::PushRenderPass(Ortho2DFor(kW, kH, kCenter, kHalfH), { 0.0f, 0.0f, (float)kW, (float)kH });
            drawScene("A??B");
            Renderer2D::PopRenderPass();
            Image frameB;
            REQUIRE(Capture(fboB, frameB));
            CHECK_MESSAGE(BytesEqual(frame, frameB), "the '?' fallback frame is not byte-identical to drawing '?' directly");
        }

        // Control characters and an empty string draw nothing.
        {
            Ref<FrameBuffer> fboC = MakeTarget();
            REQUIRE(fboC != nullptr);
            stats.Reset();
            BeginFrame(fboC);
            Renderer2D::PushRenderPass(Ortho2DFor(kW, kH, kCenter, kHalfH), { 0.0f, 0.0f, (float)kW, (float)kH });
            Renderer2D::DrawString("", font, glm::vec2{ 0.0f, 0.0f }, 1.0f, ink);
            Renderer2D::DrawString("\r\n \t", font, glm::vec2{ 0.0f, 0.0f }, 1.0f, ink);
            Renderer2D::DrawString("x", nullptr, glm::vec2{ 0.0f, 0.0f }, 1.0f, ink);   // null font: no-op
            Renderer2D::PopRenderPass();
            Image frameC;
            REQUIRE(Capture(fboC, frameC));
            CHECK(stats.Get().GlyphCount == 1);   // '\t' has no glyph -> '?' fallback (visible)
            CHECK(stats.Get().DrawCalls == 1);
            CHECK(CountInk(frameC, 0, 0, kW, kH, kClearU8) > 0);
        }

        CHECK(CheckGolden("wo08_text", frame));
    }
}
