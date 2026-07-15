// nav/NavWorld.cpp — Recast bake + Detour query/crowd, behind the pimpl. See header.
//
// This is the ONLY place rc*/dt* types appear (RecastNavigation is linked PRIVATE
// into Cosmic — the Jolt firewall). v1 builds a single-tile "solo" navmesh: it is
// simple, deterministic, and rebakes fast enough for the N2 edit→rebake loop and
// the N5 sample. A tiled build + DetourTileCache dirty-tile rebuild (the vendored
// DetourTileCache module) is the parked follow-up for very large streamed worlds.

#include "nav/NavWorld.h"
#include "core/Log.h"

#include <Recast.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <DetourCommon.h>
#include <DetourStatus.h>
#include <DetourAlloc.h>
#include <DetourCrowd.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>

namespace Cosmic
{
    namespace
    {
        // `.cnav` payload header: [magic][version][tileDataSize] then the raw Detour
        // single-tile data. Bumping kNavVersion invalidates old sidecars (the bake
        // recipe regenerates them — big binary is never hand-edited).
        constexpr uint32_t kNavMagic   = 0x5641'4E43u;   // 'C','N','A','V'
        constexpr uint32_t kNavVersion = 1u;

        constexpr int kMaxPathPolys   = 512;   // per FindPath (v1 cap; documented)
        constexpr int kMaxStraightPts = 512;

        // Deterministic, caller-seeded RNG for RandomPointAround. Detour's
        // findRandomPointAroundCircle wants a `float(*)()`; we point a thread-local
        // at the caller's uint32 state so the draw is reproducible (the two-run
        // bit-match proof) and re-entrancy-safe across threads.
        thread_local uint32_t* t_RngState = nullptr;
        float NavFrand()
        {
            uint32_t x = t_RngState ? *t_RngState : 0x9E3779B9u;
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;   // xorshift32
            if (t_RngState) *t_RngState = x;
            return float(x & 0x00FFFFFFu) / float(0x0100'0000u);
        }

        // Route Recast's build log through the engine logger (warnings/errors only —
        // progress spam stays off). The J1 assert/trace-hook discipline.
        class EngineRcContext : public rcContext
        {
        protected:
            void doLog(const rcLogCategory category, const char* msg, const int /*len*/) override
            {
                if (!msg) return;
                if (category == RC_LOG_ERROR)      CS_CORE_ERROR("Recast: {0}", msg);
                else if (category == RC_LOG_WARNING) CS_CORE_WARN("Recast: {0}", msg);
            }
        };

        inline void ToRc(const glm::vec3& v, float out[3]) { out[0] = v.x; out[1] = v.y; out[2] = v.z; }
        inline glm::vec3 FromRc(const float v[3]) { return glm::vec3(v[0], v[1], v[2]); }
    }

    // ========================================================================
    // Impl — all rc*/dt* state.
    // ========================================================================
    struct NavWorld::Impl
    {
        dtNavMesh*      Mesh  = nullptr;
        dtNavMeshQuery* Query = nullptr;
        dtCrowd*        Crowd = nullptr;

        std::vector<uint8_t> TileData;   // serialize copy (independent of Mesh's owned buffer)
        glm::vec3 BoundsMin{ 0.0f };
        glm::vec3 BoundsMax{ 0.0f };

        // Per-agent bookkeeping (indexed by crowd agent id in [0, maxAgents)).
        std::vector<glm::vec3> AgentTarget;
        std::vector<char>      AgentHasTarget;

        dtQueryFilter Filter;            // default: include 0xffff, area cost 1

        ~Impl() { DestroyAll(); }

        void DestroyCrowd()
        {
            if (Crowd) { dtFreeCrowd(Crowd); Crowd = nullptr; }
            AgentTarget.clear();
            AgentHasTarget.clear();
        }

        void DestroyMesh()
        {
            if (Query) { dtFreeNavMeshQuery(Query); Query = nullptr; }
            if (Mesh)  { dtFreeNavMesh(Mesh);       Mesh  = nullptr; }
            TileData.clear();
            BoundsMin = BoundsMax = glm::vec3(0.0f);
        }

