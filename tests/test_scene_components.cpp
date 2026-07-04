// test_scene_components.cpp — CameraComponent + EnvironmentComponent (E4).
// Headless: projection math + defaults-equality + serialization. No GL.
//
// Acceptance (plan doc 11 E4): projection matrices match glm; EnvironmentComponent
// defaults equal the SceneRenderer defaults (so a scene without one renders
// unchanged); the components round-trip through the serializer.

#include <doctest.h>

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/SceneSerializer.h"
#include "renderer/SceneRenderer.h"   // SceneRendererSettings / SceneRenderDesc defaults

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace Cosmic;

static bool Mat4Near(const glm::mat4& a, const glm::mat4& b, float eps = 1e-5f)
{
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (std::abs(a[c][r] - b[c][r]) > eps)
                return false;
    return true;
}

TEST_CASE("E4: CameraComponent projection matches glm perspective/ortho")
{
    const float aspect = 16.0f / 9.0f;

    CameraComponent persp;
    persp.ProjectionType = CameraComponent::Projection::Perspective;
    persp.FovDeg = 60.0f; persp.Near = 0.1f; persp.Far = 1000.0f;
    CHECK(Mat4Near(persp.GetProjection(aspect),
                   glm::perspective(glm::radians(60.0f), aspect, 0.1f, 1000.0f)));

    CameraComponent ortho;
    ortho.ProjectionType = CameraComponent::Projection::Orthographic;
    ortho.OrthoSize = 10.0f; ortho.Near = 0.1f; ortho.Far = 1000.0f;
    const float w = 10.0f * aspect, h = 10.0f;
    CHECK(Mat4Near(ortho.GetProjection(aspect),
                   glm::ortho(-w, w, -h, h, 0.1f, 1000.0f)));

    CHECK(persp.Primary == true);   // Play mode picks the first Primary camera
}

TEST_CASE("E4: EnvironmentComponent defaults equal the SceneRenderer defaults")
{
    EnvironmentComponent env;
    SceneRendererSettings s;   // engine defaults
    SceneRenderDesc       d;   // for the default Exposure

    // Sky / IBL toggles.
    CHECK(env.Skybox == s.Skybox);
    CHECK(env.IBL    == s.IBL);
    CHECK(env.Exposure == doctest::Approx(d.Exposure));

    // Fog block.
    CHECK(env.Fog == s.Fog);
    CHECK(env.FogColor == s.FogColor);
    CHECK(env.FogDensity       == doctest::Approx(s.FogDensity));
    CHECK(env.FogHeightFalloff == doctest::Approx(s.FogHeightFalloff));
    CHECK(env.FogBaseHeight    == doctest::Approx(s.FogBaseHeight));

    // Post block.
    CHECK(env.Bloom == s.Bloom);
    CHECK(env.BloomThreshold == doctest::Approx(s.BloomThreshold));
    CHECK(env.BloomIntensity == doctest::Approx(s.BloomIntensity));
    CHECK(env.SSAO == s.SSAO);
    CHECK(env.SsaoRadius == doctest::Approx(s.SsaoRadius));
    CHECK(env.FXAA == s.FXAA);
    CHECK(env.LensFlare == s.LensFlare);
    CHECK(env.LensFlareIntensity == doctest::Approx(s.LensFlareIntensity));
}

TEST_CASE("E4: Camera + Environment components round-trip through the serializer")
{
    Scene scene;
    Entity cam = scene.CreateEntity("Camera");
    auto& cc = cam.AddComponent<CameraComponent>();
    cc.ProjectionType = CameraComponent::Projection::Orthographic;
    cc.FovDeg = 42.0f; cc.OrthoSize = 7.5f; cc.Primary = false;

    Entity envE = scene.CreateEntity("Environment");
    auto& env = envE.AddComponent<EnvironmentComponent>();
    env.Sky = EnvironmentComponent::SkyMode::HDRI;
    env.HdriPath = "project://textures/sky.hdr";
    env.Fog = true; env.FogDensity = 0.05f;
    env.Bloom = true; env.BloomIntensity = 1.2f;
    env.TimeOfDay = 17.5f;

    const UUID camID = cam.GetComponent<IDComponent>().ID;
    const UUID envID = envE.GetComponent<IDComponent>().ID;

    const std::string save1 = SceneSerializer::SaveToString(scene);

    Scene scene2;
    REQUIRE(SceneSerializer::LoadFromString(scene2, save1));

    Entity cam2 = scene2.FindByUUID(camID);
    Entity env2 = scene2.FindByUUID(envID);
    REQUIRE(cam2);
    REQUIRE(env2);

    const auto& cc2 = cam2.GetComponent<CameraComponent>();
    CHECK(cc2.ProjectionType == CameraComponent::Projection::Orthographic);
    CHECK(cc2.FovDeg == doctest::Approx(42.0f));
    CHECK(cc2.OrthoSize == doctest::Approx(7.5f));
    CHECK(cc2.Primary == false);

    const auto& env2c = env2.GetComponent<EnvironmentComponent>();
    CHECK(env2c.Sky == EnvironmentComponent::SkyMode::HDRI);
    CHECK(env2c.HdriPath == "project://textures/sky.hdr");
    CHECK(env2c.Fog == true);
    CHECK(env2c.FogDensity == doctest::Approx(0.05f));
    CHECK(env2c.Bloom == true);
    CHECK(env2c.BloomIntensity == doctest::Approx(1.2f));
    CHECK(env2c.TimeOfDay == doctest::Approx(17.5f));

    CHECK(save1 == SceneSerializer::SaveToString(scene2));
}
