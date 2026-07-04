// BlizzardWorld.cpp — whiteout mountain storm (F14). See BlizzardWorld.h.
//
// The showcase for F8's accumulation mask: the terrain starts bare rock, and
// snow VISIBLY builds up on the cabin roof + pine tops over ~45 s while the
// ground sheltered under the cabin's eaves stays bare (the CoverageCapture mask
// stores the top-surface height per column, so receivers below it are rejected).
// Dense wind-blown snowfall + heavy fog sell the whiteout; two warm emissive
// windows glow through it.

#include "worlds/BlizzardWorld.h"

#include "common/ProceduralAudio.h"      // wind loop

#include "ui/IconsLucide.h"

#include "jobs/JobSystem.h"

#include <imgui.h>

#include <cmath>
#include <vector>

namespace Frontier
{
    // ---- Terrain shape (a compact ridged massif) ------------------------------
    static constexpr float    kWorldSize   = 768.0f;
    static constexpr float    kHeightScale = 520.0f;
    static constexpr float    kBaseHeight  = -40.0f;
    static constexpr uint32_t kSeed        = 0xB112A2Du;

    // Ridged-dominant heightfield with a flattened central shelf for the cabin
    // (so it doesn't perch on a knife-edge ridge). Deterministic per (seed,u,v).
    static float BlizzardHeight(const Cosmic::Noise& n, float u, float v)
    {
        const float ridged = n.Ridged2D(u * 2.6f, v * 2.6f, 6);        // sharp alpine ridgelines
        float h = 0.12f + 0.72f * ridged;
        h += 0.10f * (0.5f + 0.5f * n.Fbm2D(u * 7.0f + 11.0f, v * 7.0f + 3.0f, 4));

        const float r     = glm::length(glm::vec2(u - 0.5f, v - 0.5f));
        const float shelf = glm::smoothstep(0.14f, 0.03f, r);          // 1 near the centre
        h = glm::mix(h, 0.34f, shelf * 0.85f);                         // a mid plateau for the cabin
        return glm::clamp(h, 0.0f, 1.0f);
    }

    // ---- Cabin geometry (box body + gable roof with sheltering eaves) ---------
    namespace
    {
        using Cosmic::MeshVertex;

        // A planar quad p0->p1->p2->p3 (CCW), outward normal from the edges.
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

        void AddTri(std::vector<MeshVertex>& v, std::vector<uint32_t>& idx,
                    glm::vec3 p0, glm::vec3 p1, glm::vec3 p2)
        {
            glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
            n = (glm::dot(n, n) > 1e-8f) ? glm::normalize(n) : glm::vec3(0.0f, 1.0f, 0.0f);
            const uint32_t b = static_cast<uint32_t>(v.size());
            v.push_back({ p0, n, { 0.0f, 0.0f }, {} });
            v.push_back({ p1, n, { 1.0f, 0.0f }, {} });
            v.push_back({ p2, n, { 0.5f, 1.0f }, {} });
            idx.insert(idx.end(), { b, b + 1, b + 2 });
        }

        void AddBox(std::vector<MeshVertex>& v, std::vector<uint32_t>& idx, glm::vec3 mn, glm::vec3 mx)
        {
            const glm::vec3 c000{ mn.x, mn.y, mn.z }, c100{ mx.x, mn.y, mn.z };
            const glm::vec3 c110{ mx.x, mx.y, mn.z }, c010{ mn.x, mx.y, mn.z };
            const glm::vec3 c001{ mn.x, mn.y, mx.z }, c101{ mx.x, mn.y, mx.z };
            const glm::vec3 c111{ mx.x, mx.y, mx.z }, c011{ mn.x, mx.y, mx.z };
            AddQuad(v, idx, c001, c101, c111, c011);   // +Z
            AddQuad(v, idx, c100, c000, c010, c110);   // -Z
            AddQuad(v, idx, c101, c100, c110, c111);   // +X
            AddQuad(v, idx, c000, c001, c011, c010);   // -X
            AddQuad(v, idx, c010, c011, c111, c110);   // +Y
            AddQuad(v, idx, c000, c100, c101, c001);   // -Y
        }

