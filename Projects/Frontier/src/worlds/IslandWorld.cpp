// IslandWorld.cpp — flagship seamless island (F2 SceneRenderer stopgap; see IslandWorld.h).

#include "worlds/IslandWorld.h"

#include "ui/IconsLucide.h"

#include <imgui.h>

#include <cmath>

namespace Frontier
{
    const WorldInfo& IslandWorld::GetInfo() const
    {
        static const WorldInfo info{
            "Frontier Island",
            ICON_LC_GLOBE,
            "Volcano, snowy range, alpine lake, ocean coast — one seamless flight",
            { 0.0f, 220.0f, 1400.0f },   // spawn: offshore, looking at the island
            0.0f, -12.0f,
            { 0.055f, 0.075f, 0.110f, 1.0f }
        };
        return info;
    }

    void IslandWorld::OnAttach()
    {
        // --- Terrain: procedural fBm island (F2 stopgap; F11 composes the real one) ---
        Cosmic::TerrainSpecification tspec;
        tspec.Resolution  = 1025;              // (64 * 16) + 1
        tspec.WorldSize   = 2048.0f;
        tspec.HeightScale = 180.0f;
        tspec.BaseHeight  = -30.0f;            // sink the coast below the Y=0 waterline
        tspec.Seed        = 20260703;
        tspec.Octaves     = 7;
        tspec.Frequency   = 3.5f;
        tspec.EdgeFalloff = 0.35f;             // island: edges fall to the sea
        tspec.Material.HighHeight = 120.0f;    // snow caps
        tspec.Material.HighBlend  = 30.0f;
        tspec.Material.LowHeight  = 4.0f;      // sand near the waterline
        tspec.Material.LowBlend   = 3.0f;
        m_Terrain = Cosmic::Terrain::Create(tspec);

        // --- Ocean-sized water at Y = 0 (default optics) ---
        Cosmic::WaterSpecification wspec;
        wspec.Center        = { 0.0f, 0.0f };
        wspec.Extent        = { 4096.0f, 4096.0f };
        wspec.SurfaceHeight = 0.0f;
        m_Water = Cosmic::Water::Create(wspec);

        // --- Smoke plume on the terrain near the origin ---
        m_SmokePos = { 150.0f, 0.0f, 80.0f };
        if (m_Terrain)
            m_SmokePos.y = m_Terrain->SampleHeight(m_SmokePos.x, m_SmokePos.z) + 1.0f;

        Cosmic::ParticleEmitterSpec smoke;
        smoke.MaxParticles = 2048;
        smoke.SpawnRate    = 40.0f;
        smoke.Shape        = Cosmic::EmitterShape::Cone;
        smoke.ShapeRadius  = 2.0f;
        smoke.ConeAngleDeg = 12.0f;
        smoke.SpeedMin = 4.0f;  smoke.SpeedMax = 8.0f;
        smoke.LifeMin  = 4.0f;  smoke.LifeMax  = 7.0f;
        smoke.Gravity  = { 0.0f, 2.5f, 0.0f };            // buoyant rise
        smoke.Drag     = 0.25f;
        smoke.Wind     = { 2.5f, 0.0f, 1.0f };
        smoke.SizeStart = 4.0f; smoke.SizeEnd = 22.0f;
        smoke.ColorStart = { 0.62f, 0.62f, 0.64f, 0.42f };
        smoke.ColorEnd   = { 0.72f, 0.72f, 0.75f, 0.0f };
        smoke.FlipbookTilesX = 4; smoke.FlipbookTilesY = 4;
        smoke.FlipbookFps    = 9.0f;
        smoke.FlipbookBlend  = true;
        smoke.SoftFadeDistance = 4.0f;
        m_Smoke = Cosmic::ParticleEmitter::Create(smoke);

        // --- Monolith casters so shadows read against something (terrain shadow
        //     casting is F4). A ring of tall boxes on the terrain near the origin. ---
        m_Rock = Cosmic::Mesh::CreateBox({ 1.0f, 1.0f, 1.0f });
        m_RockXforms.clear();
        glm::vec3 clusterCenter{ 0.0f };
        for (int i = 0; i < 6; ++i)
        {
            const float a = static_cast<float>(i) / 6.0f * 6.28318f;
            const float x = std::cos(a) * 90.0f;
            const float z = std::sin(a) * 90.0f;
            const float groundY = m_Terrain ? m_Terrain->SampleHeight(x, z) : 0.0f;
            const float hgt = 22.0f + 6.0f * std::sin(static_cast<float>(i));
            glm::mat4 xf = glm::translate(glm::mat4(1.0f), { x, groundY + hgt * 0.5f, z });
            xf = glm::scale(xf, { 12.0f, hgt, 12.0f });
            m_RockXforms.push_back(xf);
            clusterCenter += glm::vec3{ x, groundY, z };
        }
        clusterCenter /= 6.0f;

        // --- Render policy: sky/IBL/shadows on + bloom + fog for the island ---
        m_Settings.Skybox    = true;
        m_Settings.IBL       = true;
        m_Settings.Shadows   = true;
        m_Settings.Bloom     = true;
        m_Settings.Fog       = true;
        m_Settings.FogDensity   = 0.0025f;                // gentle aerial haze at km scale
        m_Settings.ShadowCenter = clusterCenter;
        m_Settings.ShadowRadius = 160.0f;
    }

