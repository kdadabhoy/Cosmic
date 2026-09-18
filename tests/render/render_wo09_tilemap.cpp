// render_wo09_tilemap.cpp — WO-09 (2D stability) C02, the GPU half: the tilemap
// cell walk inside Scene::OnRenderSprites on a real target. No new golden (the
// `tilemap` golden pins the reference frame); every count here is checked
// against the INDEPENDENT visible-cell oracle in tests/wo09_tilemap_oracle.h
// (brute force over the grid from the camera's own rectangle), and every
// pixel against the atlas palette.
//
//   maximum map   the 1,024 x 1,024 = 1,048,576-cell map: build time, and the
//                 walked / drawn cell counts at several zooms, the camera fully
//                 outside (0), and an extreme zoom-out (every non-empty cell, in
//                 exactly ceil(n / 10,000) batched draw calls — bounded, not a
//                 million draws);
//   camera edge   a partial-cover camera with per-cell sentinels: every cell the
//                 oracle calls visible shows its tile colour at its centre, no
//                 ink where the map has no cell;
//   edges         tile ids past the atlas range, Columns = 0 auto-derive, a null
//                 texture / zero tile size (skipped, no crash), and the batch
//                 rollover across the 10,000-quad seam with a cell on each side.

#include "wo08_common.h"
#include "../wo09_tilemap_oracle.h"

#include "scene/Components.h"
#include "scene/Entity.h"
#include "scene/Scene.h"

#include <doctest.h>

#include <chrono>
#include <functional>
#include <cmath>
#include <string>
#include <vector>

using namespace Cosmic;
using namespace CosmicRender;
using namespace Wo08;

namespace
{
    // The engine maps tile id v (>0) to atlas index v-1 with row 0 at the TOP of the
    // image; MakeAtlas4x4 fills GL row r (bottom = 0) with palette[r*4 + c]. So an
    // engine index (row t, col c) samples GL row 3 - t.
    glm::u8vec4 ExpectedTile(uint16_t v)
    {
        const int idx = (int)v - 1;
        const int t = (idx / 4) % 4, c = idx % 4;
        return AtlasPalette()[(3 - t) * 4 + c];
    }

    struct MapScene
    {
        Ref<Scene> ScenePtr;
        Ref<Texture2D> Atlas;
        TilemapComponent* Map = nullptr;
    };

    MapScene MakeMap(int w, int h, glm::vec3 origin, const std::function<uint16_t(int, int)>& fill, int columns = 4)
    {
        MapScene m;
        m.ScenePtr = Scene::Create();
        m.Atlas = MakeAtlas4x4(16);
        Entity e = m.ScenePtr->CreateEntity("map");
        e.GetComponent<TransformComponent>().Position = origin;
        auto& tm = e.AddComponent<TilemapComponent>();
        tm.TileW = 16; tm.TileH = 16; tm.Columns = columns;
        tm.GridW = w; tm.GridH = h;
        tm.EnsureCells();
        tm.Resolved = m.Atlas; tm.ResolvedPath = "";
        for (int y = 0; y < tm.GridH; ++y)
            for (int x = 0; x < tm.GridW; ++x)
                tm.Cells[(size_t)y * tm.GridW + x] = fill(x, y);
        m.Map = &tm;
        return m;
    }

    // Render the map with a world-unit camera (centre, half extents) into `fbo`.
    Renderer2D::Statistics RenderMap(Scene& s, const Ref<FrameBuffer>& fbo, glm::vec2 center, float halfW, float halfH, Image* out = nullptr)
    {
        StatsScope stats;
        BeginFrame(fbo);
        const glm::mat4 proj = glm::ortho(-halfW, halfW, -halfH, halfH, -100.0f, 100.0f);
        const glm::mat4 view = glm::translate(glm::mat4(1.0f), { -center.x, -center.y, 0.0f });
        s.OnRenderSprites(proj * view, fbo->GetWidth(), fbo->GetHeight());
        if (out) REQUIRE(Capture(fbo, *out));
        return stats.Get();
    }
}

