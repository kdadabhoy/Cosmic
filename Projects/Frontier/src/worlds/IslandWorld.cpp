// IslandWorld.cpp — flagship seamless island, F12a assembly (see IslandWorld.h).
//
// F12a builds the real composed island: F11's heightfield (volcano, snow range,
// alpine lake, beach, river) as the terrain source, an ocean + the alpine lake
// (water v2 with shore awareness + caustics), the detailed per-pixel sky, and a
// time-of-day cycle (common/DayNightCycle.h). The volcano content (F12b) and the
// forests/waterfall/wildlife (F12c) land on top of this.

#include "worlds/IslandWorld.h"

#include "common/DayNightCycle.h"

#include "ui/IconsLucide.h"

#include <imgui.h>

#include <cmath>
#include <vector>

namespace Frontier
{
    // ---- Terrain shape (world scale) -------------------------------------------
    static constexpr float kWorldSize   = 4096.0f;   // island span (m)
    static constexpr float kHeightScale = 900.0f;    // world height of a 1.0 sample
    static constexpr float kBaseHeight  = -80.0f;    // world Y of a 0.0 sample -> sea at Y=0
    static constexpr float kOceanY      = 0.0f;      // ocean surface world Y

    const WorldInfo& IslandWorld::GetInfo() const
    {
        static const WorldInfo info{
            "Frontier Island",
            ICON_LC_GLOBE,
            "Volcano, snowy range, alpine lake, ocean coast — one seamless flight",
            { 0.0f, 650.0f, 2800.0f },   // spawn: offshore south, looking north over the island
            0.0f, -14.0f,
            { 0.055f, 0.075f, 0.110f, 1.0f }
        };
        return info;
    }

    // Ocean swell: six Gerstner waves rolling generally shoreward (dominant +X),
    // long lazy primaries down to short chop.
    static std::vector<Cosmic::GerstnerWave> MakeOceanWaves()
    {
        std::vector<Cosmic::GerstnerWave> waves;
        auto add = [&](glm::vec2 dir, float len, float amp, float steep, float phase)
        {
            Cosmic::GerstnerWave w;
            w.Direction  = dir;
            w.Wavelength = len;
            w.Amplitude  = amp;
            w.Steepness  = steep;
            w.Phase      = phase;
            waves.push_back(w);
        };
        add({ 1.00f,  0.25f }, 140.0f, 1.60f, 0.55f, 0.0f);
        add({ 0.85f,  0.55f },  90.0f, 1.00f, 0.50f, 1.3f);
        add({ 0.60f,  0.90f },  55.0f, 0.60f, 0.45f, 2.1f);
        add({ 1.00f, -0.20f },  34.0f, 0.35f, 0.40f, 0.7f);
        add({ 0.90f,  0.10f },  20.0f, 0.20f, 0.35f, 3.4f);
        add({ 0.70f,  0.40f },  12.0f, 0.12f, 0.30f, 5.0f);
        return waves;
    }

    // Lake: three small ripples (short, low, gentle).
    static std::vector<Cosmic::GerstnerWave> MakeLakeWaves()
    {
        std::vector<Cosmic::GerstnerWave> waves;
        auto add = [&](glm::vec2 dir, float len, float amp, float phase)
        {
            Cosmic::GerstnerWave w;
            w.Direction  = dir;
            w.Wavelength = len;
            w.Amplitude  = amp;
            w.Steepness  = 0.35f;
            w.Phase      = phase;
            waves.push_back(w);
        };
        add({ 1.0f,  0.3f }, 7.0f, 0.10f, 0.0f);
        add({ 0.4f,  1.0f }, 4.5f, 0.06f, 1.7f);
        add({ 0.8f, -0.5f }, 3.0f, 0.04f, 3.1f);
        return waves;
    }