        void DestroyAll() { DestroyCrowd(); DestroyMesh(); }

        // Init Mesh + Query from a Detour single-tile buffer the mesh will OWN
        // (freed with DT_TILE_FREE_DATA). Recomputes bounds. Returns false + frees
        // on failure.
        bool InitFromOwnedTile(unsigned char* data, int dataSize)
        {
            Mesh = dtAllocNavMesh();
            if (!Mesh) { dtFree(data); return false; }
            dtStatus st = Mesh->init(data, dataSize, DT_TILE_FREE_DATA);
            if (dtStatusFailed(st)) { dtFreeNavMesh(Mesh); Mesh = nullptr; return false; }

            Query = dtAllocNavMeshQuery();
            if (!Query || dtStatusFailed(Query->init(Mesh, 4096)))
            {
                DestroyMesh();
                return false;
            }
            ComputeBounds();
            return true;
        }

        void ComputeBounds()
        {
            BoundsMin = glm::vec3(std::numeric_limits<float>::max());
            BoundsMax = glm::vec3(std::numeric_limits<float>::lowest());
            if (!Mesh) { BoundsMin = BoundsMax = glm::vec3(0.0f); return; }
            const dtNavMesh* m = Mesh;   // const overload of getTile is the public one
            bool any = false;
            for (int i = 0; i < m->getMaxTiles(); ++i)
            {
                const dtMeshTile* t = m->getTile(i);
                if (!t || !t->header) continue;
                any = true;
                BoundsMin = glm::min(BoundsMin, FromRc(t->header->bmin));
                BoundsMax = glm::max(BoundsMax, FromRc(t->header->bmax));
            }
            if (!any) BoundsMin = BoundsMax = glm::vec3(0.0f);
        }
    };

    // ========================================================================
    // Lifetime
    // ========================================================================
    NavWorld::NavWorld() : m_Impl(std::make_unique<Impl>()) {}
    NavWorld::~NavWorld() = default;

    bool NavWorld::IsBuilt() const { return m_Impl->Mesh != nullptr; }
    void NavWorld::Clear()          { m_Impl->DestroyAll(); }

    void NavWorld::GetBounds(glm::vec3& outMin, glm::vec3& outMax) const
    {
        outMin = m_Impl->BoundsMin;
        outMax = m_Impl->BoundsMax;
    }

    // ========================================================================
    // Bake (Recast solo pipeline) → Detour navmesh
    // ========================================================================
    bool NavWorld::Build(const NavBuildDesc& desc, const NavGeometryInput& geom, std::string* outError)
    {
        auto fail = [&](const char* why) -> bool
        {
            if (outError) *outError = why;
            CS_CORE_WARN("NavWorld::Build failed: {0}", why);
            return false;
        };

        m_Impl->DestroyAll();

        if (geom.Empty() || geom.Vertices.size() % 3 != 0 || geom.Indices.size() % 3 != 0)
            return fail("empty or malformed geometry");
        if (desc.TileSize > 0.0f)
            CS_CORE_WARN("NavWorld: TileSize>0 requested — v1 builds a single-tile solo mesh (tiled path parked).");

        const float* verts = geom.Vertices.data();
        const int    nverts = geom.VertexCount();
        const int*   tris   = geom.Indices.data();
        const int    ntris  = geom.TriangleCount();

        EngineRcContext ctx;

        // --- config -----------------------------------------------------------
        rcConfig cfg{};
        cfg.cs = desc.CellSize;
        cfg.ch = desc.CellHeight;
        cfg.walkableSlopeAngle = desc.AgentMaxSlopeDeg;
        cfg.walkableHeight     = (int)std::ceil(desc.AgentHeight / cfg.ch);
        cfg.walkableClimb      = (int)std::floor(desc.AgentMaxClimb / cfg.ch);
        cfg.walkableRadius     = (int)std::ceil(desc.AgentRadius / cfg.cs);
        cfg.maxEdgeLen         = (int)(desc.EdgeMaxLen / cfg.cs);
        cfg.maxSimplificationError = desc.EdgeMaxError;
        cfg.minRegionArea      = (int)(desc.RegionMinSize   * desc.RegionMinSize);
        cfg.mergeRegionArea    = (int)(desc.RegionMergeSize * desc.RegionMergeSize);
        cfg.maxVertsPerPoly    = std::clamp(desc.VertsPerPoly, 3, 6);
        cfg.detailSampleDist   = desc.DetailSampleDist < 0.9f ? 0.0f : cfg.cs * desc.DetailSampleDist;
        cfg.detailSampleMaxError = cfg.ch * desc.DetailSampleMaxError;
        cfg.borderSize = 0;   // solo build: no tile border

        rcCalcBounds(verts, nverts, cfg.bmin, cfg.bmax);
        rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);
        if (cfg.width <= 0 || cfg.height <= 0)
            return fail("degenerate grid (bad bounds/cell size)");

