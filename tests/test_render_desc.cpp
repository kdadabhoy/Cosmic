// test_render_desc.cpp — the SceneRenderDesc baseline (Phase 29 W2 / §9.3).
//
// W5 moves Scene::BuildRenderDesc out of Scene.cpp into Scene3D.cpp. A member it
// forgets to fill in the move is INVISIBLE: no compile error, no crash — the
// frame just quietly loses its water, or its emitters, or renders at the wrong
// exposure until someone notices by eye. This suite captures what
// BuildRenderDesc produces from a fixture scene, field by field, so the move has
// an objective pass/fail.
//
// Headless (no GL). The fixture is built from the pieces that survive without a
// context: lights are pure ECS; Terrain::Create / Water::Create are procedural
// (test_worldsystems.cpp proves both build headless). ParticleEmitter is the one
// exception — its Update() lazily creates shaders — so the emitter fixture uses
// a NULL Emitter, which pins the other half of the contract: BuildRenderDesc
// must SKIP an unbuilt emitter, not push a null into the list.

#include <doctest.h>

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/WorldSystemRecipes.h"
#include "renderer/SceneRenderer.h"
#include "camera/PerspectiveCamera.h"
#include "terrain/Terrain.h"
#include "water/Water.h"

#include <glm/glm.hpp>
#include <cmath>
#include <string>
#include <vector>

using namespace Cosmic;

namespace
{
    bool NearAbs(float a, float b, float tol = 1e-5f) { return std::fabs(a - b) <= tol; }

    // A cheap procedural terrain — the smallest legal resolution keeps the build
    // fast while still producing a real asset.
    Ref<Terrain> MakeTerrain()
    {
        TerrainComponent tc;
        tc.WorldSize  = 256.0f;
        tc.Resolution = 65;
        tc.Seed       = 7u;
        return Terrain::Create(BuildTerrainSpec(tc));
    }

    Ref<Water> MakeWater(const glm::vec2& center, float surfaceHeight)
    {
        WaterComponent wc;
        wc.Center         = center;
        wc.Extent         = { 64.0f, 64.0f };
        wc.SurfaceHeight  = surfaceHeight;
        wc.GridResolution = 17;
        return Water::Create(BuildWaterSpec(wc));
    }

    // The fixture: a sun, two point lights (one disabled, one under an inactive
    // ancestor), a built terrain, three water bodies at different distances, and
    // two emitter components that were never built.
    struct Fixture
    {
        Ref<Scene>   ScenePtr;
        Ref<Terrain> TerrainAsset;
        Ref<Water>   Near, Mid, Far;
    };

