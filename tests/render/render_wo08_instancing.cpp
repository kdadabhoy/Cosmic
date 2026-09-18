// render_wo08_instancing.cpp — WO-08 R03: the two REAL 2D instanced pipelines.
//
// DrawInstancedCircles / DrawInstancedQuads at 0, 1, 19,999, 20,000, 20,001 and
// 40,001 instances (chunk ceiling MaxInstancedCircles == MaxInstancedQuads ==
// 20,000), on the default and a custom shader, with normal batched draws before
// and after. For an isolated nonempty homogeneous submission the renderer must
// issue exactly ceil(N / 20,000) instanced draws (10,000 => 1); every instance
// must land in its own grid cell with its own index colour (nothing dropped,
// duplicated or shifted across a chunk seam); and the batch state after the
// call must be the default one again.
//
// instancing2d.png is a NEW 2D golden: tests/render/goldens/instancing.png is
// the 3D InstanceSet golden (render_3d.cpp) and covers none of this.

#include "wo08_common.h"

#include "graphics/Shader.h"

using namespace Wo08;

namespace
{
    constexpr uint32_t kChunk = 20000;
    const uint32_t kCounts[] = { 0, 1, kChunk - 1, kChunk, kChunk + 1, 2 * kChunk + 1, 10000 };

    uint32_t ExpectedChunks(uint32_t n) { return (n + kChunk - 1) / kChunk; }

    struct Grid
    {
        uint32_t Cell = 4, Cols = 201, Rows = 0, W = 0, H = 0;
        Ref<FrameBuffer> Fbo;

        explicit Grid(uint32_t capacity)
        {
            Rows = (capacity + Cols - 1) / Cols;
            W = Cols * Cell;
            H = Rows * Cell;
            Fbo = MakeRgba8Target(W, H);
            REQUIRE(Fbo != nullptr);
        }
        glm::vec3 Center(uint32_t i) const
        {
            return { (float)((i % Cols) * Cell) + Cell * 0.5f, (float)((i / Cols) * Cell) + Cell * 0.5f, 0.0f };
        }
        glm::u8vec4 Probe(const Image& img, uint32_t i) const
        {
            return PixelAtGl(img, (i % Cols) * Cell + Cell / 2, (i / Cols) * Cell + Cell / 2);
        }
        void Begin() const
        {
            BeginFrame(Fbo);
            Renderer2D::PushRenderPass(PixelOrtho(W, H), { 0.0f, 0.0f, (float)W, (float)H });
        }
        void Verify(const Image& img, uint32_t first, uint32_t n, uint32_t capacity, const char* what) const
        {
            uint32_t missing = 0, wrong = 0, extra = 0;
            int64_t firstBad = -1;
            for (uint32_t i = 0; i < capacity; ++i)
            {
                const int64_t got = DecodeIndex(Probe(img, i));
                const bool expected = i >= first && i < first + n;
                if (expected)
                {
                    if (got == -1) { ++missing; if (firstBad < 0) firstBad = i; }
                    else if (got != (int64_t)i) { ++wrong; if (firstBad < 0) firstBad = i; }
                }
                else if (got != -1) { ++extra; if (firstBad < 0) firstBad = i; }
            }
            CHECK_MESSAGE(missing == 0, what, " N=", n, ": ", missing, " instance(s) DROPPED (first at ", firstBad, ")");
            CHECK_MESSAGE(wrong == 0, what, " N=", n, ": ", wrong, " cell(s) carry the WRONG index (first at ", firstBad, ")");
            CHECK_MESSAGE(extra == 0, what, " N=", n, ": ", extra, " cell(s) painted outside the submission (first at ", firstBad, ")");
        }
    };

    std::vector<Renderer2D::InstanceCircleData> MakeCircles(const Grid& g, uint32_t first, uint32_t n)
    {
        std::vector<Renderer2D::InstanceCircleData> v(n);
        for (uint32_t k = 0; k < n; ++k)
        {
            v[k].Position  = g.Center(first + k);
            v[k].Scale     = { 4.0f, 4.0f };
            v[k].Color     = EncodeIndex(first + k);
            v[k].Thickness = 1.0f;
            v[k].Fade      = 0.005f;
        }
        return v;
    }

