// test_tilemap.cpp — TilemapComponent (Phase 17 / U4).
// Headless: cell store + flood fill (pure) + the custom Cells serialization
// (int array) round-trip. The painter overlay + draw are editor/GL.

#include <doctest.h>

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/SceneSerializer.h"

using namespace Cosmic;

TEST_CASE("U4: EnsureCells clamps the grid to 1..1024 and sizes the buffer")
{
    TilemapComponent tm;
    tm.GridW = 5000;    // above the v1 cap
    tm.GridH = 0;       // below the floor
    tm.EnsureCells();
    CHECK(tm.GridW == 1024);
    CHECK(tm.GridH == 1);
    CHECK(tm.Cells.size() == 1024u);

    tm.GridW = 4; tm.GridH = 3;
    tm.EnsureCells();
    CHECK(tm.Cells.size() == 12u);
    CHECK(tm.At(3, 2) == 0);
    CHECK(tm.At(4, 0) == 0);    // out of bounds reads empty
    CHECK(tm.At(-1, 0) == 0);
}

TEST_CASE("U4: FloodFill fills the 4-connected region and reports changes")
{
    // 5x4 map: a vertical wall of 9s at x=2 splits the map.
    //   y3:  0 0 9 0 0
    //   y2:  0 0 9 0 0
    //   y1:  0 0 9 0 0
    //   y0:  0 0 9 0 0
    const int W = 5, H = 4;
    std::vector<uint16_t> cells((size_t)W * H, 0);
    for (int y = 0; y < H; ++y) cells[(size_t)y * W + 2] = 9;

    // Fill from the left half: 8 cells change, the right half stays 0.
    auto changed = TilemapComponent::FloodFill(cells, W, H, 0, 0, 7);
    CHECK(changed.size() == 8u);
    CHECK(cells[0] == 7);                    // (0,0)
    CHECK(cells[(size_t)3 * W + 1] == 7);    // (1,3)
    CHECK(cells[(size_t)1 * W + 3] == 0);    // right of the wall untouched
    CHECK(cells[(size_t)1 * W + 2] == 9);    // the wall itself untouched

    // Filling with the same value is a no-op.
    CHECK(TilemapComponent::FloodFill(cells, W, H, 0, 0, 7).empty());

    // Out of bounds is a no-op.
    CHECK(TilemapComponent::FloodFill(cells, W, H, -1, 0, 3).empty());
    CHECK(TilemapComponent::FloodFill(cells, W, H, 0, H, 3).empty());
}

TEST_CASE("U4: tilemap Cells round-trip the scene serializer as an int array")
{
    Scene s1;
    Entity e = s1.CreateEntity("Map");
    auto& tm = e.AddComponent<TilemapComponent>();
    tm.TilesetPath = "project://textures/tiles.png";
    tm.TileW = 8; tm.TileH = 8; tm.Columns = 4;
    tm.GridW = 100; tm.GridH = 60;   // the U4 acceptance map size
    tm.ZOrder = -5;
    tm.EnsureCells();

    // A recognizable pattern.
    for (int x = 0; x < tm.GridW; ++x)
        tm.Cells[(size_t)0 * tm.GridW + x] = (uint16_t)(1 + (x % 7));
    tm.Cells[(size_t)59 * tm.GridW + 99] = 321;

    const std::string save1 = SceneSerializer::SaveToString(s1);

    Scene s2;
    REQUIRE(SceneSerializer::LoadFromString(s2, save1));

    TilemapComponent* loaded = nullptr;
    for (auto h : s2.GetRegistry().view<TilemapComponent>())
        loaded = &s2.GetRegistry().get<TilemapComponent>(h);
    REQUIRE(loaded != nullptr);

    CHECK(loaded->TilesetPath == tm.TilesetPath);
    CHECK(loaded->TileW == 8);
    CHECK(loaded->TileH == 8);
    CHECK(loaded->Columns == 4);
    CHECK(loaded->GridW == 100);
    CHECK(loaded->GridH == 60);
    CHECK(loaded->ZOrder == -5);
    REQUIRE(loaded->Cells.size() == tm.Cells.size());
    CHECK(loaded->Cells == tm.Cells);

    // Save-of-load is byte-identical (stable serialization).
    const std::string save2 = SceneSerializer::SaveToString(s2);
    CHECK(save1 == save2);
}
