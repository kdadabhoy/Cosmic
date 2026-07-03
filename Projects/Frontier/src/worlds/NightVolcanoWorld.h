#pragma once

// NightVolcanoWorld — the "realistic volcano" money shot (Phase 11, doc 10 item F13).
//
// A dusk-to-night close-up of a caldera: the FlowEmissive lava lake and flows
// dominate the exposure (bloom does the glow), an ember storm (additive
// particles), god rays through the smoke column lit by three pulsing point
// lights, strong heat haze over every vent, the SkyDetail star field + moon
// overhead, and the F10 rumble loop swelling with proximity. Reuses the shared
// VolcanoScene builder (F12b) at closer range with night-tuned lighting/exposure.
//
// The close-in caldera terrain (WorldSize 1024, a lone re-centred volcano) builds
// on a background job — the loading overlay animates until it is ready.

#include "World.h"

#include "common/VolcanoScene.h"      // F12b — shared lava/smoke/ember/haze/rumble builder

#include <atomic>
#include <memory>

namespace Frontier
{
    class NightVolcanoWorld : public World
    {
    public:
        const WorldInfo& GetInfo() const override;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(WorldContext& ctx) override;
        void OnPanels(WorldContext& ctx) override;
        bool IsLoading() const override;

        // Async terrain build (lone volcano cone + caldera). Held by shared_ptr so
        // the job survives a detach mid-load; captures only a seed (never the world).
        struct LoadResult
        {
            Cosmic::Ref<Cosmic::Terrain> Terrain;
            std::atomic<bool>            Ready{ false };
        };

    private:
        void BuildContent();     // one-time main-thread volcano assembly

        std::shared_ptr<LoadResult>  m_Load;
        int                          m_RevealFrames = 0;
        Cosmic::Ref<Cosmic::Terrain> m_Terrain;

        VolcanoScene m_Volcano;
        bool         m_ContentBuilt = false;

        Cosmic::SceneRendererSettings m_Settings;
        Cosmic::SkyDetailDesc         m_Sky;

        // Locked to dusk by default; panel-overridable.
        float m_TimeHours = 20.5f;
        float m_Exposure  = 0.9f;
    };

} // namespace Frontier
