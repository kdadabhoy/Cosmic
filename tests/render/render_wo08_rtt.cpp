// render_wo08_rtt.cpp — WO-08 R05: render-to-texture, nested render passes and
// state restoration.
//
//   * Two distinct RTT targets (64x48 and 48x64) each get four corner markers
//     and an orientation bar, are captured (proving the FBO read-back is
//     top-left origin), then shown through UiImage::RuntimeTexture with the
//     documented FboTexture adapter inside the main frame's UI pass.
//   * The main frame nests render passes three deep (world camera -> pixel
//     camera on an OFFSET viewport -> a third camera), pops back, and keeps
//     drawing in the outer pass: camera and viewport restoration are proven
//     by where later geometry lands, FBO restoration by the bound handle after
//     SceneRenderer::RenderToTexture, blend and depth restoration by what a
//     half-alpha quad and a farther quad do after the UI pass.
//   * No source->target feedback: after everything that SAMPLED target A has
//     rendered, A's bytes are identical to its first capture.
//   * 200 create / render / resize / resize / destroy cycles (with the adapter
//     in a UiImage) leave the ENGINE's live GPU object counts exactly where they
//     started (GpuObjectStats — engine bookkeeping, not driver name reuse).

#include "wo08_common.h"

#include "camera/PerspectiveCamera.h"
#include "graphics/GpuObjectStats.h"
#include "renderer/SceneRenderer.h"
#include "scene/Components.h"
#include "scene/Entity.h"
#include "scene/Scene.h"
#include "scene/ui/UiComponents.h"
#include "scene/ui/UiSystem.h"

using namespace Wo08;

namespace
{
    constexpr uint32_t kW = kGoldenWidth, kH = kGoldenHeight;

    struct Corners { glm::u8vec4 TL, TR, BL, BR; glm::vec4 Clear; };

    // Render four 8x8 corner markers + a 2-px orientation bar along the top edge
    // into `fbo` through its own pixel-space render pass.
    void RenderMarkers(const Ref<FrameBuffer>& fbo, const Corners& c)
    {
        const float w = (float)fbo->GetWidth(), h = (float)fbo->GetHeight();
        BeginFrame(fbo, c.Clear);
        Renderer2D::PushRenderPass(PixelOrtho(fbo->GetWidth(), fbo->GetHeight()), { 0.0f, 0.0f, w, h });
        auto q = [&](float cx, float cy, float sx, float sy, const glm::u8vec4& col)
        {
            Renderer2D::DrawQuad(glm::vec2{ cx, cy }, { sx, sy }, glm::vec4(col) / 255.0f);
        };
        q(4.0f, h - 4.0f, 8.0f, 8.0f, c.TL);          // top-left (GL y up)
        q(w - 4.0f, h - 4.0f, 8.0f, 8.0f, c.TR);
        q(4.0f, 4.0f, 8.0f, 8.0f, c.BL);
        q(w - 4.0f, 4.0f, 8.0f, 8.0f, c.BR);
        q(w * 0.5f, h - 1.0f, w - 20.0f, 2.0f, c.TL);   // a bar along the TOP edge in the TL colour
        Renderer2D::PopRenderPass();
    }

    void CheckCorners(const Image& img, const Corners& c, const char* what)
    {
        CHECK_MESSAGE(Near(PixelAt(img, 1, 1), c.TL), what, " top-left: ", Describe(PixelAt(img, 1, 1)));
        CHECK_MESSAGE(Near(PixelAt(img, img.Width - 2, 1), c.TR), what, " top-right: ", Describe(PixelAt(img, img.Width - 2, 1)));
        CHECK_MESSAGE(Near(PixelAt(img, 1, img.Height - 2), c.BL), what, " bottom-left: ", Describe(PixelAt(img, 1, img.Height - 2)));
        CHECK_MESSAGE(Near(PixelAt(img, img.Width - 2, img.Height - 2), c.BR), what, " bottom-right: ", Describe(PixelAt(img, img.Width - 2, img.Height - 2)));
        CHECK_MESSAGE(Near(PixelAt(img, img.Width / 2, 0), c.TL), what, " top bar");
        CHECK_MESSAGE(Near(PixelAt(img, img.Width / 2, img.Height - 1), ToU8(c.Clear)), what, " bottom edge is clear");
    }

