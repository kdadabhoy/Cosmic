// test_wo09_c02_tilemap.cpp — WO-09 (2D stability) C02, the headless half:
// tilemap grid boundaries, the maximum 1,048,576-cell map, the short-Cells
// buffer (a serializer-producible crash vector), and the serializer's handling
// of malformed Cells / grid values.
//
// Contract under test (scene/Components.h TilemapComponent): grid dimensions
// clamp to 1..kMaxGrid (1024) — 1025 is the invalid boundary and is NOT accepted
// as a 1025-wide map; EnsureCells sizes Cells to GridW*GridH; At() never reads
// past the buffer; the maximum map is 1,048,576 cells.
//
// The culled cell walk itself needs Renderer2D, so its counts (against the
// independent oracle in wo09_tilemap_oracle.h) are asserted by the GPU half in
// tests/render/render_wo09_tilemap.cpp; this file exercises the oracle on the
// same geometry so a broken oracle cannot silently agree with a broken walk.

#include <doctest.h>

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/SceneSerializer.h"
#include "wo09_tilemap_oracle.h"

#include <chrono>
#include <climits>
#include <cstdint>
#include <string>
#include <vector>

using namespace Cosmic;

namespace
{
    Entity FindTagged(Scene& s, const std::string& tag)
    {
        for (auto e : s.GetRegistry().view<TagComponent>())
            if (s.GetRegistry().get<TagComponent>(e).Tag == tag) return Entity{ e, &s };
        return Entity{};
    }
}

