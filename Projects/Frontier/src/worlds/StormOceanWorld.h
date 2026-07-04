#pragma once

// StormOceanWorld — heavy weather at sea (Phase 11, doc 10 item F16).
//
// Open water only (no terrain): a large 8-wave Gerstner swell with whitecaps
// (water v2, u_WhitecapStrength), driving rain (velocity-stretched particles,
// F9) with splash rings on the surface, periodic lightning (cold sun + sky
// pulse + delayed thunder via LightningDirector/F10), a storm-grey sky, and a
// buoy bobbing on SampleHeight/SampleNormal to sell the swell scale. Diving the
// camera below the surface tints/fogs (F6 underwater). The water-v2 stress test.

#include "World.h"

#include "common/LightningDirector.h"   // F16 — strike scheduling + delayed thunder
#include "common/DistanceLoop.h"        // F10 — wind + rain ambience

namespace Frontier
{
    class StormOceanWorld : public World
    {
    public:
        const WorldInfo& GetInfo() const override;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(WorldContext& ctx) override;
        void OnPanels(WorldContext& ctx) override;

    private:
        void BuildContent();

        bool m_ContentBuilt = false;

        Cosmic::Ref<Cosmic::Water> m_Ocean;

        // Rain (camera-tracking box) + surface splash rings.
        Cosmic::Ref<Cosmic::ParticleEmitter> m_Rain;
        Cosmic::Ref<Cosmic::ParticleEmitter> m_Splash;

        // Buoy: box hull + mast, riding the swell (SampleHeight/SampleNormal).
        Cosmic::Ref<Cosmic::Mesh>     m_BuoyMesh;
        Cosmic::Ref<Cosmic::Material> m_BuoyMat;
        glm::vec2                     m_BuoyXZ{ 0.0f, 0.0f };

        LightningDirector m_Lightning;
        DistanceLoop      m_Wind;
        DistanceLoop      m_RainAmb;

        Cosmic::SceneRendererSettings m_Settings;
        Cosmic::SkyDetailDesc         m_Sky;

        bool  m_RainOn            = true;
        bool  m_UnderwaterEnabled = true;
        float m_Exposure          = 1.0f;
    };

} // namespace Frontier
