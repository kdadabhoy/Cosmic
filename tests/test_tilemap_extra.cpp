// test_tilemap_extra.cpp — tilemap edges (Phase 29 W2 / §9.2).
//
// Headless (no GL). test_tilemap.cpp already covers EnsureCells clamping, flood
// fill and a small serializer round-trip; this suite adds the edges the split
// could silently break: InBounds/At at every boundary, resize preserving cells,
// the camera-rect cull window the draw derives, and a LARGE int-array round-trip
// (the .cscene custom block that carries a real map).

#include <doctest.h>

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/SceneSerializer.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <string>
#include <vector>

using namespace Cosmic;

namespace
{
    // The cull window Scene::OnRenderSprites computes for a tilemap: the world-XY
    // bounds of the view frustum (invVP over the NDC cube), clamped to the grid.
    // Mirrored here so the visible-range arithmetic is assertable headless.
    struct CullWindow { int x0, x1, y0, y1; };

    CullWindow VisibleCells(const glm::mat4& viewProjection, const TilemapComponent& tm,
                            const glm::vec3& mapOrigin)
    {
        glm::vec2 cullMin(0.0f), cullMax(0.0f);
        bool have = false;
        const glm::mat4 invVP = glm::inverse(viewProjection);
        for (int i = 0; i < 8; ++i)
        {
            glm::vec4 c = invVP * glm::vec4((i & 1) ? 1.0f : -1.0f,
                                            (i & 2) ? 1.0f : -1.0f,
                                            (i & 4) ? 1.0f : -1.0f, 1.0f);
            if (std::abs(c.w) < 1e-9f) continue;
            c /= c.w;
            const glm::vec2 p{ c.x, c.y };
            if (!have) { cullMin = cullMax = p; have = true; }
            else       { cullMin = glm::min(cullMin, p); cullMax = glm::max(cullMax, p); }
        }

        CullWindow w{ 0, tm.GridW - 1, 0, tm.GridH - 1 };
        if (have)
        {
            w.x0 = std::max(0,           (int)std::floor(cullMin.x - mapOrigin.x));
            w.x1 = std::min(tm.GridW - 1,(int)std::ceil (cullMax.x - mapOrigin.x));
            w.y0 = std::max(0,           (int)std::floor(cullMin.y - mapOrigin.y));
            w.y1 = std::min(tm.GridH - 1,(int)std::ceil (cullMax.y - mapOrigin.y));
        }
        return w;
    }
}

TEST_SUITE("Tilemap edges (U4) — bounds")
{
    TEST_CASE("InBounds is exact on all four edges and rejects negatives")
    {
        TilemapComponent tm;
        tm.GridW = 8; tm.GridH = 5;
        tm.EnsureCells();

        CHECK(tm.InBounds(0, 0));
        CHECK(tm.InBounds(7, 4));            // last valid cell
        CHECK(tm.InBounds(0, 4));
        CHECK(tm.InBounds(7, 0));

        CHECK_FALSE(tm.InBounds(8, 4));      // one past the right edge
        CHECK_FALSE(tm.InBounds(7, 5));      // one past the top edge
        CHECK_FALSE(tm.InBounds(-1, 0));
        CHECK_FALSE(tm.InBounds(0, -1));
        CHECK_FALSE(tm.InBounds(-1, -1));
    }

    TEST_CASE("At reads inside and returns empty outside, never out of range")
    {
        TilemapComponent tm;
        tm.GridW = 4; tm.GridH = 3;
        tm.EnsureCells();
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 4; ++x)
                tm.Cells[(size_t)y * 4 + x] = (uint16_t)(y * 4 + x + 1);

        CHECK(tm.At(0, 0) == 1);
        CHECK(tm.At(3, 0) == 4);
        CHECK(tm.At(0, 2) == 9);
        CHECK(tm.At(3, 2) == 12);

        CHECK(tm.At(4, 0) == 0);
        CHECK(tm.At(0, 3) == 0);
        CHECK(tm.At(-1, 1) == 0);
        CHECK(tm.At(1, -1) == 0);
        CHECK(tm.At(9999, 9999) == 0);

        // A cell buffer SHORTER than the grid (a hand-edited scene) still reads
        // safely — the size check backs up the bounds check.
        tm.Cells.resize(2);
        CHECK(tm.At(3, 2) == 0);
        CHECK(tm.At(0, 0) == 1);
    }
}

