#pragma once
// nav/NavTypes.h
//
// ============================================================================
// Cosmic navigation — public, Recast/Detour-free value types (Phase 26 / N1).
// ============================================================================
//
// This header is the compile-time firewall in front of Recast & Detour (like
// PhysicsTypes.h is in front of Jolt): it declares the plain POD/reflected value
// types the rest of the engine and game scripts pass to NavWorld, and it
// includes NO Recast/Detour header. All rc*/dt* types live in NavWorld.cpp
// behind the pimpl. Everything here is header-only, GL-free, headless-testable.
//
// Coordinate convention matches TransformComponent (glm, right-handed, metres,
// Y up) — the same as the physics + collision path the bake reads from.
// ============================================================================

#include "core/Core.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace Cosmic
{
    // ------------------------------------------------------------------------
    // NavBuildDesc — the Recast bake recipe (mirrors NavMeshComponent's reflected
    // fields, N2). Units are metres unless noted. Defaults are the Recast "solo
    // mesh" sample values scaled to a human-sized agent.
    // ------------------------------------------------------------------------
    struct NavBuildDesc
    {
        float CellSize   = 0.30f;   // xz rasterization voxel size (m)
        float CellHeight = 0.20f;   // y rasterization voxel size (m)

        float AgentRadius   = 0.6f;   // walkable area is eroded by this (m)
        float AgentHeight   = 2.0f;   // vertical clearance required (m)
        float AgentMaxClimb = 0.9f;   // max step-up height (m)
        float AgentMaxSlopeDeg = 45.0f;

        float RegionMinSize   = 8.0f;   // min region size (voxels; area = size^2)
        float RegionMergeSize = 20.0f;  // regions smaller than this merge (voxels)

        float EdgeMaxLen   = 12.0f;   // max contour edge length (m)
        float EdgeMaxError = 1.3f;    // contour simplification error (voxels)

        float DetailSampleDist     = 6.0f;   // detail-mesh sample spacing (× CellSize)
        float DetailSampleMaxError = 1.0f;   // detail-mesh max error (× CellHeight)

        int   VertsPerPoly = 6;       // max vertices per navmesh polygon (<= 6)

        // Tiled build hint (voxels). 0 = single-tile "solo" build (v1). A >0 value
        // is reserved for the tiled + DetourTileCache path (parked; see NavWorld.cpp).
        float TileSize = 0.0f;
    };

    // ------------------------------------------------------------------------
    // NavGeometryInput — world-space triangle soup fed to the bake. Non-owning
    // spans; the caller keeps the storage alive across the Build call. Vertices
    // are xyz triples (size % 3 == 0); Indices are triangle triples into Vertices.
    // ------------------------------------------------------------------------
    struct NavGeometryInput
    {
        std::span<const float> Vertices;   // {x,y,z} per vertex
        std::span<const int>   Indices;    // {i0,i1,i2} per triangle

        int VertexCount()   const { return int(Vertices.size() / 3); }
        int TriangleCount() const { return int(Indices.size() / 3); }
        bool Empty()        const { return Indices.size() < 3; }
    };

    // ------------------------------------------------------------------------
    // NavMeshData — a serialized baked navmesh (the `.cnav` payload). Opaque
    // bytes: a small Cosmic header + the raw Detour tile data. Big-binary-out-of-
    // scene-JSON (the `.cvox` sidecar rule): scenes store the recipe, this rides
    // a sidecar file.
    // ------------------------------------------------------------------------
    struct NavMeshData
    {
        std::vector<uint8_t> Bytes;
        bool Empty() const { return Bytes.empty(); }
    };

    // ------------------------------------------------------------------------
    // NavPath — the result of a FindPath query.
    //   Corners  : straightened waypoints from the start toward the goal
    //              (Corners.front() ≈ start, Corners.back() ≈ reached point).
    //   Length   : summed corner-to-corner distance (metres).
    //   Reached  : the path arrives at the goal polygon (a full path).
    //   Partial  : a path exists but stops short (goal unreachable / off-mesh).
    // A cleanly-failed query (no polygon under either endpoint) returns
    // Reached=false, Partial=false, empty Corners — never a crash.
    // ------------------------------------------------------------------------
    struct NavPath
    {
        std::vector<glm::vec3> Corners;
        float Length  = 0.0f;
        bool  Reached = false;
        bool  Partial = false;

        bool Empty() const { return Corners.empty(); }
    };

    // ------------------------------------------------------------------------
    // NavRayHit — a straight-line "wall" raycast across the navmesh surface
    // (does the segment a->b stay walkable?). Hit=false means the segment is
    // clear all the way to b.
    // ------------------------------------------------------------------------
    struct NavRayHit
    {
        bool      Hit = false;
        float     T   = 1.0f;        // parametric [0,1] of the first wall along a->b
        glm::vec3 Point{ 0.0f };     // a + (b-a)*T
    };

    // ------------------------------------------------------------------------
    // NavDebugTri — one walkable detail-mesh triangle in world space, for the N3
    // translucent overlay (drawn via the Renderer3D line/tri batch).
    // ------------------------------------------------------------------------
    struct NavDebugTri
    {
        glm::vec3 A{ 0.0f }, B{ 0.0f }, C{ 0.0f };
    };

    // ------------------------------------------------------------------------
    // NavAgentParams — DetourCrowd agent tuning (mirrors NavAgentComponent, N4).
    // ------------------------------------------------------------------------
    struct NavAgentParams
    {
        float Radius   = 0.6f;
        float Height   = 2.0f;
        float MaxSpeed = 3.5f;    // m/s
        float MaxAccel = 8.0f;    // m/s^2
        float SeparationWeight = 2.0f;
    };
}