    void IslandWorld::OnDetach()
    {
        // Release GPU resources while the context is live (client-dev rule).
        m_Terrain.reset();
        m_Water.reset();
        m_Smoke.reset();
        m_Rock.reset();
        m_RockXforms.clear();
    }

    void IslandWorld::OnUpdate(WorldContext& ctx)
    {
        // Defensive fallback until the renderer + terrain are live (both are set
        // before the first OnUpdate in practice).
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

        // --- Time-of-day sun (Engine3DDemo's sun-from-hour math, app-side) ---
        constexpr float kPi = 3.14159265358979f;
        const float f   = (m_TimeHours - 6.0f) / 12.0f;                 // daytime parameter
        const float alt = std::sin(f * kPi) * (kPi * 0.5f);
        const float azi = f * kPi;                                      // east -> west
        const glm::vec3 toSun = glm::normalize(glm::vec3(
            std::cos(alt) * std::cos(azi), std::sin(alt), std::cos(alt) * std::sin(azi)));
        m_LightDir = -toSun;

        // Drive the IBL/skybox sun (direction TO the sun); SceneRenderer bakes it.
        renderer.GetEnvironment().SetSunDirection(toSun);

        // Fog color tracks sun elevation (warm horizon, cool noon, dark night).
        {
            const float e = glm::clamp(toSun.y, -1.0f, 1.0f);
            const glm::vec3 day{ 0.72f, 0.82f, 0.95f };
            const glm::vec3 sunset{ 0.85f, 0.60f, 0.45f };
            const glm::vec3 night{ 0.05f, 0.07f, 0.13f };
            m_Settings.FogColor = e >= 0.0f ? glm::mix(sunset, day, glm::clamp(e, 0.0f, 1.0f))
                                            : glm::mix(sunset, night, glm::clamp(-e * 3.0f, 0.0f, 1.0f));
        }

        // --- Advance the smoke plume ---
        if (m_Smoke)
        {
            m_Smoke->SetTransform(glm::translate(glm::mat4(1.0f), m_SmokePos));
            m_Smoke->Update(ctx.DeltaTime, ctx.TimeSeconds);
        }

        // --- Assemble + submit the frame ---
        Cosmic::SceneRenderDesc desc;
        desc.SetCamera(*cam);
        desc.TimeSeconds = ctx.TimeSeconds;
        desc.Exposure    = m_Exposure;
        desc.Settings    = m_Settings;

        // Sun brightness + ambient fall toward night; sun tint warms at the horizon.
        const float elev = glm::clamp(toSun.y, 0.0f, 1.0f);
        desc.Lights.SunDirection = m_LightDir;
        desc.Lights.SunColor     = glm::mix(glm::vec3(1.0f, 0.70f, 0.45f),
                                            glm::vec3(1.0f, 0.97f, 0.92f),
                                            glm::clamp(elev * 2.0f, 0.0f, 1.0f));
        desc.Lights.SunIntensity = glm::mix(0.15f, 3.2f, elev);
        desc.Lights.Ambient      = glm::mix(0.06f, 0.30f, elev);

        desc.TerrainSystem = m_Terrain.get();
        if (m_Water)
        {
            desc.WaterBodies            = { m_Water.get() };
            desc.PrimaryReflectionWater = 0;
        }
        if (m_Smoke)
            desc.Emitters = { m_Smoke.get() };

        desc.DrawOpaque = [this](const Cosmic::SceneDrawContext& c)
        {
            const glm::vec4 stone{ 0.42f, 0.40f, 0.38f, 1.0f };
            for (const glm::mat4& xf : m_RockXforms)
                c.DrawMesh(m_Rock, xf, stone);
        };

        renderer.Render(desc);
    }

    void IslandWorld::OnPanels(WorldContext& ctx)
    {
        (void)ctx;
        ImGui::Begin("World Settings");
        ImGui::TextWrapped("Frontier Island — F2 SceneRenderer stopgap scene.");
        ImGui::Separator();

        ImGui::SeparatorText("Time of day");
        ImGui::SliderFloat("Hour", &m_TimeHours, 0.0f, 24.0f, "%.1f h");
        ImGui::SliderFloat("Exposure", &m_Exposure, 0.1f, 4.0f, "%.2f");

        ImGui::SeparatorText("Features");
        ImGui::Checkbox("Skybox",            &m_Settings.Skybox);
        ImGui::Checkbox("IBL",               &m_Settings.IBL);
        ImGui::Checkbox("Shadows",           &m_Settings.Shadows);
        ImGui::Checkbox("Water reflections", &m_Settings.WaterReflections);
        ImGui::Checkbox("SSAO",              &m_Settings.SSAO);
        ImGui::Checkbox("Bloom",             &m_Settings.Bloom);
        ImGui::Checkbox("FXAA",              &m_Settings.FXAA);
        ImGui::Checkbox("Fog",               &m_Settings.Fog);
        ImGui::Checkbox("God rays",          &m_Settings.GodRays);

        ImGui::SeparatorText("Fog");
        ImGui::SliderFloat("Density", &m_Settings.FogDensity, 0.0f, 0.02f, "%.4f");

        ImGui::Separator();
        ImGui::TextWrapped("The real composed island (volcano, lake, snow range, "
                           "river) lands with F11 + F12a/b/c.");
        ImGui::End();
    }

} // namespace Frontier