    void IslandWorld::OnAttach()
    {
        // --- Terrain: the F11 composed island as HeightFunction (Source C) ---
        Cosmic::TerrainSpecification tspec;
        tspec.Resolution  = 2049;              // (64 * 32) + 1
        tspec.WorldSize   = kWorldSize;
        tspec.HeightScale = kHeightScale;
        tspec.BaseHeight  = kBaseHeight;
        tspec.HeightFunction = [this](float u, float v) { return IslandHeight(m_Island, u, v); };

        // Splat: grass base, basalt slopes, snow caps, sand at the waterline.
        tspec.Layers[0] = { { 0.22f, 0.34f, 0.14f }, 0.35f, nullptr };   // alpine grass
        tspec.Layers[1] = { { 0.30f, 0.30f, 0.32f }, 0.45f, nullptr };   // grey basalt rock
        tspec.Layers[2] = { { 0.90f, 0.93f, 0.97f }, 0.28f, nullptr };   // snow
        tspec.Layers[3] = { { 0.76f, 0.70f, 0.52f }, 0.40f, nullptr };   // sand
        tspec.Material.HighHeight = 380.0f;    // snow line (world Y)
        tspec.Material.HighBlend  = 40.0f;
        tspec.Material.LowHeight  = 2.5f;      // sand hugs the waterline
        tspec.Material.LowBlend   = 2.0f;
        tspec.Material.WetLine    = 0.4f;      // wet sand just above the sea
        tspec.Material.WetBand    = 1.6f;
        tspec.Material.WetDarken  = 0.8f;
        m_Terrain = Cosmic::Terrain::Create(tspec);

        // Derive the lake placement from the island params (world space).
        const glm::vec2 minCorner = m_Terrain ? m_Terrain->GetWorldMinCorner()
                                              : glm::vec2(-kWorldSize * 0.5f);
        m_LakeCenterWorld = minCorner + m_Island.LakeCenter * kWorldSize;
        m_LakeRadiusWorld = m_Island.LakeRadius * kWorldSize;
        m_LakeSurfaceY    = LakeSurfaceWorldY(m_Island, kHeightScale, kBaseHeight);

        // --- Ocean: 6000 m plane at Y=0, shore-aware waves + caustics + sparkle ---
        Cosmic::WaterSpecification ospec;
        ospec.Center               = { 0.0f, 0.0f };
        ospec.Extent               = { 6000.0f, 6000.0f };
        ospec.SurfaceHeight        = kOceanY;
        ospec.Waves                = MakeOceanWaves();
        ospec.ShoreDepthRange      = 8.0f;
        ospec.CausticStrength      = 0.6f;
        ospec.SparkleStrength      = 0.4f;
        ospec.ReflectionResolution = 1024;
        m_Ocean = Cosmic::Water::Create(ospec);
        if (m_Ocean)
            m_Ocean->SetShoreTerrain(m_Terrain);

        // --- Lake: sized to the basin, calm ripples, strong caustics ---
        Cosmic::WaterSpecification lspec;
        lspec.Center               = m_LakeCenterWorld;
        lspec.Extent               = { m_LakeRadiusWorld * 2.2f, m_LakeRadiusWorld * 2.2f };
        lspec.SurfaceHeight        = m_LakeSurfaceY;
        lspec.Waves                = MakeLakeWaves();
        lspec.ShoreDepthRange      = 4.0f;
        lspec.CausticStrength      = 0.9f;
        lspec.ReflectionResolution = 512;
        m_Lake = Cosmic::Water::Create(lspec);
        if (m_Lake)
            m_Lake->SetShoreTerrain(m_Terrain);

        // --- Render policy: the full island look ---
        m_Settings.Skybox              = true;
        m_Settings.IBL                 = true;
        m_Settings.Shadows             = true;
        m_Settings.WaterReflections    = true;
        m_Settings.TerrainCastsShadows = true;
        m_Settings.Bloom               = true;
        m_Settings.FXAA                = true;
        m_Settings.Fog                 = true;
        m_Settings.FogDensity          = 0.0025f;    // gentle aerial haze at km scale
        m_Settings.LensFlare           = true;
        m_Settings.ShadowRadius        = 700.0f;     // covers the terrain around the viewer
    }

    void IslandWorld::OnDetach()
    {
        // Release GPU resources while the context is live (client-dev rule).
        m_Terrain.reset();
        m_Ocean.reset();
        m_Lake.reset();
    }

