// StormOceanWorld.cpp — storm at sea (F16). See StormOceanWorld.h.
//
// The water-v2 stress test: a big 8-wave swell with breaking whitecaps under a
// storm-grey sky, driving rain streaks + surface splash rings, a buoy pitching
// on the swell, and periodic lightning that flashes the whole scene cold-white
// then rolls thunder in late. Diving below the surface tints + fogs the frame.

#include "worlds/StormOceanWorld.h"

#include "common/ProceduralMeshes.h"     // AppendCylinder (buoy mast)
#include "common/ProceduralAudio.h"

#include "ui/IconsLucide.h"

#include <imgui.h>

#include <cmath>
#include <vector>

namespace Frontier
{
    static constexpr float kOceanY = 0.0f;

    namespace
    {
        using Cosmic::MeshVertex;

        void AddQuad(std::vector<MeshVertex>& v, std::vector<uint32_t>& idx,
                     glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3)
        {
            const glm::vec3 n = glm::normalize(glm::cross(p1 - p0, p3 - p0));
            const uint32_t  b = static_cast<uint32_t>(v.size());
            v.push_back({ p0, n, { 0.0f, 0.0f }, {} });
            v.push_back({ p1, n, { 1.0f, 0.0f }, {} });
            v.push_back({ p2, n, { 1.0f, 1.0f }, {} });
            v.push_back({ p3, n, { 0.0f, 1.0f }, {} });
            idx.insert(idx.end(), { b, b + 1, b + 2,  b, b + 2, b + 3 });
        }

        void AddBox(std::vector<MeshVertex>& v, std::vector<uint32_t>& idx, glm::vec3 mn, glm::vec3 mx)
        {
            const glm::vec3 c000{ mn.x, mn.y, mn.z }, c100{ mx.x, mn.y, mn.z };
            const glm::vec3 c110{ mx.x, mx.y, mn.z }, c010{ mn.x, mx.y, mn.z };
            const glm::vec3 c001{ mn.x, mn.y, mx.z }, c101{ mx.x, mn.y, mx.z };
            const glm::vec3 c111{ mx.x, mx.y, mx.z }, c011{ mn.x, mx.y, mx.z };
            AddQuad(v, idx, c001, c101, c111, c011);
            AddQuad(v, idx, c100, c000, c010, c110);
            AddQuad(v, idx, c101, c100, c110, c111);
            AddQuad(v, idx, c000, c001, c011, c010);
            AddQuad(v, idx, c010, c011, c111, c110);
            AddQuad(v, idx, c000, c100, c101, c001);
        }

        // A channel buoy: a squat hull box + a short mast (origin at the waterline).
        Cosmic::Ref<Cosmic::Mesh> MakeBuoy()
        {
            std::vector<MeshVertex> v;
            std::vector<uint32_t>   idx;
            AddBox(v, idx, { -1.3f, -1.2f, -1.3f }, { 1.3f, 1.4f, 1.3f });   // hull
            AddBox(v, idx, { -0.9f, 1.4f, -0.9f }, { 0.9f, 1.9f, 0.9f });    // deck band
            ProceduralMeshes::detail::AppendCylinder(v, idx, { 0.0f, 1.9f, 0.0f }, 0.14f, 3.2f, 8);  // mast
            return Cosmic::Mesh::Create(v, idx);
        }

        Cosmic::Ref<Cosmic::Material> MakePBR(const char* name, const glm::vec4& albedo,
                                              float rough, const glm::vec3& emissive)
        {
            auto sh = Cosmic::AssetLibrary::GetShader("assets/shaders/PBR.glsl");
            auto m  = Cosmic::Material::Create(sh, name);
            m->Set("u_Albedo",    albedo);
            m->Set("u_Metallic",  0.0f);
            m->Set("u_Roughness", rough);
            m->Set("u_AO",        1.0f);
            m->Set("u_Emissive",  emissive);
            return m;
        }

