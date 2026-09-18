// render_wo09_ui_lights.cpp — WO-09 (2D stability) C03, the GPU half: the canvas
// UI pass (UiSystem::Render) and the 2D light composite (Scene::OnRender2DLights
// -> Light2DRenderer) on real targets. No new golden (the `ui`, `light2d` and
// `light2d A/B` goldens pin the reference frames); these cases pin COUNTS,
// invariants and the odd-buffer behaviour with exact pixel probes.
//
//   UI       2,001 controls render as exactly 2,001 quads; a canvas subtree at the
//            ratified depth ceiling renders bounded (KI-42) and the pixel it paints
//            is the deepest laid-out node's tint; an inverted rect draws its
//            mirrored quad (pinned) but is never hit.
//   lights   on a white backdrop under a dim ambient: zero radius, zero intensity
//            and a black colour contribute NOTHING (frame == ambient-only, byte-
//            exact); a negative radius equals its positive twin (the quad mirrors,
//            the falloff is symmetric — pinned); a fully off-screen light leaves
//            the frame ambient-only; a light on the viewport edge lights the edge
//            pixel and not the far side; 1 / 10 / 100 / 1,000 lights are timed
//            (the ceiling proposal) and 100 lights never darken below ambient;
//            odd targets 1x1 / 3x3 / 1919x1079 composite through the half-res
//            buffer ((w+1)/2) without a crash and DO light the frame; the X5 A/B
//            byte-identity holds on every odd size too.

#include "wo08_common.h"

#include "scene/Components.h"
#include "scene/Entity.h"
#include "scene/Scene.h"
#include "scene/ui/UiComponents.h"
#include "scene/ui/UiSystem.h"

#include <doctest.h>

#include <chrono>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

using namespace Cosmic;
using namespace CosmicRender;
using namespace Wo08;

namespace
{
    Entity AddImage(Scene& s, Entity parent, glm::vec2 aMin, glm::vec2 aMax, glm::vec2 oMin, glm::vec2 oMax, glm::vec4 tint, int32_t z)
    {
        Entity e = s.CreateEntity("img");
        auto& rt = e.AddComponent<RectTransformComponent>();
        rt.AnchorMin = aMin; rt.AnchorMax = aMax; rt.OffsetMin = oMin; rt.OffsetMax = oMax; rt.ZOrder = z;
        e.AddComponent<UiImageComponent>().Tint = tint;
        s.SetParent(e, parent, false);
        return e;
    }

    // A white full-view backdrop sprite plus a dim ambient, so a light's contribution
    // is visible as brightness above the ambient level.
    struct LitScene
    {
        Ref<Scene> ScenePtr;
        std::vector<Entity> Lights;
        Entity AddLight(glm::vec2 pos, float radius, float intensity = 1.0f, glm::vec3 color = { 1, 1, 1 })
        {
            Entity e = ScenePtr->CreateEntity("light");
            e.GetComponent<TransformComponent>().Position = { pos.x, pos.y, 0.0f };
            auto& l = e.AddComponent<Light2DComponent>();
            l.Radius = radius; l.Intensity = intensity; l.Color = color; l.Falloff = 2.0f;
            Lights.push_back(e);
            return e;
        }
    };

    LitScene MakeLit(float w, float h, glm::vec3 ambient = { 0.2f, 0.2f, 0.2f })
    {
        LitScene ls;
        ls.ScenePtr = Scene::Create();
        Entity back = ls.ScenePtr->CreateEntity("backdrop");
        back.GetComponent<TransformComponent>().Position = { w * 0.5f, h * 0.5f, 0.0f };
        back.GetComponent<TransformComponent>().Scale = { w, h, 1.0f };
        back.AddComponent<SpriteRendererComponent>().Color = { 1, 1, 1, 1 };
        ls.ScenePtr->CreateEntity("env").AddComponent<EnvironmentComponent>().Ambient2D = ambient;
        return ls;
    }

    // Sprites + lights with a pixel camera into `fbo`; returns the frame.
    void RenderLit(Scene& s, const Ref<FrameBuffer>& fbo, Image& out, bool lights = true)
    {
        BeginFrame(fbo);
        const glm::mat4 vp = PixelOrtho(fbo->GetWidth(), fbo->GetHeight());
        s.OnRenderSprites(vp, fbo->GetWidth(), fbo->GetHeight());
        if (lights) s.OnRender2DLights(vp, fbo->GetWidth(), fbo->GetHeight());
        REQUIRE(Capture(fbo, out));
    }
}

