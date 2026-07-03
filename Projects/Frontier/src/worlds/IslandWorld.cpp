// IslandWorld.cpp — flagship seamless island (F12a) + Subnautica-style water.
//
// Builds the F11 composed island (volcano, snow range, alpine lake, beach, river)
// as the terrain, an ocean + the alpine lake (water v2), the detailed sky, and a
// time-of-day cycle. Heavy content (the ~2049² terrain) is built on a background
// job so the Detroit-style loading overlay animates instead of a frozen frame.
//
// Water look (see docs/plans water-rendering-notes): Layer 0 shimmer fix (engine),
// Layer 1 vibrant turquoise→deep-blue surface (the depth palette below), Layer 2
// the dive (depth-graded fog + seafloor caustics + god-ray shafts + surface-from-
// below — engine; this world sets the palette + enables them).

#include "worlds/IslandWorld.h"

#include "common/DayNightCycle.h"
#include "common/LavaFlowBuilder.h"       // F12c — river ribbon builder (reused)
#include "common/ProceduralMeshes.h"      // F12c — pine / boulder / bird meshes
#include "common/ProceduralAudio.h"       // F12c — waterfall babble

#include "ui/IconsLucide.h"

#include "jobs/JobSystem.h"

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

    // ---- F12c content helpers --------------------------------------------------

    // A green/rock/dark PBRInstanced material (per-instance Tint gives variety).
    static Cosmic::Ref<Cosmic::Material> MakeInstancedMat(const char* name,
                                                          const glm::vec4& albedo, float rough)
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

    // A flowing-water sheet material (WaterFlow.glsl) — river / waterfall.
    static Cosmic::Ref<Cosmic::Material> MakeWaterFlowMat(const char* name, float flowSpeed,
                                                          float opacity, float foam)
    {
        auto sh = Cosmic::AssetLibrary::GetShader("assets/shaders/WaterFlow.glsl");
        auto m  = Cosmic::Material::Create(sh, name);
        m->Set("u_FlowSpeed",     flowSpeed);
        m->Set("u_Tiling",        glm::vec2(4.0f, 2.0f));
        m->Set("u_TintColor",     glm::vec3(0.10f, 0.30f, 0.35f));
        m->Set("u_Opacity",       opacity);
        m->Set("u_FoamStrength",  foam);
        m->Set("u_NormalStrength",0.5f);
        m->Set("u_SpecularPower", 180.0f);
        m->Set("u_StreakStretch", 6.0f);
        m->Set("u_Time",          0.0f);
        return m;
    }

    // Shortest distance from p to segment [a, b] (world XZ).
    static float SegDist(glm::vec2 p, glm::vec2 a, glm::vec2 b)
    {
        const glm::vec2 ab = b - a;
        const float denom  = std::max(glm::dot(ab, ab), 1e-6f);
        const float t      = glm::clamp(glm::dot(p - a, ab) / denom, 0.0f, 1.0f);
        return glm::length(p - (a + t * ab));
    }

    // A near-vertical WaterFlow sheet from a top edge (topC, y=topY) down to a
    // bottom edge (botC, y=botY), width along `across`. +U runs down the fall.
    static Cosmic::Ref<Cosmic::Mesh> MakeFallSheet(glm::vec2 topC, float topY,
                                                   glm::vec2 botC, float botY,
                                                   glm::vec2 across, float width)
    {
        const glm::vec2 a = glm::normalize(across) * (width * 0.5f);
        const glm::vec3 tl{ topC.x - a.x, topY, topC.y - a.y };
        const glm::vec3 tr{ topC.x + a.x, topY, topC.y + a.y };
        const glm::vec3 bl{ botC.x - a.x, botY, botC.y - a.y };
        const glm::vec3 br{ botC.x + a.x, botY, botC.y + a.y };

        const float drop = glm::length(glm::vec3(botC.x - topC.x, botY - topY, botC.y - topC.y));
        const float vLen = std::max(drop / std::max(width, 0.01f), 0.5f);

        glm::vec3 n = glm::normalize(glm::cross(br - tl, bl - tr));   // sheet face
        const glm::vec4 tan{ glm::normalize(glm::vec3(a.x, 0.0f, a.y)), 1.0f };

        std::vector<Cosmic::MeshVertex> v = {
            { tl, n, { 0.0f, 0.0f }, tan },
            { tr, n, { 0.0f, 1.0f }, tan },
            { bl, n, { vLen, 0.0f }, tan },
            { br, n, { vLen, 1.0f }, tan },
        };
        std::vector<uint32_t> idx = { 0, 2, 1,  1, 2, 3 };
        return Cosmic::Mesh::Create(v, idx);
    }

    // Build the heavy content (terrain + waters) into `out`. Runs on a JobSystem
    // worker — all CPU-only (Terrain/Water GPU resources are lazy), so it is safe
    // off the main thread. `params` is a copy (the job never touches the world).
    static void BuildIslandContent(IslandParams params, IslandWorld::LoadResult& out)
    {
        Cosmic::TerrainSpecification tspec;
        tspec.Resolution  = 2049;              // (64 * 32) + 1
        tspec.WorldSize   = kWorldSize;
        tspec.HeightScale = kHeightScale;
        tspec.BaseHeight  = kBaseHeight;
        tspec.HeightFunction = [params](float u, float v) { return IslandHeight(params, u, v); };

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
        out.Terrain = Cosmic::Terrain::Create(tspec);

        // Lake placement from the island params (world space).
        const glm::vec2 minCorner{ -kWorldSize * 0.5f, -kWorldSize * 0.5f };  // Origin {0,0}
        out.LakeCenterWorld = minCorner + params.LakeCenter * kWorldSize;
        out.LakeRadiusWorld = params.LakeRadius * kWorldSize;
        out.LakeSurfaceY    = LakeSurfaceWorldY(params, kHeightScale, kBaseHeight);

        // --- Ocean: 6000 m plane at Y=0. Layer 1 palette: turquoise shallows -> deep
        //     blue; shore-aware waves + caustics + sparkle (Layer 0 tames aliasing). ---
        Cosmic::WaterSpecification ospec;
        ospec.Center               = { 0.0f, 0.0f };
        ospec.Extent               = { 6000.0f, 6000.0f };
        ospec.SurfaceHeight        = kOceanY;
        ospec.Waves                = MakeOceanWaves();
        ospec.ShallowColor         = { 0.10f, 0.52f, 0.55f };   // bright tropical turquoise
        ospec.DeepColor            = { 0.02f, 0.09f, 0.20f };   // deep ocean blue
        ospec.DepthFadeDistance    = 18.0f;                     // clear shallows, sand read-through
        ospec.ShoreDepthRange      = 8.0f;
        ospec.CausticStrength      = 0.6f;
        ospec.SparkleStrength      = 0.4f;
        ospec.ReflectionResolution = 1024;
        out.Ocean = Cosmic::Water::Create(ospec);
        if (out.Ocean)
            out.Ocean->SetShoreTerrain(out.Terrain);

        // --- Lake: sized to the basin, calm ripples, strong caustics, alpine teal ---
        Cosmic::WaterSpecification lspec;
        lspec.Center               = out.LakeCenterWorld;
        lspec.Extent               = { out.LakeRadiusWorld * 2.2f, out.LakeRadiusWorld * 2.2f };
        lspec.SurfaceHeight        = out.LakeSurfaceY;
        lspec.Waves                = MakeLakeWaves();
        lspec.ShallowColor         = { 0.10f, 0.42f, 0.48f };
        lspec.DeepColor            = { 0.02f, 0.12f, 0.20f };
        lspec.DepthFadeDistance    = 10.0f;
        lspec.ShoreDepthRange      = 4.0f;
        lspec.CausticStrength      = 0.9f;
        lspec.ReflectionResolution = 512;
        out.Lake = Cosmic::Water::Create(lspec);
        if (out.Lake)
            out.Lake->SetShoreTerrain(out.Terrain);

        out.Ready.store(true, std::memory_order_release);
    }

    void IslandWorld::OnAttach()
    {
        // Kick the heavy build onto a worker thread; the loading overlay animates
        // until LoadResult::Ready flips. The job captures a shared_ptr to the result
        // (survives a detach mid-load) and a COPY of the island params (never `this`).
        m_Load = std::make_shared<LoadResult>();
        m_RevealFrames = 0;
        auto load = m_Load;
        IslandParams params = m_Island;
        Cosmic::JobSystem::Get().Submit([load, params]() { BuildIslandContent(params, *load); });

        // --- Render policy: the full island look + the Subnautica dive palette ---
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

        // Heat haze over the volcano's lava (F12b) — the distortion field is written
        // only by the caldera haze emitter, so the shimmer stays localized.
        m_Settings.HeatHaze            = true;
        m_Settings.HeatHazeStrength    = 0.015f;

        // Snow overlay (F8): dust the high pines/boulders above the treeline. Scene-
        // wide PBR overlay (no mask → uniform coverage on up-facing surfaces above
        // the line); the terrain keeps its own splat snow (OverlayAmount stays 0).
        m_Snow.Amount    = 0.9f;
        m_Snow.Line      = 360.0f;
        m_Snow.BlendHalf = 50.0f;
        m_Snow.SlopeSharp = 3.0f;
        m_Snow.Sparkle   = 0.5f;

        // God-ray shafts: on for the dive (crepuscular rays above water too); tinted
        // to the water medium underwater by the tonemap (Layer 2c).
        m_Settings.GodRays          = true;
        m_Settings.GodRaysIntensity = 0.5f;
        m_Settings.GodRaysDensity   = 0.06f;

        // Underwater medium (Layer 2a/2b): depth-graded fog + seafloor caustics.
        m_Settings.Underwater                = false;   // per-frame: on when the camera dives
        m_Settings.UnderwaterY               = kOceanY;
        m_Settings.UnderwaterColor           = { 0.10f, 0.34f, 0.42f };   // bright shallow blue-green
        m_Settings.UnderwaterDeepColor       = { 0.01f, 0.05f, 0.12f };   // deep navy
        m_Settings.UnderwaterDensity         = 0.015f;   // clear shallows; depth grading murkies deep
        m_Settings.UnderwaterTint            = { 0.45f, 0.72f, 0.85f };
        m_Settings.UnderwaterDepthReference  = 60.0f;   // depth over which fog reaches the deep color
        m_Settings.UnderwaterCausticStrength = 0.5f;
        m_Settings.UnderwaterCausticScale    = 0.12f;
    }

    void IslandWorld::OnDetach()
    {
        // Abandon any in-flight build (the job keeps its own shared_ptr copy alive
        // until it finishes, then frees it — no wait, no use-after-free). Release GPU
        // resources while the context is live (client-dev rule).
        m_Load.reset();
        m_Terrain.reset();
        m_Ocean.reset();
        m_Lake.reset();

        // F12b/c content (release GPU refs + stop ambience loops while the context lives).
        m_Volcano.Shutdown();
        m_Babble.Stop();
        m_Pines.Reset();
        m_Boulders.Reset();
        m_RiverMesh.reset(); m_FallMesh.reset(); m_RiverMat.reset(); m_FallMat.reset();
        m_Mist.reset(); m_Spray.reset();
        m_BirdMesh.reset(); m_BirdMat.reset(); m_BirdSet.reset();
        m_Splash.reset(); m_Fireflies.reset();
        m_ContentBuilt = false;

        // Drop the scene-wide snow overlay so another world doesn't inherit it.
        Cosmic::Renderer3D::ClearSnow();
    }

    bool IslandWorld::IsLoading() const
    {
        // Loading until the CPU build completes AND a few frames have rendered (so the
        // first-frame GPU/shader-compile hitch happens under the overlay).
        const bool cpuReady = m_Load && m_Load->Ready.load(std::memory_order_acquire);
        return !cpuReady || m_RevealFrames < 3;
    }

    // One-time main-thread assembly of the F12b/c content: the volcano, instanced
    // forests, waterfall + river, and wildlife. Runs the frame the async terrain is
    // adopted — the terrain's CPU queries are ready, and a GL context is live for the
    // meshes/emitters. The hitch is hidden under the loading overlay (RevealFrames).
    void IslandWorld::BuildContent()
    {
        if (m_ContentBuilt || !m_Terrain)
            return;

        const glm::vec2 minCorner = m_Terrain->GetWorldMinCorner();
        const float     size      = kWorldSize;

        // --- Volcano (F12b): south-east cone + caldera ---
        {
            VolcanoConfig vc;
            vc.CenterXZ           = minCorner + m_Island.VolcanoCenter * size;
            vc.CalderaRadiusWorld = m_Island.CalderaRadius * size;
            vc.SeaLevelY          = kOceanY;
            vc.Seed               = m_Island.Seed;
            vc.FlowCount          = 3;
            vc.FlowWidth          = 12.0f;
            vc.FlowStep           = 8.0f;
            vc.FlowMaxSteps       = 180;
            vc.EmberRate          = 320.0f;
            vc.SmokeRate          = 45.0f;
            vc.SmokeScale         = 4.0f;
            vc.FumaroleCount      = 4;
            vc.LightRadius        = 90.0f;
            vc.LightIntensity     = 6.0f;
            vc.RumbleRadius       = 1600.0f;
            vc.RumbleVolume       = 0.9f;
            m_Volcano.Build(m_Terrain, vc);
        }

        // --- Forests (F12c): keep-out = lake + river + the bare volcanic cone ---
        const glm::vec2 volcC = minCorner + m_Island.VolcanoCenter * size;
        const float     volcR = m_Island.VolcanoRadius * size;
        std::vector<glm::vec2> riverW;
        for (const glm::vec2& uv : m_Island.RiverPath)
            riverW.push_back(minCorner + uv * size);
        const glm::vec2 lakeC = m_LakeCenterWorld;
        const float     lakeR = m_LakeRadiusWorld;

        auto keepOut = [lakeC, lakeR, riverW, volcC, volcR](glm::vec2 xz) -> bool
        {
            if (glm::length(xz - lakeC) < lakeR * 1.3f) return true;
            if (glm::length(xz - volcC) < volcR * 0.9f) return true;
            for (size_t i = 0; i + 1 < riverW.size(); ++i)
                if (SegDist(xz, riverW[i], riverW[i + 1]) < 45.0f) return true;
            return false;
        };

        {   // Pines: alpine band, gentle slopes only.
            ScatterParams p;
            p.Count = 5000; p.Seed = m_Island.Seed ^ 0x1111u;
            p.MinHeight = 8.0f; p.MaxHeight = 400.0f;
            p.MinNormalY = 0.74f;
            p.MinScale = 0.8f; p.MaxScale = 1.5f;
            p.YOffset = -0.4f;
            p.BaseTint = { 0.55f, 0.72f, 0.42f };
            p.TintJitter = 0.18f;
            p.RejectXZ = keepOut;
            m_Pines.Build(ProceduralMeshes::MakePine(m_Island.Seed),
                          MakeInstancedMat("Pine", { 0.16f, 0.32f, 0.12f, 1.0f }, 0.9f),
                          Scatter::Generate(*m_Terrain, p), 7.0f);
        }
        {   // Boulders: anywhere dry, steeper allowed.
            ScatterParams p;
            p.Count = 1500; p.Seed = m_Island.Seed ^ 0x2222u;
            p.MinHeight = 2.0f; p.MaxHeight = 620.0f;
            p.MinNormalY = 0.5f;
            p.MinScale = 1.0f; p.MaxScale = 3.2f;
            p.YOffset = -0.5f;
            p.BaseTint = { 0.62f, 0.60f, 0.58f };
            p.TintJitter = 0.10f;
            p.RejectXZ = [lakeC, lakeR](glm::vec2 xz){ return glm::length(xz - lakeC) < lakeR * 1.1f; };
            m_Boulders.Build(ProceduralMeshes::MakeBoulder(m_Island.Seed),
                             MakeInstancedMat("Boulder", { 0.42f, 0.40f, 0.40f, 1.0f }, 0.85f),
                             Scatter::Generate(*m_Terrain, p), 4.0f);
        }

        // --- Waterfall + river (F12c): WaterFlow sheets drawn in the transparent pass ---
        m_RiverMat = MakeWaterFlowMat("River",     0.25f, 0.55f, 0.35f);
        m_FallMat  = MakeWaterFlowMat("Waterfall", 1.6f,  0.8f,  1.0f);
        {   // River: densified lake→coast polyline, hugging the carved channel.
            std::vector<glm::vec3> path;
            for (size_t i = 0; i + 1 < riverW.size(); ++i)
                for (int s = 0; s < 12; ++s)
                {
                    const glm::vec2 xz = glm::mix(riverW[i], riverW[i + 1], s / 12.0f);
                    path.push_back({ xz.x, m_Terrain->SampleHeight(xz.x, xz.y) + 0.6f, xz.y });
                }
            if (riverW.size() >= 2)
            {
                const glm::vec2 last = riverW.back();
                path.push_back({ last.x, m_Terrain->SampleHeight(last.x, last.y) + 0.6f, last.y });
            }
            m_RiverMesh = LavaFlowBuilder::BuildRibbon(*m_Terrain, path, 26.0f, 0.6f);
        }
        if (riverW.size() >= 2)   // Waterfall sheet at the lake outflow + plunge pool FX.
        {
            const glm::vec2 out2 = glm::normalize(riverW[1] - riverW[0]);
            const glm::vec2 topC = lakeC + out2 * (lakeR * 1.02f);
            const glm::vec2 botC = topC + out2 * 40.0f;
            const float     topY = m_LakeSurfaceY;
            const float     botY = m_Terrain->SampleHeight(botC.x, botC.y) + 1.0f;
            const glm::vec2 acr  = glm::vec2(-out2.y, out2.x);
            m_FallMesh   = MakeFallSheet(topC, topY, botC, botY, acr, 24.0f);
            m_PlungePool = { botC.x, botY, botC.y };

            m_Mist = Cosmic::ParticleEmitter::Create(Cosmic::Presets::Mist({ 40.0f, 20.0f, 40.0f }));
            if (m_Mist)
                m_Mist->SetTransform(glm::translate(glm::mat4(1.0f), m_PlungePool + glm::vec3(0.0f, 8.0f, 0.0f)));

            Cosmic::ParticleEmitterSpec sp = Cosmic::Presets::SoftPuff();
            sp.SpawnRate = 40.0f; sp.SizeStart = 1.0f; sp.SizeEnd = 4.0f;
            sp.Gravity = { 0.0f, 0.8f, 0.0f };
            m_Spray = Cosmic::ParticleEmitter::Create(sp);
            if (m_Spray)
                m_Spray->SetTransform(glm::translate(glm::mat4(1.0f), m_PlungePool + glm::vec3(0.0f, 3.0f, 0.0f)));

            m_Babble.Start(ProceduralAudio::Ensure("water"));
        }

        // --- Wildlife (F12c): birds over the range, fish rings, shore fireflies ---
        {
            const glm::vec2 rangeMid = minCorner + (m_Island.RangeA + m_Island.RangeB) * 0.5f * size;
            const glm::vec3 anchor{ rangeMid.x, 360.0f, rangeMid.y };
            m_Birds.Init(40, anchor, 420.0f, 0.0f, 22.0f, 3.5f, m_Island.Seed);
            m_BirdMesh = ProceduralMeshes::MakeBird();
            m_BirdMat  = MakeInstancedMat("Bird", { 0.09f, 0.09f, 0.10f, 1.0f }, 0.8f);
            m_BirdSet  = Cosmic::InstanceSet::Create(m_Birds.Count());
        }
        m_Splash = Cosmic::ParticleEmitter::Create(Cosmic::Presets::SplashRings(0.0f));
        {   // Fireflies (additive warm motes near the shore) — submitted only at night.
            Cosmic::ParticleEmitterSpec s = Cosmic::Presets::SoftPuff();
            s.MaxParticles = 512;
            s.SpawnRate = 60.0f;
            s.Shape = Cosmic::EmitterShape::Box;
            s.BoxExtents = { m_LakeRadiusWorld * 2.4f, 8.0f, m_LakeRadiusWorld * 2.4f };
            s.SpeedMin = 0.2f; s.SpeedMax = 1.0f;
            s.LifeMin = 1.5f; s.LifeMax = 3.5f;
            s.Gravity = { 0.0f, 0.0f, 0.0f }; s.Drag = 1.2f;
            s.SizeStart = 0.3f; s.SizeEnd = 0.15f;
            s.ColorStart = { 1.0f, 0.9f, 0.5f, 0.9f };
            s.ColorEnd   = { 1.0f, 0.6f, 0.2f, 0.0f };
            s.Blend = Cosmic::ParticleBlend::Additive;
            m_Fireflies = Cosmic::ParticleEmitter::Create(s);
            if (m_Fireflies)
                m_Fireflies->SetTransform(glm::translate(glm::mat4(1.0f),
                    { m_LakeCenterWorld.x, m_LakeSurfaceY + 4.0f, m_LakeCenterWorld.y }));
        }

        m_ContentBuilt = true;
    }

    void IslandWorld::OnUpdate(WorldContext& ctx)
    {
        // Adopt the async-built content once its CPU build completes (once).
        if (!m_Terrain && m_Load && m_Load->Ready.load(std::memory_order_acquire))
        {
            m_Terrain         = m_Load->Terrain;
            m_Ocean           = m_Load->Ocean;
            m_Lake            = m_Load->Lake;
            m_LakeCenterWorld = m_Load->LakeCenterWorld;
            m_LakeRadiusWorld = m_Load->LakeRadiusWorld;
            m_LakeSurfaceY    = m_Load->LakeSurfaceY;
        }

        // Still building (or renderer not up yet): leave the viewport at its dark
        // clear — FrontierApp draws the loading overlay on top.
        if (!m_Terrain || !ctx.Renderer || !ctx.Renderer->IsInitialized())
            return;

        // Assemble the volcano/forests/waterfall/wildlife once the terrain is live.
        BuildContent();

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

        // Underwater medium: tint + fog + caustics + shaft-tint when the camera dives
        // below the sea (small margin so crossing the surface isn't an instant pop).
        m_Settings.Underwater  = m_UnderwaterEnabled && (camPos.y < kOceanY + 1.0f);
        m_Settings.UnderwaterY = kOceanY;

        // --- Simulate the F12b/c content this frame ---
        const float dt = ctx.DeltaTime;
        m_Volcano.Update(dt, ctx.TimeSeconds, camPos);

        // Snow overlay (F8): dust the high pines/boulders. Global Renderer3D state —
        // set every frame while active so switching worlds can't leak coverage.
        m_Snow.Color = state.Night ? glm::vec3(0.80f, 0.85f, 0.95f) : glm::vec3(0.93f, 0.95f, 0.98f);
        Cosmic::Renderer3D::SetSnow(m_Snow);

        // Cull the forests once against the main camera frustum (F5 policy: draw the
        // survivors in every pass).
        const Cosmic::Frustum frustum = Cosmic::Frustum::FromViewProjection(cam->GetViewProjectionMatrix());
        m_Pines.CullAndUpload(frustum);
        m_Boulders.CullAndUpload(frustum);

        // Birds: flock + upload the per-frame oriented transforms (no cull — few).
        m_Birds.Update(dt);
        if (m_BirdSet)
            m_BirdSet->SetInstances(m_Birds.Transforms().data(), nullptr, m_Birds.Count());

        // River / waterfall scroll; plunge-pool mist + spray; babble by distance.
        if (m_RiverMat) m_RiverMat->Set("u_Time", ctx.TimeSeconds);
        if (m_FallMat)  m_FallMat->Set("u_Time",  ctx.TimeSeconds);
        if (m_Mist)  m_Mist->Update(dt, ctx.TimeSeconds);
        if (m_Spray) m_Spray->Update(dt, ctx.TimeSeconds);
        m_Babble.Update(camPos, m_PlungePool, 350.0f, 0.7f);

        // Fish splash rings: fire one over a random lake point every few seconds.
        if (m_Splash)
        {
            m_SplashTimer -= dt;
            if (m_SplashTimer <= 0.0f)
            {
                const float a = m_Rng.Range(0.0f, 6.2831853f);
                const float r = m_Rng.Range(0.0f, m_LakeRadiusWorld * 0.8f);
                m_Splash->SetTransform(glm::translate(glm::mat4(1.0f),
                    { m_LakeCenterWorld.x + std::cos(a) * r, m_LakeSurfaceY + 0.1f,
                      m_LakeCenterWorld.y + std::sin(a) * r }));
                m_Splash->Burst(8);
                m_SplashTimer = m_Rng.Range(4.0f, 9.0f);
            }
            m_Splash->Update(dt, ctx.TimeSeconds);
        }
        if (m_Fireflies) m_Fireflies->Update(dt, ctx.TimeSeconds);

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

        // Volcano emitters (smoke/embers/fumaroles) + heat-haze distortion + the warm
        // pulsing lava lights — appended after the sun/ambient are set.
        m_Volcano.Submit(desc);

        // Waterfall mist + spray, fish rings; fireflies only after dark.
        if (m_Mist)  desc.Emitters.push_back(m_Mist.get());
        if (m_Spray) desc.Emitters.push_back(m_Spray.get());
        if (m_Splash) desc.Emitters.push_back(m_Splash.get());
        if (m_Fireflies && state.Night) desc.Emitters.push_back(m_Fireflies.get());

        // Opaque content: lava, instanced forests, birds (all cast shadows + reflect).
        desc.DrawOpaque = [this](const Cosmic::SceneDrawContext& c)
        {
            m_Volcano.DrawLava(c);
            m_Pines.Draw(c);
            m_Boulders.Draw(c);
            if (m_BirdMesh && m_BirdMat && m_BirdSet && m_BirdSet->GetCount() > 0)
                c.DrawMeshInstanced(m_BirdMesh, m_BirdMat, m_BirdSet, m_BirdSet->GetCount());
        };

        // Transparent content: the flowing river + waterfall sheets (depth-write off).
        desc.DrawTransparent = [this](const Cosmic::SceneDrawContext& c)
        {
            if (!m_RiverMesh && !m_FallMesh)
                return;
            Cosmic::RenderCommand::SetDepthWrite(false);
            if (m_RiverMesh) c.DrawMesh(m_RiverMesh, glm::mat4(1.0f), m_RiverMat);
            if (m_FallMesh)  c.DrawMesh(m_FallMesh,  glm::mat4(1.0f), m_FallMat);
            Cosmic::RenderCommand::SetDepthWrite(true);
        };

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

        // Keep the loading overlay up for the first few rendered frames (hides the
        // one-time shader-compile / IBL-bake hitch), then reveal the scene.
        if (m_RevealFrames < 3)
            ++m_RevealFrames;
    }

    void IslandWorld::OnPanels(WorldContext& ctx)
    {
        (void)ctx;
        ImGui::Begin("World Settings");
        ImGui::TextWrapped("Frontier Island — volcano, forests, waterfall + wildlife "
                           "(F12a-c) with Subnautica-style water (surface + dive).");
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

        ImGui::SeparatorText("Underwater / dive");
        ImGui::Checkbox("Underwater medium", &m_UnderwaterEnabled);
        ImGui::SliderFloat("Fog density",    &m_Settings.UnderwaterDensity, 0.005f, 0.15f, "%.3f");
        ImGui::SliderFloat("Deep depth (m)", &m_Settings.UnderwaterDepthReference, 10.0f, 150.0f, "%.0f");
        ImGui::ColorEdit3("Shallow tint",    &m_Settings.UnderwaterColor.x,      ImGuiColorEditFlags_NoInputs);
        ImGui::ColorEdit3("Deep tint",       &m_Settings.UnderwaterDeepColor.x,  ImGuiColorEditFlags_NoInputs);
        ImGui::SliderFloat("Caustics",       &m_Settings.UnderwaterCausticStrength, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Caustic scale",  &m_Settings.UnderwaterCausticScale, 0.02f, 0.4f, "%.3f");
        ImGui::SliderFloat("God-ray power",  &m_Settings.GodRaysIntensity, 0.0f, 1.5f, "%.2f");

        ImGui::SeparatorText("Aerial fog");
        ImGui::SliderFloat("Density", &m_Settings.FogDensity, 0.0f, 0.02f, "%.4f");

        ImGui::End();
    }

} // namespace Frontier
