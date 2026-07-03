#pragma once

// IslandWorld — THE flagship seamless world (Phase 11, doc 10 items F11 + F12a/b/c).
//
// One ~4x4 km island containing every Phase 11 system at once:
//   F11  — composed heightfield: volcano cone + caldera, ridged snow range,
//          alpine lake basin, beach shelf + ocean floor, carved river channel.
//   F12a — terrain + ocean + lake (water v2) + detailed sky + time-of-day.
//   F12b — the volcano: FlowEmissive lava strips marched downhill via
//          Terrain::SampleHeight/Normal, caldera lava lake, smoke column,
//          embers, heat haze, steam fumaroles, ambience rumble (F10).
//   F12c — instanced pine forests + rocks (F5, frustum-culled), waterfall +
//          river (WaterFlow sheets) + mist, wildlife (boids birds, fish
//          splash rings, dusk fireflies).
//
// F2 STOPGAP: a first real SceneRenderer-driven scene — procedural fBm island
// terrain + an ocean-sized water body + a smoke plume + a few monolith shadow
// casters, sky/IBL/shadows/bloom/fog on, a time-of-day scrub. The F11/F12 orders
// replace this composed terrain + content.

#include "World.h"

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

    private:
        // Scene content (F2 stopgap).
        Cosmic::Ref<Cosmic::Terrain>         m_Terrain;
        Cosmic::Ref<Cosmic::Water>           m_Water;
        Cosmic::Ref<Cosmic::ParticleEmitter> m_Smoke;
        Cosmic::Ref<Cosmic::Mesh>            m_Rock;   // shared monolith caster mesh
        std::vector<glm::mat4>               m_RockXforms;
        glm::vec3                            m_SmokePos{ 0.0f };

        // Render policy (edited by the world panel).
        Cosmic::SceneRendererSettings m_Settings;
        float     m_TimeHours = 12.0f;                    // time-of-day scrub (0..24)
        float     m_Exposure  = 1.0f;
        glm::vec3 m_LightDir{ -0.4f, -1.0f, -0.3f };      // direction the sun TRAVELS
    };

} // namespace Frontier