        // --- rasterize --------------------------------------------------------
        using HfPtr  = std::unique_ptr<rcHeightfield, decltype(&rcFreeHeightField)>;
        using ChfPtr = std::unique_ptr<rcCompactHeightfield, decltype(&rcFreeCompactHeightfield)>;
        using CsPtr  = std::unique_ptr<rcContourSet, decltype(&rcFreeContourSet)>;
        using PmPtr  = std::unique_ptr<rcPolyMesh, decltype(&rcFreePolyMesh)>;
        using DmPtr  = std::unique_ptr<rcPolyMeshDetail, decltype(&rcFreePolyMeshDetail)>;

        HfPtr hf(rcAllocHeightfield(), rcFreeHeightField);
        if (!hf || !rcCreateHeightfield(&ctx, *hf, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch))
            return fail("rcCreateHeightfield");

        std::vector<unsigned char> areas(ntris, 0);
        rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle, verts, nverts, tris, ntris, areas.data());
        if (!rcRasterizeTriangles(&ctx, verts, nverts, tris, areas.data(), ntris, *hf, cfg.walkableClimb))
            return fail("rcRasterizeTriangles");

        rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *hf);
        rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *hf);
        rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *hf);

        // --- compact + regions ------------------------------------------------
        ChfPtr chf(rcAllocCompactHeightfield(), rcFreeCompactHeightfield);
        if (!chf || !rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, *hf, *chf))
            return fail("rcBuildCompactHeightfield");
        if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf))
            return fail("rcErodeWalkableArea");
        if (!rcBuildDistanceField(&ctx, *chf))
            return fail("rcBuildDistanceField");
        if (!rcBuildRegions(&ctx, *chf, cfg.borderSize, cfg.minRegionArea, cfg.mergeRegionArea))
            return fail("rcBuildRegions");

        // --- contours + poly mesh --------------------------------------------
        CsPtr cset(rcAllocContourSet(), rcFreeContourSet);
        if (!cset || !rcBuildContours(&ctx, *chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset))
            return fail("rcBuildContours");
        if (cset->nconts == 0)
            return fail("no walkable contours (nothing to navigate)");

        PmPtr pmesh(rcAllocPolyMesh(), rcFreePolyMesh);
        if (!pmesh || !rcBuildPolyMesh(&ctx, *cset, cfg.maxVertsPerPoly, *pmesh))
            return fail("rcBuildPolyMesh");

        DmPtr dmesh(rcAllocPolyMeshDetail(), rcFreePolyMeshDetail);
        if (!dmesh || !rcBuildPolyMeshDetail(&ctx, *pmesh, *chf, cfg.detailSampleDist, cfg.detailSampleMaxError, *dmesh))
            return fail("rcBuildPolyMeshDetail");
        if (pmesh->npolys == 0)
            return fail("empty poly mesh");

        // Flag every walkable poly so the default query filter includes it (Detour
        // excludes polys with flags==0). Keep the Recast area id as-is (cost 1).
        for (int i = 0; i < pmesh->npolys; ++i)
            pmesh->flags[i] = (pmesh->areas[i] == RC_WALKABLE_AREA) ? 1 : 0;

        // --- Detour tile data -------------------------------------------------
        dtNavMeshCreateParams params{};
        params.verts        = pmesh->verts;
        params.vertCount    = pmesh->nverts;
        params.polys        = pmesh->polys;
        params.polyAreas    = pmesh->areas;
        params.polyFlags    = pmesh->flags;
        params.polyCount    = pmesh->npolys;
        params.nvp          = pmesh->nvp;
        params.detailMeshes = dmesh->meshes;
        params.detailVerts  = dmesh->verts;
        params.detailVertsCount = dmesh->nverts;
        params.detailTris   = dmesh->tris;
        params.detailTriCount = dmesh->ntris;
        params.walkableHeight = desc.AgentHeight;
        params.walkableRadius = desc.AgentRadius;
        params.walkableClimb  = desc.AgentMaxClimb;
        rcVcopy(params.bmin, pmesh->bmin);
        rcVcopy(params.bmax, pmesh->bmax);
        params.cs = cfg.cs;
        params.ch = cfg.ch;
        params.buildBvTree = true;

        unsigned char* navData = nullptr;
        int navDataSize = 0;
        if (!dtCreateNavMeshData(&params, &navData, &navDataSize) || !navData)
            return fail("dtCreateNavMeshData (poly count / verts-per-poly out of range?)");

        // Keep an independent serialize copy, then hand the buffer to the mesh.
        m_Impl->TileData.assign(navData, navData + navDataSize);
        if (!m_Impl->InitFromOwnedTile(navData, navDataSize))
        {
            m_Impl->TileData.clear();
            return fail("dtNavMesh::init");
        }

        CS_CORE_INFO("NavWorld: baked {0} polys ({1} tris in) -> {2} KB",
                     pmesh->npolys, ntris, navDataSize / 1024);
        return true;
    }

    // ========================================================================
    // Serialize / Load (the `.cnav` sidecar payload)
    // ========================================================================
    NavMeshData NavWorld::Serialize() const
    {
        NavMeshData out;
        if (m_Impl->TileData.empty())
            return out;
        const uint32_t sz = (uint32_t)m_Impl->TileData.size();
        out.Bytes.resize(sizeof(uint32_t) * 3 + sz);
        uint8_t* p = out.Bytes.data();
        std::memcpy(p + 0,  &kNavMagic,   sizeof(uint32_t));
        std::memcpy(p + 4,  &kNavVersion, sizeof(uint32_t));
        std::memcpy(p + 8,  &sz,          sizeof(uint32_t));
        std::memcpy(p + 12, m_Impl->TileData.data(), sz);
        return out;
    }

    bool NavWorld::Load(const NavMeshData& data)
    {
        m_Impl->DestroyAll();
        if (data.Bytes.size() < sizeof(uint32_t) * 3)
            return false;

        uint32_t magic = 0, version = 0, sz = 0;
        const uint8_t* p = data.Bytes.data();
        std::memcpy(&magic,   p + 0, sizeof(uint32_t));
        std::memcpy(&version, p + 4, sizeof(uint32_t));
        std::memcpy(&sz,      p + 8, sizeof(uint32_t));
        if (magic != kNavMagic || version != kNavVersion)
        {
            CS_CORE_WARN("NavWorld::Load: bad magic/version (stale .cnav — rebake).");
            return false;
        }
        if (data.Bytes.size() != sizeof(uint32_t) * 3 + size_t(sz) || sz == 0)
            return false;

        // Detour needs a dtAlloc'd buffer it can own + free (DT_TILE_FREE_DATA).
        unsigned char* buf = (unsigned char*)dtAlloc(sz, DT_ALLOC_PERM);
        if (!buf) return false;
        std::memcpy(buf, p + 12, sz);

        m_Impl->TileData.assign(p + 12, p + 12 + sz);   // serialize copy
        if (!m_Impl->InitFromOwnedTile(buf, (int)sz))
        {
            m_Impl->TileData.clear();
            return false;
        }
        return true;
    }

    // ========================================================================
    // Queries
    // ========================================================================
    NavPath NavWorld::FindPath(const glm::vec3& a, const glm::vec3& b) const
    {
        NavPath result;
        if (!m_Impl->Query) return result;

        const glm::vec3 he(2.0f, 4.0f, 2.0f);
        float sPos[3], ePos[3], hx[3];
        ToRc(a, sPos); ToRc(b, ePos); ToRc(he, hx);

        dtPolyRef startRef = 0, endRef = 0;
        float sNear[3], eNear[3];
        m_Impl->Query->findNearestPoly(sPos, hx, &m_Impl->Filter, &startRef, sNear);
        m_Impl->Query->findNearestPoly(ePos, hx, &m_Impl->Filter, &endRef, eNear);
        if (!startRef || !endRef)
            return result;   // no polygon under an endpoint — a clean miss

        dtPolyRef polys[kMaxPathPolys];
        int npolys = 0;
        m_Impl->Query->findPath(startRef, endRef, sNear, eNear, &m_Impl->Filter,
                                polys, &npolys, kMaxPathPolys);
        if (npolys == 0)
            return result;

        // If the last polygon isn't the goal poly, the goal is unreachable — clamp
        // the straight-path target to the nearest point on the last reachable poly.
        float straightTarget[3];
        std::memcpy(straightTarget, eNear, sizeof(straightTarget));
        result.Reached = (polys[npolys - 1] == endRef);
        result.Partial = !result.Reached;
        if (result.Partial)
            m_Impl->Query->closestPointOnPoly(polys[npolys - 1], eNear, straightTarget, nullptr);

        float straight[kMaxStraightPts * 3];
        unsigned char flags[kMaxStraightPts];
        dtPolyRef refs[kMaxStraightPts];
        int nstraight = 0;
        m_Impl->Query->findStraightPath(sNear, straightTarget, polys, npolys,
                                        straight, flags, refs, &nstraight, kMaxStraightPts);

        result.Corners.reserve(nstraight);
        for (int i = 0; i < nstraight; ++i)
            result.Corners.push_back(glm::vec3(straight[i * 3], straight[i * 3 + 1], straight[i * 3 + 2]));

        result.Length = 0.0f;
        for (size_t i = 1; i < result.Corners.size(); ++i)
            result.Length += glm::distance(result.Corners[i - 1], result.Corners[i]);

        return result;
    }

    NavRayHit NavWorld::Raycast(const glm::vec3& a, const glm::vec3& b) const
    {
        NavRayHit hit;
        if (!m_Impl->Query) return hit;

        const glm::vec3 he(2.0f, 4.0f, 2.0f);
        float sPos[3], ePos[3], hx[3];
        ToRc(a, sPos); ToRc(b, ePos); ToRc(he, hx);

        dtPolyRef startRef = 0;
        float sNear[3];
        m_Impl->Query->findNearestPoly(sPos, hx, &m_Impl->Filter, &startRef, sNear);
        if (!startRef) { hit.Hit = true; hit.T = 0.0f; hit.Point = a; return hit; }

        float t = 0.0f, normal[3] = { 0, 0, 0 };
        dtPolyRef path[kMaxPathPolys];
        int npath = 0;
        m_Impl->Query->raycast(startRef, sNear, ePos, &m_Impl->Filter, &t, normal, path, &npath, kMaxPathPolys);

        if (t >= std::numeric_limits<float>::max() || t >= 1.0f)
        {
            hit.Hit = false; hit.T = 1.0f; hit.Point = b;
        }
        else
        {
            hit.Hit = true; hit.T = t; hit.Point = a + (b - a) * t;
        }
        return hit;
    }

    std::optional<glm::vec3> NavWorld::NearestPoint(const glm::vec3& p, const glm::vec3& halfExtents) const
    {
        if (!m_Impl->Query) return std::nullopt;
        float pos[3], hx[3], nearest[3];
        ToRc(p, pos); ToRc(halfExtents, hx);
        dtPolyRef ref = 0;
        m_Impl->Query->findNearestPoly(pos, hx, &m_Impl->Filter, &ref, nearest);
        if (!ref) return std::nullopt;
        return FromRc(nearest);
    }

    std::optional<glm::vec3> NavWorld::RandomPointAround(const glm::vec3& center, float radius, uint32_t& rngState) const
    {
        if (!m_Impl->Query) return std::nullopt;
        const glm::vec3 he(radius + 1.0f, 4.0f, radius + 1.0f);
        float pos[3], hx[3];
        ToRc(center, pos); ToRc(he, hx);
        dtPolyRef startRef = 0;
        float startPt[3];
        m_Impl->Query->findNearestPoly(pos, hx, &m_Impl->Filter, &startRef, startPt);
        if (!startRef) return std::nullopt;

        t_RngState = &rngState;                 // deterministic, caller-seeded
        dtPolyRef randRef = 0;
        float pt[3];
        dtStatus st = m_Impl->Query->findRandomPointAroundCircle(
            startRef, pos, radius, &m_Impl->Filter, NavFrand, &randRef, pt);
        t_RngState = nullptr;
        if (dtStatusFailed(st) || !randRef) return std::nullopt;
        return FromRc(pt);
    }

    void NavWorld::GetDebugTriangles(std::vector<NavDebugTri>& out) const
    {
        const dtNavMesh* mesh = m_Impl->Mesh;
        if (!mesh) return;
        for (int ti = 0; ti < mesh->getMaxTiles(); ++ti)
        {
            const dtMeshTile* tile = mesh->getTile(ti);
            if (!tile || !tile->header) continue;
            for (int i = 0; i < tile->header->polyCount; ++i)
            {
                const dtPoly* poly = &tile->polys[i];
                if (poly->getType() == DT_POLYTYPE_OFFMESH_CONNECTION) continue;
                const dtPolyDetail* pd = &tile->detailMeshes[i];
                for (int j = 0; j < pd->triCount; ++j)
                {
                    const unsigned char* t = &tile->detailTris[(pd->triBase + j) * 4];
                    glm::vec3 v[3];
                    for (int k = 0; k < 3; ++k)
                    {
                        if (t[k] < poly->vertCount)
                            v[k] = FromRc(&tile->verts[poly->verts[t[k]] * 3]);
                        else
                            v[k] = FromRc(&tile->detailVerts[(pd->vertBase + (t[k] - poly->vertCount)) * 3]);
                    }
                    out.push_back(NavDebugTri{ v[0], v[1], v[2] });
                }
            }
        }
    }

    // ========================================================================
    // Crowd (N4)
    // ========================================================================
    void NavWorld::CrowdInit(float maxAgentRadius, int maxAgents)
    {
        m_Impl->DestroyCrowd();
        if (!m_Impl->Mesh) return;
        m_Impl->Crowd = dtAllocCrowd();
        if (!m_Impl->Crowd) return;
        if (!m_Impl->Crowd->init(std::max(1, maxAgents), std::max(0.1f, maxAgentRadius), m_Impl->Mesh))
        {
            m_Impl->DestroyCrowd();
            return;
        }
        m_Impl->AgentTarget.assign(std::max(1, maxAgents), glm::vec3(0.0f));
        m_Impl->AgentHasTarget.assign(std::max(1, maxAgents), 0);
    }

    void NavWorld::CrowdShutdown() { m_Impl->DestroyCrowd(); }
    bool NavWorld::CrowdReady() const { return m_Impl->Crowd != nullptr; }

    int NavWorld::AddAgent(const glm::vec3& pos, const NavAgentParams& params)
    {
        if (!m_Impl->Crowd) return -1;

        // Snap the spawn to the navmesh so the agent starts on a valid poly.
        glm::vec3 spawn = pos;
        if (auto np = NearestPoint(pos)) spawn = *np;

        dtCrowdAgentParams ap{};
        ap.radius = params.Radius;
        ap.height = params.Height;
        ap.maxAcceleration = params.MaxAccel;
        ap.maxSpeed = params.MaxSpeed;
        ap.collisionQueryRange = params.Radius * 12.0f;
        ap.pathOptimizationRange = params.Radius * 30.0f;
        ap.separationWeight = params.SeparationWeight;
        ap.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OPTIMIZE_VIS |
                         DT_CROWD_OPTIMIZE_TOPO   | DT_CROWD_OBSTACLE_AVOIDANCE |
                         DT_CROWD_SEPARATION;
        ap.obstacleAvoidanceType = 3;

        float p[3]; ToRc(spawn, p);
        int id = m_Impl->Crowd->addAgent(p, &ap);
        if (id >= 0 && id < (int)m_Impl->AgentHasTarget.size())
            m_Impl->AgentHasTarget[id] = 0;
        return id;
    }

    void NavWorld::RemoveAgent(int agentId)
    {
        if (!m_Impl->Crowd || agentId < 0) return;
        m_Impl->Crowd->removeAgent(agentId);
        if (agentId < (int)m_Impl->AgentHasTarget.size())
            m_Impl->AgentHasTarget[agentId] = 0;
    }

    void NavWorld::SetAgentTarget(int agentId, const glm::vec3& target)
    {
        if (!m_Impl->Crowd || !m_Impl->Query || agentId < 0) return;

        const glm::vec3 he(2.0f, 4.0f, 2.0f);
        float pos[3], hx[3], nearest[3];
        ToRc(target, pos); ToRc(he, hx);
        dtPolyRef ref = 0;
        m_Impl->Query->findNearestPoly(pos, hx, &m_Impl->Filter, &ref, nearest);
        if (!ref) return;

        if (m_Impl->Crowd->requestMoveTarget(agentId, ref, nearest) &&
            agentId < (int)m_Impl->AgentHasTarget.size())
        {
            m_Impl->AgentTarget[agentId]    = FromRc(nearest);
            m_Impl->AgentHasTarget[agentId] = 1;
        }
    }

    void NavWorld::ResetAgentTarget(int agentId)
    {
        if (!m_Impl->Crowd || agentId < 0) return;
        m_Impl->Crowd->resetMoveTarget(agentId);
        if (agentId < (int)m_Impl->AgentHasTarget.size())
            m_Impl->AgentHasTarget[agentId] = 0;
    }

    void NavWorld::UpdateCrowd(float dt)
    {
        if (m_Impl->Crowd) m_Impl->Crowd->update(dt, nullptr);
    }

    glm::vec3 NavWorld::GetAgentPosition(int agentId) const
    {
        if (!m_Impl->Crowd || agentId < 0) return glm::vec3(0.0f);
        const dtCrowdAgent* ag = m_Impl->Crowd->getAgent(agentId);
        return (ag && ag->active) ? FromRc(ag->npos) : glm::vec3(0.0f);
    }

    glm::vec3 NavWorld::GetAgentVelocity(int agentId) const
    {
        if (!m_Impl->Crowd || agentId < 0) return glm::vec3(0.0f);
        const dtCrowdAgent* ag = m_Impl->Crowd->getAgent(agentId);
        return (ag && ag->active) ? FromRc(ag->vel) : glm::vec3(0.0f);
    }

    bool NavWorld::AgentHasTarget(int agentId) const
    {
        return agentId >= 0 && agentId < (int)m_Impl->AgentHasTarget.size() && m_Impl->AgentHasTarget[agentId];
    }

    float NavWorld::AgentDistanceToTarget(int agentId) const
    {
        if (!AgentHasTarget(agentId)) return std::numeric_limits<float>::max();
        return glm::distance(GetAgentPosition(agentId), m_Impl->AgentTarget[agentId]);
    }
}