    Fixture MakeFixture()
    {
        Fixture f;
        f.ScenePtr = Scene::Create();
        Scene& s = *f.ScenePtr;

        // --- lights ----------------------------------------------------------
        {
            Entity sun = s.CreateEntity("Sun");
            auto& dl = sun.AddComponent<DirectionalLightComponent>();
            dl.Direction = glm::normalize(glm::vec3{ -0.3f, -1.0f, -0.2f });
            dl.Color     = { 1.0f, 0.95f, 0.85f };
            dl.Intensity = 3.25f;

            Entity lamp = s.CreateEntity("Lamp");
            lamp.GetComponent<TransformComponent>().Position = { 4.0f, 2.0f, -1.0f };
            auto& pl = lamp.AddComponent<PointLightComponent>();
            pl.Color = { 0.2f, 0.4f, 1.0f }; pl.Intensity = 7.5f; pl.Radius = 12.0f;

            // T12 — a disabled point light must not reach the desc.
            Entity off = s.CreateEntity("LampOff");
            off.AddComponent<PointLightComponent>().Enabled = false;

            // T13 — a light under an inactive ancestor must not either.
            Entity parent = s.CreateEntity("HiddenRoom");
            parent.GetComponent<TagComponent>().Active = false;
            Entity hidden = s.CreateEntity("HiddenLamp");
            hidden.AddComponent<PointLightComponent>();
            s.SetParent(hidden, parent);
        }

        // --- terrain ----------------------------------------------------------
        {
            f.TerrainAsset = MakeTerrain();
            REQUIRE(f.TerrainAsset != nullptr);
            Entity e = s.CreateEntity("Terrain");
            // UseRecipe stays FALSE: a code-set asset, so SyncWorldSystems is a
            // no-op and nothing tries to build (or touch GL) during the call.
            e.AddComponent<TerrainComponent>().TerrainAsset = f.TerrainAsset;
        }

        // --- water: pushed far -> near, so ordering is observable --------------
        {
            f.Far = MakeWater({ 300.0f, 0.0f }, 1.0f);
            f.Mid = MakeWater({ 60.0f, 0.0f },  2.0f);
            f.Near= MakeWater({ 5.0f, 0.0f },   3.0f);
            REQUIRE(f.Far != nullptr);
            REQUIRE(f.Mid != nullptr);
            REQUIRE(f.Near != nullptr);

            Entity a = s.CreateEntity("WaterFar");
            auto& wa = a.AddComponent<WaterComponent>();
            wa.WaterAsset = f.Far;  wa.Center = { 300.0f, 0.0f }; wa.SurfaceHeight = 1.0f;

            Entity b = s.CreateEntity("WaterMid");
            auto& wb = b.AddComponent<WaterComponent>();
            wb.WaterAsset = f.Mid;  wb.Center = { 60.0f, 0.0f };  wb.SurfaceHeight = 2.0f;

            Entity c = s.CreateEntity("WaterNear");
            auto& wc = c.AddComponent<WaterComponent>();
            wc.WaterAsset = f.Near; wc.Center = { 5.0f, 0.0f };   wc.SurfaceHeight = 3.0f;
        }

        // --- emitters that were never built ------------------------------------
        {
            Entity e = s.CreateEntity("Smoke");
            e.AddComponent<ParticleEmitterComponent>();     // Emitter stays null
            Entity g = s.CreateEntity("Sparks");
            g.AddComponent<ParticleEmitterComponent>();
        }

        return f;
    }

    PerspectiveCamera MakeCamera()
    {
        PerspectiveCamera cam(60.0f, 16.0f / 9.0f, 0.1f, 500.0f);
        cam.SetPosition({ 0.0f, 3.0f, 0.0f });
        return cam;
    }
}