    std::vector<Renderer2D::InstanceQuadData> MakeQuads(const Grid& g, uint32_t first, uint32_t n)
    {
        std::vector<Renderer2D::InstanceQuadData> v(n);
        for (uint32_t k = 0; k < n; ++k)
        {
            v[k].Position       = g.Center(first + k);
            v[k].Scale          = { 3.0f, 3.0f };
            v[k].Color          = EncodeIndex(first + k);
            v[k].TexCoordOffset = { 0.0f, 0.0f };
            v[k].TexCoordScale  = { 1.0f, 1.0f };
            v[k].TexIndex       = 0.0f;
            v[k].TilingFactor   = 1.0f;
        }
        return v;
    }

    Ref<Shader> LoadTinted(const char* fixture, const glm::vec4& tint)
    {
        Ref<Shader> s = Shader::Create(FixturePath(fixture));
        REQUIRE(s != nullptr);
        s->Bind();
        s->SetFloat4("u_Tint", tint);
        return s;
    }
}

TEST_SUITE("WO-08 R03")
{
    TEST_CASE("R03 instanced circles — 0/1/19999/20000/20001/40001 (+10000): ceil(N/20000) instance draws, every instance in its cell")
    {
        Grid grid(2 * kChunk + 1);
        StatsScope stats;
        for (uint32_t n : kCounts)
        {
            const std::vector<Renderer2D::InstanceCircleData> data = MakeCircles(grid, 0, n);
            stats.Reset();
            grid.Begin();
            Renderer2D::DrawInstancedCircles(data.data(), n);
            Renderer2D::PopRenderPass();

            const Renderer2D::Statistics st = stats.Get();
            CHECK_MESSAGE(st.InstanceDrawCalls == ExpectedChunks(n), "N=", n, " instance draws=", st.InstanceDrawCalls);
            CHECK_MESSAGE(st.InstanceCount == n, "N=", n);
            CHECK_MESSAGE(st.DrawCalls == ExpectedChunks(n), "N=", n, " total draws=", st.DrawCalls);   // nothing else drawn
            CHECK(st.CircleCount == n);
            CHECK(st.QuadCount == 0);

            Image img;
            REQUIRE(Capture(grid.Fbo, img));
            grid.Verify(img, 0, n, 2 * kChunk + 1, "instanced circles");
            if (n == kChunk + 1) WriteEvidence("r03-circles-20001", img);
        }
        // A null pointer with a nonzero count is a no-op, not a crash.
        stats.Reset();
        grid.Begin();
        Renderer2D::DrawInstancedCircles(nullptr, 5);
        Renderer2D::PopRenderPass();
        CHECK(stats.Get().InstanceDrawCalls == 0);
    }

    TEST_CASE("R03 instanced quads — 0/1/19999/20000/20001/40001 (+10000): ceil(N/20000) instance draws, every instance in its cell")
    {
        Grid grid(2 * kChunk + 1);
        StatsScope stats;
        for (uint32_t n : kCounts)
        {
            const std::vector<Renderer2D::InstanceQuadData> data = MakeQuads(grid, 0, n);
            stats.Reset();
            grid.Begin();
            Renderer2D::DrawInstancedQuads(data.data(), n);
            Renderer2D::PopRenderPass();

            const Renderer2D::Statistics st = stats.Get();
            CHECK_MESSAGE(st.InstanceDrawCalls == ExpectedChunks(n), "N=", n, " instance draws=", st.InstanceDrawCalls);
            CHECK_MESSAGE(st.InstanceCount == n, "N=", n);
            CHECK_MESSAGE(st.DrawCalls == ExpectedChunks(n), "N=", n, " total draws=", st.DrawCalls);
            CHECK(st.QuadCount == n);
            CHECK(st.CircleCount == 0);

            Image img;
            REQUIRE(Capture(grid.Fbo, img));
            grid.Verify(img, 0, n, 2 * kChunk + 1, "instanced quads");
            if (n == kChunk + 1) WriteEvidence("r03-quads-20001", img);
        }
        stats.Reset();
        grid.Begin();
        Renderer2D::DrawInstancedQuads(nullptr, 5);
        Renderer2D::PopRenderPass();
        CHECK(stats.Get().InstanceDrawCalls == 0);
    }

    TEST_CASE("R03 custom shader paths — tinted instanced circles/quads, and the default path is untouched afterwards")
    {
        Ref<Shader> tintCircles = LoadTinted("wo08_tint_circle_instance.glsl", { 1.0f, 0.0f, 1.0f, 1.0f });
        Ref<Shader> tintQuads   = LoadTinted("wo08_tint_quad_instance.glsl",   { 0.0f, 1.0f, 1.0f, 1.0f });
        const glm::u8vec4 magenta{ 255, 0, 255, 255 }, cyan{ 0, 255, 255, 255 }, white{ 255, 255, 255, 255 };

        Grid grid(2 * kChunk + 1);
        StatsScope stats;

        for (uint32_t n : { 1u, kChunk, kChunk + 1 })
        {
            // Custom circle shader, white instances -> magenta on screen, same chunking.
            std::vector<Renderer2D::InstanceCircleData> circles = MakeCircles(grid, 0, n);
            for (auto& c : circles) c.Color = { 1.0f, 1.0f, 1.0f, 1.0f };
            stats.Reset();
            grid.Begin();
            Renderer2D::DrawInstancedCircles(circles.data(), n, tintCircles);
            Renderer2D::PopRenderPass();
            CHECK_MESSAGE(stats.Get().InstanceDrawCalls == ExpectedChunks(n), "custom circles N=", n);
            Image img;
            REQUIRE(Capture(grid.Fbo, img));
            CHECK(Near(grid.Probe(img, 0), magenta));
            CHECK(Near(grid.Probe(img, n - 1), magenta));
            if (n < 2 * kChunk + 1) CHECK(Near(grid.Probe(img, n), kClearU8));

            // Custom quad shader, white instances -> cyan.
            std::vector<Renderer2D::InstanceQuadData> quads = MakeQuads(grid, 0, n);
            for (auto& q : quads) q.Color = { 1.0f, 1.0f, 1.0f, 1.0f };
            stats.Reset();
            grid.Begin();
            Renderer2D::DrawInstancedQuads(quads.data(), n, tintQuads);
            Renderer2D::PopRenderPass();
            CHECK_MESSAGE(stats.Get().InstanceDrawCalls == ExpectedChunks(n), "custom quads N=", n);
            REQUIRE(Capture(grid.Fbo, img));
            CHECK(Near(grid.Probe(img, 0), cyan));
            CHECK(Near(grid.Probe(img, n - 1), cyan));
            if (n < 2 * kChunk + 1) CHECK(Near(grid.Probe(img, n), kClearU8));
        }

        // After a custom-shader instanced call, the default instanced path and the
        // batched paths are back on their own shaders (no tint leaks).
        {
            std::vector<Renderer2D::InstanceCircleData> c1 = MakeCircles(grid, 0, 3);
            for (auto& c : c1) c.Color = { 1.0f, 1.0f, 1.0f, 1.0f };
            std::vector<Renderer2D::InstanceCircleData> c2 = MakeCircles(grid, 3, 3);
            for (auto& c : c2) c.Color = { 1.0f, 1.0f, 1.0f, 1.0f };
            std::vector<Renderer2D::InstanceQuadData> q1 = MakeQuads(grid, 6, 3);
            for (auto& q : q1) q.Color = { 1.0f, 1.0f, 1.0f, 1.0f };
            std::vector<Renderer2D::InstanceQuadData> q2 = MakeQuads(grid, 9, 3);
            for (auto& q : q2) q.Color = { 1.0f, 1.0f, 1.0f, 1.0f };

            stats.Reset();
            grid.Begin();
            Renderer2D::DrawInstancedCircles(c1.data(), 3, tintCircles);
            Renderer2D::DrawInstancedCircles(c2.data(), 3);                 // default again
            Renderer2D::DrawInstancedQuads(q1.data(), 3, tintQuads);
            Renderer2D::DrawInstancedQuads(q2.data(), 3);                   // default again
            Renderer2D::DrawCircle(glm::vec2(grid.Center(12)), { 4.0f, 4.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, 1.0f, 0.005f);   // batched, default
            Renderer2D::DrawQuad(glm::vec2(grid.Center(13)), { 3.0f, 3.0f }, { 1.0f, 1.0f, 1.0f, 1.0f });                    // batched, default
            Renderer2D::PopRenderPass();

            const Renderer2D::Statistics st = stats.Get();
            CHECK(st.InstanceDrawCalls == 4);
            CHECK(st.InstanceCount == 12);
            CHECK(st.DrawCalls == 6);   // 4 instanced + 1 circle batch + 1 quad batch
            Image img;
            REQUIRE(Capture(grid.Fbo, img));
            WriteEvidence("r03-shader-restoration", img);
            for (uint32_t i = 0; i < 3; ++i)  CHECK(Near(grid.Probe(img, i), magenta));
            for (uint32_t i = 3; i < 6; ++i)  CHECK(Near(grid.Probe(img, i), white));
            for (uint32_t i = 6; i < 9; ++i)  CHECK(Near(grid.Probe(img, i), cyan));
            for (uint32_t i = 9; i < 12; ++i) CHECK(Near(grid.Probe(img, i), white));
            CHECK(Near(grid.Probe(img, 12), white));
            CHECK(Near(grid.Probe(img, 13), white));
        }
    }

    TEST_CASE("R03 normal draws before and after — the instanced call flushes what precedes it and resets state for what follows")
    {
        Grid grid(2 * kChunk + 1);
        StatsScope stats;
        for (uint32_t n : { 1u, 10000u, kChunk + 1 })
        {
            const uint32_t before = 50, after = 50;
            const std::vector<Renderer2D::InstanceQuadData>   quads   = MakeQuads(grid, before, n);
            const std::vector<Renderer2D::InstanceCircleData> circles = MakeCircles(grid, before, n);

            for (int pipeline = 0; pipeline < 2; ++pipeline)
            {
                stats.Reset();
                grid.Begin();
                for (uint32_t i = 0; i < before; ++i)
                    Renderer2D::DrawQuad(glm::vec2(grid.Center(i)), { 3.0f, 3.0f }, EncodeIndex(i));
                for (uint32_t i = 0; i < before; ++i)   // a pending circle batch too
                    Renderer2D::DrawCircle(glm::vec2(grid.Center(i)), { 2.0f, 2.0f }, EncodeIndex(i), 1.0f, 0.005f);
                if (pipeline == 0) Renderer2D::DrawInstancedQuads(quads.data(), n);
                else               Renderer2D::DrawInstancedCircles(circles.data(), n);
                for (uint32_t i = 0; i < after; ++i)
                    Renderer2D::DrawQuad(glm::vec2(grid.Center(before + n + i)), { 3.0f, 3.0f }, EncodeIndex(before + n + i));
                for (uint32_t i = 0; i < after; ++i)
                    Renderer2D::DrawCircle(glm::vec2(grid.Center(before + n + i)), { 2.0f, 2.0f }, EncodeIndex(before + n + i), 1.0f, 0.005f);
                Renderer2D::PopRenderPass();

                const Renderer2D::Statistics st = stats.Get();
                CHECK_MESSAGE(st.InstanceDrawCalls == ExpectedChunks(n), (pipeline ? "circles" : "quads"), " N=", n);
                CHECK(st.Flushes == 2);                                   // the instanced call + the pop
                CHECK(st.DrawCalls == ExpectedChunks(n) + 4);             // quad+circle batch before, quad+circle batch after
                Image img;
                REQUIRE(Capture(grid.Fbo, img));
                grid.Verify(img, 0, before + n + after, 2 * kChunk + 1, pipeline ? "circles before/after" : "quads before/after");
            }
        }
    }

    TEST_CASE("R03 golden — instancing2d: instanced quads + circles with batched draws before and after (NEW 2D golden)")
    {
        // A designed 320x180 frame: a 24x12 rainbow of instanced quads through
        // the default QuadInstance shader, a 12x6 field of instanced SDF discs /
        // small discs / soft discs through CircleInstance, a batched backdrop
        // quad before and a batched rotated quad + circle after — so the golden
        // pins both instanced shaders AND the batch/instanced interleave.
        constexpr uint32_t kW = kGoldenWidth, kH = kGoldenHeight;
        const glm::vec2 center{ 0.0f, 0.0f };
        const float halfH = 5.0f;

        std::vector<Renderer2D::InstanceQuadData> quads;
        for (uint32_t y = 0; y < 12; ++y)
            for (uint32_t x = 0; x < 24; ++x)
            {
                Renderer2D::InstanceQuadData d;
                d.Position = { -8.4f + x * 0.62f, 4.3f - y * 0.62f, 0.0f };
                d.Scale    = { 0.5f, 0.5f };
                const float h = (float)(x + y * 24) / (24.0f * 12.0f);
                d.Color    = { 0.5f + 0.5f * std::cos(6.2832f * h), 0.5f + 0.5f * std::cos(6.2832f * (h + 0.333f)),
                               0.5f + 0.5f * std::cos(6.2832f * (h + 0.667f)), 1.0f };
                d.TexCoordOffset = { 0.0f, 0.0f }; d.TexCoordScale = { 1.0f, 1.0f };
                d.TexIndex = 0.0f; d.TilingFactor = 1.0f;
                quads.push_back(d);
            }
        std::vector<Renderer2D::InstanceCircleData> circles;
        for (uint32_t y = 0; y < 6; ++y)
            for (uint32_t x = 0; x < 12; ++x)
            {
                Renderer2D::InstanceCircleData c;
                c.Position  = { -7.4f + x * 1.24f, 3.0f - y * 1.24f, 0.0f };
                c.Scale     = { 1.0f, 1.0f };
                c.Color     = { 0.95f, 0.85f - 0.1f * y, 0.35f + 0.05f * x, 1.0f };
                c.Thickness = (x % 3 == 0) ? 1.0f : (x % 3 == 1) ? 0.3f : 0.6f;
                c.Fade      = (y % 2 == 0) ? 0.005f : 0.3f;
                circles.push_back(c);
            }

        Ref<FrameBuffer> fbo = MakeTarget();
        REQUIRE(fbo != nullptr);
        StatsScope stats;
        BeginFrame(fbo);
        Renderer2D::PushRenderPass(Ortho2DFor(kW, kH, center, halfH), { 0.0f, 0.0f, (float)kW, (float)kH });
        Renderer2D::DrawQuad(glm::vec2{ 0.0f, 0.0f }, { 17.0f, 9.6f }, { 0.16f, 0.18f, 0.26f, 1.0f });   // batched backdrop (before)
        Renderer2D::DrawInstancedQuads(quads.data(), (uint32_t)quads.size());
        Renderer2D::DrawInstancedCircles(circles.data(), (uint32_t)circles.size());
        Renderer2D::DrawRotatedQuad(glm::vec2{ 6.6f, -4.2f }, { 2.4f, 2.4f }, glm::radians(20.0f), { 0.95f, 0.95f, 0.98f, 0.85f });   // batched (after)
        Renderer2D::DrawCircle(glm::vec2{ -6.6f, -4.2f }, { 2.4f, 2.4f }, { 0.30f, 0.90f, 0.70f, 1.0f }, 1.0f, 0.02f);              // batched (after)
        Renderer2D::PopRenderPass();

        const Renderer2D::Statistics st = stats.Get();
        CHECK(st.InstanceDrawCalls == 2);
        CHECK(st.InstanceCount == 24 * 12 + 12 * 6);
        CHECK(st.DrawCalls == 5);   // backdrop batch, 2 instanced, quad batch + circle batch after

        Image frame;
        REQUIRE(Capture(fbo, frame));
        WriteEvidence("r03-instancing2d", frame);

        // Sentinels: an instanced quad centre carries its rainbow colour; a
        // full-disc instance centre carries its colour; a 0.3-thickness instance
        // at the centre follows the thickness contract asserted by R01.
        auto px = [&](const glm::vec2& w) { const glm::vec2 p = WorldToImagePx(kW, kH, center, halfH, w); return PixelAt(frame, (uint32_t)p.x, (uint32_t)p.y); };
        CHECK(Near(px({ -8.4f, 4.3f }), ToU8(quads[0].Color), 3));
        CHECK(Near(px({ -8.4f + 23 * 0.62f, 4.3f - 11 * 0.62f }), ToU8(quads[24 * 12 - 1].Color), 3));
        CHECK(Near(px({ -7.4f, 3.0f }), ToU8({ 0.95f, 0.85f, 0.35f, 1.0f }), 3));   // full-disc instance (0,0) centre
        CHECK(Near(px({ -6.6f, -4.2f }), ToU8({ 0.30f, 0.90f, 0.70f, 1.0f })));

        CHECK(CheckGolden("instancing2d", frame));
    }
}
