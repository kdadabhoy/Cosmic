// MirrorLakeWorld.cpp — golden-hour mirror lake (F15). See MirrorLakeWorld.h.
//
// The showcase for F6's water v2 + planar reflection: a glass-calm lake in a
// pine basin, lit by a low warm sun so the reflection reads as a true mirror.
// Mist banks drift over the surface, god rays rake through the shoreline pines,
// caustics dance on the visible lakebed, and fish ring the surface now and then.

#include "worlds/MirrorLakeWorld.h"

#include "common/DayNightCycle.h"
#include "common/ProceduralAudio.h"

#include "ui/IconsLucide.h"

#include "jobs/JobSystem.h"

#include <imgui.h>

#include <cmath>
#include <vector>

namespace Frontier
{
    // ---- Terrain shape (a lake basin ringed by pine slopes + peaks) -----------
    static constexpr float    kWorldSize   = 1024.0f;
    static constexpr float    kHeightScale = 220.0f;
    static constexpr float    kBaseHeight  = -30.0f;
    static constexpr float    kLakeY       = 0.0f;      // lake surface world Y
    static constexpr uint32_t kSeed        = 0x1A4E205u;

    static float MirrorHeight(const Cosmic::Noise& n, float u, float v)
    {
        const glm::vec2 c{ u - 0.5f, v - 0.5f };
        const float r = glm::length(c) * 2.0f;                          // 0 centre .. ~1.41 corner

        const float hills  = 0.5f + 0.5f * n.Fbm2D(u * 3.0f, v * 3.0f, 5);
        const float ridged = n.Ridged2D(u * 4.0f + 5.0f, v * 4.0f + 2.0f, 4);

        const float rise = glm::smoothstep(0.12f, 0.72f, r);            // 0 in the lake, 1 at the rim
        const float floorH = 0.06f;                                     // submerged basin floor
        const float slope  = 0.22f + 0.35f * hills
                                   + 0.30f * ridged * glm::smoothstep(0.5f, 0.95f, r);
        const float h = glm::mix(floorH, slope, rise);
        return glm::clamp(h, 0.0f, 1.0f);
    }

    static Cosmic::Ref<Cosmic::Material> MakeInstancedPBR(const char* name, const glm::vec4& albedo, float rough)
    {
        auto sh = Cosmic::AssetLibrary::GetShader("assets/shaders/PBRInstanced.glsl");
        auto m  = Cosmic::Material::Create(sh, name);
        m->Set("u_Albedo",    albedo);
        m->Set("u_Metallic",  0.0f);
        m->Set("u_Roughness", rough);
        m->Set("u_AO",        1.0f);
        m->Set("u_Emissive",  glm::vec3(0.0f));
        return m;
    }

    // Two whisper-quiet ripples (amplitude <= 0.02 m) — the mirror contract.
    static std::vector<Cosmic::GerstnerWave> MakeLakeWaves()
    {
        std::vector<Cosmic::GerstnerWave> waves;
        auto add = [&](glm::vec2 dir, float len, float amp, float phase)
        {
            Cosmic::GerstnerWave w;
            w.Direction = dir; w.Wavelength = len; w.Amplitude = amp;
            w.Steepness = 0.12f; w.Phase = phase;
            waves.push_back(w);
        };
        add({ 1.0f, 0.2f }, 9.0f, 0.018f, 0.0f);
        add({ 0.3f, 1.0f }, 6.0f, 0.012f, 1.7f);
        return waves;
    }

    const WorldInfo& MirrorLakeWorld::GetInfo() const
    {
        static const WorldInfo info{
            "Dawn Mirror Lake",
            ICON_LC_DROPLETS,
            "Glass-calm water at golden hour — mirror reflections and mist banks",
            { 60.0f, 24.0f, 260.0f },   // low over the water, looking back toward the peaks
            186.0f, -6.0f,
            { 0.16f, 0.12f, 0.10f, 1.0f }
        };
        return info;
    }

