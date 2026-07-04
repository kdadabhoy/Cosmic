#pragma once

// BlizzardWorld — the "realistic snow" money shot (Phase 11, doc 10 item F14).
//
// A whiteout mountain storm: dense wind-blown snow (velocity-stretched
// particles in a box emitter tracking the camera), the S11.1 snow overlay on
// everything, DYNAMIC accumulation visibly building on a cabin + instanced
// pines (CoverageCapture, F8), heavy fog, grey overcast sky, warm emissive
// window lights. The showcase for F8's accumulation mask.
//
// The ridged mountain terrain (WorldSize 768) builds on a background job — the
// loading overlay animates until it is ready; the cabin/pines/coverage assemble
// on the main thread the frame the terrain is adopted.

#include "World.h"

#include "common/Scatter.h"   // F5/F12c — instanced pine field (frustum-culled)

#include <atomic>
#include <memory>

namespace Frontier
{
    class BlizzardWorld : public World
    {
    public:
        const WorldInfo& GetInfo() const override;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(WorldContext& ctx) override;
        void OnPanels(WorldContext& ctx) override;
        bool IsLoading() const override;

        // Async terrain build. Held by shared_ptr so the job survives a detach
        // mid-load; the job captures a seed copy (never the world).
        struct LoadResult
        {
            Cosmic::Ref<Cosmic::Terrain> Terrain;
            std::atomic<bool>            Ready{ false };
        };

    private:
        void BuildContent();     // one-time main-thread cabin/pines/coverage assembly

        std::shared_ptr<LoadResult>  m_Load;
        int                          m_RevealFrames = 0;
        Cosmic::Ref<Cosmic::Terrain> m_Terrain;

        bool m_ContentBuilt = false;

        // Cabin (box body + gable roof with sheltering eaves) + warm window quads.
        Cosmic::Ref<Cosmic::Mesh>     m_CabinMesh, m_WindowMesh;
        Cosmic::Ref<Cosmic::Material> m_CabinMat,  m_WindowMat;
        glm::mat4                     m_CabinXform{ 1.0f };

        ScatterField m_Pines;                      // F5 — instanced snowy pines

        // F8 — top-down coverage accumulation mask (snow builds on top surfaces;
        // sheltered ground under the eaves stays bare via the mask's Y-rejection).
        Cosmic::CoverageCapture              m_Coverage;
        Cosmic::Ref<Cosmic::ParticleEmitter> m_Snowfall;   // camera-tracking box

        Cosmic::Renderer3D::SnowDesc  m_Snow;      // scene-wide overlay (mask-driven)
        Cosmic::SceneRendererSettings m_Settings;
        Cosmic::SkyDetailDesc         m_Sky;       // grey overcast (desc.DetailedSky)

        // Gusting wind ambience (raw looping voice + a volume LFO). Headless-safe.
        Cosmic::SoundHandle m_WindVoice = Cosmic::InvalidSoundHandle;

        // Accumulation state (panel-controlled).
        float m_Accum        = 0.0f;      // 0..1 fraction toward full coverage
        float m_AccumSeconds = 45.0f;     // seconds to reach full accumulation
        bool  m_Accumulate   = true;
        bool  m_ShowSnowfall = true;      // submit the falling-snow emitter
        float m_SnowfallRate = 4000.0f;   // fixed at Create (no runtime rate setter)
        float m_Exposure     = 1.15f;
    };

} // namespace Frontier
