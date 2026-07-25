// render_3d.cpp — 3D golden images (Phase 29 W2 / plan doc 28 §9.5).
//
// These frames exist for ONE reason: W6 partitions SceneRenderer, the riskiest
// phase of the engine split. Every pass the 3D renderer owns is captured here
// BEFORE that surgery, so "the 3D build is pixel-identical" is a measurement
// rather than a claim.
//
//   mesh_pbr       lit PBR spheres over a ground plane, directional light + shadow
//   terrain        the procedural terrain system through desc.TerrainSystem
//   water          a water body with the planar reflection (PrimaryReflectionWater)
//   sky_ibl        procedural sky + IBL, driven the way ApplyEnvironment drives it
//   particles      a CPU-simulated emitter (deterministic — no GPU compute)
//   postchain_off  the same scene with bloom / SSAO / god-rays / FXAA OFF
//   postchain_on   ... and ON. The pair is asserted to actually DIFFER, so a
//                  silently-disabled post stack cannot pass as "unchanged"
//   outline        the K12 selection outline over an ECS scene
//   instancing     an auto-instanced run vs the same scene drawn one mesh at a
//                  time — the two must match, and the stats prove the paths differ
//
// This whole file is 3D-only and is excluded from the 2D configuration by the
// COSMIC_2D_ONLY fence below (the 2D engine has no Renderer3D, terrain, water or
// particles to render).

#include <doctest.h>

#include "GoldenImage.h"

#ifndef COSMIC_2D_ONLY

#include "assets/AssetLibrary.h"
#include "camera/PerspectiveCamera.h"
#include "graphics/FrameBuffer.h"
#include "graphics/Material.h"
#include "graphics/MaterialAsset.h"
#include "graphics/Mesh.h"
#include "graphics/Shader.h"
#include "particles/ParticleSystem.h"
#include "renderer/RenderCommand.h"
#include "renderer/Renderer3D.h"
#include "renderer/SceneRenderer.h"
#include "scene/Components.h"
#ifndef COSMIC_2D_ONLY
#include "scene/Components3D.h"
#endif
#include "scene/Entity.h"
#include "scene/Scene.h"
#include "scene/WorldSystemRecipes.h"
#include "terrain/Terrain.h"
#include "water/Water.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

using namespace Cosmic;
using namespace CosmicRender;

namespace
{
    constexpr uint32_t kW = kGoldenWidth;
    constexpr uint32_t kH = kGoldenHeight;

    // ------------------------------------------------------------------------
    // Shared frame setup
    // ------------------------------------------------------------------------

    // A fixed camera looking down at the origin from the front-right. Every
    // golden that does not need its own framing uses this one, so a change in
    // camera maths shows up across the whole set at once.
    PerspectiveCamera StandardCamera(const glm::vec3& eye   = { 4.5f, 3.0f, 6.0f },
                                     const glm::vec3& target = { 0.0f, 0.5f, 0.0f })
    {
        PerspectiveCamera cam(50.0f, (float)kW / (float)kH, 0.1f, 200.0f);
        cam.LookAt(eye, target);
        return cam;
    }

    // The baseline desc: fixed clock, fixed lights, no sky/IBL/post. Individual
    // goldens turn on exactly the feature they are pinning.
    SceneRenderDesc BaseDesc(const PerspectiveCamera& cam)
    {
        SceneRenderDesc desc;
        desc.SetCamera(cam);
        desc.TimeSeconds = 3.0f;    // fixed: water and particles are time-driven
        desc.DeltaTime   = 0.0f;
        desc.Exposure    = 1.0f;

        desc.Lights.SunDirection = glm::normalize(glm::vec3{ -0.45f, -1.0f, -0.35f });
        desc.Lights.SunColor     = { 1.0f, 0.96f, 0.88f };
        desc.Lights.SunIntensity = 3.0f;
        desc.Lights.Ambient      = 0.25f;

        auto& s = desc.Settings;
        s.ClearColor = { 0.09f, 0.11f, 0.16f, 1.0f };
        s.Skybox = false; s.IBL = false; s.Shadows = false;
        s.SSAO = false; s.Bloom = false; s.GodRays = false; s.FXAA = false;
        s.WaterReflections = false;
        return desc;
    }

