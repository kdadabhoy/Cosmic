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
// SKELETON: renders the shared placeholder until F12a replaces OnUpdate with
// the SceneRenderer-driven frame (F2).

#include "World.h"

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
    };

} // namespace Frontier