        // A log-cabin body + gable roof. Origin at the base centre, +Z is the front
        // (a wide porch eave overhangs +Z so the ground beneath it stays snow-free).
        Cosmic::Ref<Cosmic::Mesh> MakeCabin()
        {
            std::vector<MeshVertex> v;
            std::vector<uint32_t>   idx;

            // Walls (8 wide, 6 deep, 3.8 tall).
            AddBox(v, idx, { -4.0f, 0.0f, -3.0f }, { 4.0f, 3.8f, 3.0f });

            // Gable roof: ridge along X at y = 5.8, z = 0; eaves at y = 3.9, with a
            // big +Z porch overhang (z = 5.5) and a modest -Z eave (z = -4.5).
            const glm::vec3 ridgeL{ -4.0f, 5.8f, 0.0f }, ridgeR{ 4.0f, 5.8f, 0.0f };
            const glm::vec3 eaveFL{ -4.0f, 3.9f, 5.5f }, eaveFR{ 4.0f, 3.9f, 5.5f };
            const glm::vec3 eaveBL{ -4.0f, 3.9f, -4.5f }, eaveBR{ 4.0f, 3.9f, -4.5f };
            AddQuad(v, idx, eaveFL, eaveFR, ridgeR, ridgeL);   // +Z slope (up + toward Z)
            AddQuad(v, idx, ridgeL, ridgeR, eaveBR, eaveBL);   // -Z slope (up - toward Z)

            // Gable end triangles closing the roof over the end walls (x = +-4).
            AddTri(v, idx, { 4.0f, 3.8f, 3.0f }, { 4.0f, 3.8f, -3.0f }, { 4.0f, 5.8f, 0.0f });
            AddTri(v, idx, { -4.0f, 3.8f, -3.0f }, { -4.0f, 3.8f, 3.0f }, { -4.0f, 5.8f, 0.0f });

            return Cosmic::Mesh::Create(v, idx);
        }

        // Two warm window quads on the +Z (front) wall, just proud of the face.
        Cosmic::Ref<Cosmic::Mesh> MakeWindows()
        {
            std::vector<MeshVertex> v;
            std::vector<uint32_t>   idx;
            const float z = 3.02f;
            AddQuad(v, idx, { -2.6f, 1.4f, z }, { -1.1f, 1.4f, z }, { -1.1f, 2.7f, z }, { -2.6f, 2.7f, z });
            AddQuad(v, idx, {  1.1f, 1.4f, z }, {  2.6f, 1.4f, z }, {  2.6f, 2.7f, z }, {  1.1f, 2.7f, z });
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

        Cosmic::Ref<Cosmic::Material> MakeInstancedPBR(const char* name, const glm::vec4& albedo, float rough)
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
    } // namespace

    const WorldInfo& BlizzardWorld::GetInfo() const
    {
        static const WorldInfo info{
            "Blizzard Peak",
            ICON_LC_SNOWFLAKE,
            "Whiteout storm — snow accumulating on a cabin and pines in real time",
            { 18.0f, 150.0f, 120.0f },   // above + south-east, looking down at the cabin
            200.0f, -22.0f,
            { 0.62f, 0.66f, 0.72f, 1.0f }
        };
        return info;
    }

    static void BuildBlizzardTerrain(BlizzardWorld::LoadResult& out)
    {
        Cosmic::TerrainSpecification tspec;
        tspec.Resolution  = 1025;   // must be 32*2^k + 1
        tspec.WorldSize   = kWorldSize;
        tspec.HeightScale = kHeightScale;
        tspec.BaseHeight  = kBaseHeight;
        // A Noise captured by value: constructed once, sampled per texel on the
        // single job thread (deterministic; const methods only).
        tspec.HeightFunction = [noise = Cosmic::Noise(kSeed)](float u, float v)
        {
            return BlizzardHeight(noise, u, v);
        };

        // Bare rock (NOT snow) so the accumulation overlay reads as it builds.
        tspec.Layers[0] = { { 0.28f, 0.30f, 0.33f }, 0.40f, nullptr };   // dark rock
        tspec.Layers[1] = { { 0.22f, 0.23f, 0.26f }, 0.50f, nullptr };   // grey slope rock
        tspec.Layers[2] = { { 0.52f, 0.54f, 0.58f }, 0.30f, nullptr };   // light exposed rock
        tspec.Layers[3] = { { 0.20f, 0.20f, 0.22f }, 0.40f, nullptr };   // scree
        tspec.Material.HighHeight = 100000.0f;   // disable built-in splat snow (overlay owns snow)
        tspec.Material.HighBlend  = 1.0f;
        tspec.Material.LowHeight  = -100000.0f;
        tspec.Material.LowBlend   = 1.0f;
        out.Terrain = Cosmic::Terrain::Create(tspec);

        out.Ready.store(true, std::memory_order_release);
    }

