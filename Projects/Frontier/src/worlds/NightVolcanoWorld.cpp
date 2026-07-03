// NightVolcanoWorld.cpp — night caldera close-up (F13). See NightVolcanoWorld.h.
//
// Reuses the shared VolcanoScene builder (F12b) on a small, lone-volcano terrain,
// under a fixed dusk/night sky with the exposure tuned so the lava carries the
// frame. The heavy geometry (terrain) builds on a JobSystem worker behind the
// loading overlay; the volcano meshes/emitters assemble on the main thread once
// the terrain is live.

#include "worlds/NightVolcanoWorld.h"

#include "common/HeightfieldComposer.h"   // IslandHeight (reused for a lone volcano)
#include "common/DayNightCycle.h"

#include "ui/IconsLucide.h"

#include "jobs/JobSystem.h"

#include <imgui.h>

#include <cmath>

namespace Frontier
{
    // ---- Terrain shape (a lone volcano islet) ---------------------------------
    static constexpr float kWorldSize   = 1024.0f;
    static constexpr float kHeightScale = 640.0f;
    static constexpr float kBaseHeight  = -60.0f;
    static constexpr uint32_t kSeed     = 424242u;

    // Volcano-only island params (no range / lake / river) centred in the terrain.
    static IslandParams MakeVolcanoParams()
    {
        IslandParams p;
        p.Seed          = kSeed;
        p.VolcanoCenter = { 0.5f, 0.5f };
        p.VolcanoRadius = 0.34f;
        p.VolcanoHeight = 1.0f;
        p.CalderaRadius = 0.10f;
        p.CalderaDepth  = 0.30f;
        p.RangeHeight   = 0.0f;      // no snow range
        p.LakeDepth     = 0.0f;      // no lake carve
        p.RiverDepth    = 0.0f;
        p.RiverPath.clear();         // no river
        p.SeaShelf      = 0.14f;
        return p;
    }

    const WorldInfo& NightVolcanoWorld::GetInfo() const
    {
        static const WorldInfo info{
            "Night Volcano",
            ICON_LC_FLAME,
            "Glowing caldera at night — lava, embers, god rays through smoke",
            { 0.0f, 650.0f, 520.0f },   // offshore + above, looking down into the caldera
            0.0f, -20.0f,
            { 0.020f, 0.012f, 0.016f, 1.0f }
        };
        return info;
    }

    // Build the small terrain on a worker thread (CPU-only; GPU resources lazy).
    static void BuildNightTerrain(IslandParams params, NightVolcanoWorld::LoadResult& out)
    {
        Cosmic::TerrainSpecification tspec;
        tspec.Resolution     = 1025;
        tspec.WorldSize      = kWorldSize;
        tspec.HeightScale    = kHeightScale;
        tspec.BaseHeight     = kBaseHeight;
        tspec.HeightFunction = [params](float u, float v) { return IslandHeight(params, u, v); };

        tspec.Layers[0] = { { 0.20f, 0.16f, 0.14f }, 0.35f, nullptr };   // dark volcanic soil
        tspec.Layers[1] = { { 0.14f, 0.13f, 0.14f }, 0.45f, nullptr };   // basalt
        tspec.Layers[2] = { { 0.55f, 0.35f, 0.28f }, 0.28f, nullptr };   // ashy rim
        tspec.Layers[3] = { { 0.24f, 0.20f, 0.18f }, 0.40f, nullptr };   // beach ash
        tspec.Material.HighHeight = 480.0f;
        tspec.Material.HighBlend  = 60.0f;
        tspec.Material.LowHeight  = 3.0f;
        tspec.Material.LowBlend   = 3.0f;
        out.Terrain = Cosmic::Terrain::Create(tspec);

        out.Ready.store(true, std::memory_order_release);
    }

    void NightVolcanoWorld::OnAttach()
    {
        m_Load = std::make_shared<LoadResult>();
        m_RevealFrames = 0;
        m_ContentBuilt = false;
        auto load = m_Load;
        IslandParams params = MakeVolcanoParams();
        Cosmic::JobSystem::Get().Submit([load, params]() { BuildNightTerrain(params, *load); });

        // --- Night render policy: lava carries the exposure; bloom does the glow ---
        m_Settings.Skybox              = true;
        m_Settings.IBL                 = true;
        m_Settings.Shadows             = true;
        m_Settings.WaterReflections    = false;   // no water in this variant
        m_Settings.TerrainCastsShadows = true;
        m_Settings.ClearColor          = { 0.02f, 0.012f, 0.016f, 1.0f };
        m_Settings.Bloom               = true;
        m_Settings.BloomThreshold      = 1.1f;
        m_Settings.BloomIntensity      = 0.7f;
        m_Settings.FXAA                = true;
        m_Settings.Fog                 = true;
        m_Settings.FogDensity          = 0.004f;
        m_Settings.GodRays             = true;    // shafts through the smoke column
        m_Settings.GodRaysIntensity    = 0.7f;
        m_Settings.GodRaysDensity      = 0.08f;
        m_Settings.HeatHaze            = true;    // strong shimmer over the vents
        m_Settings.HeatHazeStrength    = 0.03f;
        m_Settings.LensFlare           = false;
        m_Settings.ShadowRadius        = 450.0f;
        m_Settings.ShadowCenter        = { 0.0f, 420.0f, 0.0f };
    }

    void NightVolcanoWorld::OnDetach()
    {
        m_Load.reset();
        m_Terrain.reset();
        m_Volcano.Shutdown();
        m_ContentBuilt = false;
    }

    bool NightVolcanoWorld::IsLoading() const
    {
        const bool cpuReady = m_Load && m_Load->Ready.load(std::memory_order_acquire);
        return !cpuReady || m_RevealFrames < 3;
    }