TEST_SUITE("Tilemap edges (U4) — resize")
{
    TEST_CASE("growing preserves existing cells (row-major) and zero-fills the rest")
    {
        TilemapComponent tm;
        tm.GridW = 3; tm.GridH = 2;
        tm.EnsureCells();
        tm.Cells = { 1, 2, 3, 4, 5, 6 };     // y0: 1 2 3   y1: 4 5 6

        tm.GridH = 4;                         // grow vertically only
        tm.EnsureCells();
        REQUIRE(tm.Cells.size() == 12u);
        CHECK(tm.At(0, 0) == 1);
        CHECK(tm.At(2, 1) == 6);
        CHECK(tm.At(0, 2) == 0);              // new rows are empty
        CHECK(tm.At(2, 3) == 0);
    }

    TEST_CASE("shrinking truncates the buffer and clamps out-of-range reads")
    {
        TilemapComponent tm;
        tm.GridW = 4; tm.GridH = 4;
        tm.EnsureCells();
        for (size_t i = 0; i < tm.Cells.size(); ++i)
            tm.Cells[i] = (uint16_t)(i + 1);

        tm.GridH = 2;
        tm.EnsureCells();
        CHECK(tm.Cells.size() == 8u);
        CHECK(tm.At(0, 0) == 1);
        CHECK(tm.At(3, 1) == 8);
        CHECK(tm.At(0, 2) == 0);              // the truncated rows are gone
    }

    TEST_CASE("EnsureCells is idempotent and clamps to the 1..1024 cap both ways")
    {
        TilemapComponent tm;
        tm.GridW = 6; tm.GridH = 6;
        tm.EnsureCells();
        tm.Cells[7] = 42;
        const auto before = tm.Cells;
        tm.EnsureCells();
        tm.EnsureCells();
        CHECK(tm.Cells == before);

        tm.GridW = TilemapComponent::kMaxGrid + 1;
        tm.GridH = -3;
        tm.EnsureCells();
        CHECK(tm.GridW == TilemapComponent::kMaxGrid);
        CHECK(tm.GridH == 1);
    }
}

TEST_SUITE("Tilemap edges (U4) — cull window")
{
    // Every camera below is placed on a half-unit so the window's floor()/ceil()
    // boundaries sit 0.5 away from an integer — the assertions then survive the
    // last-bit noise of inverting a projection matrix.

    TEST_CASE("an ortho camera over the map centre selects only the visible cells")
    {
        TilemapComponent tm;
        tm.GridW = 64; tm.GridH = 64;
        tm.EnsureCells();
        const glm::vec3 origin{ 0.0f, 0.0f, 0.0f };   // map bottom-left corner

        // A 10x10-world-unit window centred on (32.5, 32.5) => world [27.5, 37.5].
        const glm::mat4 proj = glm::ortho(-5.0f, 5.0f, -5.0f, 5.0f, -1.0f, 1.0f);
        const glm::mat4 view = glm::translate(glm::mat4(1.0f), { -32.5f, -32.5f, 0.0f });

        const CullWindow w = VisibleCells(proj * view, tm, origin);
        CHECK(w.x0 == 27);
        CHECK(w.x1 == 38);
        CHECK(w.y0 == 27);
        CHECK(w.y1 == 38);
        CHECK((w.x1 - w.x0 + 1) < tm.GridW);   // it really culled
    }

    TEST_CASE("the window clamps at the map edges instead of walking off the grid")
    {
        TilemapComponent tm;
        tm.GridW = 20; tm.GridH = 12;
        tm.EnsureCells();
        const glm::vec3 origin{ 0.0f, 0.0f, 0.0f };
        const glm::mat4 proj = glm::ortho(-8.0f, 8.0f, -8.0f, 8.0f, -1.0f, 1.0f);

        // Bottom-left corner (camera at 0.5, 0.5 => world [-7.5, 8.5]): the
        // window must not produce negative indices.
        {
            const glm::mat4 view = glm::translate(glm::mat4(1.0f), { -0.5f, -0.5f, 0.0f });
            const CullWindow w = VisibleCells(proj * view, tm, origin);
            CHECK(w.x0 == 0);       // clamped up from -8
            CHECK(w.y0 == 0);
            CHECK(w.x1 == 9);
            CHECK(w.y1 == 9);
        }

        // Top-right corner (camera at 19.5, 11.5): the window must not exceed
        // GridW-1 / GridH-1.
        {
            const glm::mat4 view = glm::translate(glm::mat4(1.0f), { -19.5f, -11.5f, 0.0f });
            const CullWindow w = VisibleCells(proj * view, tm, origin);
            CHECK(w.x0 == 11);
            CHECK(w.y0 == 3);
            CHECK(w.x1 == tm.GridW - 1);   // clamped down from 28
            CHECK(w.y1 == tm.GridH - 1);   // clamped down from 20
        }

        // Entirely off the map: the range is empty (x1 < x0), so the cell walk
        // never executes and no draw is issued.
        {
            const glm::mat4 view = glm::translate(glm::mat4(1.0f), { 500.5f, -0.5f, 0.0f });
            const CullWindow w = VisibleCells(proj * view, tm, origin);
            CHECK(w.x1 < w.x0);
        }
    }

    TEST_CASE("the map origin offsets the window by the entity position")
    {
        TilemapComponent tm;
        tm.GridW = 40; tm.GridH = 40;
        tm.EnsureCells();
        const glm::mat4 proj = glm::ortho(-5.0f, 5.0f, -5.0f, 5.0f, -1.0f, 1.0f);
        const glm::mat4 view = glm::translate(glm::mat4(1.0f), { -20.5f, -20.5f, 0.0f });

        const CullWindow a = VisibleCells(proj * view, tm, { 0.0f, 0.0f, 0.0f });
        const CullWindow b = VisibleCells(proj * view, tm, { 10.0f, 0.0f, 0.0f });
        CHECK(a.x0 == 15);
        CHECK(a.x1 == 26);
        CHECK(b.x0 == a.x0 - 10);   // moving the map right slides the window left
        CHECK(b.x1 == a.x1 - 10);
        CHECK(b.y0 == a.y0);
    }
}

