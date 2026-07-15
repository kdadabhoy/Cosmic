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

// ---------------------------------------------------------------------------
// U4 — flipbook sprite animation frame math (pure)
// ---------------------------------------------------------------------------

TEST_CASE("U4: SelectFrame wraps when looping and clamps when one-shot")
{
    // 4 frames @ 8 fps -> a frame lasts 0.125 s.
    CHECK(SpriteAnimationComponent::SelectFrame(0.0f,   8.0f, 4, true) == 0);
    CHECK(SpriteAnimationComponent::SelectFrame(0.20f,  8.0f, 4, true) == 1);
    CHECK(SpriteAnimationComponent::SelectFrame(0.30f,  8.0f, 4, true) == 2);
    CHECK(SpriteAnimationComponent::SelectFrame(0.50f,  8.0f, 4, true) == 0);   // wrap

    // One-shot clamps to the last frame.
    CHECK(SpriteAnimationComponent::SelectFrame(0.50f,  8.0f, 4, false) == 3);
    CHECK(SpriteAnimationComponent::SelectFrame(10.0f,  8.0f, 4, false) == 3);

    // Degenerate inputs -> frame 0.
    CHECK(SpriteAnimationComponent::SelectFrame(1.0f, 0.0f, 4, true) == 0);
    CHECK(SpriteAnimationComponent::SelectFrame(1.0f, 8.0f, 1, true) == 0);
}

TEST_CASE("U4: FrameUV maps a cell to normalized UV (top-left origin)")
{
    // 64x32 sheet, 16x16 cells -> 4 columns x 2 rows.
    glm::vec4 uv = SpriteAnimationComponent::FrameUV(64, 32, 16, 16, /*row=*/1, /*frame=*/2);
    CHECK(uv.x == doctest::Approx(0.50f));   // u0 = 2*16/64
    CHECK(uv.z == doctest::Approx(0.75f));   // u1 = 3*16/64
    CHECK(uv.y == doctest::Approx(0.50f));   // v0 = 1*16/32
    CHECK(uv.w == doctest::Approx(1.00f));   // v1 = 2*16/32
}

TEST_CASE("U4: SpriteAnimation + SourceRect round-trip through the serializer")
{
    Scene scene;
    Entity e = scene.CreateEntity("Sprite");
    auto& anim = e.AddComponent<SpriteAnimationComponent>();
    anim.SheetPath = "project://textures/hero.png";
    anim.FrameW = 24; anim.FrameH = 24; anim.Frames = 6; anim.Row = 2; anim.FPS = 12.0f;
    auto& sr = e.AddComponent<SpriteRendererComponent>();
    sr.PixelsPerUnit = 32.0f; sr.ZOrder = 5;
    sr.SourceRect = { 0.1f, 0.2f, 0.3f, 0.4f };

    const std::string text = SceneSerializer::SaveToString(scene);

    Scene loaded;
    REQUIRE(SceneSerializer::LoadFromString(loaded, text));

    int found = 0;
    for (auto h : loaded.GetRegistry().view<SpriteAnimationComponent>())
    {
        const auto& a = loaded.GetRegistry().get<SpriteAnimationComponent>(h);
        CHECK(a.SheetPath == "project://textures/hero.png");
        CHECK(a.Frames == 6);
        CHECK(a.Row == 2);
        CHECK(a.FPS == doctest::Approx(12.0f));
        const auto& s = loaded.GetRegistry().get<SpriteRendererComponent>(h);
        CHECK(s.ZOrder == 5);
        CHECK(s.PixelsPerUnit == doctest::Approx(32.0f));
        CHECK(s.SourceRect.z == doctest::Approx(0.3f));
        ++found;
    }
    CHECK(found == 1);
}

TEST_CASE("X5: Light2DComponent defaults + serializer round-trip")
{
    // Defaults are the warm campfire light; Enabled defaults true (omitted while true).
    Light2DComponent def;
    CHECK(def.Enabled);
    CHECK(def.Radius    == doctest::Approx(4.0f));
    CHECK(def.Intensity == doctest::Approx(1.5f));
    CHECK(def.Falloff   == doctest::Approx(2.0f));

    Scene scene;
    Entity e = scene.CreateEntity("Light");
    auto& lc = e.AddComponent<Light2DComponent>();
    lc.Color = { 0.2f, 0.4f, 1.0f };
    lc.Radius = 7.5f; lc.Intensity = 3.0f; lc.Falloff = 1.25f;

    const std::string text = SceneSerializer::SaveToString(scene);

    Scene loaded;
    REQUIRE(SceneSerializer::LoadFromString(loaded, text));
    int found = 0;
    for (auto h : loaded.GetRegistry().view<Light2DComponent>())
    {
        const auto& l = loaded.GetRegistry().get<Light2DComponent>(h);
        CHECK(l.Color.b   == doctest::Approx(1.0f));
        CHECK(l.Radius    == doctest::Approx(7.5f));
        CHECK(l.Intensity == doctest::Approx(3.0f));
        CHECK(l.Falloff   == doctest::Approx(1.25f));
        CHECK(l.Enabled);
        ++found;
    }
    CHECK(found == 1);
}

TEST_CASE("X7: SceneRenderer::RenderToTexture is a safe no-op headless")
{
    // Uninitialized (no GL context) + null target ⇒ early return, no GL calls,
    // nothing allocated — the "headless-safe no-op without GL" acceptance.
    SceneRenderer sr;
    REQUIRE(!sr.IsInitialized());
    SceneRenderDesc desc;
    sr.RenderToTexture(desc, nullptr);
    sr.RenderToTexture(desc, Ref<FrameBuffer>{});
    CHECK(!sr.IsInitialized());
}
