// render_wo08_camera.cpp — WO-08 R04 (GPU half): the 2D camera on a real target.
//
// The headless half (tests/test_wo08_camera.cpp) pins the pure math; this half
// proves the same model holds once a projection reaches the rasterizer: a
// marker drawn at a known world point lands on the predicted pixel under a
// viewport OFFSET inside a larger target, under 100/125/150/200 % DPI target
// sizes, on the awkward 641x359 target, and across a viewport that keeps
// changing size (1x1 -> 641x359 -> 320x180 -> 3x3 -> 1920x1080 -> 1x1) with a
// Camera2DController supplying the matrices. The oracle is the independent
// pinhole model from the headless half (WorldToImagePx), never the renderer.

#include "wo08_common.h"

#include "camera/Camera2DController.h"

using namespace Wo08;

namespace
{
    const glm::vec4 kMarker{ 1.0f, 0.2f, 0.9f, 1.0f };
    const glm::u8vec4 kMarkerU8 = ToU8(kMarker);

    // Draw a marker quad (`sizePx` target pixels wide) centred on `world`.
    void DrawMarker(uint32_t w, uint32_t h, float halfH, const glm::vec2& world, float sizePx)
    {
        const float unitsPerPx = (2.0f * halfH) / (float)h;
        (void)w;
        Renderer2D::DrawQuad(world, { sizePx * unitsPerPx, sizePx * unitsPerPx }, kMarker);
    }

    // The image pixel the model predicts for `world`.
    glm::uvec2 Predict(uint32_t w, uint32_t h, const glm::vec2& center, float halfH, const glm::vec2& world)
    {
        const glm::vec2 p = WorldToImagePx(w, h, center, halfH, world);
        return { (uint32_t)std::floor(p.x), (uint32_t)std::floor(p.y) };
    }
}

