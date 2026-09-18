// render_wo08_batches.cpp — WO-08 R02: Renderer2D batch boundaries.
//
// For quads, lines, SDF circles and text glyphs: 0, 1, limit-1, limit, limit+1
// and 2*limit+1 items (limit = 10,000 as declared in Renderer2D.cpp), the
// texture-slot rollover at 30/31/32/33 (and 64) distinct non-white textures
// (slot 0 is the reserved white texture, so 31 fit in one batch), material and
// shader transitions, and mixed submissions.
//
// ORACLE. Every item i is drawn at grid cell i with the colour EncodeIndex(i),
// under a pixel-exact camera; afterwards cell i's centre pixel must decode to i
// for i < N and to "clear" for i >= N. A dropped item leaves a clear cell, a
// duplicated or shifted item puts the wrong index in a cell, and the armed
// Renderer2D::Statistics pin the ACTUAL flush/draw counts for each isolated
// homogeneous submission. Lines use a dash-safe camera: Line.glsl discards
// fragments on a 0.05-unit world pattern, so the camera maps pixel centres onto
// the kept half of that pattern with a 0.25-period margin either side.

#include "wo08_common.h"

#include "graphics/Font.h"
#include "graphics/Material.h"
#include "graphics/Shader.h"

#include <string>

using namespace Wo08;

namespace
{
    constexpr uint32_t kLimit = 10000;   // MaxQuads == MaxLines == MaxCircles == MaxTextQuads
    constexpr uint32_t kSlots = 31;      // 32 texture slots minus the reserved white slot 0

    // A grid of `cell`-pixel cells, `cols` wide, big enough for `capacity` items.
    struct Grid
    {
        uint32_t Cell = 4, Cols = 201, Rows = 0, W = 0, H = 0;
        Ref<FrameBuffer> Fbo;

        Grid(uint32_t capacity, uint32_t cell = 4, uint32_t cols = 201)
            : Cell(cell), Cols(cols)
        {
            Rows = (capacity + cols - 1) / cols;
            if (Rows == 0) Rows = 1;
            W = Cols * Cell;
            H = Rows * Cell;
            Fbo = MakeRgba8Target(W, H);
            REQUIRE(Fbo != nullptr);
        }

        // Pixel-space (bottom-left origin) centre of cell i.
        glm::vec2 Center(uint32_t i) const
        {
            return { (float)((i % Cols) * Cell) + Cell * 0.5f, (float)((i / Cols) * Cell) + Cell * 0.5f };
        }

        // The image pixel (top-left origin) at the centre of cell i.
        glm::u8vec4 Probe(const Image& img, uint32_t i) const
        {
            const uint32_t px = (i % Cols) * Cell + Cell / 2;
            const uint32_t py = (i / Cols) * Cell + Cell / 2;
            return PixelAtGl(img, px, py);
        }

        void Begin() const
        {
            BeginFrame(Fbo);
            Renderer2D::PushRenderPass(PixelOrtho(W, H), { 0.0f, 0.0f, (float)W, (float)H });
        }

        // Verify cells [0, n) decode to their index and cells [n, capacity) are clear.
        void Verify(const Image& img, uint32_t n, uint32_t capacity, const char* what) const
        {
            uint32_t missing = 0, wrong = 0, extra = 0;
            int64_t firstBad = -1;
            for (uint32_t i = 0; i < capacity; ++i)
            {
                const int64_t got = DecodeIndex(Probe(img, i));
                if (i < n)
                {
                    if (got == -1) { ++missing; if (firstBad < 0) firstBad = i; }
                    else if (got != (int64_t)i) { ++wrong; if (firstBad < 0) firstBad = i; }
                }
                else if (got != -1)
                {
                    ++extra; if (firstBad < 0) firstBad = i;
                }
            }
            CHECK_MESSAGE(missing == 0, what, " N=", n, ": ", missing, " cell(s) DROPPED (first at ", firstBad, ")");
            CHECK_MESSAGE(wrong == 0, what, " N=", n, ": ", wrong, " cell(s) carry the WRONG index (first at ", firstBad, ")");
            CHECK_MESSAGE(extra == 0, what, " N=", n, ": ", extra, " cell(s) painted beyond N (duplicate/overrun, first at ", firstBad, ")");
        }
    };