    Ref<Material> MakePbrMaterial(const glm::vec4& albedo, float metallic, float roughness,
                                  const char* name, bool instancable = false)
    {
        MaterialAsset a;
        a.Albedo    = albedo;
        a.Metallic  = metallic;
        a.Roughness = roughness;
        Ref<Material> m = AssetLibrary::BuildMaterial(a, name);
        if (m && instancable)
        {
            // The S12.3 twin: with it set, runs of identical (mesh, material)
            // opaque submissions collapse into one instanced draw.
            if (Ref<Shader> twin = AssetLibrary::GetShader("engine://shaders/PBRInstanced.glsl"))
                m->SetInstancingShader(twin);
        }
        return m;
    }

    struct Props
    {
        Ref<Mesh>     Ground, Sphere, Box;
        Ref<Material> GroundMat, MetalMat, ClayMat;
    };

    Props MakeProps()
    {
        Props p;
        p.Ground = Mesh::CreatePlane(24.0f, 24.0f);
        p.Sphere = Mesh::CreateUVSphere(1.0f, 24, 32);
        p.Box    = Mesh::CreateBox({ 1.2f, 1.2f, 1.2f });
        p.GroundMat = MakePbrMaterial({ 0.42f, 0.45f, 0.40f, 1.0f }, 0.0f, 0.85f, "ground");
        p.MetalMat  = MakePbrMaterial({ 0.85f, 0.68f, 0.22f, 1.0f }, 0.95f, 0.22f, "metal");
        p.ClayMat   = MakePbrMaterial({ 0.72f, 0.28f, 0.24f, 1.0f }, 0.0f, 0.55f, "clay");
        return p;
    }

    // The standard prop layout, submitted through the routed context so it lands
    // in the shadow, reflection and main passes alike. The three props are spread
    // along the camera's screen-right axis (~(0.75, 0, -0.56) for StandardCamera)
    // so none occludes another — every material stays visible in the frame, which
    // is what makes a material or lighting regression legible in the diff.
    void SubmitProps(const Props& p, const SceneDrawContext& c)
    {
        c.DrawMesh(p.Ground, glm::mat4(1.0f), p.GroundMat);
        c.DrawMesh(p.Sphere, glm::translate(glm::mat4(1.0f), { -1.9f, 1.0f, 1.4f }), p.MetalMat);
        c.DrawMesh(p.Box,    glm::translate(glm::mat4(1.0f), {  0.0f, 0.6f, 0.0f }) *
                             glm::rotate(glm::mat4(1.0f), glm::radians(24.0f), { 0, 1, 0 }),
                   p.ClayMat);
        c.DrawMesh(p.Sphere, glm::translate(glm::mat4(1.0f), {  1.9f, 0.8f, -1.4f }) *
                             glm::scale(glm::mat4(1.0f), glm::vec3(0.8f)), p.ClayMat);
    }

    // Render `desc` into a fresh golden-sized target and read it back.
    bool RenderToImage(SceneRenderer& renderer, const SceneRenderDesc& desc, Image& out)
    {
        Ref<FrameBuffer> fbo = MakeTarget();
        if (!fbo)
            return false;
        renderer.RenderToTexture(desc, fbo);
        return Capture(fbo, out);
    }
}

