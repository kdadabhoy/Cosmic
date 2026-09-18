#pragma once
// wo09_tilemap_oracle.h — WO-09 C02: the INDEPENDENT visible-cell calculation the
// tilemap cull/draw counts are compared against (headless + GPU halves).
//
// It is deliberately NOT the engine's walk (Scene::OnRenderSprites computes a
// [floor(min - origin), ceil(max - origin)] index range from the inverse
// view-projection). This oracle starts from the CAMERA's world rectangle
// (centre ± half extents, the definition of an orthographic 2D camera) and
// asks, per cell, a plain geometric question about the cell's unit square
// against that rectangle. Two counts come out of it:
//
//   Strict   — cells whose unit square [x, x+1) x [y, y+1) overlaps the camera
//              rectangle with POSITIVE area (a square that only touches an edge
//              has nothing to show);
//   Enclosed — cells whose INDEX lies inside the rectangle snapped OUTWARD to
//              whole cells: floor(min edge) <= index <= ceil(max edge). That is
//              the documented conservative walk (Components.h: "the visible-cell
//              walk is culled by the camera's world rect"), so the engine's
//              non-empty cell count must equal the Enclosed count exactly, never
//              fall below the Strict count, and exceed it by at most one extra
//              row and one extra column (the ceil() on the max edges).
//
// Both are brute force over the whole grid (1,048,576 cells at the maximum):
// slow, obvious, and independent of the engine's inverse-view-projection
// corner walk — the camera rectangle here comes from the camera's own
// definition (centre ± half extents), not from any matrix.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Wo09Tilemap
{
    struct CameraRect { double MinX, MinY, MaxX, MaxY; };   // world XY of the ortho view

    inline CameraRect OrthoRect(double cx, double cy, double halfW, double halfH)
    {
        return { cx - halfW, cy - halfH, cx + halfW, cy + halfH };
    }

    struct Counts
    {
        size_t StrictCells   = 0;   // squares meeting the exact camera rect
        size_t StrictNonZero = 0;   // ... that hold a non-zero tile
        size_t EnclosedCells = 0;   // squares meeting the whole-cell-snapped rect
        size_t EnclosedNonZero = 0;
    };

    // `cells` is row-major [y * gridW + x] like TilemapComponent::Cells; a short
    // buffer reads as zero beyond its end (the At() rule).
    inline Counts Count(const std::vector<uint16_t>& cells, int gridW, int gridH,
                        double originX, double originY, const CameraRect& cam)
    {
        Counts c;
        // Whole-cell snap of the camera rect, in cell units relative to the origin.
        const double sMinX = std::floor(cam.MinX - originX), sMaxX = std::ceil(cam.MaxX - originX);
        const double sMinY = std::floor(cam.MinY - originY), sMaxY = std::ceil(cam.MaxY - originY);
        for (int y = 0; y < gridH; ++y)
        {
            const double y0 = originY + y, y1 = y0 + 1.0;
            const bool strictY   = y1 > cam.MinY && y0 < cam.MaxY;
            const bool enclosedY = (double)y >= sMinY && (double)y <= sMaxY;
            if (!strictY && !enclosedY) continue;
            for (int x = 0; x < gridW; ++x)
            {
                const double x0 = originX + x, x1 = x0 + 1.0;
                const bool strictX   = x1 > cam.MinX && x0 < cam.MaxX;
                const bool enclosedX = (double)x >= sMinX && (double)x <= sMaxX;
                const size_t i = (size_t)y * (size_t)gridW + (size_t)x;
                const uint16_t v = i < cells.size() ? cells[i] : (uint16_t)0;
                if (strictY && strictX)     { ++c.StrictCells;   if (v) ++c.StrictNonZero; }
                if (enclosedY && enclosedX) { ++c.EnclosedCells; if (v) ++c.EnclosedNonZero; }
            }
        }
        return c;
    }
}