    void IslandWorld::OnUpdate(WorldContext& ctx)
    {
        // Defensive fallback until the renderer + terrain are live.
        if (!ctx.Renderer || !ctx.Renderer->IsInitialized() || !m_Terrain)
        {
            DrawPlaceholder(ctx, { 0.35f, 0.75f, 0.45f, 1.0f });
            return;
        }

        const Cosmic::Camera* cam = ctx.Camera        ? &ctx.Camera->GetCamera()
                                  : ctx.OrbitFallback ? &ctx.OrbitFallback->GetCamera()
                                                      : nullptr;
        if (!cam)
            return;

        Cosmic::SceneRenderer& renderer = *ctx.Renderer;
        const glm::vec3 camPos = cam->GetPosition();

        // --- Time of day: advance while playing, then evaluate the lighting state ---
        if (m_Playing)
        {
            m_TimeHours += ctx.DeltaTime * m_PlaySpeed;
            m_TimeHours = std::fmod(m_TimeHours, 24.0f);
            if (m_TimeHours < 0.0f) m_TimeHours += 24.0f;
        }
        const DayState state = DayNightCycle::Evaluate(m_TimeHours, ctx.TimeSeconds);
        m_Sky = state.Sky;   // desc.DetailedSky points here (must outlive Render)

        // Drive the environment: the REAL sun direction always feeds the sky/IBL
        // (so stars/moon appear when the sun is down), plus the night bake + moon.
        renderer.GetEnvironment().SetSunDirection(state.ToSun);
        renderer.GetEnvironment().SetNightSky(state.Night);
        renderer.GetEnvironment().SetMoon(state.MoonDir, state.MoonIntensity);

        // Fog color tracks the time of day; density stays a panel control.
        m_Settings.FogColor = state.FogColor;

        // Shadow map follows the camera's ground point (single-cascade coverage).
        m_Settings.ShadowCenter = { camPos.x, m_Terrain->SampleHeight(camPos.x, camPos.z), camPos.z };

        // Underwater medium: tint + fog the frame when the camera dives below the sea.
        m_Settings.Underwater = m_Underwater && (camPos.y < kOceanY);
        m_Settings.UnderwaterY = kOceanY;

        // --- Assemble + submit the frame ---
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

        // Both waters, ocean (far/around) first; reflect whichever the camera is near.
        desc.WaterBodies.clear();
        if (m_Ocean) desc.WaterBodies.push_back(m_Ocean.get());
        if (m_Lake)  desc.WaterBodies.push_back(m_Lake.get());

        const glm::vec2 camXZ{ camPos.x, camPos.z };
        const bool nearLake = m_Lake &&
            glm::length(camXZ - m_LakeCenterWorld) < m_LakeRadiusWorld * 2.5f;
        desc.PrimaryReflectionWater =
            nearLake ? static_cast<int>(desc.WaterBodies.size()) - 1 : 0;

        renderer.Render(desc);
    }

    void IslandWorld::OnPanels(WorldContext& ctx)
    {
        (void)ctx;
        ImGui::Begin("World Settings");
        ImGui::TextWrapped("Frontier Island — F12a: composed terrain, ocean + alpine "
                           "lake, detailed sky, time of day.");
        ImGui::Separator();

        ImGui::SeparatorText("Time of day");
        ImGui::SliderFloat("Hour", &m_TimeHours, 0.0f, 24.0f, "%.1f h");
        ImGui::Checkbox("Play", &m_Playing);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        ImGui::SliderFloat("Speed", &m_PlaySpeed, 0.1f, 6.0f, "%.1f h/s");
        ImGui::SliderFloat("Exposure", &m_Exposure, 0.1f, 4.0f, "%.2f");

        ImGui::SeparatorText("Features");
        ImGui::Checkbox("Skybox",            &m_Settings.Skybox);
        ImGui::Checkbox("IBL",               &m_Settings.IBL);
        ImGui::Checkbox("Shadows",           &m_Settings.Shadows);
        ImGui::Checkbox("Terrain shadows",   &m_Settings.TerrainCastsShadows);
        ImGui::Checkbox("Water reflections", &m_Settings.WaterReflections);
        ImGui::Checkbox("SSAO",              &m_Settings.SSAO);
        ImGui::Checkbox("Bloom",             &m_Settings.Bloom);
        ImGui::Checkbox("FXAA",              &m_Settings.FXAA);
        ImGui::Checkbox("Fog",               &m_Settings.Fog);
        ImGui::Checkbox("God rays",          &m_Settings.GodRays);
        ImGui::Checkbox("Lens flare",        &m_Settings.LensFlare);
        ImGui::Checkbox("Underwater medium", &m_Underwater);

        ImGui::SeparatorText("Fog");
        ImGui::SliderFloat("Density", &m_Settings.FogDensity, 0.0f, 0.02f, "%.4f");

        ImGui::Separator();
        ImGui::TextWrapped("The volcano (lava, smoke, embers) lands with F12b; forests, "
                           "the waterfall/river and wildlife with F12c.");
        ImGui::End();
    }

} // namespace Frontier