    void BlizzardWorld::OnAttach()
    {
        m_Load = std::make_shared<LoadResult>();
        m_RevealFrames = 0;
        m_ContentBuilt = false;
        m_Accum        = 0.0f;
        auto load = m_Load;
        Cosmic::JobSystem::Get().Submit([load]() { BuildBlizzardTerrain(*load); });

        // --- Storm render policy: heavy fog, bright overcast ambient, bloom for
        //     the warm windows. No god rays / lens flare in a whiteout. ---
        m_Settings.Skybox              = true;
        m_Settings.IBL                 = true;
        m_Settings.Shadows             = true;
        m_Settings.WaterReflections    = false;
        m_Settings.TerrainCastsShadows = true;
        m_Settings.ClearColor          = { 0.62f, 0.66f, 0.72f, 1.0f };
        m_Settings.Bloom               = true;
        m_Settings.BloomThreshold      = 1.0f;
        m_Settings.BloomIntensity      = 0.5f;
        m_Settings.FXAA                = true;
        m_Settings.Fog                 = true;
        m_Settings.FogColor            = { 0.62f, 0.65f, 0.70f };
        m_Settings.FogDensity          = 0.012f;   // dense whiteout depth cue
        m_Settings.GodRays             = false;
        m_Settings.HeatHaze            = false;
        m_Settings.LensFlare           = false;
        m_Settings.ShadowRadius        = 260.0f;

        // Snow overlay look (mask fields filled per frame by CoverageCapture).
        m_Snow.Amount    = 1.0f;
        m_Snow.Line      = -100000.0f;    // no altitude gate — the mask + slope govern
        m_Snow.BlendHalf = 20.0f;
        m_Snow.SlopeSharp = 2.5f;
        m_Snow.Color     = { 0.95f, 0.97f, 1.0f };
        m_Snow.Sparkle   = 0.6f;
        m_Snow.MaskYTolerance = 0.6f;

        // Grey overcast detailed sky: dim, no visible sun disc, no stars.
        m_Sky.SkyIntensity      = 0.40f;
        m_Sky.SunDiscIntensity  = 0.0f;
        m_Sky.MoonIntensity     = 0.0f;
        m_Sky.StarIntensity     = 0.0f;
        m_Sky.MilkyWayIntensity = 0.0f;
    }

    void BlizzardWorld::OnDetach()
    {
        if (m_WindVoice != Cosmic::InvalidSoundHandle)
        {
            Cosmic::AudioEngine::Stop(m_WindVoice);
            m_WindVoice = Cosmic::InvalidSoundHandle;
        }
        m_Load.reset();
        m_Terrain.reset();
        m_CabinMesh.reset(); m_WindowMesh.reset(); m_CabinMat.reset(); m_WindowMat.reset();
        m_Pines.Reset();
        m_Snowfall.reset();
        m_Coverage.Shutdown();
        m_ContentBuilt = false;

        // Drop the scene-wide snow overlay so another world doesn't inherit it.
        Cosmic::Renderer3D::ClearSnow();
    }

    bool BlizzardWorld::IsLoading() const
    {
        const bool cpuReady = m_Load && m_Load->Ready.load(std::memory_order_acquire);
        return !cpuReady || m_RevealFrames < 3;
    }