    static void BuildMirrorContent(MirrorLakeWorld::LoadResult& out)
    {
        Cosmic::TerrainSpecification tspec;
        tspec.Resolution  = 1025;
        tspec.WorldSize   = kWorldSize;
        tspec.HeightScale = kHeightScale;
        tspec.BaseHeight  = kBaseHeight;
        tspec.HeightFunction = [noise = Cosmic::Noise(kSeed)](float u, float v)
        {
            return MirrorHeight(noise, u, v);
        };

        tspec.Layers[0] = { { 0.16f, 0.26f, 0.14f }, 0.35f, nullptr };   // meadow grass
        tspec.Layers[1] = { { 0.34f, 0.32f, 0.30f }, 0.48f, nullptr };   // rock slope
        tspec.Layers[2] = { { 0.90f, 0.93f, 0.97f }, 0.28f, nullptr };   // snow peaks
        tspec.Layers[3] = { { 0.60f, 0.56f, 0.44f }, 0.40f, nullptr };   // lakebed silt
        tspec.Material.HighHeight = 150.0f;
        tspec.Material.HighBlend  = 30.0f;
        tspec.Material.LowHeight  = 2.5f;
        tspec.Material.LowBlend   = 3.0f;
        tspec.Material.WetLine    = 0.4f;
        tspec.Material.WetBand    = 1.4f;
        tspec.Material.WetDarken  = 0.7f;
        out.Terrain = Cosmic::Terrain::Create(tspec);

        out.LakeCenter   = { 0.0f, 0.0f };
        out.LakeRadius   = 130.0f;
        out.LakeSurfaceY = kLakeY;

        Cosmic::WaterSpecification wspec;
        wspec.Center               = out.LakeCenter;
        wspec.Extent               = { 460.0f, 460.0f };   // only shows where terrain is below the surface
        wspec.SurfaceHeight        = kLakeY;
        wspec.Waves                = MakeLakeWaves();
        wspec.ShallowColor         = { 0.14f, 0.34f, 0.36f };
        wspec.DeepColor            = { 0.02f, 0.10f, 0.16f };
        wspec.DepthFadeDistance    = 12.0f;
        wspec.DetailStrength       = 0.08f;                // barely-perturbed surface
        wspec.ShoreDepthRange      = 4.0f;
        wspec.CausticStrength      = 0.9f;
        wspec.SparkleStrength      = 0.15f;
        wspec.ReflectionResolution = 2048;                 // crisp mirror
        out.Lake = Cosmic::Water::Create(wspec);
        if (out.Lake)
            out.Lake->SetShoreTerrain(out.Terrain);

        out.Ready.store(true, std::memory_order_release);
    }

    void MirrorLakeWorld::OnAttach()
    {
        m_Load = std::make_shared<LoadResult>();
        m_RevealFrames = 0;
        m_ContentBuilt = false;
        auto load = m_Load;
        Cosmic::JobSystem::Get().Submit([load]() { BuildMirrorContent(*load); });

        // --- Golden-hour render policy: reflections + lens flare + god rays ---
        m_Settings.Skybox              = true;
        m_Settings.IBL                 = true;
        m_Settings.Shadows             = true;
        m_Settings.WaterReflections    = true;
        m_Settings.TerrainCastsShadows = true;
        m_Settings.Bloom               = true;
        m_Settings.BloomIntensity      = 0.5f;
        m_Settings.FXAA                = true;
        m_Settings.Fog                 = true;
        m_Settings.FogDensity          = 0.004f;
        m_Settings.LensFlare           = true;
        m_Settings.LensFlareIntensity  = 0.4f;
        m_Settings.GodRays             = true;
        m_Settings.GodRaysIntensity    = 0.6f;
        m_Settings.GodRaysDensity      = 0.05f;
        m_Settings.ShadowRadius        = 320.0f;

        // Underwater medium for the occasional dive under the glassy surface.
        m_Settings.Underwater                = false;   // per-frame when the camera dips below
        m_Settings.UnderwaterY               = kLakeY;
        m_Settings.UnderwaterColor           = { 0.10f, 0.30f, 0.34f };
        m_Settings.UnderwaterDeepColor       = { 0.02f, 0.08f, 0.14f };
        m_Settings.UnderwaterDensity         = 0.03f;
        m_Settings.UnderwaterTint            = { 0.5f, 0.72f, 0.82f };
        m_Settings.UnderwaterDepthReference  = 30.0f;
        m_Settings.UnderwaterCausticStrength = 0.6f;
        m_Settings.UnderwaterCausticScale    = 0.12f;
    }

    void MirrorLakeWorld::OnDetach()
    {
        m_WaterAmb.Stop();
        m_Wind.Stop();
        m_Load.reset();
        m_Terrain.reset();
        m_Lake.reset();
        m_Pines.Reset();
        for (auto& m : m_Mist) m.reset();
        m_Splash.reset();
        m_ContentBuilt = false;
    }