    const uint32_t kCounts[] = { 0, 1, kLimit - 1, kLimit, kLimit + 1, 2 * kLimit + 1 };

    uint32_t ExpectedBatches(uint32_t n) { return (n + kLimit - 1) / kLimit; }   // ceil(n / limit), 0 for 0
}

TEST_SUITE("WO-08 R02")
{
    TEST_CASE("R02 quads — 0/1/9999/10000/10001/20001 flat quads: exact cells, exact draw counts")
    {
        Grid grid(2 * kLimit + 1);
        StatsScope stats;
        for (uint32_t n : kCounts)
        {
            stats.Reset();
            grid.Begin();
            for (uint32_t i = 0; i < n; ++i)
                Renderer2D::DrawQuad(grid.Center(i), { 3.0f, 3.0f }, EncodeIndex(i));
            Renderer2D::PopRenderPass();

            const Renderer2D::Statistics st = stats.Get();
            CHECK_MESSAGE(st.QuadCount == n, "N=", n);
            CHECK_MESSAGE(st.DrawCalls == ExpectedBatches(n), "N=", n, " draws=", st.DrawCalls);
            CHECK_MESSAGE(st.Flushes == ExpectedBatches(n), "N=", n, " flushes=", st.Flushes);
            CHECK(st.CircleCount == 0); CHECK(st.LineCount == 0); CHECK(st.GlyphCount == 0);

            Image img;
            REQUIRE(Capture(grid.Fbo, img));
            grid.Verify(img, n, 2 * kLimit + 1, "quads");
            if (n == kLimit + 1) WriteEvidence("r02-quads-10001", img);
        }
    }

    TEST_CASE("R02 circles — 0/1/9999/10000/10001/20001 SDF circles: exact cells, exact draw counts")
    {
        Grid grid(2 * kLimit + 1);
        StatsScope stats;
        for (uint32_t n : kCounts)
        {
            stats.Reset();
            grid.Begin();
            for (uint32_t i = 0; i < n; ++i)
                Renderer2D::DrawCircle(grid.Center(i), { 4.0f, 4.0f }, EncodeIndex(i), 1.0f, 0.005f);
            Renderer2D::PopRenderPass();

            const Renderer2D::Statistics st = stats.Get();
            CHECK_MESSAGE(st.CircleCount == n, "N=", n);
            CHECK_MESSAGE(st.DrawCalls == ExpectedBatches(n), "N=", n, " draws=", st.DrawCalls);
            CHECK_MESSAGE(st.Flushes == ExpectedBatches(n), "N=", n, " flushes=", st.Flushes);
            CHECK(st.QuadCount == 0); CHECK(st.InstanceDrawCalls == 0);

            Image img;
            REQUIRE(Capture(grid.Fbo, img));
            grid.Verify(img, n, 2 * kLimit + 1, "circles");
            if (n == kLimit + 1) WriteEvidence("r02-circles-10001", img);
        }
    }

    TEST_CASE("R02 lines — 0/1/9999/10000/10001/20001 lines: exact cells, exact draw counts")
    {
        Grid grid(2 * kLimit + 1);
        // Dash-safe camera (see the header note): pixel (px, py) centre <-> world
        // (0.05*px + 0.0125, 0.05*py + 0.0125), so 20*x mod 1 == 0.25 at every centre.
        const float s = 0.05f, o = -0.0125f;
        const glm::mat4 cam = glm::ortho(o, o + grid.W * s, o, o + grid.H * s, -1.0f, 1.0f);
        auto world = [&](uint32_t px, uint32_t py) { return glm::vec3(s * px + 0.0125f, s * py + 0.0125f, 0.0f); };

        StatsScope stats;
        for (uint32_t n : kCounts)
        {
            stats.Reset();
            BeginFrame(grid.Fbo);
            Renderer2D::PushRenderPass(cam, { 0.0f, 0.0f, (float)grid.W, (float)grid.H });
            for (uint32_t i = 0; i < n; ++i)
            {
                const uint32_t px = (i % grid.Cols) * grid.Cell;
                const uint32_t py = (i / grid.Cols) * grid.Cell + grid.Cell / 2;
                Renderer2D::DrawLine(world(px, py), world(px + 3, py), EncodeIndex(i));
            }
            Renderer2D::PopRenderPass();

            const Renderer2D::Statistics st = stats.Get();
            CHECK_MESSAGE(st.LineCount == n, "N=", n);
            CHECK_MESSAGE(st.DrawCalls == ExpectedBatches(n), "N=", n, " draws=", st.DrawCalls);
            CHECK_MESSAGE(st.Flushes == ExpectedBatches(n), "N=", n, " flushes=", st.Flushes);
            CHECK(st.QuadCount == 0); CHECK(st.CircleCount == 0);

            Image img;
            REQUIRE(Capture(grid.Fbo, img));
            grid.Verify(img, n, 2 * kLimit + 1, "lines");
            if (n == kLimit + 1) WriteEvidence("r02-lines-10001", img);
        }
    }

    TEST_CASE("R02 glyphs — 0/1/9999/10000/10001/20001 SDF glyphs (per-call and one string crossing the limit)")
    {
        Ref<Font> font = Font::Create(kPinnedFontPath, kPinnedFontAtlasPx);
        REQUIRE(font != nullptr);
        const Glyph* g = font->GetGlyph('I');
        REQUIRE(g != nullptr);
        REQUIRE(g->size.x > 0.0f);

        // 16-px cells; the 'I' stem is placed on the cell centre and stretched
        // 50 px/em horizontally so its centre pixel is deep inside the ink.
        Grid grid(2 * kLimit + 1, 16, 128);
        const glm::vec2 boxCenter{ g->offset.x + g->size.x * 0.5f, g->offset.y - g->size.y * 0.5f };
        const glm::vec2 scale{ 50.0f, 12.0f };
        auto glyphTransform = [&](const glm::vec2& cellCenter)
        {
            const glm::vec2 t = cellCenter - boxCenter * scale;
            return glm::translate(glm::mat4(1.0f), { t.x, t.y, 0.0f }) * glm::scale(glm::mat4(1.0f), { scale.x, scale.y, 1.0f });
        };

        StatsScope stats;
        for (uint32_t n : kCounts)
        {
            stats.Reset();
            grid.Begin();
            for (uint32_t i = 0; i < n; ++i)
                Renderer2D::DrawString("I", font, glyphTransform(grid.Center(i)), EncodeIndex(i));
            Renderer2D::PopRenderPass();

            const Renderer2D::Statistics st = stats.Get();
            CHECK_MESSAGE(st.GlyphCount == n, "N=", n);
            CHECK_MESSAGE(st.DrawCalls == ExpectedBatches(n), "N=", n, " draws=", st.DrawCalls);
            CHECK_MESSAGE(st.Flushes == ExpectedBatches(n), "N=", n, " flushes=", st.Flushes);
            CHECK(st.CircleCount == 0); CHECK(st.LineCount == 0);

            Image img;
            REQUIRE(Capture(grid.Fbo, img));
            grid.Verify(img, n, 2 * kLimit + 1, "glyphs");
            if (n == kLimit + 1) WriteEvidence("r02-glyphs-10001", img);
        }

        // One string of limit+1 glyphs crosses MaxTextQuads MID-STRING: every
        // glyph still lands (ink in every cell along the pen), in two draws.
        {
            const uint32_t n = kLimit + 1;
            // The pen advances g->advance em per glyph; with scale.x == cell/advance
            // each glyph sits in its own 16-px cell of a single 16*(limit+1)-px row —
            // too wide for a target, so wrap through the transform: draw the string
            // in 128-glyph rows via '\n' (a newline resets the pen, one LineHeight down).
            std::string text;
            for (uint32_t i = 0; i < n; ++i)
            {
                if (i && (i % grid.Cols) == 0) text += '\n';
                text += 'I';
            }
            const float sx = (float)grid.Cell / g->advance;          // one cell per advance
            const float sy = (float)grid.Cell / font->LineHeight();  // one cell per line
            const glm::mat4 xf = glm::translate(glm::mat4(1.0f),
                                    { -g->offset.x * sx, grid.H - (g->offset.y) * sy - grid.Cell * 0.25f, 0.0f })
                               * glm::scale(glm::mat4(1.0f), { sx, sy, 1.0f });
            stats.Reset();
            grid.Begin();
            Renderer2D::DrawString(text, font, xf, { 1.0f, 1.0f, 1.0f, 1.0f });
            Renderer2D::PopRenderPass();

            const Renderer2D::Statistics st = stats.Get();
            CHECK(st.GlyphCount == n);
            CHECK(st.DrawCalls == 2);
            CHECK(st.Flushes == 2);

            Image img;
            REQUIRE(Capture(grid.Fbo, img));
            WriteEvidence("r02-glyphs-onestring-10001", img);
            // Every glyph i occupies row i/cols from the TOP, column i%cols: its
            // cell must have white ink; the cell after the last glyph must not.
            uint32_t inked = 0;
            for (uint32_t i = 0; i < n + grid.Cols; ++i)
            {
                const uint32_t col = i % grid.Cols, row = i / grid.Cols;
                const int ink = CountColor(img, col * grid.Cell, row * grid.Cell, grid.Cell, grid.Cell,
                                           glm::u8vec4{ 255, 255, 255, 255 }, 40);
                if (i < n) { if (ink > 0) ++inked; }
                else CHECK_MESSAGE(ink == 0, "cell ", i, " beyond the string has ink");
            }
            CHECK_MESSAGE(inked == n, "one-string crossing: ", inked, " of ", n, " glyph cells inked");
        }
    }

    TEST_CASE("R02 texture slots — 30/31/32/33/64 distinct non-white textures (slot 0 reserved => 31 per batch)")
    {
        Grid grid(64);
        std::vector<Ref<Texture2D>> textures;
        for (uint32_t i = 0; i < 64; ++i)
            textures.push_back(MakeFlatTexture(2, 2, EncodeIndexU8(i)));

        StatsScope stats;
        for (uint32_t n : { 30u, 31u, 32u, 33u, 62u, 63u, 64u })
        {
            stats.Reset();
            grid.Begin();
            for (uint32_t i = 0; i < n; ++i)
                Renderer2D::DrawQuad(grid.Center(i), { 3.0f, 3.0f }, textures[i]);
            Renderer2D::PopRenderPass();

            const uint32_t expected = (n + kSlots - 1) / kSlots;
            const Renderer2D::Statistics st = stats.Get();
            CHECK_MESSAGE(st.DrawCalls == expected, "N=", n, " textures -> draws=", st.DrawCalls, " (expected ", expected, ")");
            CHECK_MESSAGE(st.Flushes == expected, "N=", n, " flushes=", st.Flushes);
            CHECK(st.QuadCount == n);

            Image img;
            REQUIRE(Capture(grid.Fbo, img));
            grid.Verify(img, n, 64, "textured quads");   // each quad samples ITS texture's colour
            if (n == 32) WriteEvidence("r02-texture-slots-32", img);
        }

        // Re-using a texture within a batch costs no slot; flat-colour quads use
        // the reserved white slot: 31 distinct + 31 repeats + 31 flat == 1 draw.
        {
            stats.Reset();
            grid.Begin();
            for (uint32_t i = 0; i < 31; ++i) Renderer2D::DrawQuad(grid.Center(i), { 3.0f, 3.0f }, textures[i]);
            for (uint32_t i = 0; i < 31; ++i) Renderer2D::DrawQuad(grid.Center(31 + i), { 3.0f, 3.0f }, textures[i], 1.0f, glm::vec4(1.0f));
            for (uint32_t i = 0; i < 2; ++i)  Renderer2D::DrawQuad(grid.Center(62 + i), { 3.0f, 3.0f }, EncodeIndex(62 + i));
            Renderer2D::PopRenderPass();
            const Renderer2D::Statistics st = stats.Get();
            CHECK(st.DrawCalls == 1);
            CHECK(st.QuadCount == 64);
            Image img;
            REQUIRE(Capture(grid.Fbo, img));
            for (uint32_t i = 0; i < 31; ++i)
            {
                CHECK(DecodeIndex(grid.Probe(img, i)) == (int64_t)i);
                CHECK(DecodeIndex(grid.Probe(img, 31 + i)) == (int64_t)i);   // the repeat shows texture i
            }
            CHECK(DecodeIndex(grid.Probe(img, 62)) == 62);
            CHECK(DecodeIndex(grid.Probe(img, 63)) == 63);
        }

        // After a rollover the NEXT batch starts a fresh slot table: texture 0
        // (registered in batch 1) sampled again in batch 2 still shows texture 0.
        {
            stats.Reset();
            grid.Begin();
            for (uint32_t i = 0; i < 32; ++i) Renderer2D::DrawQuad(grid.Center(i), { 3.0f, 3.0f }, textures[i]);   // 31 + rollover
            Renderer2D::DrawQuad(grid.Center(32), { 3.0f, 3.0f }, textures[0]);
            Renderer2D::PopRenderPass();
            CHECK(stats.Get().DrawCalls == 2);
            Image img;
            REQUIRE(Capture(grid.Fbo, img));
            CHECK(DecodeIndex(grid.Probe(img, 31)) == 31);
            CHECK(DecodeIndex(grid.Probe(img, 32)) == 0);
        }
    }

    TEST_CASE("R02 material / shader transitions — batch breaks land where the state changes, nothing leaks across")
    {
        Ref<Shader> tintShader = Shader::Create(FixturePath("wo08_tint_quad.glsl"));
        REQUIRE(tintShader != nullptr);
        Ref<Material> magenta = Material::Create(tintShader, "wo08_magenta");
        magenta->Set("u_Color", glm::vec4(1.0f));
        magenta->Set("u_Tint", glm::vec4(1.0f, 0.0f, 1.0f, 1.0f));
        Ref<Material> cyan = Material::Create(tintShader, "wo08_cyan");
        cyan->Set("u_Color", glm::vec4(1.0f));
        cyan->Set("u_Tint", glm::vec4(0.0f, 1.0f, 1.0f, 1.0f));

        Grid grid(64);
        const glm::vec4 white{ 1.0f, 1.0f, 1.0f, 1.0f };
        const glm::u8vec4 magentaU8{ 255, 0, 255, 255 }, cyanU8{ 0, 255, 255, 255 }, whiteU8{ 255, 255, 255, 255 };
        StatsScope stats;

        SUBCASE("material quad followed by flat quads: the flat quads return to the default batch")
        {
            stats.Reset();
            grid.Begin();
            Renderer2D::DrawQuad(grid.Center(0), { 3.0f, 3.0f }, magenta);
            for (uint32_t i = 1; i <= 5; ++i)
                Renderer2D::DrawQuad(grid.Center(i), { 3.0f, 3.0f }, white);
            Renderer2D::PopRenderPass();

            const Renderer2D::Statistics st = stats.Get();
            CHECK_MESSAGE(st.DrawCalls == 2, "1 material batch + 1 default batch expected, got ", st.DrawCalls);
            // 3 flushes for 2 draws: entering the material bucket from an EMPTY
            // default bucket flushes nothing (a documented empty flush), the first
            // flat quad flushes the material batch, the pop flushes the flat batch.
            CHECK_MESSAGE(st.Flushes == 3, "flushes=", st.Flushes);
            CHECK(st.QuadCount == 6);

            Image img;
            REQUIRE(Capture(grid.Fbo, img));
            WriteEvidence("r02-material-then-flat", img);
            CHECK_MESSAGE(Near(grid.Probe(img, 0), magentaU8), "material quad is tinted: ", Describe(grid.Probe(img, 0)));
            for (uint32_t i = 1; i <= 5; ++i)
                CHECK_MESSAGE(Near(grid.Probe(img, i), whiteU8), "flat quad ", i, " must NOT carry the material's shader/tint: ", Describe(grid.Probe(img, i)));
        }

        SUBCASE("material quad followed by textured and sub-textured quads")
        {
            Ref<Texture2D> tex = MakeFlatTexture(2, 2, whiteU8);
            stats.Reset();
            grid.Begin();
            Renderer2D::DrawQuad(grid.Center(0), { 3.0f, 3.0f }, cyan);
            Renderer2D::DrawQuad(grid.Center(1), { 3.0f, 3.0f }, tex);
            Renderer2D::DrawRotatedQuad(grid.Center(2), { 3.0f, 3.0f }, 0.0f, tex);
            Renderer2D::DrawRotatedQuad(grid.Center(3), { 3.0f, 3.0f }, 0.0f, white);
            Renderer2D::PopRenderPass();
            const Renderer2D::Statistics st = stats.Get();
            CHECK(st.DrawCalls == 2);
            Image img;
            REQUIRE(Capture(grid.Fbo, img));
            CHECK(Near(grid.Probe(img, 0), cyanU8));
            for (uint32_t i = 1; i <= 3; ++i)
                CHECK_MESSAGE(Near(grid.Probe(img, i), whiteU8), "quad ", i, ": ", Describe(grid.Probe(img, i)));
        }

        SUBCASE("alternating flat / material / flat / material: one draw per switch")
        {
            stats.Reset();
            grid.Begin();
            Renderer2D::DrawQuad(grid.Center(0), { 3.0f, 3.0f }, white);
            Renderer2D::DrawQuad(grid.Center(1), { 3.0f, 3.0f }, magenta);
            Renderer2D::DrawQuad(grid.Center(2), { 3.0f, 3.0f }, white);
            Renderer2D::DrawQuad(grid.Center(3), { 3.0f, 3.0f }, magenta);
            Renderer2D::PopRenderPass();
            CHECK(stats.Get().DrawCalls == 4);
            Image img;
            REQUIRE(Capture(grid.Fbo, img));
            CHECK(Near(grid.Probe(img, 0), whiteU8));
            CHECK(Near(grid.Probe(img, 1), magentaU8));
            CHECK(Near(grid.Probe(img, 2), whiteU8));
            CHECK(Near(grid.Probe(img, 3), magentaU8));
        }

        SUBCASE("two materials on the SAME shader: material identity breaks the batch, uniforms do not leak")
        {
            stats.Reset();
            grid.Begin();
            Renderer2D::DrawQuad(grid.Center(0), { 3.0f, 3.0f }, magenta);
            Renderer2D::DrawQuad(grid.Center(1), { 3.0f, 3.0f }, magenta);   // same material: same batch
            Renderer2D::DrawQuad(grid.Center(2), { 3.0f, 3.0f }, cyan);
            Renderer2D::DrawQuad(grid.Center(3), { 3.0f, 3.0f }, magenta);
            Renderer2D::PopRenderPass();
            CHECK(stats.Get().DrawCalls == 3);
            Image img;
            REQUIRE(Capture(grid.Fbo, img));
            CHECK(Near(grid.Probe(img, 0), magentaU8));
            CHECK(Near(grid.Probe(img, 1), magentaU8));
            CHECK(Near(grid.Probe(img, 2), cyanU8));
            CHECK(Near(grid.Probe(img, 3), magentaU8));
        }

        SUBCASE("custom circle shader: default / custom / default circles are three batches, tint stays put")
        {
            Ref<Shader> tintCircle = Shader::Create(FixturePath("wo08_tint_circle.glsl"));
            REQUIRE(tintCircle != nullptr);
            tintCircle->Bind();
            tintCircle->SetFloat4("u_Tint", { 1.0f, 0.0f, 1.0f, 1.0f });

            stats.Reset();
            grid.Begin();
            Renderer2D::DrawCircle(grid.Center(0), { 4.0f, 4.0f }, white, 1.0f, 0.005f);
            Renderer2D::DrawCircle(grid.Center(1), { 4.0f, 4.0f }, white, 1.0f, 0.005f, tintCircle);
            Renderer2D::DrawCircle(grid.Center(2), { 4.0f, 4.0f }, white, 1.0f, 0.005f, tintCircle);   // same custom: same batch
            Renderer2D::DrawCircle(grid.Center(3), { 4.0f, 4.0f }, white, 1.0f, 0.005f);
            Renderer2D::DrawCircle(grid.Center(4), { 4.0f, 4.0f }, white, 1.0f, 0.005f, nullptr);   // null == default
            Renderer2D::PopRenderPass();
            const Renderer2D::Statistics st = stats.Get();
            CHECK(st.DrawCalls == 3);
            CHECK(st.CircleCount == 5);
            Image img;
            REQUIRE(Capture(grid.Fbo, img));
            CHECK(Near(grid.Probe(img, 0), whiteU8));
            CHECK(Near(grid.Probe(img, 1), magentaU8));
            CHECK(Near(grid.Probe(img, 2), magentaU8));
            CHECK(Near(grid.Probe(img, 3), whiteU8));
            CHECK(Near(grid.Probe(img, 4), whiteU8));
        }

        SUBCASE("a fresh render pass always starts on the default material and default circle shader")
        {
            Ref<Shader> tintCircle = Shader::Create(FixturePath("wo08_tint_circle.glsl"));
            REQUIRE(tintCircle != nullptr);
            tintCircle->Bind();
            tintCircle->SetFloat4("u_Tint", { 1.0f, 0.0f, 1.0f, 1.0f });

            grid.Begin();
            Renderer2D::DrawQuad(grid.Center(0), { 3.0f, 3.0f }, magenta);
            Renderer2D::DrawCircle(grid.Center(1), { 4.0f, 4.0f }, white, 1.0f, 0.005f, tintCircle);
            Renderer2D::PopRenderPass();

            stats.Reset();
            grid.Begin();
            Renderer2D::DrawQuad(grid.Center(2), { 3.0f, 3.0f }, white);
            Renderer2D::DrawCircle(grid.Center(3), { 4.0f, 4.0f }, white, 1.0f, 0.005f);
            Renderer2D::PopRenderPass();
            CHECK(stats.Get().DrawCalls == 2);   // one quad batch + one circle batch, no spurious break
            CHECK(stats.Get().Flushes == 1);
            Image img;
            REQUIRE(Capture(grid.Fbo, img));
            CHECK(Near(grid.Probe(img, 2), whiteU8));
            CHECK(Near(grid.Probe(img, 3), whiteU8));
        }
    }

    TEST_CASE("R02 mixed calls — interleaved quads/lines/circles/glyphs in one pass, and an instanced call mid-batch")
    {
        Ref<Font> font = Font::Create(kPinnedFontPath, kPinnedFontAtlasPx);
        REQUIRE(font != nullptr);
        const Glyph* g = font->GetGlyph('I');
        REQUIRE(g != nullptr);

        Grid grid(512, 16, 32);   // 16-px cells so glyphs fit; 32 x 16 cells
        const glm::vec2 boxCenter{ g->offset.x + g->size.x * 0.5f, g->offset.y - g->size.y * 0.5f };
        const glm::vec2 scale{ 50.0f, 12.0f };
        auto glyphTransform = [&](const glm::vec2& c)
        {
            const glm::vec2 t = c - boxCenter * scale;
            return glm::translate(glm::mat4(1.0f), { t.x, t.y, 0.0f }) * glm::scale(glm::mat4(1.0f), { scale.x, scale.y, 1.0f });
        };
        StatsScope stats;

        SUBCASE("100 of each kind interleaved: one flush, one draw per pipeline, every cell correct")
        {
            // Lines share the pixel camera (world == pixel) with everything else
            // here, so they cannot use the dash-safe mapping of the dedicated line
            // case: they run through the pixel CENTRES of their row (y + 0.5 — an
            // integer y would sit on the boundary between two rows and rasterize
            // into either), where 20*x mod 1 == 0 keeps the fragment, and their
            // cells are verified by "ink of the encoded colour present on that row".
            stats.Reset();
            grid.Begin();
            for (uint32_t k = 0; k < 100; ++k)
            {
                const uint32_t q = k * 4 + 0, l = k * 4 + 1, c = k * 4 + 2, t = k * 4 + 3;
                Renderer2D::DrawQuad(grid.Center(q), { 12.0f, 12.0f }, EncodeIndex(q));
                const glm::vec2 lc = grid.Center(l);
                Renderer2D::DrawLine({ lc.x - 6.0f, lc.y + 0.5f, 0.0f }, { lc.x + 6.0f, lc.y + 0.5f, 0.0f }, EncodeIndex(l));
                Renderer2D::DrawCircle(grid.Center(c), { 14.0f, 14.0f }, EncodeIndex(c), 1.0f, 0.005f);
                Renderer2D::DrawString("I", font, glyphTransform(grid.Center(t)), EncodeIndex(t));
            }
            Renderer2D::PopRenderPass();

            const Renderer2D::Statistics st = stats.Get();
            CHECK(st.Flushes == 1);
            CHECK(st.DrawCalls == 4);
            CHECK(st.QuadCount == 200);   // 100 quads + 100 glyphs (historical accounting)
            CHECK(st.GlyphCount == 100);
            CHECK(st.CircleCount == 100);
            CHECK(st.LineCount == 100);

            Image img;
            REQUIRE(Capture(grid.Fbo, img));
            WriteEvidence("r02-mixed", img);
            for (uint32_t k = 0; k < 100; ++k)
            {
                const uint32_t q = k * 4 + 0, l = k * 4 + 1, c = k * 4 + 2, t = k * 4 + 3;
                CHECK_MESSAGE(DecodeIndex(grid.Probe(img, q)) == (int64_t)q, "quad cell ", q);
                CHECK_MESSAGE(DecodeIndex(grid.Probe(img, c)) == (int64_t)c, "circle cell ", c);
                CHECK_MESSAGE(DecodeIndex(grid.Probe(img, t)) == (int64_t)t, "glyph cell ", t);
                // Dashed line: some pixel on its row carries the encoded colour.
                const uint32_t col = l % grid.Cols, row = l / grid.Cols;
                const uint32_t yImg = grid.H - 1 - (row * grid.Cell + grid.Cell / 2);
                CHECK_MESSAGE(CountColor(img, col * grid.Cell, yImg, grid.Cell, 1, EncodeIndexU8(l), 2) > 0, "line cell ", l, " has no ink");
            }
            for (uint32_t i = 400; i < 512; ++i)
                CHECK(DecodeIndex(grid.Probe(img, i)) == -1);
        }

        SUBCASE("an instanced call in the middle of a quad batch flushes what came before and leaves what comes after intact")
        {
            std::vector<Renderer2D::InstanceQuadData> inst;
            for (uint32_t i = 10; i < 15; ++i)
            {
                Renderer2D::InstanceQuadData d;
                d.Position = { grid.Center(i).x, grid.Center(i).y, 0.0f };
                d.Scale = { 12.0f, 12.0f };
                d.Color = EncodeIndex(i);
                d.TexCoordOffset = { 0.0f, 0.0f }; d.TexCoordScale = { 1.0f, 1.0f };
                d.TexIndex = 0.0f; d.TilingFactor = 1.0f;
                inst.push_back(d);
            }
            stats.Reset();
            grid.Begin();
            for (uint32_t i = 0; i < 10; ++i) Renderer2D::DrawQuad(grid.Center(i), { 12.0f, 12.0f }, EncodeIndex(i));
            Renderer2D::DrawInstancedQuads(inst.data(), (uint32_t)inst.size());
            for (uint32_t i = 15; i < 25; ++i) Renderer2D::DrawQuad(grid.Center(i), { 12.0f, 12.0f }, EncodeIndex(i));
            Renderer2D::PopRenderPass();

            const Renderer2D::Statistics st = stats.Get();
            CHECK(st.DrawCalls == 3);
            CHECK(st.InstanceDrawCalls == 1);
            CHECK(st.InstanceCount == 5);
            CHECK(st.Flushes == 2);
            CHECK(st.QuadCount == 25);

            Image img;
            REQUIRE(Capture(grid.Fbo, img));
            grid.Verify(img, 25, 512, "quads around an instanced call");
        }
    }
}