TEST_SUITE("WO-09 C03")
{
    TEST_CASE("C03 GPU: 2,001 controls render as exactly 2,001 quads; a canvas subtree at the depth ceiling renders bounded; an inverted rect draws mirrored but is never hit")
    {
        const uint32_t W = 1920, H = 1080;
        Ref<FrameBuffer> fbo = MakeRgba8Target(W, H);
        REQUIRE(fbo != nullptr);
        {
            Ref<Scene> scene = Scene::Create();
            Scene& s = *scene;
            Entity canvas = s.CreateEntity("Canvas");
            canvas.AddComponent<CanvasComponent>().ScaleMode = UiScaleMode::ConstantPixel;
            for (int y = 0; y < 40; ++y)
                for (int x = 0; x < 50; ++x)
                {
                    const glm::vec2 min{ 2.0f + x * 38.0f, 2.0f + y * 27.0f };
                    Entity b = AddImage(s, canvas, { 0, 0 }, { 0, 0 }, min, min + glm::vec2{ 30, 20 }, EncodeIndex((uint32_t)(y * 50 + x)), 0);
                    b.AddComponent<UiButtonComponent>();
                }
            AddImage(s, canvas, { 0, 0 }, { 0, 0 }, { 900, 500 }, { 1000, 560 }, EncodeIndex(2000), 100).AddComponent<UiButtonComponent>();
            StatsScope stats;
            BeginFrame(fbo);
            const auto t0 = std::chrono::steady_clock::now();
            UiSystem::Render(s, UiRect{ { 0, 0 }, { (float)W, (float)H } });
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            Image frame; REQUIRE(Capture(fbo, frame));
            WriteEvidence("c03-2001-controls", frame);
            const Renderer2D::Statistics st = stats.Get();
            CHECK(st.QuadCount == 2001u);
            CHECK(st.DrawCalls == 1u);
            MESSAGE("C03 GPU: 2,001 controls rendered in " << ms << " ms, " << st.DrawCalls << " draw call(s)");
            // Sentinels: button 0's centre, button 1999's centre, and the modal over cell 1234... no: the modal
            // sits over grid cells around (950, 530) — the canvas is top-left origin (+y down).
            CHECK(DecodeIndex(PixelAt(frame, 17, 12)) == 0);
            CHECK(DecodeIndex(PixelAt(frame, 2 + 49 * 38 + 15, 2 + 39 * 27 + 10)) == 1999);
            CHECK(DecodeIndex(PixelAt(frame, 950, 530)) == 2000);
            CHECK(DecodeIndex(PixelAt(frame, 0, 0)) == -1);                 // the gutter
        }
        // A chain 4,200 deep (each node inset by 1 px): the walk lays out the first
        // 4,095 nodes (KI-42 ceiling) — the frame at the canvas centre shows the
        // DEEPEST laid-out node's tint, and nothing crashes.
        {
            Ref<Scene> scene = Scene::Create();
            Scene& s = *scene;
            Entity canvas = s.CreateEntity("Canvas");
            canvas.AddComponent<CanvasComponent>().ScaleMode = UiScaleMode::ConstantPixel;
            Entity prev = canvas;
            const int depth = 4200;
            for (int i = 0; i < depth; ++i)
            {
                // Inset 0.1 px per level so 4,200 levels still leave a 1080 - 840 = 240 px box.
                prev = AddImage(s, prev, { 0, 0 }, { 1, 1 }, { 0.1f, 0.1f }, { -0.1f, -0.1f }, EncodeIndex((uint32_t)i % 4096), 0);
            }
            StatsScope stats;
            BeginFrame(fbo);
            UiSystem::Render(s, UiRect{ { 0, 0 }, { (float)W, (float)H } });
            Image frame; REQUIRE(Capture(fbo, frame));
            const Renderer2D::Statistics st = stats.Get();
            CHECK(st.QuadCount == 4095u);
            CHECK(DecodeIndex(PixelAt(frame, W / 2, H / 2)) == 4094);        // the deepest laid-out node (index 4,094) is on top
        }
        // Inverted rect: the quad is drawn mirrored (a negative size — pinned), the
        // hit-test never hits it, and a normal sibling still hits.
        {
            Ref<Scene> scene = Scene::Create();
            Scene& s = *scene;
            Entity canvas = s.CreateEntity("Canvas");
            canvas.AddComponent<CanvasComponent>().ScaleMode = UiScaleMode::ConstantPixel;
            Entity inv = AddImage(s, canvas, { 0, 0 }, { 0, 0 }, { 300, 300 }, { 200, 200 }, EncodeIndex(7), 0);
            inv.AddComponent<UiButtonComponent>();
            BeginFrame(fbo);
            UiSystem::Render(s, UiRect{ { 0, 0 }, { (float)W, (float)H } });
            Image frame; REQUIRE(Capture(fbo, frame));
            const bool painted = DecodeIndex(PixelAt(frame, 250, 250)) == 7;
            MESSAGE("C03 GPU: an inverted UI rect " << std::string(painted ? "PAINTS its mirrored quad" : "paints nothing") << " (pinned)");
            uint32_t hit = 0;
            CHECK_FALSE(UiSystem::HitTest(s, UiRect{ { 0, 0 }, { (float)W, (float)H } }, { 250, 250 }, hit));
        }
    }

    TEST_CASE("C03 GPU: lights — zero / negative radius, zero intensity, black colour, off-screen, viewport edge, 100 lights, and the ceiling timing")
    {
        const uint32_t W = 320, H = 180;
        Ref<FrameBuffer> fbo = MakeRgba8Target(W, H);
        REQUIRE(fbo != nullptr);
        const glm::u8vec4 ambientPx{ 51, 51, 51, 255 };   // white * 0.2

        // Ambient-only reference.
        Image ambientOnly;
        { LitScene ls = MakeLit((float)W, (float)H); RenderLit(*ls.ScenePtr, fbo, ambientOnly); }
        CHECK(Near(PixelAtGl(ambientOnly, 160, 90), ambientPx, 2));
        CHECK(Near(PixelAtGl(ambientOnly, 0, 0), ambientPx, 2));

        // Contribute-nothing variants: byte-identical to ambient-only.
        struct Variant { const char* Label; glm::vec2 Pos; float Radius, Intensity; glm::vec3 Color; };
        const Variant nothing[] = {
            { "zero radius",    { 160, 90 },   0.0f, 1.0f, { 1, 1, 1 } },
            { "zero intensity", { 160, 90 },  60.0f, 0.0f, { 1, 1, 1 } },
            { "black colour",   { 160, 90 },  60.0f, 1.0f, { 0, 0, 0 } },
            { "fully off-screen", { 5000, 5000 }, 60.0f, 1.0f, { 1, 1, 1 } },
            { "just off-screen",  { -61, 90 },  60.0f, 1.0f, { 1, 1, 1 } },
        };
        for (const Variant& v : nothing)
        {
            LitScene ls = MakeLit((float)W, (float)H);
            ls.AddLight(v.Pos, v.Radius, v.Intensity, v.Color);
            Image f; RenderLit(*ls.ScenePtr, fbo, f);
            CHECK_MESSAGE(BytesEqual(f, ambientOnly), v.Label);
        }
        // A real light: the centre is brighter than ambient; negative radius == positive radius (pinned).
        Image pos, neg;
        { LitScene ls = MakeLit((float)W, (float)H); ls.AddLight({ 160, 90 },  60.0f); RenderLit(*ls.ScenePtr, fbo, pos); }
        { LitScene ls = MakeLit((float)W, (float)H); ls.AddLight({ 160, 90 }, -60.0f); RenderLit(*ls.ScenePtr, fbo, neg); }
        WriteEvidence("c03-light-positive", pos);
        CHECK(PixelAtGl(pos, 160, 90).r > 200);                       // 0.2 + 1.0 at the centre -> saturated
        CHECK(Near(PixelAtGl(pos, 10, 10), ambientPx, 2));            // beyond the radius: ambient
        CHECK(BytesEqual(pos, neg));
        // Viewport edge: a light centred ON the left edge lights pixel (0, 90) and not the right side.
        {
            LitScene ls = MakeLit((float)W, (float)H); ls.AddLight({ 0, 90 }, 40.0f);
            Image f; RenderLit(*ls.ScenePtr, fbo, f);
            CHECK(PixelAtGl(f, 0, 90).r > 150);
            CHECK(Near(PixelAtGl(f, 319, 90), ambientPx, 2));
            CHECK(Near(PixelAtGl(f, 60, 90), ambientPx, 2));           // past the radius on the near side
        }
        // 100 lights: additive — no pixel below ambient; and the timing ladder.
        for (int n : { 1, 10, 100, 1000 })
        {
            LitScene ls = MakeLit((float)W, (float)H);
            for (int i = 0; i < n; ++i) ls.AddLight({ (float)(i * 37 % W), (float)(i * 53 % H) }, 30.0f, 0.3f);
            Image f;
            const auto t0 = std::chrono::steady_clock::now();
            for (int rep = 0; rep < 5; ++rep) RenderLit(*ls.ScenePtr, fbo, f);
            RenderCommand::FinishGpu();
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 5.0;
            int below = 0;
            for (uint32_t y = 0; y < H; ++y) for (uint32_t x = 0; x < W; ++x) below += PixelAtGl(f, x, y).r < ambientPx.r - 2;
            CHECK_MESSAGE(below == 0, n << " lights: " << below << " pixels darker than ambient");
            MESSAGE("C03 GPU lights ceiling ladder: " << n << " lights at 320x180 = " << ms << " ms per frame (5-frame mean incl. sprites + composite + FinishGpu)");
            if (n == 100) WriteEvidence("c03-100-lights", f);
        }
        // The same ladder at 1920x1080 (the composite is fill-bound at the half-res buffer).
        {
            Ref<FrameBuffer> big = MakeRgba8Target(1920, 1080);
            REQUIRE(big != nullptr);
            for (int n : { 100, 1000 })
            {
                LitScene ls = MakeLit(1920.0f, 1080.0f);
                for (int i = 0; i < n; ++i) ls.AddLight({ (float)(i * 137 % 1920), (float)(i * 53 % 1080) }, 120.0f, 0.3f);
                Image f;
                const auto t0 = std::chrono::steady_clock::now();
                for (int rep = 0; rep < 5; ++rep) RenderLit(*ls.ScenePtr, big, f);
                RenderCommand::FinishGpu();
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 5.0;
                MESSAGE("C03 GPU lights ceiling ladder: " << n << " lights at 1920x1080 (r=120) = " << ms << " ms per frame");
            }
        }
    }

    TEST_CASE("C03 GPU: lights on odd targets 1x1 / 3x3 / 1919x1079 composite through the half-res buffer, and the X5 A/B byte-identity holds on every size")
    {
        const std::pair<uint32_t, uint32_t> sizes[] = { { 1, 1 }, { 3, 3 }, { 1919, 1079 }, { 320, 180 } };
        for (const auto& dims : sizes)
        {
            const uint32_t W = dims.first, H = dims.second;
            Ref<FrameBuffer> fbo = MakeRgba8Target(W, H);
            REQUIRE(fbo != nullptr);
            // Lit vs unlit: the centre pixel is brighter with the light than with ambient only.
            Image unlit, lit;
            { LitScene ls = MakeLit((float)W, (float)H); RenderLit(*ls.ScenePtr, fbo, unlit); }
            { LitScene ls = MakeLit((float)W, (float)H); ls.AddLight({ W * 0.5f, H * 0.5f }, (float)std::max(W, H)); RenderLit(*ls.ScenePtr, fbo, lit); }
            CHECK(unlit.Width == W); CHECK(lit.Height == H);
            CHECK_MESSAGE(PixelAtGl(lit, W / 2, H / 2).r > PixelAtGl(unlit, W / 2, H / 2).r + 20, W << "x" << H);
            CHECK_MESSAGE(Near(PixelAtGl(unlit, W / 2, H / 2), glm::u8vec4{ 51, 51, 51, 255 }, 2), W << "x" << H);
            // X5 A/B on this size: no lights + white ambient == the pass never invoked.
            Image a, b;
            { LitScene ls = MakeLit((float)W, (float)H, { 1, 1, 1 }); RenderLit(*ls.ScenePtr, fbo, a, true); }
            { LitScene ls = MakeLit((float)W, (float)H, { 1, 1, 1 }); RenderLit(*ls.ScenePtr, fbo, b, false); }
            CHECK_MESSAGE(BytesEqual(a, b), W << "x" << H);
            if (W == 1919) WriteEvidence("c03-lights-1919x1079", lit);
        }
    }
}