    void BlizzardWorld::BuildContent()
    {
        if (m_ContentBuilt || !m_Terrain)
            return;

        const glm::vec2 minCorner = m_Terrain->GetWorldMinCorner();

        // --- Coverage volume: the whole terrain rect, spanning its height range ---
        m_Coverage.Init(1024, minCorner, kWorldSize, kBaseHeight - 20.0f, kBaseHeight + kHeightScale);

        // --- Cabin on the central shelf ---
        m_CabinMesh  = MakeCabin();
        m_WindowMesh = MakeWindows();
        m_CabinMat   = MakePBR("CabinWood", { 0.20f, 0.12f, 0.07f, 1.0f }, 0.9f, glm::vec3(0.0f));
        m_WindowMat  = MakePBR("CabinWindow", { 0.10f, 0.07f, 0.04f, 1.0f }, 0.4f,
                               glm::vec3(2.4f, 1.5f, 0.7f));   // warm glow (bloom picks it up)

        const glm::vec2 cabinXZ{ 0.0f, 0.0f };
        const float     cabinY = m_Terrain->SampleHeight(cabinXZ.x, cabinXZ.y) - 0.2f;
        m_CabinXform = glm::translate(glm::mat4(1.0f), { cabinXZ.x, cabinY, cabinXZ.y });

        // --- Instanced pines around the cabin (keep-out on the cabin footprint) ---
        {
            ScatterParams p;
            p.Count      = 1400;
            p.Seed       = kSeed ^ 0x7717u;
            p.MinHeight  = kBaseHeight + 4.0f;
            p.MaxHeight  = kBaseHeight + kHeightScale;
            p.MinNormalY = 0.72f;
            p.MinScale   = 0.8f; p.MaxScale = 1.6f;
            p.YOffset    = -0.4f;
            p.BaseTint   = { 0.24f, 0.34f, 0.24f };   // dark evergreen (snow adds white)
            p.TintJitter = 0.10f;
            p.RejectXZ   = [](glm::vec2 xz) { return glm::length(xz) < 22.0f; };  // clear the cabin
            m_Pines.Build(ProceduralMeshes::MakePine(kSeed),
                          MakeInstancedPBR("Pine", { 0.14f, 0.26f, 0.14f, 1.0f }, 0.9f),
                          Scatter::Generate(*m_Terrain, p), 7.0f);
        }

        // --- Dense wind-blown snowfall (velocity-stretched streaks, camera-tracked) ---
        {
            Cosmic::ParticleEmitterSpec s = Cosmic::Presets::Snowfall({ 60.0f, 45.0f, 60.0f }, m_SnowfallRate);
            s.MaxParticles      = 16384;
            s.StretchByVelocity = 0.01f;             // slight streak in the wind
            s.Wind              = { 9.0f, 0.0f, 3.0f };
            s.Gravity           = { 0.0f, -2.2f, 0.0f };
            m_Snowfall = Cosmic::ParticleEmitter::Create(s);
        }

        // --- Gusting wind ambience (raw looping voice; volume LFO in OnUpdate) ---
        m_WindVoice = Cosmic::AudioEngine::PlayLooping(ProceduralAudio::Ensure("wind"), 0.0f);

        m_ContentBuilt = true;
    }

    void BlizzardWorld::OnUpdate(WorldContext& ctx)
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
        const float     dt     = ctx.DeltaTime;

        // --- Storm lighting: a dim, cool, high sun behind the cloud deck. Bright
        //     ambient so the snow reads even in shadow (overcast bounce). ---
        const glm::vec3 toSun = glm::normalize(glm::vec3(0.30f, 0.62f, 0.40f));
        renderer.GetEnvironment().SetSunDirection(toSun);
        renderer.GetEnvironment().SetNightSky(false);
        renderer.GetEnvironment().SetMoon({ 0.0f, 1.0f, 0.0f }, 0.0f);

        // --- Accumulation: advance the 0..1 fraction; the CoverageCapture mask
        //     builds per-column, this fraction drives the terrain overlay ramp. ---
        if (m_Accumulate)
            m_Accum = glm::clamp(m_Accum + dt / std::max(m_AccumSeconds, 1.0f), 0.0f, 1.0f);

        // Snow overlay: pull the latest mask (texture id + rect + Y-decode), keep
        // the look fields, ramp the terrain overlay by the accumulated fraction.
        m_Coverage.FillSnowDesc(m_Snow);
        m_Snow.OverlayAmount = m_Accum;
        Cosmic::Renderer3D::SetSnow(m_Snow);

        // Snowfall box tracks the camera (spawn a bit above so it falls through view).
        if (m_Snowfall)
        {
            m_Snowfall->SetTransform(glm::translate(glm::mat4(1.0f),
                                     camPos + glm::vec3(0.0f, 25.0f, 0.0f)));
            m_Snowfall->Update(dt, ctx.TimeSeconds);
        }

        // Cull the pine field once against the main frustum (F5 policy).
        const Cosmic::Frustum frustum = Cosmic::Frustum::FromViewProjection(cam->GetViewProjectionMatrix());
        m_Pines.CullAndUpload(frustum);

        // Gusting wind volume (two out-of-phase LFOs so it never sits still).
        if (m_WindVoice != Cosmic::InvalidSoundHandle)
        {
            const float g = 0.55f + 0.30f * std::sin(ctx.TimeSeconds * 0.37f)
                                  + 0.12f * std::sin(ctx.TimeSeconds * 1.30f + 1.1f);
            Cosmic::AudioEngine::SetVolume(m_WindVoice, glm::clamp(g, 0.15f, 1.0f));
        }