TEST_SUITE("W5 baseline: Scene::BuildRenderDesc")
{
    TEST_CASE("camera, clock and callbacks")
    {
        Fixture f = MakeFixture();
        PerspectiveCamera cam = MakeCamera();

        SceneRenderDesc desc;
        f.ScenePtr->BuildRenderDesc(cam, 0.25f, desc);

        // Camera: SetCamera fills View / Projection / CameraPosition from the
        // camera, and nothing else may rewrite them.
        CHECK(desc.View       == cam.GetViewMatrix());
        CHECK(desc.Projection == cam.GetProjectionMatrix());
        CHECK(NearAbs(desc.CameraPosition.x, 0.0f));
        CHECK(NearAbs(desc.CameraPosition.y, 3.0f));
        CHECK(NearAbs(desc.CameraPosition.z, 0.0f));

        // Clock: DeltaTime is this frame's dt; TimeSeconds is the scene's own
        // accumulating world clock (NOT a copy of dt).
        CHECK(NearAbs(desc.DeltaTime, 0.25f));
        CHECK(NearAbs(desc.TimeSeconds, 0.25f));

        SceneRenderDesc second;
        f.ScenePtr->BuildRenderDesc(cam, 0.25f, second);
        CHECK(NearAbs(second.DeltaTime, 0.25f));
        CHECK(NearAbs(second.TimeSeconds, 0.5f));    // the clock advanced

        // Callbacks: DrawOpaque is the routed submit hook; the other two belong
        // to the CALLER and must be left alone.
        CHECK(static_cast<bool>(desc.DrawOpaque));
        CHECK_FALSE(static_cast<bool>(desc.DrawTransparent));
        CHECK_FALSE(static_cast<bool>(desc.DrawOverlay2D));

        // EcsScene stays NULL on purpose — leaving it set would double-draw
        // against DrawOpaque and re-draw the terrain in the opaque pass.
        CHECK(desc.EcsScene == nullptr);
    }

    TEST_CASE("lights are gathered with the T12 / T13 gates applied")
    {
        Fixture f = MakeFixture();
        PerspectiveCamera cam = MakeCamera();

        SceneRenderDesc desc;
        f.ScenePtr->BuildRenderDesc(cam, 0.016f, desc);

        // The first enabled + active directional light becomes the sun.
        CHECK(NearAbs(desc.Lights.SunIntensity, 3.25f));
        CHECK(NearAbs(desc.Lights.SunColor.g, 0.95f));
        CHECK(NearAbs(desc.Lights.SunColor.b, 0.85f));
        CHECK(desc.Lights.SunDirection.y < 0.0f);

        // Exactly ONE point light survives: the disabled one and the one under an
        // inactive ancestor are both dropped.
        REQUIRE(desc.Lights.Points.size() == 1u);
        const auto& p = desc.Lights.Points[0];
        CHECK(NearAbs(p.Position.x, 4.0f));
        CHECK(NearAbs(p.Position.y, 2.0f));
        CHECK(NearAbs(p.Position.z, -1.0f));
        CHECK(NearAbs(p.Intensity, 7.5f));
        CHECK(NearAbs(p.Radius, 12.0f));
        CHECK(NearAbs(p.Color.b, 1.0f));
    }

    TEST_CASE("terrain and water: the primary reflection is the nearest body")
    {
        Fixture f = MakeFixture();
        PerspectiveCamera cam = MakeCamera();

        SceneRenderDesc desc;
        f.ScenePtr->BuildRenderDesc(cam, 0.016f, desc);

        // The first BUILT terrain drives the desc.
        CHECK(desc.TerrainSystem == f.TerrainAsset.get());

        // Every built water body is submitted exactly once. The submit ORDER is
        // the registry's, which is an entt implementation detail — what the
        // contract fixes is the membership and the reflection index.
        REQUIRE(desc.WaterBodies.size() == 3u);
        int seenNear = 0, seenMid = 0, seenFar = 0;
        for (Water* w : desc.WaterBodies)
        {
            if (w == f.Near.get()) ++seenNear;
            if (w == f.Mid.get())  ++seenMid;
            if (w == f.Far.get())  ++seenFar;
        }
        CHECK(seenNear == 1);
        CHECK(seenMid  == 1);
        CHECK(seenFar  == 1);

        // The index of the body nearest the camera gets the real planar
        // reflection. The camera sits at (0, 3, 0); "WaterNear" is at x = 5.
        REQUIRE(desc.PrimaryReflectionWater >= 0);
        REQUIRE(desc.PrimaryReflectionWater < (int)desc.WaterBodies.size());
        CHECK(desc.WaterBodies[(size_t)desc.PrimaryReflectionWater] == f.Near.get());
    }

    TEST_CASE("unbuilt emitters are skipped, never pushed as nulls")
    {
        Fixture f = MakeFixture();
        PerspectiveCamera cam = MakeCamera();

        SceneRenderDesc desc;
        f.ScenePtr->BuildRenderDesc(cam, 0.016f, desc);

        CHECK(desc.Emitters.empty());
        for (ParticleEmitter* e : desc.Emitters)
            CHECK(e != nullptr);

        // Nothing else in the optional-content set is invented either.
        CHECK(desc.Ribbons.empty());
        CHECK(desc.DistortionEmitters.empty());
        CHECK(desc.DetailedSky == nullptr);
        CHECK(desc.Coverage == nullptr);
        CHECK(desc.SelectedEntities == nullptr);
    }

    TEST_CASE("an empty scene yields the documented empty desc, not garbage")
    {
        Scene empty;
        PerspectiveCamera cam = MakeCamera();

        SceneRenderDesc desc;
        empty.BuildRenderDesc(cam, 0.016f, desc);

        CHECK(desc.TerrainSystem == nullptr);
        CHECK(desc.WaterBodies.empty());
        CHECK(desc.PrimaryReflectionWater == -1);   // the "IBL-only for all" sentinel
        CHECK(desc.Emitters.empty());
        CHECK(desc.Lights.Points.empty());
        CHECK(static_cast<bool>(desc.DrawOpaque));  // still installed
        CHECK(desc.EcsScene == nullptr);
    }

    TEST_CASE("the settings block is left at its defaults for the caller to apply")
    {
        // BuildRenderDesc gathers CONTENT; environment/exposure policy is applied
        // by the caller through SceneRenderer::ApplyEnvironment. If the move ever
        // starts writing Settings here, that contract silently changes.
        Fixture f = MakeFixture();
        Entity env = f.ScenePtr->CreateEntity("Environment");
        auto& ec = env.AddComponent<EnvironmentComponent>();
        ec.Exposure = 4.0f; ec.Bloom = true; ec.FXAA = false; ec.Gamma = 1.8f;

        PerspectiveCamera cam = MakeCamera();
        SceneRenderDesc desc;
        f.ScenePtr->BuildRenderDesc(cam, 0.016f, desc);

        const SceneRendererSettings defaults;
        CHECK(NearAbs(desc.Exposure, 1.0f));
        CHECK(desc.Settings.Bloom == defaults.Bloom);
        CHECK(desc.Settings.FXAA  == defaults.FXAA);
        CHECK(NearAbs(desc.Settings.Gamma, defaults.Gamma));
        CHECK(NearAbs(desc.Settings.AmbientIntensity, defaults.AmbientIntensity));
        CHECK(desc.Settings.OutlineEnabled == defaults.OutlineEnabled);
        CHECK(desc.Settings.Wireframe == defaults.Wireframe);

        // The component IS reachable — the caller just has to ask for it.
        CHECK(f.ScenePtr->FindEnvironment() != nullptr);
    }

    TEST_CASE("two builds of the same scene produce the same desc")
    {
        // A cheap stability net over the whole gather: only TimeSeconds may
        // differ between two identical calls.
        Fixture f = MakeFixture();
        PerspectiveCamera cam = MakeCamera();

        SceneRenderDesc a, b;
        f.ScenePtr->BuildRenderDesc(cam, 0.0f, a);
        f.ScenePtr->BuildRenderDesc(cam, 0.0f, b);

        CHECK(a.View == b.View);
        CHECK(a.Projection == b.Projection);
        CHECK(a.TerrainSystem == b.TerrainSystem);
        CHECK(a.PrimaryReflectionWater == b.PrimaryReflectionWater);
        REQUIRE(a.WaterBodies.size() == b.WaterBodies.size());
        for (size_t i = 0; i < a.WaterBodies.size(); ++i)
            CHECK(a.WaterBodies[i] == b.WaterBodies[i]);
        REQUIRE(a.Lights.Points.size() == b.Lights.Points.size());
        for (size_t i = 0; i < a.Lights.Points.size(); ++i)
        {
            CHECK(a.Lights.Points[i].Position == b.Lights.Points[i].Position);
            CHECK(a.Lights.Points[i].Intensity == b.Lights.Points[i].Intensity);
        }
        CHECK(a.Lights.SunDirection == b.Lights.SunDirection);
        CHECK(NearAbs(a.TimeSeconds, b.TimeSeconds));   // dt was 0
    }
}