        // Open-water storm swell: two big primaries + six choppy detail waves.
        std::vector<Cosmic::GerstnerWave> MakeStormWaves()
        {
            std::vector<Cosmic::GerstnerWave> waves;
            auto add = [&](glm::vec2 dir, float len, float amp, float steep, float phase)
            {
                Cosmic::GerstnerWave w;
                w.Direction = dir; w.Wavelength = len; w.Amplitude = amp;
                w.Steepness = steep; w.Phase = phase;
                waves.push_back(w);
            };
            add({ 1.00f,  0.15f }, 60.0f, 1.60f, 0.80f, 0.0f);   // primary swell
            add({ 0.80f,  0.60f }, 38.0f, 1.00f, 0.75f, 1.1f);   // secondary swell
            add({ 1.00f, -0.20f }, 22.0f, 0.50f, 0.55f, 2.0f);   // chop
            add({ 0.60f,  0.90f }, 15.0f, 0.35f, 0.50f, 0.5f);
            add({ 0.90f,  0.30f }, 10.0f, 0.22f, 0.45f, 3.2f);
            add({ 0.40f,  1.00f },  7.0f, 0.14f, 0.40f, 4.4f);
            add({ 1.00f,  0.50f },  5.0f, 0.09f, 0.35f, 5.6f);
            add({ 0.70f, -0.50f },  3.5f, 0.06f, 0.35f, 2.7f);
            return waves;
        }
    } // namespace

    const WorldInfo& StormOceanWorld::GetInfo() const
    {
        static const WorldInfo info{
            "Storm Ocean",
            ICON_LC_CLOUD_LIGHTNING,
            "Heavy swell, driving rain, lightning — the water v2 stress test",
            { 0.0f, 10.0f, 42.0f },   // low over the water, looking at the buoy
            0.0f, -7.0f,
            { 0.10f, 0.12f, 0.15f, 1.0f }
        };
        return info;
    }

    void StormOceanWorld::OnAttach()
    {
        m_ContentBuilt = false;

        // --- Storm render policy ---
        m_Settings.Skybox              = true;
        m_Settings.IBL                 = true;
        m_Settings.Shadows             = true;    // the buoy grounds itself
        m_Settings.WaterReflections    = true;
        m_Settings.TerrainCastsShadows = false;   // no terrain
        m_Settings.ClearColor          = { 0.30f, 0.33f, 0.38f, 1.0f };
        m_Settings.Bloom               = true;
        m_Settings.BloomThreshold      = 1.1f;
        m_Settings.BloomIntensity      = 0.5f;
        m_Settings.FXAA                = true;
        m_Settings.Fog                 = true;
        m_Settings.FogColor            = { 0.55f, 0.58f, 0.62f };
        m_Settings.FogDensity          = 0.010f;
        m_Settings.GodRays             = false;
        m_Settings.HeatHaze            = false;
        m_Settings.LensFlare           = false;
        m_Settings.ShadowRadius        = 60.0f;

        // Underwater medium (below the swell).
        m_Settings.Underwater                = false;
        m_Settings.UnderwaterY               = kOceanY;
        m_Settings.UnderwaterColor           = { 0.06f, 0.16f, 0.20f };
        m_Settings.UnderwaterDeepColor       = { 0.01f, 0.04f, 0.08f };
        m_Settings.UnderwaterDensity         = 0.06f;
        m_Settings.UnderwaterTint            = { 0.45f, 0.62f, 0.72f };
        m_Settings.UnderwaterDepthReference  = 25.0f;
        m_Settings.UnderwaterCausticStrength = 0.0f;   // no lit floor in open water

        // Storm-grey detailed sky (no sun disc, no stars; dim).
        m_Sky.SkyIntensity      = 0.35f;
        m_Sky.SunDiscIntensity  = 4.0f;
        m_Sky.MoonIntensity     = 0.0f;
        m_Sky.StarIntensity     = 0.0f;
        m_Sky.MilkyWayIntensity = 0.0f;
    }

    void StormOceanWorld::OnDetach()
    {
        m_Wind.Stop();
        m_RainAmb.Stop();
        m_Ocean.reset();
        m_Rain.reset();
        m_Splash.reset();
        m_BuoyMesh.reset();
        m_BuoyMat.reset();
        m_ContentBuilt = false;
    }