        // --- Assemble + submit the frame ---
        Cosmic::SceneRenderDesc desc;
        desc.SetCamera(*cam);
        desc.TimeSeconds = ctx.TimeSeconds;
        desc.DeltaTime   = dt;
        desc.Exposure    = m_Exposure;
        desc.Settings    = m_Settings;

        desc.Lights.SunDirection = -toSun;
        desc.Lights.SunColor     = { 0.80f, 0.85f, 0.95f };
        desc.Lights.SunIntensity = 1.1f;
        desc.Lights.Ambient      = 0.55f;

        // Shadow map follows the camera's ground point.
        desc.Settings.ShadowCenter = { camPos.x, m_Terrain->SampleHeight(camPos.x, camPos.z), camPos.z };

        desc.TerrainSystem = m_Terrain.get();
        desc.DetailedSky   = &m_Sky;

        // F8: run the top-down coverage capture + advance the mask this frame.
        desc.Coverage           = &m_Coverage;
        desc.CoverageAccumPerSec = m_Accumulate ? (1.0f / std::max(m_AccumSeconds, 1.0f)) : 0.0f;
        desc.CoverageMeltPerSec  = 0.0f;

        if (m_ShowSnowfall && m_Snowfall)
            desc.Emitters.push_back(m_Snowfall.get());

        // Opaque content: cabin + windows + instanced pines. Routed per pass —
        // in ShadowDepth / TopDownDepth they become depth casters (so the cabin
        // shelters the porch and the pines cast their own coverage shadows).
        desc.DrawOpaque = [this](const Cosmic::SceneDrawContext& c)
        {
            if (m_CabinMesh)  c.DrawMesh(m_CabinMesh,  m_CabinXform, m_CabinMat);
            if (m_WindowMesh) c.DrawMesh(m_WindowMesh, m_CabinXform, m_WindowMat);
            m_Pines.Draw(c);
        };

        renderer.Render(desc);

        if (m_RevealFrames < 3)
            ++m_RevealFrames;
    }

    void BlizzardWorld::OnPanels(WorldContext& ctx)
    {
        (void)ctx;
        ImGui::Begin("World Settings");
        ImGui::TextWrapped("Blizzard Peak — dynamic snow accumulation (F14). Snow builds on "
                           "the cabin roof + pine tops; the porch under the eaves stays bare.");
        ImGui::Separator();

        ImGui::SeparatorText("Accumulation");
        ImGui::ProgressBar(m_Accum, ImVec2(-1, 0), "");
        ImGui::Checkbox("Accumulate", &m_Accumulate);
        ImGui::SameLine();
        if (ImGui::Button("Reset snow"))
        {
            // Reallocate the mask (Init is a no-op while initialized) to clear the
            // ping-pong accumulation back to bare, and reset the overlay ramp.
            m_Accum = 0.0f;
            if (m_Terrain)
            {
                m_Coverage.Shutdown();
                m_Coverage.Init(1024, m_Terrain->GetWorldMinCorner(), kWorldSize,
                                kBaseHeight - 20.0f, kBaseHeight + kHeightScale);
            }
        }
        ImGui::SliderFloat("Fill time (s)", &m_AccumSeconds, 5.0f, 120.0f, "%.0f");
        ImGui::Checkbox("Snowfall", &m_ShowSnowfall);

        ImGui::SeparatorText("Storm");
        ImGui::SliderFloat("Fog density", &m_Settings.FogDensity, 0.002f, 0.03f, "%.3f");
        ImGui::SliderFloat("Exposure",    &m_Exposure, 0.4f, 2.0f, "%.2f");
        ImGui::ColorEdit3("Fog color", &m_Settings.FogColor.x, ImGuiColorEditFlags_NoInputs);

        ImGui::SeparatorText("Features");
        ImGui::Checkbox("Shadows",         &m_Settings.Shadows);
        ImGui::Checkbox("Terrain shadows", &m_Settings.TerrainCastsShadows);
        ImGui::Checkbox("Bloom",           &m_Settings.Bloom);
        ImGui::Checkbox("FXAA",            &m_Settings.FXAA);
        ImGui::Checkbox("Fog",             &m_Settings.Fog);

        ImGui::End();
    }

} // namespace Frontier