    void NightVolcanoWorld::BuildContent()
    {
        if (m_ContentBuilt || !m_Terrain)
            return;

        VolcanoConfig vc;
        vc.CenterXZ           = { 0.0f, 0.0f };          // volcano centred in the terrain
        vc.CalderaRadiusWorld = 0.10f * kWorldSize;
        vc.SeaLevelY          = 0.0f;
        vc.Seed               = kSeed;
        vc.FlowCount          = 2;                       // lava lake + 2 flows (F13)
        vc.FlowWidth          = 12.0f;
        vc.FlowStep           = 5.0f;                    // small step = no ribbon gaps
        vc.FlowLift           = 3.0f;                    // clear the terrain LOD (no clipping)
        vc.FlowMaxSteps       = 180;
        vc.EmberRate          = 960.0f;                  // ember storm (island x3)
        vc.SmokeRate          = 70.0f;
        vc.FumaroleCount      = 3;
        vc.ColumnLights       = 3;                       // 3 pulsing lights up the smoke column
        vc.LightRadius        = 120.0f;
        vc.LightIntensity     = 8.0f;
        vc.RumbleRadius       = 900.0f;
        vc.RumbleVolume       = 1.0f;                    // loud
        m_Volcano.Build(m_Terrain, vc);

        m_ContentBuilt = true;
    }

    void NightVolcanoWorld::OnUpdate(WorldContext& ctx)
    {
        if (!m_Terrain && m_Load && m_Load->Ready.load(std::memory_order_acquire))
            m_Terrain = m_Load->Terrain;

        if (!m_Terrain || !ctx.Renderer || !ctx.Renderer->IsInitialized())
            return;

        BuildContent();

        const Cosmic::Camera* cam = ctx.Camera        ? &ctx.Camera->GetCamera()
                                  : ctx.OrbitFallback ? &ctx.OrbitFallback->GetCamera()
                                                      : nullptr;
        if (!cam)
            return;

        Cosmic::SceneRenderer& renderer = *ctx.Renderer;
        const glm::vec3 camPos = cam->GetPosition();

        // Fixed dusk/night (panel-overridable) → full lighting state.
        const DayState state = DayNightCycle::Evaluate(m_TimeHours, ctx.TimeSeconds);
        m_Sky = state.Sky;

        renderer.GetEnvironment().SetSunDirection(state.ToSun);
        renderer.GetEnvironment().SetNightSky(state.Night);
        renderer.GetEnvironment().SetMoon(state.MoonDir, state.MoonIntensity);
        m_Settings.FogColor = state.FogColor;

        // No snow in this variant — clear any overlay a previous world set (global state).
        Cosmic::Renderer3D::ClearSnow();

        m_Volcano.Update(ctx.DeltaTime, ctx.TimeSeconds, camPos);

        Cosmic::SceneRenderDesc desc;
        desc.SetCamera(*cam);
        desc.TimeSeconds = ctx.TimeSeconds;
        desc.DeltaTime   = ctx.DeltaTime;
        desc.Exposure    = m_Exposure;
        desc.Settings    = m_Settings;

        desc.Lights.SunDirection = state.SunDir;
        desc.Lights.SunColor     = state.SunColor;
        desc.Lights.SunIntensity = state.SunIntensity;
        desc.Lights.Ambient      = state.Ambient;

        desc.TerrainSystem = m_Terrain.get();
        desc.DetailedSky   = &m_Sky;

        m_Volcano.Submit(desc);   // emitters + heat haze + pulsing lava lights

        desc.DrawOpaque = [this](const Cosmic::SceneDrawContext& c) { m_Volcano.DrawLava(c); };

        renderer.Render(desc);

        if (m_RevealFrames < 3)
            ++m_RevealFrames;
    }

    void NightVolcanoWorld::OnPanels(WorldContext& ctx)
    {
        (void)ctx;
        ImGui::Begin("World Settings");
        ImGui::TextWrapped("Night Volcano — the caldera money shot (F13).");
        ImGui::Separator();

        // Eruption trigger — a lava-bomb fountain + ember/smoke surge + glow spike.
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.70f, 0.22f, 0.06f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.32f, 0.10f, 1.0f));
        if (ImGui::Button(ICON_LC_FLAME "  TRIGGER ERUPTION", ImVec2(-1, 0)))
            m_Volcano.TriggerEruption();
        ImGui::PopStyleColor(2);
        if (m_Volcano.IsErupting())
            ImGui::TextColored({ 1.0f, 0.55f, 0.2f, 1.0f }, "  ERUPTING");

        ImGui::SeparatorText("Time of day");
        ImGui::SliderFloat("Hour", &m_TimeHours, 0.0f, 24.0f, "%.1f h");
        ImGui::SliderFloat("Exposure", &m_Exposure, 0.2f, 2.0f, "%.2f");

        ImGui::SeparatorText("Effects");
        ImGui::Checkbox("Bloom",    &m_Settings.Bloom);
        ImGui::Checkbox("God rays", &m_Settings.GodRays);
        ImGui::Checkbox("Heat haze",&m_Settings.HeatHaze);
        ImGui::Checkbox("Shadows",  &m_Settings.Shadows);
        ImGui::Checkbox("Fog",      &m_Settings.Fog);
        ImGui::SliderFloat("Bloom threshold", &m_Settings.BloomThreshold, 0.5f, 2.0f, "%.2f");
        ImGui::SliderFloat("God-ray power",   &m_Settings.GodRaysIntensity, 0.0f, 1.5f, "%.2f");

        ImGui::End();
    }

} // namespace Frontier