TEST_SUITE("WO-08 R04")
{
    TEST_CASE("R04 viewport offset — a pass viewport inside a larger target puts the world where the model says, and nowhere else")
    {
        const uint32_t tw = 400, th = 300;      // the target
        const uint32_t vw = 320, vh = 180;      // the pass viewport
        const uint32_t vx = 40, vy = 60;        // its bottom-left offset in the target (GL)
        Ref<FrameBuffer> fbo = MakeRgba8Target(tw, th);
        REQUIRE(fbo != nullptr);

        BeginFrame(fbo);
        Renderer2D::PushRenderPass(Ortho2DFor(vw, vh, { 0.0f, 0.0f }, 5.0f), { (float)vx, (float)vy, (float)vw, (float)vh });
        DrawMarker(vw, vh, 5.0f, { 0.0f, 0.0f }, 3.0f);          // viewport centre
        DrawMarker(vw, vh, 5.0f, { -8.0f, 4.0f }, 3.0f);         // near the viewport's top-left
        DrawMarker(vw, vh, 5.0f, { 8.0f, -4.0f }, 3.0f);         // near its bottom-right
        Renderer2D::DrawQuad(glm::vec2{ 0.0f, 0.0f }, { 400.0f, 400.0f }, { 0.2f, 0.4f, 0.2f, 1.0f });   // a huge quad: clipped to the viewport
        Renderer2D::PopRenderPass();

        Image img;
        REQUIRE(Capture(fbo, img));
        WriteEvidence("r04-viewport-offset", img);

        auto predictInTarget = [&](const glm::vec2& world)
        {
            const glm::vec2 p = WorldToImagePx(vw, vh, { 0.0f, 0.0f }, 5.0f, world);   // viewport-local, top-left
            // Target image y: the viewport's top edge sits th - (vy + vh) rows down.
            return glm::uvec2{ (uint32_t)std::floor(p.x) + vx, (uint32_t)std::floor(p.y) + (th - vy - vh) };
        };
        // The huge quad fills exactly the viewport rectangle...
        const uint32_t top = th - vy - vh, left = vx;
        CHECK(Near(PixelAt(img, left, top), ToU8({ 0.2f, 0.4f, 0.2f, 1.0f })));
        CHECK(Near(PixelAt(img, left + vw - 1, top + vh - 1), ToU8({ 0.2f, 0.4f, 0.2f, 1.0f })));
        // ...and nothing outside it.
        CHECK(Near(PixelAt(img, left - 1, top), kClearU8));
        CHECK(Near(PixelAt(img, left, top - 1), kClearU8));
        CHECK(Near(PixelAt(img, left + vw, top + vh - 1), kClearU8));
        CHECK(Near(PixelAt(img, left + vw - 1, top + vh), kClearU8));
        CHECK(Near(PixelAt(img, 0, 0), kClearU8));
        CHECK(Near(PixelAt(img, tw - 1, th - 1), kClearU8));
        // The markers sit on their predicted pixels (drawn before the big quad, so
        // they are under it... the big quad is drawn last in submission order and
        // batched geometry rasterizes in submission order — so re-check with the
        // markers on top in a second frame).
        BeginFrame(fbo);
        Renderer2D::PushRenderPass(Ortho2DFor(vw, vh, { 0.0f, 0.0f }, 5.0f), { (float)vx, (float)vy, (float)vw, (float)vh });
        DrawMarker(vw, vh, 5.0f, { 0.0f, 0.0f }, 3.0f);
        DrawMarker(vw, vh, 5.0f, { -8.0f, 4.0f }, 3.0f);
        DrawMarker(vw, vh, 5.0f, { 8.0f, -4.0f }, 3.0f);
        Renderer2D::PopRenderPass();
        REQUIRE(Capture(fbo, img));
        for (const glm::vec2 world : { glm::vec2{ 0.0f, 0.0f }, glm::vec2{ -8.0f, 4.0f }, glm::vec2{ 8.0f, -4.0f } })
        {
            const glm::uvec2 p = predictInTarget(world);
            CHECK_MESSAGE(Near(PixelAt(img, p.x, p.y), kMarkerU8), "marker at world (", world.x, ",", world.y, ") missing at target pixel (", p.x, ",", p.y, "): ", Describe(PixelAt(img, p.x, p.y)));
        }
    }

    TEST_CASE("R04 DPI — the same world scene at 100/125/150/200 % target sizes scales pixel positions and sizes exactly")
    {
        struct Dpi { uint32_t W, H; float Scale; };
        const Dpi dpis[] = { { 320, 180, 1.0f }, { 400, 225, 1.25f }, { 480, 270, 1.5f }, { 640, 360, 2.0f } };
        const glm::vec2 world{ 2.0f, 1.0f };
        const float halfH = 5.0f;
        for (const Dpi& d : dpis)
        {
            Ref<FrameBuffer> fbo = MakeRgba8Target(d.W, d.H);
            REQUIRE(fbo != nullptr);
            BeginFrame(fbo);
            Renderer2D::PushRenderPass(Ortho2DFor(d.W, d.H, { 0.0f, 0.0f }, halfH), { 0.0f, 0.0f, (float)d.W, (float)d.H });
            Renderer2D::DrawQuad(world, { 0.5f, 0.5f }, kMarker);   // 0.5 world units == 9 px at 100 %
            Renderer2D::PopRenderPass();
            Image img;
            REQUIRE(Capture(fbo, img));

            const glm::uvec2 p = Predict(d.W, d.H, { 0.0f, 0.0f }, halfH, world);
            CHECK_MESSAGE(Near(PixelAt(img, p.x, p.y), kMarkerU8), d.W, "x", d.H, ": marker not at predicted (", p.x, ",", p.y, ")");
            // Position scales with the DPI factor relative to the 100 % frame.
            const glm::vec2 base = WorldToImagePx(320, 180, { 0.0f, 0.0f }, halfH, world);
            const glm::vec2 here = WorldToImagePx(d.W, d.H, { 0.0f, 0.0f }, halfH, world);
            CHECK(std::abs(here.x - base.x * d.Scale) < 1e-3f);
            CHECK(std::abs(here.y - base.y * d.Scale) < 1e-3f);
            // Size scales too: the marker covers (9 * scale)^2 pixels, +-1 px per edge.
            const int expectedSide = (int)std::lround(9.0f * d.Scale);
            const int count = CountColor(img, 0, 0, d.W, d.H, kMarkerU8, 2);
            CHECK_MESSAGE((count >= (expectedSide - 1) * (expectedSide - 1) && count <= (expectedSide + 1) * (expectedSide + 1)),
                          d.W, "x", d.H, ": marker covers ", count, " px, expected ~", expectedSide * expectedSide);
        }
    }

    TEST_CASE("R04 641x359 — an odd, non-multiple-of-8 target: centre and all four corners land on the predicted pixels")
    {
        // 641x359 is deliberately awkward: odd on both axes (the centre column /
        // row are single pixels, not a boundary), not a multiple of 8 (no tile or
        // row-pitch alignment helps), and its aspect (1.7855) is not 16:9, so any
        // rounding that only works for "nice" sizes shows up here.
        const uint32_t w = 641, h = 359;
        const float halfH = 5.0f;
        const float aspect = (float)w / (float)h;
        Ref<FrameBuffer> fbo = MakeRgba8Target(w, h);
        REQUIRE(fbo != nullptr);
        REQUIRE(fbo->GetWidth() == w);
        REQUIRE(fbo->GetHeight() == h);

        Camera2DController cam(aspect);
        cam.SetViewportRect({ 0.0f, 0.0f }, { (float)w, (float)h });
        cam.SetFocus({ 0.0f, 0.0f });
        cam.SetZoom(halfH);

        const float unitsPerPx = 2.0f * halfH / (float)h;
        BeginFrame(fbo);
        Renderer2D::PushRenderPass(cam.GetCamera().GetViewProjectionMatrix(), { 0.0f, 0.0f, (float)w, (float)h });
        Renderer2D::DrawQuad(glm::vec2{ 0.0f, 0.0f }, { unitsPerPx, unitsPerPx }, kMarker);   // exactly the centre pixel
        // Corner markers: 3x3 px quads centred on the corner PIXEL centres.
        const glm::vec2 cornersWorld[4] = {
            { -halfH * aspect + 0.5f * unitsPerPx,  halfH - 0.5f * unitsPerPx },   // top-left pixel (0, 0)
            {  halfH * aspect - 0.5f * unitsPerPx,  halfH - 0.5f * unitsPerPx },   // top-right (640, 0)
            { -halfH * aspect + 0.5f * unitsPerPx, -halfH + 0.5f * unitsPerPx },   // bottom-left (0, 358)
            {  halfH * aspect - 0.5f * unitsPerPx, -halfH + 0.5f * unitsPerPx },   // bottom-right (640, 358)
        };
        for (const glm::vec2& c : cornersWorld)
            Renderer2D::DrawQuad(c, { 3.0f * unitsPerPx, 3.0f * unitsPerPx }, kMarker);
        Renderer2D::PopRenderPass();

        Image img;
        REQUIRE(Capture(fbo, img));
        WriteEvidence("r04-641x359", img);
        CHECK(img.Width == w);
        CHECK(img.Height == h);
        CHECK(Near(PixelAt(img, 320, 179), kMarkerU8));    // the single centre pixel
        CHECK(Near(PixelAt(img, 319, 179), kClearU8));     // its neighbours are untouched
        CHECK(Near(PixelAt(img, 321, 179), kClearU8));
        CHECK(Near(PixelAt(img, 320, 178), kClearU8));
        CHECK(Near(PixelAt(img, 320, 180), kClearU8));
        CHECK(Near(PixelAt(img, 0, 0), kMarkerU8));
        CHECK(Near(PixelAt(img, 640, 0), kMarkerU8));
        CHECK(Near(PixelAt(img, 0, 358), kMarkerU8));
        CHECK(Near(PixelAt(img, 640, 358), kMarkerU8));
        CHECK(Near(PixelAt(img, 3, 3), kClearU8));
        CHECK(Near(PixelAt(img, 637, 355), kClearU8));
        // Model vs controller: the predicted centre pixel is (320, 179).
        const glm::uvec2 p = Predict(w, h, { 0.0f, 0.0f }, halfH, { 0.0f, 0.0f });
        CHECK(p.x == 320);
        CHECK(p.y == 179);
    }

    TEST_CASE("R04 changing viewport — resize through 1x1 / 641x359 / 320x180 / 3x3 / 1920x1080 / 1x1 with the controller driving the matrices")
    {
        Ref<FrameBuffer> fbo = MakeRgba8Target(8, 8);
        REQUIRE(fbo != nullptr);
        Camera2DController cam(1.0f);
        cam.SetFocus({ 1.5f, -0.5f });
        cam.SetZoom(3.0f);

        const glm::uvec2 sizes[] = { { 1, 1 }, { 641, 359 }, { 320, 180 }, { 3, 3 }, { 1920, 1080 }, { 1, 1 }, { 2, 1080 }, { 640, 360 } };
        for (const glm::uvec2& s : sizes)
        {
            fbo->Resize(s.x, s.y);
            REQUIRE(fbo->GetWidth() == s.x);
            REQUIRE(fbo->GetHeight() == s.y);
            cam.SetViewportRect({ 0.0f, 0.0f }, { (float)s.x, (float)s.y });
            CHECK(cam.GetAspect() == doctest::Approx((float)s.x / (float)s.y));

            const float unitsPerPx = 2.0f * cam.GetZoom() / (float)s.y;
            BeginFrame(fbo);
            Renderer2D::PushRenderPass(cam.GetCamera().GetViewProjectionMatrix(), { 0.0f, 0.0f, (float)s.x, (float)s.y });
            // A 1.5-pixel marker at the focus: it must paint the centre pixel of
            // the viewport whatever the size (1.5 rather than 1.0 so an even size,
            // whose centre is a pixel BOUNDARY, still covers a pixel centre).
            Renderer2D::DrawQuad(cam.GetFocus(), { unitsPerPx * 1.5f, unitsPerPx * 1.5f }, kMarker);
            Renderer2D::PopRenderPass();

            Image img;
            REQUIRE(Capture(fbo, img));
            REQUIRE(img.Width == s.x);
            REQUIRE(img.Height == s.y);
            // Centre pixel: for even sizes the focus sits on a pixel BOUNDARY; the
            // one-pixel quad then straddles it and the rasterizer picks the pixel
            // whose centre is inside the quad's half-open extent — (s/2, s/2) in
            // GL coords for the x axis and rows accordingly. Accept either pixel
            // adjacent to the boundary, but require exactly the model's pixel for
            // odd sizes.
            const glm::vec2 c = WorldToImagePx(s.x, s.y, cam.GetFocus(), cam.GetZoom(), cam.GetFocus());
            bool hit = false;
            for (int dx = -1; dx <= 0 && !hit; ++dx)
                for (int dy = -1; dy <= 0 && !hit; ++dy)
                {
                    const int px = (int)std::floor(c.x) + dx, py = (int)std::floor(c.y) + dy;
                    if (px < 0 || py < 0 || px >= (int)s.x || py >= (int)s.y) continue;
                    if (Near(PixelAt(img, (uint32_t)px, (uint32_t)py), kMarkerU8)) hit = true;
                }
            CHECK_MESSAGE(hit, s.x, "x", s.y, ": no marker pixel around the predicted centre (", c.x, ",", c.y, ")");
            if ((s.x & 1) && (s.y & 1))
                CHECK_MESSAGE(Near(PixelAt(img, (uint32_t)std::floor(c.x), (uint32_t)std::floor(c.y)), kMarkerU8),
                              s.x, "x", s.y, ": odd size must hit exactly the model's centre pixel");
            // The marker is 1.5 pixels: never more than 4 marker pixels (2x2 when straddling).
            CHECK(CountColor(img, 0, 0, s.x, s.y, kMarkerU8, 2) <= 4);
            CHECK(CountColor(img, 0, 0, s.x, s.y, kMarkerU8, 2) >= 1);
        }
        // Zero-size resizes are refused by the FrameBuffer and ignored by the controller.
        fbo->Resize(0, 0);
        CHECK(fbo->GetWidth() == 640);
        CHECK(fbo->GetHeight() == 360);
        cam.SetViewportRect({ 0.0f, 0.0f }, { 0.0f, 0.0f });
        CHECK(cam.GetAspect() == doctest::Approx(640.0f / 360.0f));
    }
}