TEST_SUITE("WO-09 C02")
{
    TEST_CASE("C02 GPU: the 1,048,576-cell map — walked and drawn cell counts equal the oracle at every zoom; the extreme zoom-out is bounded")
    {
        const uint32_t W = 320, H = 180;
        Ref<FrameBuffer> fbo = MakeRgba8Target(W, H);
        REQUIRE(fbo != nullptr);
        const auto t0 = std::chrono::steady_clock::now();
        MapScene m = MakeMap(1024, 1024, { 0, 0, 0 }, [](int x, int y) { return ((x + y) % 4 == 3) ? (uint16_t)0 : (uint16_t)(1 + (x + y) % 15); });
        const double buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        MESSAGE("C02 GPU: 1024x1024 map built in " << buildMs << " ms");
        REQUIRE(m.Map->Cells.size() == 1048576u);

        struct View { const char* Label; glm::vec2 Center; float HalfW, HalfH; };
        const View views[] = {
            { "10x5.6 cells",       { 100.5f, 100.5f },   5.0f * (float)W / H, 5.0f },
            { "200x112 cells",      { 512.3f, 511.7f }, 100.0f * (float)W / H, 100.0f },
            { "corner overlap",     { 2.25f, 1023.75f },  8.0f * (float)W / H, 8.0f },
            { "whole map + margin", { 512.0f, 512.0f }, 700.0f * (float)W / H, 700.0f },
            { "fully outside",      { -5000.0f, 40.0f },  8.0f * (float)W / H, 8.0f },
            { "just outside right", { 1024.0f + 20.0f + 0.5f, 500.0f }, 20.0f, 20.0f },
            { "extreme zoom-out",   { 512.0f, 512.0f }, 100000.0f, 100000.0f },
        };
        for (const View& v : views)
        {
            const Wo09Tilemap::Counts oracle = Wo09Tilemap::Count(m.Map->Cells, 1024, 1024, 0.0, 0.0,
                Wo09Tilemap::OrthoRect(v.Center.x, v.Center.y, v.HalfW, v.HalfH));
            const auto t1 = std::chrono::steady_clock::now();
            const Renderer2D::Statistics st = RenderMap(*m.ScenePtr, fbo, v.Center, v.HalfW, v.HalfH);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
            const uint32_t expectDraws = st.QuadCount == 0 ? 0u : (uint32_t)((st.QuadCount + 9999) / 10000);
            MESSAGE("C02 GPU view '" << std::string(v.Label) << "': oracle strict=" << oracle.StrictNonZero << " enclosed=" << oracle.EnclosedNonZero
                    << " drawn quads=" << st.QuadCount << " draws=" << st.DrawCalls << " (" << ms << " ms)");
            // Every view here has FRACTIONAL edges, so the inverse-projection rounding
            // cannot move a floor()/ceil() — the documented walk must match exactly.
            CHECK_MESSAGE(st.QuadCount == oracle.EnclosedNonZero, std::string(v.Label));
            CHECK_MESSAGE(st.QuadCount >= oracle.StrictNonZero, std::string(v.Label));   // never fewer than what is visible
            CHECK_MESSAGE(st.DrawCalls == expectDraws, std::string(v.Label));            // batched: ceil(n / 10,000)
        }
        // The extreme zoom-out draws every one of the 786,432 non-empty cells in 79
        // draw calls — the walk is bounded by the map, and the draw count by the batch.
        const Renderer2D::Statistics all = RenderMap(*m.ScenePtr, fbo, { 512.0f, 512.0f }, 100000.0f, 100000.0f);
        CHECK(all.QuadCount == 786432u);
        CHECK(all.DrawCalls == 79u);
    }

    TEST_CASE("C02 GPU: partial-cover camera edge with per-cell sentinels — visible cells show their tile, nothing outside the map")
    {
        // 8 px per cell: a 40x22.5-cell window; the map's bottom-left corner sits
        // 3.5 cells right of / 2.5 cells above the window's bottom-left, so the map
        // runs off the top and right edges and the empty margin shows the clear.
        const uint32_t W = 320, H = 180;
        Ref<FrameBuffer> fbo = MakeRgba8Target(W, H);
        REQUIRE(fbo != nullptr);
        MapScene m = MakeMap(64, 64, { 3.5f, 2.5f, 0.0f }, [](int x, int y) { return ((x * 7 + y * 3) % 9 == 0) ? (uint16_t)0 : (uint16_t)(1 + (x + 3 * y) % 16); });
        const float halfW = (float)W / 16.0f, halfH = (float)H / 16.0f;   // 20 x 11.25 world units
        const glm::vec2 center{ halfW, halfH };                             // world (0,0) at the bottom-left pixel
        Image frame;
        const Renderer2D::Statistics st = RenderMap(*m.ScenePtr, fbo, center, halfW, halfH, &frame);
        WriteEvidence("c02-camera-edge", frame);
        // The camera's top edge (world y = 22.5) minus the map origin (2.5) is EXACTLY 20:
        // the engine derives its rectangle from the inverse view-projection, whose last-
        // bit noise can push ceil(20.0000019) to 21 — one extra (empty, off-screen) row.
        // So the count is bracketed: at least the exact enclosed walk, at most the walk
        // over a rectangle widened by 1e-3 world units (one row / column more).
        const Wo09Tilemap::Counts oracle = Wo09Tilemap::Count(m.Map->Cells, 64, 64, 3.5, 2.5, Wo09Tilemap::OrthoRect(center.x, center.y, halfW, halfH));
        const Wo09Tilemap::Counts widened = Wo09Tilemap::Count(m.Map->Cells, 64, 64, 3.5, 2.5, Wo09Tilemap::OrthoRect(center.x, center.y, halfW + 1e-3, halfH + 1e-3));
        MESSAGE("C02 GPU camera edge: oracle strict=" << oracle.StrictNonZero << " enclosed=" << oracle.EnclosedNonZero << " widened=" << widened.EnclosedNonZero << " drawn=" << st.QuadCount);
        CHECK(st.QuadCount >= oracle.StrictNonZero);
        CHECK(st.QuadCount >= oracle.EnclosedNonZero);
        CHECK(st.QuadCount <= widened.EnclosedNonZero);
        // Every cell whose 8x8 pixel square lies fully inside the target: its centre
        // pixel is the tile's colour (or clear for an empty cell).
        int checked = 0, wrong = 0;
        for (int cy = 0; cy < 64; ++cy)
            for (int cx = 0; cx < 64; ++cx)
            {
                const float wx = 3.5f + cx + 0.5f, wy = 2.5f + cy + 0.5f;     // cell centre, world
                const float px = wx * 8.0f, py = wy * 8.0f;                    // -> GL pixel
                if (px - 4.0f < 0.0f || px + 4.0f > (float)W || py - 4.0f < 0.0f || py + 4.0f > (float)H) continue;
                const uint16_t v = m.Map->At(cx, cy);
                const glm::u8vec4 got = PixelAtGl(frame, (uint32_t)px, (uint32_t)py);
                const glm::u8vec4 want = v ? ExpectedTile(v) : kClearU8;
                ++checked;
                if (!Near(got, want, 2)) ++wrong;
            }
        CHECK(checked > 500);
        CHECK_MESSAGE(wrong == 0, wrong << " of " << checked << " cell centres show the wrong colour");
        // The margin left of / below the map (world x < 3.5 or y < 2.5) is clear.
        CHECK(CountInk(frame, 0, 0, 27, H, kClearU8) == 0);          // x < 3.5 cells = 28 px
        CHECK(CountInk(frame, 0, H - 19, W, 19, kClearU8) == 0);     // y < 2.5 cells = 20 px (image rows from the bottom)
    }

    TEST_CASE("C02 GPU: tile ids past the atlas range, Columns = 0 auto-derive, null texture / zero tile size, and the 10,000-quad batch seam")
    {
        const uint32_t W = 320, H = 180;
        Ref<FrameBuffer> fbo = MakeRgba8Target(W, H);
        REQUIRE(fbo != nullptr);

        // (a) ids 17, 255, 65535 on a 16-tile atlas: drawn without a crash (the UVs
        //     wrap through the atlas's REPEAT sampling — pinned as "some atlas texel").
        {
            MapScene m = MakeMap(3, 1, { 0, 0, 0 }, [](int x, int) { return (uint16_t)(x == 0 ? 17 : (x == 1 ? 255 : 65535)); });
            Image frame;
            const Renderer2D::Statistics st = RenderMap(*m.ScenePtr, fbo, { 1.5f, 0.5f }, 1.5f * (float)W / H * 0.5f, 0.75f, &frame);
            CHECK(st.QuadCount == 3);
            CHECK(CountInk(frame, 0, 0, W, H, kClearU8) > 0);
            WriteEvidence("c02-ids-past-atlas", frame);
        }
        // (b) Columns = 0 derives texW / TileW = 4: pixel-identical to Columns = 4.
        {
            MapScene a = MakeMap(8, 4, { 0, 0, 0 }, [](int x, int y) { return (uint16_t)(1 + (x + y) % 16); }, 4);
            MapScene b = MakeMap(8, 4, { 0, 0, 0 }, [](int x, int y) { return (uint16_t)(1 + (x + y) % 16); }, 0);
            Image fa, fb;
            RenderMap(*a.ScenePtr, fbo, { 4.0f, 2.0f }, 4.0f * (float)W / H * 0.5f, 2.0f, &fa);
            RenderMap(*b.ScenePtr, fbo, { 4.0f, 2.0f }, 4.0f * (float)W / H * 0.5f, 2.0f, &fb);
            CHECK(BytesEqual(fa, fb));
            CHECK(CountInk(fa, 0, 0, W, H, kClearU8) > 1000);
        }
        // (c) a null texture, and a zero tile size: the map is skipped — no draw, no crash.
        {
            MapScene m = MakeMap(8, 8, { 0, 0, 0 }, [](int, int) { return (uint16_t)1; });
            m.Map->Resolved = nullptr; m.Map->ResolvedPath = ""; m.Map->TilesetPath = "";
            Image frame;
            Renderer2D::Statistics st = RenderMap(*m.ScenePtr, fbo, { 4.0f, 4.0f }, 8.0f, 4.5f, &frame);
            CHECK(st.QuadCount == 0);
            CHECK(CountInk(frame, 0, 0, W, H, kClearU8) == 0);
            m.Map->Resolved = m.Atlas; m.Map->TileW = 0;
            st = RenderMap(*m.ScenePtr, fbo, { 4.0f, 4.0f }, 8.0f, 4.5f, &frame);
            CHECK(st.QuadCount == 0);
            m.Map->TileW = 16; m.Map->TileH = -16;
            st = RenderMap(*m.ScenePtr, fbo, { 4.0f, 4.0f }, 8.0f, 4.5f, &frame);
            CHECK(st.QuadCount == 0);
        }
        // (d) the batch seam: a 101x100 map (10,100 non-empty cells) fully in view ->
        //     10,100 quads in exactly 2 draw calls; the cells on both sides of the
        //     seam (cell 9,999 and cell 10,000 in walk order) are painted.
        {
            MapScene m = MakeMap(101, 100, { 0, 0, 0 }, [](int, int) { return (uint16_t)5; });
            Ref<FrameBuffer> big = MakeRgba8Target(1010, 1000);        // 10 px per cell
            REQUIRE(big != nullptr);
            Image frame;
            const Renderer2D::Statistics st = RenderMap(*m.ScenePtr, big, { 50.5f, 50.0f }, 50.5f, 50.0f, &frame);
            CHECK(st.QuadCount == 10100u);
            CHECK(st.DrawCalls == 2u);
            const glm::u8vec4 tile = ExpectedTile(5);
            // walk order is row-major from the bottom: cell k = (x = k % 101, y = k / 101)
            for (uint32_t k : { 0u, 9999u, 10000u, 10099u })
            {
                const uint32_t cx = k % 101, cy = k / 101;
                CHECK(Near(PixelAtGl(frame, cx * 10 + 5, cy * 10 + 5), tile, 2));
            }
        }
    }
}