TEST_SUITE("3D goldens")
{
    TEST_CASE("mesh_pbr — lit spheres over a ground plane with a cast shadow")
    {
        Props p = MakeProps();
        REQUIRE(p.MetalMat != nullptr);

        PerspectiveCamera cam = StandardCamera();
        SceneRenderDesc desc = BaseDesc(cam);
        desc.Settings.Shadows      = true;
        desc.Settings.ShadowCenter = { 0.0f, 0.0f, 0.0f };
        desc.Settings.ShadowRadius = 12.0f;
        desc.DrawOpaque = [&p](const SceneDrawContext& c) { SubmitProps(p, c); };

        SceneRenderer renderer;
        renderer.Init(kW, kH, 1024);
        REQUIRE(renderer.IsInitialized());

        Image frame;
        REQUIRE(RenderToImage(renderer, desc, frame));
        CHECK(CheckGolden("mesh_pbr", frame));
        renderer.Shutdown();
    }

    TEST_CASE("terrain — the procedural terrain system")
    {
        TerrainComponent tc;
        tc.WorldSize   = 256.0f;
        tc.Resolution  = 129;
        tc.HeightScale = 28.0f;
        tc.BaseHeight  = -4.0f;
        tc.Seed        = 1337u;
        tc.Octaves     = 6;
        tc.Frequency   = 3.0f;
        tc.EdgeFalloff = 0.35f;
        Ref<Terrain> terrain = Terrain::Create(BuildTerrainSpec(tc));
        REQUIRE(terrain != nullptr);

        // Far enough back that the whole 256-unit island is in frame: the golden
        // then covers the quadtree LOD seams and the height-based layer blend,
        // not just one hillside.
        PerspectiveCamera cam = StandardCamera({ 150.0f, 90.0f, 190.0f }, { 0.0f, 4.0f, 0.0f });
        SceneRenderDesc desc = BaseDesc(cam);
        desc.TerrainSystem         = terrain.get();
        desc.Settings.Shadows      = true;
        desc.Settings.ShadowCenter = { 0.0f, 0.0f, 0.0f };
        desc.Settings.ShadowRadius = 120.0f;

        SceneRenderer renderer;
        renderer.Init(kW, kH, 1024);
        REQUIRE(renderer.IsInitialized());

        Image frame;
        REQUIRE(RenderToImage(renderer, desc, frame));
        CHECK(CheckGolden("terrain", frame));
        renderer.Shutdown();
    }

    TEST_CASE("water — a water body with the planar reflection")
    {
        Props p = MakeProps();

        WaterComponent wc;
        wc.Preset         = WaterPreset::Lake;
        wc.Center         = { 0.0f, 0.0f };
        wc.Extent         = { 40.0f, 40.0f };
        wc.SurfaceHeight  = 0.35f;
        wc.GridResolution = 129;
        Ref<Water> water = Water::Create(BuildWaterSpec(wc));
        REQUIRE(water != nullptr);

        PerspectiveCamera cam = StandardCamera({ 6.0f, 2.4f, 9.0f }, { 0.0f, 0.6f, 0.0f });
        SceneRenderDesc desc = BaseDesc(cam);
        desc.WaterBodies.push_back(water.get());
        desc.PrimaryReflectionWater  = 0;                 // the real planar reflection
        desc.Settings.WaterReflections = true;
        // Geometry above the surface, so the reflection has something to show.
        desc.DrawOpaque = [&p](const SceneDrawContext& c)
        {
            c.DrawMesh(p.Sphere, glm::translate(glm::mat4(1.0f), { -1.8f, 1.6f, 0.0f }), p.MetalMat);
            c.DrawMesh(p.Box,    glm::translate(glm::mat4(1.0f), {  1.6f, 1.2f, -1.0f }), p.ClayMat);
        };

        SceneRenderer renderer;
        renderer.Init(kW, kH, 1024);
        REQUIRE(renderer.IsInitialized());

        Image frame;
        REQUIRE(RenderToImage(renderer, desc, frame));
        CHECK(CheckGolden("water", frame));
        renderer.Shutdown();
    }

    TEST_CASE("sky_ibl — procedural sky plus image-based lighting")
    {
        Props p = MakeProps();

        // Driven exactly as the editor and PlayerLayer drive it: an authored
        // EnvironmentComponent mapped in through ApplyEnvironment, which also
        // bakes the environment cube the IBL samples.
        EnvironmentComponent env;
        env.Sky          = EnvironmentComponent::SkyMode::Procedural;
        env.SunDirection = glm::normalize(glm::vec3{ -0.4f, -0.55f, -0.5f });
        env.SunColor     = { 1.0f, 0.94f, 0.82f };
        env.SunIntensity = 3.2f;
        env.Skybox       = true;
        env.IBL          = true;
        env.IBLIntensity = 1.0f;
        // Under-exposed on purpose: at 1.0 the procedural sky clips to flat white
        // and a sky regression would be invisible in the diff. 0.4 keeps the
        // horizon gradient and the metal's specular response inside the 8-bit range.
        env.Exposure     = 0.4f;

        PerspectiveCamera cam = StandardCamera();
        SceneRenderDesc desc = BaseDesc(cam);
        desc.DrawOpaque = [&p](const SceneDrawContext& c) { SubmitProps(p, c); };

        SceneRenderer renderer;
        renderer.Init(kW, kH, 1024);
        REQUIRE(renderer.IsInitialized());
        renderer.ApplyEnvironment(env, desc);
        REQUIRE(desc.Settings.Skybox);
        REQUIRE(desc.Settings.IBL);

        Image frame;
        REQUIRE(RenderToImage(renderer, desc, frame));
        CHECK(CheckGolden("sky_ibl", frame));
        renderer.Shutdown();
    }

    TEST_CASE("particles — a CPU-simulated emitter")
    {
        // GpuSimulation is OFF on purpose: the CPU twin (StepCpu) is bit-exact
        // run to run (test_particle_noise.cpp pins that), so the golden is stable
        // rather than dependent on compute-shader scheduling.
        ParticleEmitterSpec spec;
        spec.MaxParticles   = 512;
        spec.SpawnRate      = 180.0f;
        spec.Shape          = EmitterShape::Cone;
        spec.ShapeRadius    = 0.35f;
        spec.ConeAngleDeg   = 22.0f;
        spec.SpeedMin       = 2.0f;
        spec.SpeedMax       = 3.5f;
        spec.LifeMin        = 1.4f;
        spec.LifeMax        = 2.2f;
        spec.Gravity        = { 0.0f, -1.2f, 0.0f };
        spec.SizeStart      = 0.30f;
        spec.SizeEnd        = 0.05f;
        spec.ColorStart     = { 1.0f, 0.75f, 0.30f, 0.95f };
        spec.ColorEnd       = { 0.55f, 0.10f, 0.05f, 0.0f };
        spec.Blend          = ParticleBlend::Additive;
        spec.GpuSimulation  = false;

        Ref<ParticleEmitter> emitter = ParticleEmitter::Create(spec);
        REQUIRE(emitter != nullptr);
        emitter->SetTransform(glm::translate(glm::mat4(1.0f), { 0.0f, 0.2f, 0.0f }));

        // A fixed number of fixed-size steps: the emitter's state is a pure
        // function of that sequence.
        for (int i = 0; i < 90; ++i)
            emitter->Update(1.0f / 60.0f, (float)i / 60.0f);

        Props p = MakeProps();
        PerspectiveCamera cam = StandardCamera({ 0.0f, 2.2f, 7.0f }, { 0.0f, 1.6f, 0.0f });
        SceneRenderDesc desc = BaseDesc(cam);
        desc.TimeSeconds = 90.0f / 60.0f;
        desc.Emitters.push_back(emitter.get());
        desc.DrawOpaque = [&p](const SceneDrawContext& c)
        {
            c.DrawMesh(p.Ground, glm::mat4(1.0f), p.GroundMat);
        };

        SceneRenderer renderer;
        renderer.Init(kW, kH, 1024);
        REQUIRE(renderer.IsInitialized());

        Image frame;
        REQUIRE(RenderToImage(renderer, desc, frame));
        CHECK(CheckGolden("particles", frame));
        renderer.Shutdown();
    }

    TEST_CASE("postchain — bloom / SSAO / god-rays / FXAA off versus on")
    {
        Props p = MakeProps();

        EnvironmentComponent env;
        env.Sky          = EnvironmentComponent::SkyMode::Procedural;
        env.SunDirection = glm::normalize(glm::vec3{ -0.35f, -0.42f, -0.60f });
        env.SunIntensity = 4.0f;
        env.Skybox       = true;
        env.IBL          = true;
        env.Exposure     = 0.4f;   // keep the frame off the clipping ceiling (see sky_ibl)

        PerspectiveCamera cam = StandardCamera();

        SceneRenderer renderer;
        renderer.Init(kW, kH, 1024);
        REQUIRE(renderer.IsInitialized());

        auto build = [&](bool post)
        {
            SceneRenderDesc d = BaseDesc(cam);
            d.DrawOpaque = [&p](const SceneDrawContext& c) { SubmitProps(p, c); };
            renderer.ApplyEnvironment(env, d);
            d.Settings.Shadows      = true;
            d.Settings.ShadowCenter = { 0.0f, 0.0f, 0.0f };
            d.Settings.ShadowRadius = 12.0f;
            d.Settings.Bloom     = post;
            d.Settings.SSAO      = post;
            d.Settings.GodRays   = post;
            d.Settings.FXAA      = post;
            d.Settings.BloomThreshold = 0.9f;
            d.Settings.BloomIntensity = 0.8f;
            return d;
        };

        Image off, on;
        REQUIRE(RenderToImage(renderer, build(false), off));
        REQUIRE(RenderToImage(renderer, build(true),  on));

        CHECK(CheckGolden("postchain_off", off));
        CHECK(CheckGolden("postchain_on",  on));

        // The pair must actually differ — otherwise a post chain that silently
        // stopped running would sail through both goldens.
        CHECK_FALSE(BytesEqual(off, on));
        const Diff d = Compare(on, off);
        CHECK(d.DifferingFraction() > kPixelBudget);

        renderer.Shutdown();
    }

    TEST_CASE("outline — the K12 selection silhouette over an ECS scene")
    {
        Props p = MakeProps();

        // The outline pass runs a selection-filtered id pass over desc.EcsScene,
        // so this golden needs real entities. With EcsScene set and NO DrawOpaque
        // hook, Scene::OnRender3D is the only submitter — no double draw.
        Ref<Scene> scene = Scene::Create();
        Scene& s = *scene;

        Entity ground = s.CreateEntity("ground");
        {
            auto& mr = ground.AddComponent<MeshRendererComponent>();
            mr.MeshAsset = p.Ground; mr.MaterialAsset = p.GroundMat;
        }

        Entity selected = s.CreateEntity("selected");
        selected.GetComponent<TransformComponent>().Position = { -1.8f, 1.0f, 0.0f };
        {
            auto& mr = selected.AddComponent<MeshRendererComponent>();
            mr.MeshAsset = p.Sphere; mr.MaterialAsset = p.MetalMat;
        }

        Entity other = s.CreateEntity("other");
        other.GetComponent<TransformComponent>().Position = { 1.4f, 0.8f, -1.2f };
        {
            auto& mr = other.AddComponent<MeshRendererComponent>();
            mr.MeshAsset = p.Box; mr.MaterialAsset = p.ClayMat;
        }

        const std::vector<entt::entity> selection{ (entt::entity)selected };

        PerspectiveCamera cam = StandardCamera();
        SceneRenderDesc desc = BaseDesc(cam);
        desc.EcsScene                = &s;
        desc.SelectedEntities        = &selection;
        desc.Settings.OutlineEnabled = true;
        desc.Settings.OutlineColor   = { 1.0f, 0.62f, 0.11f };
        desc.Settings.OutlineWidthPx = 2.0f;

        SceneRenderer renderer;
        renderer.Init(kW, kH, 1024);
        REQUIRE(renderer.IsInitialized());

        Image frame;
        REQUIRE(RenderToImage(renderer, desc, frame));
        CHECK(CheckGolden("outline", frame));
        renderer.Shutdown();
    }

    TEST_CASE("instancing — an auto-instanced run matches the one-mesh-at-a-time draw")
    {
        // S12.3 collapses runs of identical (mesh, material) opaque submissions
        // with entityID == -1 into a single instanced draw, but ONLY when the
        // material carries an instancing twin. Two otherwise identical materials
        // — one with the twin, one without — therefore take genuinely different
        // GPU paths and must produce the same pixels.
        Ref<Mesh> box     = Mesh::CreateBox({ 0.7f, 0.7f, 0.7f });
        Ref<Mesh> ground  = Mesh::CreatePlane(24.0f, 24.0f);
        Ref<Material> groundMat   = MakePbrMaterial({ 0.42f, 0.45f, 0.40f, 1.0f }, 0.0f, 0.85f, "i_ground");
        Ref<Material> instanced   = MakePbrMaterial({ 0.30f, 0.55f, 0.85f, 1.0f }, 0.1f, 0.40f, "i_on",  true);
        Ref<Material> plain       = MakePbrMaterial({ 0.30f, 0.55f, 0.85f, 1.0f }, 0.1f, 0.40f, "i_off", false);
        REQUIRE(instanced != nullptr);
        REQUIRE(plain != nullptr);
        REQUIRE(instanced->GetInstancingShader() != nullptr);
        REQUIRE(plain->GetInstancingShader() == nullptr);

        // A 3x3 grid: nine identical submissions, well past the auto-instancing
        // minimum run length.
        auto submit = [&](const Ref<Material>& mat, const SceneDrawContext& c)
        {
            c.DrawMesh(ground, glm::mat4(1.0f), groundMat);
            for (int z = 0; z < 3; ++z)
                for (int x = 0; x < 3; ++x)
                    c.DrawMesh(box, glm::translate(glm::mat4(1.0f),
                                                   { -2.0f + x * 2.0f, 0.5f, -2.0f + z * 2.0f }), mat);
        };

        PerspectiveCamera cam = StandardCamera({ 5.5f, 4.5f, 7.5f }, { 0.0f, 0.4f, 0.0f });

        SceneRenderer renderer;
        renderer.Init(kW, kH, 1024);
        REQUIRE(renderer.IsInitialized());

        Image withInstancing, withoutInstancing;
        uint32_t batchesOn = 0, batchesOff = 0;

        {
            SceneRenderDesc d = BaseDesc(cam);
            d.DrawOpaque = [&](const SceneDrawContext& c) { submit(instanced, c); };
            Renderer3D::ResetStats();
            REQUIRE(RenderToImage(renderer, d, withInstancing));
            batchesOn = Renderer3D::GetStats().AutoInstanceBatches;
        }
        {
            SceneRenderDesc d = BaseDesc(cam);
            d.DrawOpaque = [&](const SceneDrawContext& c) { submit(plain, c); };
            Renderer3D::ResetStats();
            REQUIRE(RenderToImage(renderer, d, withoutInstancing));
            batchesOff = Renderer3D::GetStats().AutoInstanceBatches;
        }

        // The two runs really did take different paths...
        CHECK(batchesOn >= 1u);
        CHECK(batchesOff == 0u);

        // ... and produced the same frame. Same GPU, same run, so the comparison
        // is tolerance-based only to absorb the different shader's rounding.
        const Diff d = Compare(withInstancing, withoutInstancing);
        CHECK_FALSE(d.SizeMismatch);
        CHECK_MESSAGE(d.Passes(), "auto-instanced and per-mesh draws differ: ",
                      d.DifferingPixels, " / ", d.TotalPixels, " pixels, max delta ",
                      d.MaxChannelDelta);

        CHECK(CheckGolden("instancing", withInstancing));
        renderer.Shutdown();
    }
}

#endif // !COSMIC_2D_ONLY
