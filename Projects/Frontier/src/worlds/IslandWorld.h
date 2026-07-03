#pragma once

// IslandWorld — THE flagship seamless world (Phase 11, doc 10 items F11 + F12a/b/c).
//
// One ~4x4 km island containing every Phase 11 system at once:
//   F11  — composed heightfield: volcano cone + caldera, ridged snow range,
//          alpine lake basin, beach shelf + ocean floor, carved river channel
//          (common/HeightfieldComposer.h -> TerrainSpecification::HeightFunction).
//   F12a — terrain + ocean + lake (water v2) + detailed sky + time-of-day
//          (common/DayNightCycle.h): THIS assembly.
//   F12b — the volcano: FlowEmissive lava strips, caldera lava lake, smoke
//          column, embers, heat haze, steam fumaroles, ambience rumble (F10).
//   F12c — instanced pine forests + rocks (F5, frustum-culled), waterfall +
//          river (WaterFlow sheets) + mist, wildlife.

#include "World.h"

#include "common/HeightfieldComposer.h"   // IslandParams (the island shape)
#include "common/VolcanoScene.h"          // F12b — lava flows/lake, smoke, embers, haze, rumble
#include "common/Scatter.h"               // F12c — instanced pine/boulder fields (F5 culled)
#include "common/Boids.h"                 // F12c — wheeling bird flock
#include "common/DistanceLoop.h"          // F12c — waterfall babble

#include <atomic>
#include <memory>
#include <vector>

namespace Frontier
{
    class IslandWorld : public World
    {
    public:
        const WorldInfo& GetInfo() const override;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(WorldContext& ctx) override;
        void OnPanels(WorldContext& ctx) override;
        bool IsLoading() const override;

        // Async build result — the heavy terrain (~2049², a few seconds of pure CPU)
        // and the waters are built on a JobSystem worker so the main thread keeps
        // pumping frames (the loading overlay animates). Held by shared_ptr so the
        // job stays valid even if the world is detached mid-load; the job captures a
        // COPY of IslandParams (never the world) so it is fully self-contained.
        // Public so the file-local build helper (IslandWorld.cpp) can fill it.
        struct LoadResult
        {
            Cosmic::Ref<Cosmic::Terrain> Terrain;
            Cosmic::Ref<Cosmic::Water>   Ocean;
            Cosmic::Ref<Cosmic::Water>   Lake;
            glm::vec2         LakeCenterWorld{ 0.0f };
            float             LakeRadiusWorld = 0.0f;
            float             LakeSurfaceY    = 0.0f;
            std::atomic<bool> Ready{ false };
        };

    private:
        std::shared_ptr<LoadResult> m_Load;
        int m_RevealFrames = 0;     // keep the overlay up a few frames post-build to
                                    // hide the first-frame GPU/shader-compile hitch

        // Scene content (adopted from m_Load once its CPU build completes).
        Cosmic::Ref<Cosmic::Terrain> m_Terrain;
        Cosmic::Ref<Cosmic::Water>   m_Ocean;
        Cosmic::Ref<Cosmic::Water>   m_Lake;

        // Island shape + derived lake placement (world space).
        IslandParams m_Island;
        glm::vec2    m_LakeCenterWorld{ 0.0f };
        float        m_LakeRadiusWorld = 0.0f;
        float        m_LakeSurfaceY    = 0.0f;

        // --- Volcano + forests + waterfall + wildlife (F12b/c). Built once on the
        //     main thread the frame the async terrain is adopted (needs a GL context
        //     for the meshes/emitters; the terrain queries are CPU-ready by then). ---
        bool m_ContentBuilt = false;
        void BuildContent();     // one-time GPU/content assembly (IslandWorld.cpp)

        VolcanoScene m_Volcano;                         // F12b

        ScatterField m_Pines;                           // F12c — instanced forests
        ScatterField m_Boulders;

        // Waterfall + river (WaterFlow.glsl sheets, drawn in the transparent pass).
        Cosmic::Ref<Cosmic::Mesh>     m_RiverMesh, m_FallMesh;
        Cosmic::Ref<Cosmic::Material> m_RiverMat,  m_FallMat;
        Cosmic::Ref<Cosmic::ParticleEmitter> m_Mist, m_Spray;
        DistanceLoop m_Babble;
        glm::vec3    m_PlungePool{ 0.0f };

        // Wildlife: a wheeling bird flock + lake fish splash rings + shore fireflies.
        Boids                                m_Birds;
        Cosmic::Ref<Cosmic::Mesh>            m_BirdMesh;
        Cosmic::Ref<Cosmic::Material>        m_BirdMat;
        Cosmic::Ref<Cosmic::InstanceSet>     m_BirdSet;
        Cosmic::Ref<Cosmic::ParticleEmitter> m_Splash;      // fish rings (Burst on a timer)
        Cosmic::Ref<Cosmic::ParticleEmitter> m_Fireflies;   // night only
        float m_SplashTimer = 3.0f;
        Cosmic::Random m_Rng{ 0xF15Eu };                    // fish-point picker

        // Snow overlay (F8): dust the high pines/boulders above the snow line.
        Cosmic::Renderer3D::SnowDesc m_Snow;

        // Render policy + per-frame detailed-sky (desc.DetailedSky points at m_Sky,
        // so it must outlive the Render call — a member does).
        Cosmic::SceneRendererSettings m_Settings;
        Cosmic::SkyDetailDesc         m_Sky;

        // Time-of-day (edited by the world panel).
        float m_TimeHours  = 8.0f;    // 0..24 clock
        float m_PlaySpeed  = 1.0f;    // hours advanced per real second while playing
        bool  m_Playing    = false;
        float m_Exposure         = 1.0f;
        bool  m_UnderwaterEnabled = true;   // tint/fog/caustics when the camera dives below the sea
    };

} // namespace Frontier