    void StormOceanWorld::BuildContent()
    {
        if (m_ContentBuilt)
            return;

        Cosmic::WaterSpecification wspec;
        wspec.Center               = { 0.0f, 0.0f };
        wspec.Extent               = { 8000.0f, 8000.0f };
        wspec.SurfaceHeight        = kOceanY;
        wspec.Waves                = MakeStormWaves();
        wspec.ShallowColor         = { 0.10f, 0.22f, 0.24f };
        wspec.DeepColor            = { 0.02f, 0.05f, 0.08f };
        wspec.DepthFadeDistance    = 30.0f;
        wspec.DetailStrength       = 0.5f;
        wspec.WhitecapStrength     = 0.8f;     // breaking crests
        wspec.SparkleStrength      = 0.0f;     // no sun to sparkle
        wspec.ReflectionResolution = 512;      // it's all spray anyway
        m_Ocean = Cosmic::Water::Create(wspec);

        // Rain (velocity-stretched streaks, camera-tracking box) + splash rings.
        m_Rain   = Cosmic::ParticleEmitter::Create(Cosmic::Presets::Rain({ 100.0f, 60.0f, 100.0f }, 4000.0f));
        m_Splash = Cosmic::ParticleEmitter::Create(Cosmic::Presets::SplashRings(40.0f));

        // Buoy.
        m_BuoyMesh = MakeBuoy();
        m_BuoyMat  = MakePBR("Buoy", { 0.85f, 0.22f, 0.05f, 1.0f }, 0.6f, glm::vec3(0.15f, 0.02f, 0.0f));
        m_BuoyXZ   = { 0.0f, 0.0f };

        // Lightning + ambience.
        m_Lightning.Init(ProceduralAudio::Ensure("thunder"));
        m_Wind.Start(ProceduralAudio::Ensure("wind"));
        m_RainAmb.Start(ProceduralAudio::Ensure("water"));

        m_ContentBuilt = true;
    }

    void StormOceanWorld::OnUpdate(WorldContext& ctx)
    {
        if (!ctx.Renderer || !ctx.Renderer->IsInitialized())
            return;

        BuildContent();

        const Cosmic::Camera* cam = ctx.Camera        ? &ctx.Camera->GetCamera()
                                  : ctx.OrbitFallback ? &ctx.OrbitFallback->GetCamera()
                                                      : nullptr;
        if (!cam || !m_Ocean)
            return;

        Cosmic::SceneRenderer& renderer = *ctx.Renderer;
        const glm::vec3 camPos = cam->GetPosition();
        const float     dt     = ctx.DeltaTime;
        const float     t      = ctx.TimeSeconds;

        // No snow here — drop any overlay a previous world left set.
        Cosmic::Renderer3D::ClearSnow();

        // --- Lightning: advance the schedule, derive this frame's flash ---
        m_Lightning.Update(dt);
        const float flash = m_Lightning.FlashStrength();

        // Base storm light: a low, cool, dim key. A strike ramps it x6 cold-white
        // and boosts the sky (SkyDetail) intensity x3.
        const glm::vec3 toSun = glm::normalize(glm::vec3(0.40f, 0.32f, 0.50f));
        renderer.GetEnvironment().SetSunDirection(toSun);
        renderer.GetEnvironment().SetNightSky(false);
        renderer.GetEnvironment().SetMoon({ 0.0f, 1.0f, 0.0f }, 0.0f);

        m_Sky.SkyIntensity = 0.35f * m_Lightning.SkyMultiplier();

        // --- Rain box tracks the camera; splash rings scatter near the viewer ---
        if (m_Rain)
        {
            m_Rain->SetTransform(glm::translate(glm::mat4(1.0f), camPos + glm::vec3(0.0f, 30.0f, 0.0f)));
            m_Rain->Update(dt, t);
        }
        if (m_Splash)
        {
            // Move the point emitter around the camera each frame so its rings
            // scatter across the nearby surface (rain hitting the water).
            static thread_local Cosmic::Random rng(0x5A17u);
            const float a = rng.Range(0.0f, 6.2831853f);
            const float r = rng.Range(2.0f, 45.0f);
            const float sx = camPos.x + std::cos(a) * r;
            const float sz = camPos.z + std::sin(a) * r;
            m_Splash->SetTransform(glm::translate(glm::mat4(1.0f),
                { sx, m_Ocean->SampleHeight(sx, sz, t) + 0.05f, sz }));
            m_Splash->Update(dt, t);
        }

        // --- Buoy rides the swell (position + tilt from the surface) ---
        glm::mat4 buoyXform(1.0f);
        {
            const float bx = m_BuoyXZ.x, bz = m_BuoyXZ.y;
            const float by = m_Ocean->SampleHeight(bx, bz, t);
            const glm::vec3 up = glm::normalize(m_Ocean->SampleNormal(bx, bz, t));
            glm::vec3 right = glm::cross(up, glm::vec3(0.0f, 0.0f, 1.0f));
            if (glm::dot(right, right) < 1e-5f) right = glm::cross(up, glm::vec3(1.0f, 0.0f, 0.0f));
            right = glm::normalize(right);
            const glm::vec3 fwd = glm::normalize(glm::cross(right, up));
            buoyXform[0] = glm::vec4(right, 0.0f);
            buoyXform[1] = glm::vec4(up,    0.0f);
            buoyXform[2] = glm::vec4(fwd,   0.0f);
            buoyXform[3] = glm::vec4(bx, by - 0.6f, bz, 1.0f);   // sit low in the water
        }

        // Ambience (source = listener -> constant levels; rain a touch louder).
        m_Wind.Update(camPos, camPos, 1.0f, 0.35f);
        m_RainAmb.Update(camPos, camPos, 1.0f, 0.45f);

        // Underwater when the camera drops below the swell.
        m_Settings.Underwater  = m_UnderwaterEnabled && (camPos.y < kOceanY + 0.5f);
        m_Settings.UnderwaterY = kOceanY;
        m_Settings.ShadowCenter = { camPos.x, kOceanY, camPos.z };

        // --- Assemble + submit the frame ---
        Cosmic::SceneRenderDesc desc;
        desc.SetCamera(*cam);
        desc.TimeSeconds = t;
        desc.DeltaTime   = dt;
        desc.Exposure    = m_Exposure;
        desc.Settings    = m_Settings;

        desc.Lights.SunDirection = -toSun;
        desc.Lights.SunColor     = glm::mix(glm::vec3(0.70f, 0.72f, 0.78f),
                                            glm::vec3(0.85f, 0.90f, 1.0f), flash);
        desc.Lights.SunIntensity = 0.9f * m_Lightning.SunMultiplier();
        desc.Lights.Ambient      = 0.35f + 0.4f * flash;

        desc.DetailedSky = &m_Sky;

        if (m_RainOn && m_Rain) desc.Emitters.push_back(m_Rain.get());
        if (m_Splash)           desc.Emitters.push_back(m_Splash.get());

        desc.DrawOpaque = [this, &buoyXform](const Cosmic::SceneDrawContext& c)
        {
            if (m_BuoyMesh) c.DrawMesh(m_BuoyMesh, buoyXform, m_BuoyMat);
        };

        desc.WaterBodies.clear();
        desc.WaterBodies.push_back(m_Ocean.get());
        desc.PrimaryReflectionWater = 0;

        renderer.Render(desc);
    }