    glm::u8vec4 Over(const glm::vec4& src, const glm::u8vec4& dst)
    {
        const glm::vec4 d{ dst.r / 255.0f, dst.g / 255.0f, dst.b / 255.0f, dst.a / 255.0f };
        return ToU8(src * src.a + d * (1.0f - src.a));
    }

    Entity AddImage(Scene& s, const char* name, Entity parent, glm::vec2 topLeftPx, glm::vec2 sizePx,
                    const Ref<Texture2D>& tex, glm::vec4 tint, int32_t z)
    {
        Entity e = s.CreateEntity(name);
        auto& rt = e.AddComponent<RectTransformComponent>();
        rt.AnchorMin = { 0.0f, 0.0f }; rt.AnchorMax = { 0.0f, 0.0f };
        rt.OffsetMin = topLeftPx; rt.OffsetMax = topLeftPx + sizePx;
        rt.ZOrder = z;
        auto& img = e.AddComponent<UiImageComponent>();
        img.Tint = tint;
        img.RuntimeTexture = tex;
        s.SetParent(e, parent);
        return e;
    }
}

TEST_SUITE("WO-08 R05")
{
    TEST_CASE("R05 two RTT targets through UiImage, nested passes, and camera/viewport/FBO/blend/depth restoration")
    {
        const Corners ca{ { 255, 0, 0, 255 }, { 0, 255, 0, 255 }, { 0, 0, 255, 255 }, { 255, 255, 0, 255 }, { 0.05f, 0.05f, 0.05f, 1.0f } };
        const Corners cb{ { 255, 0, 255, 255 }, { 0, 255, 255, 255 }, { 255, 255, 255, 255 }, { 40, 40, 40, 255 }, { 0.30f, 0.10f, 0.10f, 1.0f } };

        Ref<FrameBuffer> a = MakeRgba8Target(64, 48);
        Ref<FrameBuffer> b = MakeRgba8Target(48, 64);
        Ref<FrameBuffer> main = MakeTarget();
        REQUIRE((a && b && main));

        // 1) Render both RTT targets and prove the read-back orientation.
        RenderMarkers(a, ca);
        RenderMarkers(b, cb);
        Image imgA0, imgB0;
        REQUIRE(Capture(a, imgA0));
        REQUIRE(Capture(b, imgB0));
        CHECK(imgA0.Width == 64); CHECK(imgA0.Height == 48);
        CHECK(imgB0.Width == 48); CHECK(imgB0.Height == 64);
        CheckCorners(imgA0, ca, "RTT A capture");
        CheckCorners(imgB0, cb, "RTT B capture");
        WriteEvidence("r05-rtt-a", imgA0);
        WriteEvidence("r05-rtt-b", imgB0);

        // 2) A UI scene with the two live images (documented adapter) + a solid panel.
        Ref<Scene> scene = Scene::Create();
        Entity canvas = scene->CreateEntity("Canvas");
        auto& cc = canvas.AddComponent<CanvasComponent>();
        cc.ScaleMode = UiScaleMode::ConstantPixel;
        cc.ReferenceHeight = (float)kH;
        Ref<Texture2D> texA = CreateRef<FboTexture>(a);
        Ref<Texture2D> texB = CreateRef<FboTexture>(b);
        const glm::vec2 rectA{ 20.0f, 20.0f }, rectB{ 270.0f, 30.0f };
        AddImage(*scene, "imgA", canvas, rectA, { 64.0f, 48.0f }, texA, glm::vec4(1.0f), 1);
        AddImage(*scene, "imgB", canvas, rectB, { 48.0f, 64.0f }, texB, glm::vec4(1.0f), 1);
        AddImage(*scene, "panel", canvas, { 100.0f, 100.0f }, { 80.0f, 40.0f }, nullptr, { 0.9f, 0.9f, 0.2f, 1.0f }, 0);
        // A GL-native 4x4 texture whose TOP two rows are red and bottom two blue
        // (row 0 of SetData is GL's bottom row — the orientation a file texture has
        // after flip-on-load), shown through the plain path and the 9-slice path.
        std::vector<uint8_t> rows(4 * 4 * 4);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
            {
                uint8_t* px = &rows[((size_t)y * 4 + x) * 4];
                px[0] = y >= 2 ? 255 : 0; px[1] = 0; px[2] = y >= 2 ? 0 : 255; px[3] = 255;
            }
        Ref<Texture2D> redOverBlue = Texture2D::Create(4, 4);
        redOverBlue->SetData(rows.data(), (uint32_t)rows.size());
        const glm::vec2 rectPlain{ 200.0f, 150.0f }, rectNine{ 60.0f, 70.0f };
        AddImage(*scene, "plain", canvas, rectPlain, { 40.0f, 26.0f }, redOverBlue, glm::vec4(1.0f), 1);
        Entity nine = AddImage(*scene, "nine", canvas, rectNine, { 40.0f, 26.0f }, redOverBlue, glm::vec4(1.0f), 1);
        nine.GetComponent<UiImageComponent>().NineSlice = { 1.0f, 1.0f, 1.0f, 1.0f };

        // 3) The main frame: world pass -> UI pass (nested by UiSystem) + manual nesting.
        const glm::vec2 center{ 0.0f, 0.0f };
        const float halfH = 5.0f;
        const glm::vec4 qBefore{ 0.2f, 0.6f, 0.9f, 1.0f }, qAfter{ 0.9f, 0.5f, 0.2f, 1.0f }, qHalf{ 1.0f, 1.0f, 1.0f, 0.5f };
        const glm::vec4 qTop{ 0.1f, 0.9f, 0.3f, 1.0f }, qFar{ 0.9f, 0.1f, 0.3f, 1.0f }, qMini{ 0.7f, 0.2f, 0.9f, 1.0f }, qInner{ 0.2f, 0.9f, 0.9f, 1.0f };

        main->Bind();
        const uint32_t mainHandle = RenderCommand::GetBoundFramebuffer();
        REQUIRE(mainHandle != 0);

        StatsScope stats;
        BeginFrame(main);
        Renderer2D::PushRenderPass(Ortho2DFor(kW, kH, center, halfH), { 0.0f, 0.0f, (float)kW, (float)kH });
        Renderer2D::DrawQuad(glm::vec2{ -6.0f, -3.0f }, { 2.0f, 2.0f }, qBefore);                 // outer, before nesting

        // Manual nesting, two levels: a pixel camera on an OFFSET viewport, then a
        // third camera inside it. Each level fills its own viewport with a colour.
        Renderer2D::PushRenderPass(PixelOrtho(60, 40), { 200.0f, 100.0f, 60.0f, 40.0f });
        Renderer2D::DrawQuad(glm::vec2{ 30.0f, 20.0f }, { 60.0f, 40.0f }, qMini);
        Renderer2D::PushRenderPass(PixelOrtho(20, 10), { 220.0f, 110.0f, 20.0f, 10.0f });
        Renderer2D::DrawQuad(glm::vec2{ 10.0f, 5.0f }, { 20.0f, 10.0f }, qInner);
        Renderer2D::PopRenderPass();
        Renderer2D::DrawQuad(glm::vec2{ 5.0f, 5.0f }, { 4.0f, 4.0f }, qInner);                    // back at level 1: bottom-left of the mini viewport
        Renderer2D::PopRenderPass();

        // The UI pass (UiSystem pushes/pops its own screen-space pass and toggles depth/blend).
        UiSystem::Render(*scene, UiRect{ { 0.0f, 0.0f }, { (float)kW, (float)kH } });

        // Back in the outer pass: geometry must land where the OUTER camera says.
        Renderer2D::DrawQuad(glm::vec2{ 6.0f, -3.0f }, { 2.0f, 2.0f }, qAfter);
        Renderer2D::DrawQuad(glm::vec2{ -6.0f, -3.0f }, { 1.0f, 1.0f }, qHalf);                   // blend probe over qBefore
        Renderer2D::DrawQuad(glm::vec3{ 0.0f, -3.5f, 0.5f }, { 2.0f, 1.0f }, qTop);               // nearer first...
        Renderer2D::DrawQuad(glm::vec3{ 0.0f, -3.5f, -0.5f }, { 2.0f, 1.0f }, qFar);              // ...farther second: depth test must hide it
        Renderer2D::PopRenderPass();

        // 4) SceneRenderer::RenderToTexture into a THIRD target while main is bound:
        //    the bound framebuffer must come back.
        Ref<FrameBuffer> c = MakeRgba8Target(64, 48);
        REQUIRE(c != nullptr);
        {
            Ref<Scene> tiny = Scene::Create();
            Entity e = tiny->CreateEntity("sprite");
            e.GetComponent<TransformComponent>().Scale = { 4.0f, 3.0f, 1.0f };
            e.AddComponent<SpriteRendererComponent>().Color = { 0.2f, 0.9f, 0.4f, 1.0f };
            PerspectiveCamera cam(45.0f, 64.0f / 48.0f, 0.1f, 100.0f);
            cam.SetPosition({ 0.0f, 0.0f, 10.0f });
            SceneRenderDesc desc;
            tiny->BuildRenderDesc(cam, 0.0f, desc);
            desc.Projection = glm::ortho(-4.0f, 4.0f, -3.0f, 3.0f, -100.0f, 100.0f);
            desc.View = glm::mat4(1.0f);
            desc.Settings.ClearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
            desc.Settings.Skybox = false; desc.Settings.IBL = false; desc.Settings.Shadows = false; desc.Settings.FXAA = false;
            Scene* sp = tiny.get();
            desc.DrawTransparent = [sp](const SceneDrawContext& ctx) { sp->OnRenderSprites(ctx.ViewProjection, 64, 48); };
            SceneRenderer renderer;
            renderer.Init(64, 48, 256);
            REQUIRE(renderer.IsInitialized());
            main->Bind();
            REQUIRE(RenderCommand::GetBoundFramebuffer() == mainHandle);
            renderer.RenderToTexture(desc, c);
            CHECK_MESSAGE(RenderCommand::GetBoundFramebuffer() == mainHandle, "RenderToTexture did not re-bind the caller's framebuffer");
            renderer.Shutdown();
        }

        Image frame;
        REQUIRE(Capture(main, frame));
        WriteEvidence("r05-main", frame);

        // --- Orientation through the UiImage: A's top-left marker at the rect's top-left, etc.
        auto at = [&](const glm::vec2& rect, uint32_t dx, uint32_t dy) { return PixelAt(frame, (uint32_t)rect.x + dx, (uint32_t)rect.y + dy); };
        CHECK_MESSAGE(Near(at(rectA, 2, 2), ca.TL), "UiImage(A) top-left shows A's top-left marker: ", Describe(at(rectA, 2, 2)));
        CHECK_MESSAGE(Near(at(rectA, 61, 2), ca.TR), "UiImage(A) top-right: ", Describe(at(rectA, 61, 2)));
        CHECK_MESSAGE(Near(at(rectA, 2, 45), ca.BL), "UiImage(A) bottom-left: ", Describe(at(rectA, 2, 45)));
        CHECK_MESSAGE(Near(at(rectA, 61, 45), ca.BR), "UiImage(A) bottom-right: ", Describe(at(rectA, 61, 45)));
        CHECK_MESSAGE(Near(at(rectA, 32, 0), ca.TL), "UiImage(A) top bar is at the TOP of the rect: ", Describe(at(rectA, 32, 0)));
        CHECK_MESSAGE(Near(at(rectB, 2, 2), cb.TL), "UiImage(B) top-left: ", Describe(at(rectB, 2, 2)));
        CHECK_MESSAGE(Near(at(rectB, 45, 2), cb.TR), "UiImage(B) top-right: ", Describe(at(rectB, 45, 2)));
        CHECK_MESSAGE(Near(at(rectB, 2, 61), cb.BL), "UiImage(B) bottom-left: ", Describe(at(rectB, 2, 61)));
        CHECK_MESSAGE(Near(at(rectB, 45, 61), cb.BR), "UiImage(B) bottom-right: ", Describe(at(rectB, 45, 61)));
        CHECK(Near(PixelAt(frame, 140, 120), ToU8({ 0.9f, 0.9f, 0.2f, 1.0f })));   // the solid panel
        // GL-native texture: its top rows show at the rect top on BOTH image paths.
        CHECK_MESSAGE(Near(at(rectPlain, 20, 3), glm::u8vec4{ 255, 0, 0, 255 }), "plain path: rect top shows the texture's top rows: ", Describe(at(rectPlain, 20, 3)));
        CHECK_MESSAGE(Near(at(rectPlain, 20, 22), glm::u8vec4{ 0, 0, 255, 255 }), "plain path: rect bottom shows the texture's bottom rows: ", Describe(at(rectPlain, 20, 22)));
        CHECK_MESSAGE(Near(at(rectNine, 20, 3), glm::u8vec4{ 255, 0, 0, 255 }), "9-slice path: rect top shows the texture's top rows: ", Describe(at(rectNine, 20, 3)));
        CHECK_MESSAGE(Near(at(rectNine, 20, 22), glm::u8vec4{ 0, 0, 255, 255 }), "9-slice path: rect bottom shows the texture's bottom rows: ", Describe(at(rectNine, 20, 22)));

        // --- Camera + viewport restoration: outer geometry after the nesting.
        auto worldPx = [&](const glm::vec2& w) { const glm::vec2 p = WorldToImagePx(kW, kH, center, halfH, w); return glm::uvec2{ (uint32_t)p.x, (uint32_t)p.y }; };
        {
            const glm::uvec2 p = worldPx({ 6.0f, -3.0f });
            CHECK_MESSAGE(Near(PixelAt(frame, p.x, p.y), ToU8(qAfter)), "outer-pass quad after the nested passes landed at ", Describe(PixelAt(frame, p.x, p.y)));
            const glm::uvec2 p2 = worldPx({ 6.0f + 0.9f, -3.0f + 0.9f });
            CHECK(Near(PixelAt(frame, p2.x, p2.y), ToU8(qAfter)));
        }
        // The nested viewports: level 1 filled (200..259, GL y 100..139) except the
        // level-2 rectangle (220..239, GL y 110..119) and the level-1 corner probe.
        CHECK(Near(PixelAtGl(frame, 201, 138), ToU8(qMini)));
        CHECK(Near(PixelAtGl(frame, 258, 101), ToU8(qMini)));
        CHECK(Near(PixelAtGl(frame, 199, 120), kClearU8));      // just outside the mini viewport
        CHECK(Near(PixelAtGl(frame, 260, 120), kClearU8));
        CHECK(Near(PixelAtGl(frame, 230, 115), ToU8(qInner)));  // level 2 filled its viewport
        CHECK(Near(PixelAtGl(frame, 219, 115), ToU8(qMini)));   // and nothing beyond it
        CHECK(Near(PixelAtGl(frame, 240, 115), ToU8(qMini)));
        CHECK(Near(PixelAtGl(frame, 204, 104), ToU8(qInner)));  // level-1 probe after the level-2 pop: level-1 camera + viewport restored
        CHECK(Near(PixelAtGl(frame, 209, 104), ToU8(qMini)));

        // --- Blend restoration: qHalf over qBefore is alpha-over.
        {
            const glm::uvec2 p = worldPx({ -6.0f, -3.0f });
            CHECK_MESSAGE(Near(PixelAt(frame, p.x, p.y), Over(qHalf, ToU8(qBefore))), "alpha blend after the UI pass: ", Describe(PixelAt(frame, p.x, p.y)));
            const glm::uvec2 p2 = worldPx({ -6.0f - 0.8f, -3.0f });
            CHECK(Near(PixelAt(frame, p2.x, p2.y), ToU8(qBefore)));
        }
        // --- Depth restoration: the farther quad drawn second stays hidden.
        {
            const glm::uvec2 p = worldPx({ 0.0f, -3.5f });
            CHECK_MESSAGE(Near(PixelAt(frame, p.x, p.y), ToU8(qTop)), "depth test after the UI pass: ", Describe(PixelAt(frame, p.x, p.y)));
        }
        // Draw accounting: the UI pass flushed the outer batch and its own.
        CHECK(stats.Get().Flushes >= 4);

        // 5) No source->target feedback: A and B are byte-identical to their first captures.
        Image imgA1, imgB1, imgC;
        REQUIRE(Capture(a, imgA1));
        REQUIRE(Capture(b, imgB1));
        REQUIRE(Capture(c, imgC));
        CHECK_MESSAGE(BytesEqual(imgA0, imgA1), "RTT A was written while it was only a sampling source");
        CHECK_MESSAGE(BytesEqual(imgB0, imgB1), "RTT B was written while it was only a sampling source");
        // RenderToTexture did land in C (through the tonemap chain, so "green and
        // bright at the sprite, black at the cleared corner" rather than exact values).
        CHECK(PixelAt(imgC, 32, 24).g > 100);
        CHECK(PixelAt(imgC, 32, 24).g > PixelAt(imgC, 32, 24).r + 40);
        CHECK(Near(PixelAt(imgC, 1, 1), glm::u8vec4{ 0, 0, 0, 255 }, 8));

        // Re-rendering A with different markers and re-drawing the UI shows the NEW
        // content (the adapter reads the live attachment, no stale copy).
        const Corners ca2{ { 0, 255, 255, 255 }, { 255, 0, 255, 255 }, { 255, 255, 255, 255 }, { 0, 0, 0, 255 }, { 0.05f, 0.05f, 0.05f, 1.0f } };
        RenderMarkers(a, ca2);
        BeginFrame(main);
        UiSystem::Render(*scene, UiRect{ { 0.0f, 0.0f }, { (float)kW, (float)kH } });
        REQUIRE(Capture(main, frame));
        CHECK(Near(at(rectA, 2, 2), ca2.TL));
        CHECK(Near(at(rectA, 61, 45), ca2.BR));

        // Clear the runtime slots before the adapters die (the documented rule).
        for (auto ent : scene->View<UiImageComponent>())
            Entity{ ent, scene.get() }.GetComponent<UiImageComponent>().RuntimeTexture = nullptr;
    }

    TEST_CASE("R05 200x create / render / resize / resize / destroy leaves no engine-owned GPU object behind")
    {
        // Warm everything the loop touches (renderer buffers, shaders, the scene's
        // canvas) BEFORE taking the baseline, so only per-cycle growth is measured.
        Ref<Scene> scene = Scene::Create();
        Entity canvas = scene->CreateEntity("Canvas");
        canvas.AddComponent<CanvasComponent>().ScaleMode = UiScaleMode::ConstantPixel;
        Entity image = AddImage(*scene, "live", canvas, { 10.0f, 10.0f }, { 64.0f, 48.0f }, nullptr, glm::vec4(1.0f), 0);
        Ref<FrameBuffer> main = MakeTarget();
        REQUIRE(main != nullptr);

        const glm::uvec2 sizes[] = { { 64, 48 }, { 641, 359 }, { 1, 1 }, { 320, 180 }, { 3, 3 }, { 256, 256 }, { 1920, 1080 }, { 2, 2 } };
        auto cycle = [&](uint32_t i)
        {
            const glm::uvec2 s0 = sizes[i % 8], s1 = sizes[(i + 3) % 8], s2 = sizes[(i + 5) % 8];
            Ref<FrameBuffer> f = MakeRgba8Target(s0.x, s0.y);
            REQUIRE(f != nullptr);
            for (const glm::uvec2& s : { s0, s1, s2 })
            {
                if (s != s0) f->Resize(s.x, s.y);
                REQUIRE(f->GetWidth() == s.x);
                REQUIRE(f->GetHeight() == s.y);
                BeginFrame(f, { 0.1f, 0.2f, 0.3f, 1.0f });
                Renderer2D::PushRenderPass(PixelOrtho(s.x, s.y), { 0.0f, 0.0f, (float)s.x, (float)s.y });
                Renderer2D::DrawQuad(glm::vec2{ s.x * 0.5f, s.y * 0.5f }, { (float)s.x, (float)s.y }, { 0.9f, 0.4f, 0.1f, 1.0f });
                Renderer2D::PopRenderPass();

                // Show it through the UI each time (adapter + RuntimeTexture path).
                image.GetComponent<UiImageComponent>().RuntimeTexture = CreateRef<FboTexture>(f);
                BeginFrame(main);
                UiSystem::Render(*scene, UiRect{ { 0.0f, 0.0f }, { (float)kW, (float)kH } });
                image.GetComponent<UiImageComponent>().RuntimeTexture = nullptr;
            }
            Image img;
            REQUIRE(Capture(f, img));   // the last size read back correctly
            REQUIRE(img.Width == s2.x);
            REQUIRE(img.Height == s2.y);
            CHECK(Near(PixelAt(img, 0, 0), ToU8({ 0.9f, 0.4f, 0.1f, 1.0f })));
            f.reset();
        };

        cycle(0);   // warm-up
        cycle(1);
        const GpuObjectCounts base = GpuObjectStats::Live();
        REQUIRE(base.Framebuffers >= 1);   // at least `main` is alive

        for (uint32_t i = 0; i < 200; ++i)
            cycle(i);

        const GpuObjectCounts after = GpuObjectStats::Live();
        CHECK_MESSAGE(after.Framebuffers == base.Framebuffers, "framebuffers: ", base.Framebuffers, " -> ", after.Framebuffers);
        CHECK_MESSAGE(after.FramebufferAttachments == base.FramebufferAttachments, "attachments: ", base.FramebufferAttachments, " -> ", after.FramebufferAttachments);
        CHECK_MESSAGE(after.Textures == base.Textures, "textures: ", base.Textures, " -> ", after.Textures);
        CHECK_MESSAGE(after.Buffers == base.Buffers, "buffers: ", base.Buffers, " -> ", after.Buffers);
        CHECK_MESSAGE(after.VertexArrays == base.VertexArrays, "vertex arrays: ", base.VertexArrays, " -> ", after.VertexArrays);
        CHECK_MESSAGE(after.Shaders == base.Shaders, "shaders: ", base.Shaders, " -> ", after.Shaders);
        MESSAGE("R05 live engine GPU objects after 200 cycles: fbo=" << after.Framebuffers << " att=" << after.FramebufferAttachments
                << " tex=" << after.Textures << " buf=" << after.Buffers << " vao=" << after.VertexArrays << " prog=" << after.Shaders);

        // And a single target really does account for its objects: +1 fbo, +2 attachments.
        {
            Ref<FrameBuffer> probe = MakeRgba8Target(16, 16);
            const GpuObjectCounts with = GpuObjectStats::Live();
            CHECK(with.Framebuffers == after.Framebuffers + 1);
            CHECK(with.FramebufferAttachments == after.FramebufferAttachments + 2);
            probe->Resize(32, 32);
            CHECK(GpuObjectStats::Live().FramebufferAttachments == after.FramebufferAttachments + 2);   // resize replaces, never accumulates
            probe.reset();
            CHECK(GpuObjectStats::Live() == after);
        }
    }
}
