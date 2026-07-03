#pragma once

// HeightfieldComposer.h
//
// The Frontier island shape (Phase 11, doc 10 F11). Pure, header-only, fully
// deterministic: IslandHeight(params, u, v) composes ONE ~4x4 km island out of a
// rolling base, a ridged snow range, a volcano cone + caldera, an alpine lake
// basin, a carved river, and a beach shelf falling into the sea. Plug it into
// Cosmic::TerrainSpecification::HeightFunction (Source C) — the terrain samples
// it at every texel (u = i/(res-1), v = j/(res-1), world = minCorner + uv*size).
//
// App-side by design: the engine ships the generic Noise::Ridged2D verb (F11);
// the *island* is app content. Same seed => the same island (the defaults ARE
// the island). No globals; a per-thread Noise is cached by seed so the terrain's
// ~4M-texel build doesn't rebuild the permutation table on every sample.

#include <Cosmic.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Frontier
{
    /** The composed island's tunables. All positions/radii are world-normalized
     *  [0,1] over the terrain UV; heights are normalized [0,1] (scaled to world
     *  by the terrain's HeightScale/BaseHeight). The defaults are THE island. */
    struct IslandParams
    {
        uint32_t  Seed = 20260703;

        // Volcano (south-east): cone + summit caldera.
        glm::vec2 VolcanoCenter{ 0.62f, 0.38f };
        float     VolcanoRadius = 0.16f;
        float     VolcanoHeight = 1.0f;     // normalized cone peak (maps to HeightScale)
        float     CalderaRadius = 0.045f;
        float     CalderaDepth  = 0.35f;

        // Ridged snow range (north-west spine A->B).
        glm::vec2 RangeA{ 0.18f, 0.22f }, RangeB{ 0.42f, 0.78f };
        float     RangeWidth  = 0.16f;
        float     RangeHeight = 0.75f;

        // Alpine lake basin.
        glm::vec2 LakeCenter{ 0.36f, 0.55f };
        float     LakeRadius = 0.085f;
        float     LakeDepth  = 0.16f;

        // Coast: beach shelf width as a fraction of the half-extent.
        float     SeaShelf = 0.10f;

        // River: lake -> coast waypoints (normalized UV), carved channel.
        std::vector<glm::vec2> RiverPath{
            { 0.36f, 0.55f }, { 0.30f, 0.42f }, { 0.22f, 0.28f }, { 0.13f, 0.11f }
        };
        float     RiverWidth = 0.012f;
        float     RiverDepth = 0.05f;
    };

    namespace detail
    {
        // Deep sea-floor normalized height — the coast re-bias floor so the ocean
        // bottom is an underwater shelf, not a zero cliff. Shared by IslandHeight
        // and OceanFloor01 (one source of truth).
        inline constexpr float kSeaFloor01 = 0.04f;

        // Lowest a carved lake basin may reach — keeps inland lakes above the sea.
        inline constexpr float kLakeFloor01 = 0.10f;

        /** A Noise cached per thread, rebuilt only when the seed changes — so a
         *  4M-texel terrain build reuses one permutation table (still deterministic
         *  per (seed, u, v)). */
        inline const Cosmic::Noise& IslandNoise(uint32_t seed)
        {
            thread_local Cosmic::Noise cached(seed);
            thread_local uint32_t      cachedSeed = seed;
            if (cachedSeed != seed)
            {
                cached     = Cosmic::Noise(seed);
                cachedSeed = seed;
            }
            return cached;
        }

        /** Shortest distance from p to segment [a, b] (all in UV space). */
        inline float DistToSegment(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b)
        {
            const glm::vec2 ab = b - a;
            const float denom  = std::max(glm::dot(ab, ab), 1e-8f);
            const float t      = glm::clamp(glm::dot(p - a, ab) / denom, 0.0f, 1.0f);
            return glm::length(p - (a + t * ab));
        }

        /** Shortest distance from p to a polyline (sequence of UV waypoints). */
        inline float DistToPolyline(const glm::vec2& p, const std::vector<glm::vec2>& pts)
        {
            if (pts.empty())
                return 1e9f;
            if (pts.size() == 1)
                return glm::length(p - pts[0]);
            float d = 1e9f;
            for (size_t i = 0; i + 1 < pts.size(); ++i)
                d = std::min(d, DistToSegment(p, pts[i], pts[i + 1]));
            return d;
        }
    } // namespace detail

    /** Deep sea-floor normalized height (the coast re-bias floor). Exposed so
     *  world code / tests can reason about the ocean bottom without re-deriving. */
    inline float OceanFloor01(const IslandParams& /*p*/) { return detail::kSeaFloor01; }

    /**
     * Composed island height at normalized UV -> [0,1] (plug into HeightFunction).
     * Compose in order, smoothing everything (smoothstep, no hard max except the
     * sharp ridgelines / cone, which SHOULD be crisp):
     *   base rolling fBm -> ridged range (max) -> volcano cone + caldera (max)
     *   -> lake basin (subtract) -> river (subtract) -> coast falloff + sea shelf.
     */
    inline float IslandHeight(const IslandParams& p, float u, float v)
    {
        const Cosmic::Noise& n = detail::IslandNoise(p.Seed);
        const glm::vec2 uv{ u, v };

        // 1) Base rolling hills (gentle lowland; fBm normalized to [0, 0.18]).
        float h = 0.18f * (0.5f + 0.5f * n.Fbm2D(u * 3.0f, v * 3.0f, 6));

        // 2) Ridged snow range along the spine A->B — sharp ridgelines (max).
        {
            const float d     = detail::DistToSegment(uv, p.RangeA, p.RangeB);
            const float band  = glm::smoothstep(p.RangeWidth, 0.0f, d);   // 1 on the spine
            const float ridge = band * n.Ridged2D(u * 5.0f, v * 5.0f, 5) * p.RangeHeight;
            h = std::max(h, ridge);
        }

        // 3) Volcano cone + rim noise, then carve the summit caldera (max).
        {
            const float r  = glm::length(uv - p.VolcanoCenter);
            float cone = p.VolcanoHeight * glm::smoothstep(p.VolcanoRadius, p.VolcanoRadius * 0.15f, r);
            cone += cone * 0.06f * n.Fbm2D(u * 18.0f, v * 18.0f, 3);       // +-6% rim texture
            cone -= p.CalderaDepth * glm::smoothstep(p.CalderaRadius, p.CalderaRadius * 0.45f, r);
            h = std::max(h, cone);
        }

        // 4) Alpine lake basin (subtract; floored so it stays above the sea).
        {
            const float rL    = glm::length(uv - p.LakeCenter);
            const float carve = p.LakeDepth * glm::smoothstep(p.LakeRadius, p.LakeRadius * 0.3f, rL);
            h = std::max(h - carve, detail::kLakeFloor01);
        }

        // 5) River channel from the lake to the coast (subtract).
        {
            const float dR = detail::DistToPolyline(uv, p.RiverPath);
            h -= p.RiverDepth * glm::smoothstep(p.RiverWidth, 0.0f, dR);
        }

        // 6) Coast: falloff to the sea over the SeaShelf band, then re-bias so the
        //    ocean bottom sits on a shelf (kSeaFloor01), not a zero cliff.
        {
            const float edge    = std::max(std::fabs(u - 0.5f), std::fabs(v - 0.5f));
            const float falloff = glm::smoothstep(0.5f, 0.5f - p.SeaShelf, edge);
            h *= falloff;
            h = detail::kSeaFloor01 + (1.0f - detail::kSeaFloor01) * h;
        }

        return glm::clamp(h, 0.0f, 1.0f);
    }

    /**
     * World-Y of the alpine lake surface: the carved basin rim (averaged around
     * the lake) minus ~2 m, so the lake reads as filled to the brim. `heightScale`
     * / `baseHeight` are the terrain's (world = baseHeight + h01 * heightScale).
     */
    inline float LakeSurfaceWorldY(const IslandParams& p, float heightScale, float baseHeight)
    {
        float rimSum = 0.0f;
        for (int i = 0; i < 8; ++i)
        {
            const float a  = static_cast<float>(i) / 8.0f * glm::two_pi<float>();
            const float ru = p.LakeCenter.x + std::cos(a) * p.LakeRadius;
            const float rv = p.LakeCenter.y + std::sin(a) * p.LakeRadius;
            rimSum += IslandHeight(p, ru, rv);
        }
        const float rim01 = rimSum / 8.0f;
        return baseHeight + rim01 * heightScale - 2.0f;
    }

} // namespace Frontier
