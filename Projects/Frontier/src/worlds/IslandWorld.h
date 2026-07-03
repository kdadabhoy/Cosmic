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
        // Scene content.
        Cosmic::Ref<Cosmic::Terrain> m_Terrain;
        Cosmic::Ref<Cosmic::Water>   m_Ocean;
        Cosmic::Ref<Cosmic::Water>   m_Lake;

        // Island shape + derived lake placement (world space).
        IslandParams m_Island;
        glm::vec2    m_LakeCenterWorld{ 0.0f };
        float        m_LakeRadiusWorld = 0.0f;
        float        m_LakeSurfaceY    = 0.0f;

        // Render policy + per-frame detailed-sky (desc.DetailedSky points at m_Sky,
        // so it must outlive the Render call — a member does).
        Cosmic::SceneRendererSettings m_Settings;
        Cosmic::SkyDetailDesc         m_Sky;

        // Time-of-day (edited by the world panel).
        float m_TimeHours  = 8.0f;    // 0..24 clock
        float m_PlaySpeed  = 1.0f;    // hours advanced per real second while playing
        bool  m_Playing    = false;
        float m_Exposure   = 1.0f;
        bool  m_Underwater = true;    // tint/fog the frame when the camera dives below the sea
    };

} // namespace Frontier