    bool MirrorLakeWorld::IsLoading() const
    {
        const bool cpuReady = m_Load && m_Load->Ready.load(std::memory_order_acquire);
        return !cpuReady || m_RevealFrames < 3;
    }

    void MirrorLakeWorld::BuildContent()
    {
        if (m_ContentBuilt || !m_Terrain)
            return;

        // --- Instanced pine shoreline (dry land around the lake) ---
        {
            const glm::vec2 lakeC = m_LakeCenter;
            const float     lakeR = m_LakeRadius;
            ScatterParams p;
            p.Count      = 2600;
            p.Seed       = kSeed ^ 0x51A6u;
            p.MinHeight  = kLakeY + 1.0f;      // above the waterline
            p.MaxHeight  = 130.0f;
            p.MinNormalY = 0.7f;
            p.MinScale   = 0.9f; p.MaxScale = 1.7f;
            p.YOffset    = -0.4f;
            p.BaseTint   = { 0.40f, 0.56f, 0.34f };
            p.TintJitter = 0.14f;
            p.RejectXZ   = [lakeC, lakeR](glm::vec2 xz){ return glm::length(xz - lakeC) < lakeR * 1.05f; };
            m_Pines.Build(ProceduralMeshes::MakePine(kSeed),
                          MakeInstancedPBR("Pine", { 0.15f, 0.30f, 0.13f, 1.0f }, 0.9f),
                          Scatter::Generate(*m_Terrain, p), 7.0f);
        }

        // --- Mist banks skimming the surface (three wide, slow, low-alpha) ---
        const glm::vec2 spots[3] = {
            m_LakeCenter + glm::vec2(-60.0f, 30.0f),
            m_LakeCenter + glm::vec2(50.0f, -40.0f),
            m_LakeCenter + glm::vec2(10.0f, 80.0f),
        };
        for (int i = 0; i < 3; ++i)
        {
            m_Mist[i] = Cosmic::ParticleEmitter::Create(Cosmic::Presets::Mist({ 120.0f, 6.0f, 120.0f }));
            if (m_Mist[i])
                m_Mist[i]->SetTransform(glm::translate(glm::mat4(1.0f),
                    { spots[i].x, m_LakeSurfaceY + 3.0f, spots[i].y }));
        }

        // --- Fish splash rings (Burst on a timer over random lake points) ---
        m_Splash = Cosmic::ParticleEmitter::Create(Cosmic::Presets::SplashRings(0.0f));

        // --- Ambience ---
        m_WaterAmb.Start(ProceduralAudio::Ensure("water"));
        m_Wind.Start(ProceduralAudio::Ensure("wind"));

        m_ContentBuilt = true;
    }