TEST_SUITE("Tilemap edges (U4) — serialization")
{
    TEST_CASE("a full 1024x64 int-array map round-trips exactly and re-saves identically")
    {
        Scene s1;
        Entity e = s1.CreateEntity("BigMap");
        e.GetComponent<TransformComponent>().Position = { -12.5f, 3.25f, -2.0f };
        auto& tm = e.AddComponent<TilemapComponent>();
        tm.TilesetPath = "project://textures/atlas.png";
        tm.TileW = 32; tm.TileH = 32; tm.Columns = 16;
        tm.GridW = TilemapComponent::kMaxGrid;   // the v1 cap — the widest legal map
        tm.GridH = 64;
        tm.ZOrder = -3;
        tm.EnsureCells();

        // A deterministic pattern covering the whole range of a uint16 cell,
        // including 0 (empty) and the top of the atlas index space.
        for (int y = 0; y < tm.GridH; ++y)
            for (int x = 0; x < tm.GridW; ++x)
                tm.Cells[(size_t)y * tm.GridW + x] = (uint16_t)((x * 7919 + y * 104729) % 65536);
        tm.Cells[0] = 0;
        tm.Cells[tm.Cells.size() - 1] = 65535;

        const std::string save1 = SceneSerializer::SaveToString(s1);

        Scene s2;
        REQUIRE(SceneSerializer::LoadFromString(s2, save1));

        TilemapComponent* loaded = nullptr;
        for (auto h : s2.GetRegistry().view<TilemapComponent>())
            loaded = &s2.GetRegistry().get<TilemapComponent>(h);
        REQUIRE(loaded != nullptr);

        CHECK(loaded->GridW == tm.GridW);
        CHECK(loaded->GridH == tm.GridH);
        CHECK(loaded->TileW == 32);
        CHECK(loaded->Columns == 16);
        CHECK(loaded->ZOrder == -3);
        REQUIRE(loaded->Cells.size() == tm.Cells.size());
        CHECK(loaded->Cells == tm.Cells);

        CHECK(SceneSerializer::SaveToString(s2) == save1);
    }

    TEST_CASE("an empty map round-trips without inventing cells")
    {
        Scene s1;
        Entity e = s1.CreateEntity("Empty");
        auto& tm = e.AddComponent<TilemapComponent>();
        tm.GridW = 3; tm.GridH = 3;
        tm.EnsureCells();                     // all zero

        const std::string save1 = SceneSerializer::SaveToString(s1);
        Scene s2;
        REQUIRE(SceneSerializer::LoadFromString(s2, save1));

        TilemapComponent* loaded = nullptr;
        for (auto h : s2.GetRegistry().view<TilemapComponent>())
            loaded = &s2.GetRegistry().get<TilemapComponent>(h);
        REQUIRE(loaded != nullptr);
        CHECK(loaded->GridW == 3);
        CHECK(loaded->GridH == 3);
        for (uint16_t v : loaded->Cells)
            CHECK(v == 0);
        CHECK(SceneSerializer::SaveToString(s2) == save1);
    }
}