TEST_SUITE("WO-09 C02 tilemaps (headless)")
{
    TEST_CASE("WO-09 C02: EnsureCells clamps 0 / -1 / 1 / 1024 / 1025 / kMaxGrid+1 / INT_MIN / INT_MAX and sizes the buffer exactly")
    {
        struct Probe { int32_t in; int32_t out; };
        const Probe probes[] = {
            { 0, 1 }, { -1, 1 }, { 1, 1 }, { 1024, 1024 }, { 1025, 1024 },
            { TilemapComponent::kMaxGrid + 1, 1024 }, { INT_MIN, 1 }, { INT_MAX, 1024 }, { 512, 512 },
        };
        for (const Probe& p : probes)
        {
            TilemapComponent tm;
            tm.GridW = p.in; tm.GridH = 3;
            tm.EnsureCells();
            CHECK_MESSAGE(tm.GridW == p.out, "GridW " << p.in << " -> " << tm.GridW);
            CHECK(tm.GridH == 3);
            CHECK(tm.Cells.size() == (size_t)p.out * 3u);
            TilemapComponent th;
            th.GridW = 3; th.GridH = p.in;
            th.EnsureCells();
            CHECK(th.GridH == p.out);
            CHECK(th.Cells.size() == (size_t)p.out * 3u);
        }
        // Every cell of a 0/-1 map is reachable through At without a read past the end.
        TilemapComponent one; one.GridW = 0; one.GridH = -1; one.EnsureCells();
        CHECK(one.Cells.size() == 1);
        CHECK(one.At(0, 0) == 0);
        CHECK(one.At(1, 0) == 0);
        CHECK(one.At(-1, 0) == 0);
        CHECK(TilemapComponent::kMaxGrid == 1024);
    }

    TEST_CASE("WO-09 C02: the maximum map — 1,024 x 1,024 = 1,048,576 cells; memory, build time and a full serializer round-trip")
    {
        TilemapComponent tm;
        tm.GridW = 1024; tm.GridH = 1024;
        const auto t0 = std::chrono::steady_clock::now();
        tm.EnsureCells();
        const double msEnsure = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        REQUIRE(tm.Cells.size() == 1048576u);
        CHECK(tm.Cells.capacity() * sizeof(uint16_t) >= 2u * 1024u * 1024u);
        MESSAGE("C02 EnsureCells(1024x1024) = " << msEnsure << " ms; buffer = "
                << tm.Cells.capacity() * sizeof(uint16_t) << " bytes (2 MiB of cells)");

        // A deterministic pattern: tile id = 1 + (x + y) % 15, every 4th cell empty.
        for (int y = 0; y < 1024; ++y)
            for (int x = 0; x < 1024; ++x)
                tm.Cells[(size_t)y * 1024 + x] = ((x + y) % 4 == 3) ? 0 : (uint16_t)(1 + (x + y) % 15);
        CHECK(tm.At(0, 0) == 1);
        CHECK(tm.At(1023, 1023) == 1 + 2046 % 15);
        CHECK(tm.At(1024, 0) == 0);      // outside
        CHECK(tm.At(0, 1024) == 0);
        CHECK(tm.At(-1, 1023) == 0);

        // The oracle on the full map: the camera fully outside sees nothing; a camera
        // over the whole map sees every cell; a 10x10 window sees 100..121 cells.
        using namespace Wo09Tilemap;
        const Counts outside = Count(tm.Cells, 1024, 1024, 0.0, 0.0, OrthoRect(-5000.0, -5000.0, 8.0, 4.5));
        CHECK(outside.StrictCells == 0);
        CHECK(outside.EnclosedCells == 0);
        const Counts all = Count(tm.Cells, 1024, 1024, 0.0, 0.0, OrthoRect(512.0, 512.0, 2000.0, 2000.0));
        CHECK(all.StrictCells == 1048576u);
        CHECK(all.EnclosedCells == 1048576u);
        CHECK(all.StrictNonZero == 786432u);          // 3 of every 4 cells hold a tile
        const Counts window = Count(tm.Cells, 1024, 1024, 0.0, 0.0, OrthoRect(100.5, 100.5, 5.0, 5.0));
        CHECK(window.StrictCells == 121u);            // [95.5, 105.5] meets cells 95..105 = 11 per axis
        CHECK(window.EnclosedCells == 144u);          // floor 95 .. ceil 106 = 12 per axis
        const Counts exact = Count(tm.Cells, 1024, 1024, 0.0, 0.0, OrthoRect(100.0, 100.0, 5.0, 5.0));
        CHECK(exact.StrictCells == 100u);             // [95, 105]: positive-area overlap = cells 95..104
        CHECK(exact.EnclosedCells == 121u);           // integer edges: floor 95 .. ceil 105 = 11 per axis

        // Full 1M-cell JSON round-trip: time it, and prove every cell survives.
        Ref<Scene> scene = Scene::Create();
        Entity e = scene->CreateEntity("BigMap");
        e.AddComponent<TilemapComponent>() = tm;
        const auto t1 = std::chrono::steady_clock::now();
        const std::string json = SceneSerializer::SaveToString(*scene);
        const double msSave = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
        Ref<Scene> back = Scene::Create();
        const auto t2 = std::chrono::steady_clock::now();
        REQUIRE(SceneSerializer::LoadFromString(*back, json));
        const double msLoad = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t2).count();
        Entity loaded = FindTagged(*back, "BigMap");
        REQUIRE(loaded);
        const auto& lm = loaded.GetComponent<TilemapComponent>();
        REQUIRE(lm.GridW == 1024); REQUIRE(lm.GridH == 1024);
        REQUIRE(lm.Cells.size() == 1048576u);
        CHECK(lm.Cells == tm.Cells);
        MESSAGE("C02 1,048,576-cell map: save " << msSave << " ms (" << json.size() << " bytes), load " << msLoad << " ms");
        CHECK(msSave + msLoad < 10000.0);
    }

    TEST_CASE("WO-09 C02: a Cells buffer SHORTER than GridW*GridH never reads out of bounds (At, FloodFill, EnsureCells)")
    {
        // The crash vector: a component whose grid says 64x64 but whose buffer holds
        // 10 values — producible by any code that sets GridW/GridH without calling
        // EnsureCells (the serializer pads; a script or an older file may not).
        TilemapComponent tm;
        tm.GridW = 64; tm.GridH = 64;
        tm.Cells.assign(10, (uint16_t)7);
        CHECK(tm.At(9, 0) == 7);
        CHECK(tm.At(10, 0) == 0);          // inside the grid, past the buffer: empty, not a read
        CHECK(tm.At(63, 63) == 0);
        CHECK(tm.At(0, 1) == 0);           // index 64 > 10
        CHECK(tm.InBounds(63, 63));

        // FloodFill on the short buffer: bounded by the buffer check, not the grid.
        std::vector<uint16_t> shortCells(10, (uint16_t)7);
        const auto changed = TilemapComponent::FloodFill(shortCells, 64, 64, 0, 0, 9);
        CHECK(shortCells.size() >= 10);    // it may grow the buffer to the grid, never index past it
        for (uint32_t idx : changed) CHECK(idx < shortCells.size());

        // EnsureCells repairs it: the 10 values are preserved, the rest are empty.
        tm.EnsureCells();
        REQUIRE(tm.Cells.size() == 4096u);
        for (int i = 0; i < 10; ++i) CHECK(tm.Cells[i] == 7);
        for (int i = 10; i < 4096; ++i) if (tm.Cells[i] != 0) { FAIL("padding cell " << i << " is not empty"); break; }
    }

    TEST_CASE("WO-09 C02: serializer — Cells shorter / longer than the grid, ragged values, GridW 1025 / 0 / -7 / 1e9, and a non-array Cells block")
    {
        auto load = [](const std::string& tilemapBlock) -> TilemapComponent
        {
            const std::string text =
                "{ \"cosmic_scene\": 1, \"entities\": [ { \"id\": \"00000000000000AA\", \"components\": { "
                "\"Tag\": { \"Tag\": \"M\" }, \"Tilemap\": " + tilemapBlock + " } } ] }";
            Ref<Scene> s = Scene::Create();
            REQUIRE(SceneSerializer::LoadFromString(*s, text));
            Entity e = FindTagged(*s, "M");
            REQUIRE(e);
            REQUIRE(e.HasComponent<TilemapComponent>());
            return e.GetComponent<TilemapComponent>();
        };

        // Shorter than the grid: padded with empties, values kept.
        {
            TilemapComponent tm = load("{ \"GridW\": 4, \"GridH\": 3, \"Cells\": [1, 2, 3] }");
            CHECK(tm.GridW == 4); CHECK(tm.GridH == 3);
            REQUIRE(tm.Cells.size() == 12u);
            CHECK(tm.Cells[0] == 1); CHECK(tm.Cells[2] == 3); CHECK(tm.Cells[3] == 0); CHECK(tm.Cells[11] == 0);
        }
        // Longer than the grid: truncated to GridW*GridH (the extra values are dropped, not kept out of range).
        {
            TilemapComponent tm = load("{ \"GridW\": 2, \"GridH\": 2, \"Cells\": [1, 2, 3, 4, 5, 6, 7] }");
            REQUIRE(tm.Cells.size() == 4u);
            CHECK(tm.Cells[3] == 4);
        }
        // Ragged values: non-numbers read as 0; floats truncate; negatives and
        // > 65535 wrap through uint32 -> uint16 (pinned: no rejection, no crash).
        {
            TilemapComponent tm = load("{ \"GridW\": 6, \"GridH\": 1, \"Cells\": [\"x\", null, 2.9, 65536, 65537, -1] }");
            REQUIRE(tm.Cells.size() == 6u);
            CHECK(tm.Cells[0] == 0); CHECK(tm.Cells[1] == 0); CHECK(tm.Cells[2] == 2);
            CHECK(tm.Cells[3] == 0); CHECK(tm.Cells[4] == 1); CHECK(tm.Cells[5] == 65535);
        }
        // 1025 is the invalid boundary: clamped to 1024, the buffer sized for 1024.
        {
            TilemapComponent tm = load("{ \"GridW\": 1025, \"GridH\": 2, \"Cells\": [] }");
            CHECK(tm.GridW == 1024);
            CHECK(tm.Cells.size() == 2048u);
        }
        {
            TilemapComponent tm = load("{ \"GridW\": 0, \"GridH\": -7, \"Cells\": [9] }");
            CHECK(tm.GridW == 1); CHECK(tm.GridH == 1);
            REQUIRE(tm.Cells.size() == 1u);
            CHECK(tm.Cells[0] == 9);
        }
        {
            // 1e9 is a JSON number the int32 field accepts (wraps or saturates in the
            // reflection layer) — whatever it becomes, EnsureCells clamps it to 1..1024
            // and the buffer never exceeds 1,048,576 cells.
            TilemapComponent tm = load("{ \"GridW\": 1000000000, \"GridH\": 1000000000, \"Cells\": [] }");
            CHECK(tm.GridW >= 1); CHECK(tm.GridW <= 1024);
            CHECK(tm.GridH >= 1); CHECK(tm.GridH <= 1024);
            CHECK(tm.Cells.size() <= 1048576u);
            CHECK(tm.Cells.size() == (size_t)tm.GridW * (size_t)tm.GridH);
        }
        // A non-array Cells block is ignored: the grid fields load, the buffer is
        // left EMPTY (the walk calls EnsureCells before reading; At() guards).
        {
            TilemapComponent tm = load("{ \"GridW\": 8, \"GridH\": 8, \"Cells\": \"not an array\" }");
            CHECK(tm.GridW == 8);
            CHECK(tm.Cells.size() <= 64u);
            CHECK(tm.At(7, 7) == 0);
            tm.EnsureCells();
            CHECK(tm.Cells.size() == 64u);
        }
    }

    TEST_CASE("WO-09 C02: the visible-cell oracle agrees with the documented walk on the catalog's pinned cases")
    {
        using namespace Wo09Tilemap;
        std::vector<uint16_t> cells(64 * 64, (uint16_t)1);
        // The test_tilemap_extra case: a 10x10 window centred on (32.5, 32.5) =>
        // world [27.5, 37.5] => cells 27..38 walked (12 per axis), 27..37 strictly visible (11).
        const Counts c = Count(cells, 64, 64, 0.0, 0.0, OrthoRect(32.5, 32.5, 5.0, 5.0));
        CHECK(c.EnclosedCells == 144u);
        CHECK(c.StrictCells == 121u);
        // Off the grid entirely (the catalog's "camera fully outside => 0 cells").
        const Counts o = Count(cells, 64, 64, 0.0, 0.0, OrthoRect(500.5, 0.5, 8.0, 8.0));
        CHECK(o.EnclosedCells == 0u);
        CHECK(o.StrictCells == 0u);
        // The map origin offsets the window (the same case as the extra suite).
        const Counts a = Count(cells, 40, 40, 0.0, 0.0, OrthoRect(20.5, 20.5, 5.0, 5.0));
        const Counts b = Count(cells, 40, 40, 10.0, 0.0, OrthoRect(20.5, 20.5, 5.0, 5.0));
        CHECK(a.EnclosedCells == 144u);
        CHECK(b.EnclosedCells == 144u);
    }
}