    void MirrorLakeWorld::OnUpdate(WorldContext& ctx)
    {
        if (!m_Terrain && m_Load && m_Load->Ready.load(std::memory_order_acquire))
        {
            m_Terrain      = m_Load->Terrain;
            m_Lake         = m_Load->Lake;
            m_LakeCenter   = m_Load->LakeCenter;
            m_LakeRadius   = m_Load->LakeRadius;
            m_LakeSurfaceY = m_Load->LakeSurfaceY;
        }

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
        const float     dt     = ctx.DeltaTime;

        // --- Time of day (locked near golden hour; panel can scrub/play) ---
        if (m_Playing)
        {
            m_TimeHours += dt * m_PlaySpeed;
            m_TimeHours = std::fmod(m_TimeHours, 24.0f);
            if (m_TimeHours < 0.0f) m_TimeHours += 24.0f;
        }
        const DayState state = DayNightCycle::Evaluate(m_TimeHours, ctx.TimeSeconds);
        m_Sky = state.Sky;

        renderer.GetEnvironment().SetSunDirection(state.ToSun);
        renderer.GetEnvironment().SetNightSky(state.Night);
        renderer.GetEnvironment().SetMoon(state.MoonDir, state.MoonIntensity);
        m_Settings.FogColor = state.FogColor;

        // No snow in this variant — drop any overlay a previous world left set.
        Cosmic::Renderer3D::ClearSnow();

        // Shadow map follows the camera ground point.
        m_Settings.ShadowCenter = { camPos.x, m_Terrain->SampleHeight(camPos.x, camPos.z), camPos.z };

        // Underwater when the camera dips below the glassy surface.
        m_Settings.Underwater  = m_UnderwaterEnabled && (camPos.y < m_LakeSurfaceY + 0.5f);
        m_Settings.UnderwaterY = m_LakeSurfaceY;

        // Mist drift + fish rings.
        for (auto& m : m_Mist)
            if (m) m->Update(dt, ctx.TimeSeconds);

        if (m_Splash)
        {
            m_SplashTimer -= dt;
            if (m_SplashTimer <= 0.0f)
            {
                const float a = m_Rng.Range(0.0f, 6.2831853f);
                const float r = m_Rng.Range(0.0f, m_LakeRadius * 0.85f);
                m_Splash->SetTransform(glm::translate(glm::mat4(1.0f),
                    { m_LakeCenter.x + std::cos(a) * r, m_LakeSurfaceY + 0.05f,
                      m_LakeCenter.y + std::sin(a) * r }));
                m_Splash->Burst(6);
                m_SplashTimer = m_Rng.Range(5.0f, 10.0f);
            }
            m_Splash->Update(dt, ctx.TimeSeconds);
        }

        // Ambience: water by distance to the lake centre; a soft constant breeze.
        m_WaterAmb.Update(camPos, { m_LakeCenter.x, m_LakeSurfaceY, m_LakeCenter.y }, 500.0f, 0.6f);
        m_Wind.Update(camPos, camPos, 1.0f, 0.15f);   // source = listener -> constant soft level

        // Cull the pines once against the main frustum (F5 policy).
        const Cosmic::Frustum frustum = Cosmic::Frustum::FromViewProjection(cam->GetViewProjectionMatrix());
        m_Pines.CullAndUpload(frustum);

        // --- Assemble + submit the frame ---
        Cosmic::SceneRenderDesc desc;
        desc.SetCamera(*cam);
        desc.TimeSeconds = ctx.TimeSeconds;
        desc.DeltaTime   = dt;
        desc.Exposure    = m_Exposure;
        desc.Settings    = m_Settings;

        desc.Lights.SunDirection = state.SunDir;
        desc.Lights.SunColor     = state.SunColor;
        desc.Lights.SunIntensity = state.SunIntensity;
        desc.Lights.Ambient      = state.Ambient;

        desc.TerrainSystem = m_Terrain.get();
        desc.DetailedSky   = &m_Sky;

        for (auto& m : m_Mist)
            if (m) desc.Emitters.push_back(m.get());
        if (m_Splash) desc.Emitters.push_back(m_Splash.get());

        desc.DrawOpaque = [this](const Cosmic::SceneDrawContext& c) { m_Pines.Draw(c); };

        desc.WaterBodies.clear();
        if (m_Lake) desc.WaterBodies.push_back(m_Lake.get());
        desc.PrimaryReflectionWater = 0;

        renderer.Render(desc);

        if (m_RevealFrames < 3)
            ++m_RevealFrames;
    }

    void MirrorLakeWorld::OnPanels(WorldContext& ctx)
    {
        (void)ctx;
        ImGui::Begin("World Settings");
        ImGui::TextWrapped("Dawn Mirror Lake — glass-calm water at golden hour (F15). "
                           "High-res planar reflection + mist + caustics.");
        ImGui::Separator();

        ImGui::SeparatorText("Time of day");
        ImGui::SliderFloat("Hour", &m_TimeHours, 0.0f, 24.0f, "%.1f h");
        ImGui::Checkbox("Play", &m_Playing);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        ImGui::SliderFloat("Speed", &m_PlaySpeed, 0.1f, 3.0f, "%.1f h/s");
        ImGui::SliderFloat("Exposure", &m_Exposure, 0.3f, 3.0f, "%.2f");

        ImGui::SeparatorText("Features");
        ImGui::Checkbox("Water reflections", &m_Settings.WaterReflections);
        ImGui::Checkbox("Shadows",           &m_Settings.Shadows);
        ImGui::Checkbox("Bloom",             &m_Settings.Bloom);
        ImGui::Checkbox("God rays",          &m_Settings.GodRays);
        ImGui::Checkbox("Lens flare",        &m_Settings.LensFlare);
        ImGui::Checkbox("FXAA",              &m_Settings.FXAA);
        ImGui::Checkbox("Fog",               &m_Settings.Fog);
        ImGui::SliderFloat("Fog density", &m_Settings.FogDensity, 0.0f, 0.02f, "%.4f");

        ImGui::SeparatorText("Underwater / dive");
        ImGui::Checkbox("Underwater medium", &m_UnderwaterEnabled);
        ImGui::SliderFloat("Caustics", &m_Settings.UnderwaterCausticStrength, 0.0f, 2.0f, "%.2f");

        ImGui::End();
    }

} // namespace Frontier
