#pragma once

// MirrorLakeWorld — the "realistic water" money shot (Phase 11, doc 10 item F15).
//
// A glass-calm alpine lake at golden hour: near-zero Gerstner amplitude so the
// high-res planar reflection reads like a mirror, drifting mist banks over the
// surface, long god rays through an instanced pine shoreline, water-v2 caustics
// on the visible lakebed, and a gentle fish splash-ring every so often. The
// showcase for F6's water v2 + the planar reflection.
//
// The basin terrain (WorldSize 1024) + lake build on a background job — the
// loading overlay animates until ready; the pines/mist assemble on the main
// thread the frame the terrain is adopted.

#include "World.h"

#include "common/Scatter.h"        // F5 — instanced pine shoreline
#include "common/DistanceLoop.h"   // F10 — lapping-water + wind ambience

#include <atomic>
#include <memory>

namespace Frontier
{
    class MirrorLakeWorld : public World
    {
    public:
        const WorldInfo& GetInfo() const override;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(WorldContext& ctx) override;
        void OnPanels(WorldContext& ctx) override;
        bool IsLoading() const override;

        struct LoadResult
        {
            Cosmic::Ref<Cosmic::Terrain> Terrain;
            Cosmic::Ref<Cosmic::Water>   Lake;
            glm::vec2         LakeCenter{ 0.0f };
            float             LakeRadius   = 0.0f;
            float             LakeSurfaceY = 0.0f;
            std::atomic<bool> Ready{ false };
        };

    private:
        void BuildContent();

        std::shared_ptr<LoadResult>  m_Load;
        int                          m_RevealFrames = 0;
        Cosmic::Ref<Cosmic::Terrain> m_Terrain;
        Cosmic::Ref<Cosmic::Water>   m_Lake;
        glm::vec2                    m_LakeCenter{ 0.0f };
        float                        m_LakeRadius   = 0.0f;
        float                        m_LakeSurfaceY = 0.0f;

        bool m_ContentBuilt = false;

        ScatterField                         m_Pines;
        Cosmic::Ref<Cosmic::ParticleEmitter> m_Mist[3];
        Cosmic::Ref<Cosmic::ParticleEmitter> m_Splash;      // fish rings (Burst on a timer)
        float                                m_SplashTimer = 3.0f;
        Cosmic::Random                       m_Rng{ 0x11A6Eu };

        DistanceLoop m_WaterAmb;    // lake lapping (source = lake centre)
        DistanceLoop m_Wind;        // soft constant breeze

        Cosmic::SceneRendererSettings m_Settings;
        Cosmic::SkyDetailDesc         m_Sky;

        float m_TimeHours       = 6.8f;   // golden hour (panel-adjustable)
        float m_PlaySpeed       = 0.5f;
        bool  m_Playing         = false;
        float m_Exposure        = 1.0f;
        bool  m_UnderwaterEnabled = true;
    };

} // namespace Frontier