    void StormOceanWorld::OnPanels(WorldContext& ctx)
    {
        (void)ctx;
        ImGui::Begin("World Settings");
        ImGui::TextWrapped("Storm Ocean — 8-wave swell, whitecaps, rain + lightning (F16). "
                           "Dive below the surface for the underwater medium.");
        ImGui::Separator();

        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.35f, 0.45f, 0.75f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.55f, 0.65f, 0.95f, 1.0f));
        if (ImGui::Button(ICON_LC_CLOUD_LIGHTNING "  Strike now", ImVec2(-1, 0)))
            m_Lightning.Strike();
        ImGui::PopStyleColor(2);

        ImGui::SeparatorText("Storm");
        ImGui::Checkbox("Rain", &m_RainOn);
        ImGui::SliderFloat("Fog density", &m_Settings.FogDensity, 0.002f, 0.03f, "%.3f");
        ImGui::SliderFloat("Exposure",    &m_Exposure, 0.4f, 2.0f, "%.2f");
        ImGui::ColorEdit3("Fog color", &m_Settings.FogColor.x, ImGuiColorEditFlags_NoInputs);

        ImGui::SeparatorText("Features");
        ImGui::Checkbox("Water reflections", &m_Settings.WaterReflections);
        ImGui::Checkbox("Shadows",           &m_Settings.Shadows);
        ImGui::Checkbox("Bloom",             &m_Settings.Bloom);
        ImGui::Checkbox("FXAA",              &m_Settings.FXAA);
        ImGui::Checkbox("Fog",               &m_Settings.Fog);

        ImGui::SeparatorText("Underwater / dive");
        ImGui::Checkbox("Underwater medium", &m_UnderwaterEnabled);
        ImGui::SliderFloat("Fog density##uw", &m_Settings.UnderwaterDensity, 0.01f, 0.15f, "%.3f");

        ImGui::End();
    }

} // namespace Frontier
