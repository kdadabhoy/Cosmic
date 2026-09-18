// test_wo08_camera.cpp — WO-08 R04 (headless half): the 2D camera's pan/zoom
// anchors, viewport offsets, DPI scales, the zoom range, the zero/negative/
// nonfinite input policy, the 641x359 target and a changing viewport.
//
// ORACLE. An INDEPENDENT world->screen model (the four-line pinhole the rig
// documents: centre pixel == focus, viewport height == 2*zoom world units, +y
// screen is -y world) is written here, never borrowed from the controller, and
// every round trip is measured in SCREEN PIXELS against the catalog bar:
//
//     round-trip error <= 0.5 px inside the declared local envelope
//
// DECLARED LOCAL ENVELOPE (the bar is float arithmetic, so it has to be
// declared, not assumed): |focus| <= 1000 * zoom on each axis and
// |world - focus| <= 4 * zoom * max(aspect, 1). Outside it (a 0.01-unit zoom
// parked 1,000 units from the origin) a float ulp is already several pixels;
// that is measured and reported below, not asserted.

#include <doctest.h>

#include "camera/Camera2DController.h"
#include "events/MouseEvent.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace Cosmic;

namespace
{
    const float kNaN = std::numeric_limits<float>::quiet_NaN();
    const float kInf = std::numeric_limits<float>::infinity();

    // The independent model.
    glm::vec2 WorldToScreen(const glm::vec2& world, const glm::vec2& vpPos, const glm::vec2& vpSize,
                            const glm::vec2& focus, float zoom)
    {
        const glm::vec2 center = vpPos + vpSize * 0.5f;
        const float pxPerUnit = vpSize.y / (2.0f * zoom);
        return { center.x + (world.x - focus.x) * pxPerUnit,
                 center.y - (world.y - focus.y) * pxPerUnit };
    }

    float PxPerUnit(const glm::vec2& vpSize, float zoom) { return vpSize.y / (2.0f * zoom); }

    bool Finite(const glm::vec2& v) { return std::isfinite(v.x) && std::isfinite(v.y); }
    bool Finite(const glm::mat4& m)
    {
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                if (!std::isfinite(m[c][r])) return false;
        return true;
    }

    // Project through the controller's OWN projection (the matrices the GPU gets)
    // to viewport pixels: NDC -> pixel with +y screen down.
    glm::vec2 ProjectToScreen(const Camera2DController& cam, const glm::vec2& vpPos, const glm::vec2& vpSize,
                              const glm::vec2& world)
    {
        const glm::vec4 clip = cam.GetCamera().GetViewProjectionMatrix() * glm::vec4(world, 0.0f, 1.0f);
        const glm::vec2 ndc{ clip.x / clip.w, clip.y / clip.w };
        return { vpPos.x + (ndc.x + 1.0f) * 0.5f * vpSize.x,
                 vpPos.y + (1.0f - ndc.y) * 0.5f * vpSize.y };
    }

    struct Viewport { glm::vec2 Pos, Size; const char* Name; };

    // 100 / 125 / 150 / 200 % DPI of a 1280x720 viewport, with and without an
    // on-screen offset, plus the awkward 641x359 target (odd, non-multiple-of-8,
    // so no pixel centre sits on the NDC axes and the aspect is not 16:9 exactly).
    std::vector<Viewport> Viewports()
    {
        return {
            { { 0.0f, 0.0f },      { 1280.0f, 720.0f },  "100% no offset" },
            { { 100.0f, 50.0f },   { 1280.0f, 720.0f },  "100% offset" },
            { { 0.0f, 0.0f },      { 1600.0f, 900.0f },  "125%" },
            { { 137.0f, 41.0f },   { 1600.0f, 900.0f },  "125% offset" },
            { { 0.0f, 0.0f },      { 1920.0f, 1080.0f }, "150%" },
            { { 33.0f, 1017.0f },  { 1920.0f, 1080.0f }, "150% offset (second monitor row)" },
            { { 0.0f, 0.0f },      { 2560.0f, 1440.0f }, "200%" },
            { { 2560.0f, 0.0f },   { 2560.0f, 1440.0f }, "200% offset (right monitor)" },
            { { 0.0f, 0.0f },      { 641.0f, 359.0f },   "641x359 (odd, non-multiple-of-8)" },
            { { 7.0f, 3.0f },      { 641.0f, 359.0f },   "641x359 offset" },
        };
    }

    const float kZooms[] = { 0.01f, 0.05f, 0.5f, 1.0f, 5.0f, 100.0f, 2500.0f, 10000.0f };
}

TEST_CASE("WO-08 R04: ScreenToWorld round trip <= 0.5 px across zoom range, DPI scales, viewport offsets and 641x359")
{
    float worstPx = 0.0f;
    int samples = 0;
    for (const Viewport& vp : Viewports())
    {
        const float aspect = vp.Size.x / vp.Size.y;
        for (float zoom : kZooms)
        {
            // Focus inside the declared envelope (|focus| <= 1000 * zoom).
            const glm::vec2 focusCandidates[] = {
                { 0.0f, 0.0f }, { 3.7f * zoom, -2.1f * zoom }, { 999.0f * zoom, -999.0f * zoom }, { -250.0f * zoom, 640.0f * zoom } };
            for (const glm::vec2& focus : focusCandidates)
            {
                const float reach = 4.0f * zoom * std::max(aspect, 1.0f);
                // World points on a coarse lattice inside the envelope...
                for (int ix = -4; ix <= 4; ++ix)
                    for (int iy = -4; iy <= 4; ++iy)
                    {
                        const glm::vec2 world = focus + glm::vec2(ix, iy) * (reach / 4.0f) * 0.99f;
                        const glm::vec2 screen = WorldToScreen(world, vp.Pos, vp.Size, focus, zoom);
                        const glm::vec2 back   = Camera2DController::ScreenToWorld(screen, vp.Pos, vp.Size, focus, zoom);
                        const float errPx = glm::length(back - world) * PxPerUnit(vp.Size, zoom);
                        worstPx = std::max(worstPx, errPx);
                        ++samples;
                        CHECK_MESSAGE(errPx <= 0.5f, vp.Name, " zoom=", zoom, " focus=(", focus.x, ",", focus.y, ") world=(", world.x, ",", world.y, ") err=", errPx, " px");
                    }
                // ...and screen pixel CENTRES across the whole viewport, back to screen.
                for (int sx = 0; sx <= 8; ++sx)
                    for (int sy = 0; sy <= 8; ++sy)
                    {
                        const glm::vec2 px{ vp.Pos.x + std::floor(vp.Size.x * sx / 8.0f) + 0.5f,
                                            vp.Pos.y + std::floor(vp.Size.y * sy / 8.0f) + 0.5f };
                        const glm::vec2 world = Camera2DController::ScreenToWorld(px, vp.Pos, vp.Size, focus, zoom);
                        const glm::vec2 back  = WorldToScreen(world, vp.Pos, vp.Size, focus, zoom);
                        const float errPx = glm::length(back - px);
                        worstPx = std::max(worstPx, errPx);
                        ++samples;
                        CHECK_MESSAGE(errPx <= 0.5f, vp.Name, " zoom=", zoom, " pixel=(", px.x, ",", px.y, ") err=", errPx, " px");
                    }
            }
        }
    }
    MESSAGE("R04 ScreenToWorld round trip: " << samples << " samples, worst " << worstPx << " px (bar 0.5)");
    CHECK(samples > 20000);
}

TEST_CASE("WO-08 R04: the controller's own projection agrees with the pinhole model to <= 0.5 px, and inverts")
{
    float worstPx = 0.0f;
    for (const Viewport& vp : Viewports())
    {
        Camera2DController cam(vp.Size.x / vp.Size.y);
        cam.SetViewportRect(vp.Pos, vp.Size);
        for (float zoom : kZooms)
        {
            cam.SetZoom(zoom);
            const glm::vec2 focus{ 12.5f * zoom, -7.25f * zoom };
            cam.SetFocus(focus);
            REQUIRE(Finite(cam.GetCamera().GetViewProjectionMatrix()));
            const float reach = 4.0f * zoom * std::max(vp.Size.x / vp.Size.y, 1.0f);
            for (int ix = -3; ix <= 3; ++ix)
                for (int iy = -3; iy <= 3; ++iy)
                {
                    const glm::vec2 world = focus + glm::vec2(ix, iy) * (reach / 3.0f) * 0.99f;
                    const glm::vec2 viaMatrix = ProjectToScreen(cam, vp.Pos, vp.Size, world);
                    const glm::vec2 viaModel  = WorldToScreen(world, vp.Pos, vp.Size, focus, zoom);
                    const float errPx = glm::length(viaMatrix - viaModel);
                    worstPx = std::max(worstPx, errPx);
                    CHECK_MESSAGE(errPx <= 0.5f, vp.Name, " zoom=", zoom, " projection vs model: ", errPx, " px");

                    // And the matrix inverts: unproject the projected point.
                    const glm::mat4 inv = glm::inverse(cam.GetCamera().GetViewProjectionMatrix());
                    const glm::vec4 clip = cam.GetCamera().GetViewProjectionMatrix() * glm::vec4(world, 0.0f, 1.0f);
                    const glm::vec4 w2 = inv * clip;
                    const float errPx2 = glm::length(glm::vec2(w2.x / w2.w, w2.y / w2.w) - world) * PxPerUnit(vp.Size, zoom);
                    CHECK_MESSAGE(errPx2 <= 0.5f, vp.Name, " zoom=", zoom, " matrix inverse round trip: ", errPx2, " px");
                }
        }
    }
    MESSAGE("R04 projection-vs-model worst " << worstPx << " px");
}

TEST_CASE("WO-08 R04: zoom-about-cursor keeps the anchor pixel fixed and PanBy keeps the world under the cursor (<= 0.5 px)")
{
    int asserted = 0, measuredOnly = 0;
    float worstOutside = 0.0f;
    for (const Viewport& vp : Viewports())
    {
        for (float before : kZooms)
            for (float after : kZooms)
            {
                const glm::vec2 focus{ -3.0f * before, 2.0f * before };
                // Anchor under an off-centre pixel, including the viewport corners.
                const glm::vec2 pixels[] = { vp.Pos + glm::vec2(0.5f, 0.5f), vp.Pos + vp.Size - glm::vec2(0.5f, 0.5f),
                                             vp.Pos + glm::vec2(vp.Size.x * 0.31f, vp.Size.y * 0.77f) };
                for (const glm::vec2& px : pixels)
                {
                    const glm::vec2 anchor = Camera2DController::ScreenToWorld(px, vp.Pos, vp.Size, focus, before);
                    const glm::vec2 f2 = Camera2DController::ZoomAboutPoint(focus, anchor, before, after);
                    const glm::vec2 px2 = WorldToScreen(anchor, vp.Pos, vp.Size, f2, after);
                    const float errPx = glm::length(px2 - px);
                    // The bar applies inside the declared envelope of the zoom being
                    // zoomed TO (|anchor|, |focus| <= 1000 * after): zooming 10000 -> 0.01
                    // about a corner 18,000 units away is a request for sub-ulp precision.
                    const float envelope = 1000.0f * after;
                    const bool inside = std::abs(anchor.x) <= envelope && std::abs(anchor.y) <= envelope &&
                                        std::abs(f2.x) <= envelope && std::abs(f2.y) <= envelope;
                    if (inside)
                    {
                        ++asserted;
                        CHECK_MESSAGE(errPx <= 0.5f, vp.Name, " zoom ", before, "->", after, " anchor drift ", errPx, " px");
                    }
                    else
                    {
                        ++measuredOnly;
                        worstOutside = std::max(worstOutside, errPx);
                    }
                }
            }

        // Pan: a drag of (dx, dy) pixels moves the world point under the cursor by exactly that.
        for (float zoom : kZooms)
        {
            const glm::vec2 focus{ 1.5f * zoom, -0.5f * zoom };
            const glm::vec2 cursor = vp.Pos + vp.Size * 0.5f + glm::vec2(17.0f, -23.0f);
            const glm::vec2 under = Camera2DController::ScreenToWorld(cursor, vp.Pos, vp.Size, focus, zoom);
            const glm::vec2 deltas[] = { { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 37.0f, 12.0f }, { -vp.Size.x * 0.4f, vp.Size.y * 0.4f } };
            for (const glm::vec2& d : deltas)
            {
                const glm::vec2 f2 = Camera2DController::PanBy(focus, d, zoom, vp.Size.y);
                const glm::vec2 px2 = WorldToScreen(under, vp.Pos, vp.Size, f2, zoom);
                CHECK_MESSAGE(glm::length(px2 - (cursor + d)) <= 0.5f, vp.Name, " zoom=", zoom, " pan drift");
            }
        }
    }
    MESSAGE("R04 zoom-about-cursor: " << asserted << " pairs asserted inside the envelope, "
            << measuredOnly << " outside it measured only (worst " << worstOutside << " px)");
    CHECK(asserted >= 1000);
}

TEST_CASE("WO-08 R04: default zoom range 0.01..10000 — clamps at both ends, zero and negative go to the minimum")
{
    Camera2DController cam(16.0f / 9.0f);
    CHECK(cam.GetZoom() == doctest::Approx(5.0f));
    cam.SetZoom(0.01f);    CHECK(cam.GetZoom() == doctest::Approx(0.01f));
    cam.SetZoom(10000.0f); CHECK(cam.GetZoom() == doctest::Approx(10000.0f));
    cam.SetZoom(0.005f);   CHECK(cam.GetZoom() == doctest::Approx(0.01f));
    cam.SetZoom(20000.0f); CHECK(cam.GetZoom() == doctest::Approx(10000.0f));
    cam.SetZoom(0.0f);     CHECK(cam.GetZoom() == doctest::Approx(0.01f));
    cam.SetZoom(-5.0f);    CHECK(cam.GetZoom() == doctest::Approx(0.01f));
    CHECK(Finite(cam.GetCamera().GetViewProjectionMatrix()));

    // At both ends the projection is finite and invertible, and a world point
    // at the visible edge lands on the viewport edge.
    for (float z : { 0.01f, 10000.0f })
    {
        cam.SetZoom(z);
        cam.SetFocus({ 0.0f, 0.0f });
        const glm::vec2 vpPos{ 0.0f, 0.0f }, vpSize{ 1920.0f, 1080.0f };
        cam.SetViewportRect(vpPos, vpSize);
        const glm::vec2 top = ProjectToScreen(cam, vpPos, vpSize, { 0.0f, z });
        CHECK(std::abs(top.y - 0.0f) <= 0.5f);
        const glm::vec2 right = ProjectToScreen(cam, vpPos, vpSize, { z * cam.GetAspect(), 0.0f });
        CHECK(std::abs(right.x - 1920.0f) <= 0.5f);
        glm::vec2 mn, mx;
        cam.VisibleRect(mn, mx);
        CHECK(mx.y - mn.y == doctest::Approx(2.0f * z));
    }
}

TEST_CASE("WO-08 R04: nonfinite input policy — NaN/inf zoom, focus, size, viewport, bounds and scroll never reach the projection")
{
    Camera2DController cam(16.0f / 9.0f);
    cam.SetViewportRect({ 10.0f, 20.0f }, { 1280.0f, 720.0f });
    cam.SetFocus({ 3.0f, 4.0f });
    cam.SetZoom(2.5f);
    const glm::mat4 good = cam.GetCamera().GetViewProjectionMatrix();
    REQUIRE(Finite(good));

    auto unchanged = [&](const char* whatC)
    {
        const std::string what(whatC);   // doctest prints a bare const char* as a pointer
        CHECK_MESSAGE(cam.GetZoom() == doctest::Approx(2.5f), what, ": zoom changed to ", cam.GetZoom());
        CHECK_MESSAGE(cam.GetFocus() == glm::vec2(3.0f, 4.0f), what, ": focus changed");
        CHECK_MESSAGE(cam.GetAspect() == doctest::Approx(1280.0f / 720.0f), what, ": aspect changed to ", cam.GetAspect());
        CHECK_MESSAGE(Finite(cam.GetCamera().GetViewProjectionMatrix()), what, ": projection is not finite");
        CHECK_MESSAGE(cam.GetCamera().GetViewProjectionMatrix() == good, what, ": projection moved");
    };

    // Zoom.
    cam.SetZoom(kNaN);  unchanged("SetZoom(NaN)");
    cam.SetZoom(kInf);  unchanged("SetZoom(+inf)");
    cam.SetZoom(-kInf); unchanged("SetZoom(-inf)");
    // Focus.
    cam.SetFocus({ kNaN, 1.0f });  unchanged("SetFocus(NaN, 1)");
    cam.SetFocus({ 1.0f, kInf });  unchanged("SetFocus(1, inf)");
    cam.SetFocus({ -kInf, kNaN }); unchanged("SetFocus(-inf, NaN)");
    // Resize / viewport (zero and negative sizes were already ignored; nonfinite must be too).
    cam.OnResize(0.0f, 0.0f);            unchanged("OnResize(0, 0)");
    cam.OnResize(-1.0f, 5.0f);           unchanged("OnResize(-1, 5)");
    cam.OnResize(kNaN, 720.0f);          unchanged("OnResize(NaN, 720)");
    cam.OnResize(1280.0f, kNaN);         unchanged("OnResize(1280, NaN)");
    cam.OnResize(kInf, 720.0f);          unchanged("OnResize(inf, 720)");
    cam.OnResize(1280.0f, kInf);         unchanged("OnResize(1280, inf)");
    cam.SetViewportRect({ kNaN, 0.0f }, { 1280.0f, 720.0f }); unchanged("SetViewportRect(NaN pos)");
    cam.SetViewportRect({ 0.0f, 0.0f }, { kInf, 720.0f });    unchanged("SetViewportRect(inf size)");
    cam.SetViewportRect({ 0.0f, 0.0f }, { 0.0f, 0.0f });      unchanged("SetViewportRect(0 size)");
    // Framing.
    cam.FrameBounds({ kNaN, 0.0f }, { 1.0f, 1.0f });     unchanged("FrameBounds(NaN min)");
    cam.FrameBounds({ 0.0f, 0.0f }, { kInf, 1.0f });     unchanged("FrameBounds(inf max)");
    cam.FrameBounds({ -kInf, -kInf }, { kInf, kInf });   unchanged("FrameBounds(infinite box)");
    // Scroll through the real event path (viewport is nonzero, but the scroll
    // amount itself is not a number: rejected before any anchor math).
    {
        MouseScrolledEvent nan(0.0f, kNaN);
        cam.OnEvent(nan);  unchanged("scroll(NaN)");
        MouseScrolledEvent inf(0.0f, kInf);
        cam.OnEvent(inf);  unchanged("scroll(+inf)");
        MouseScrolledEvent ninf(0.0f, -kInf);
        cam.OnEvent(ninf); unchanged("scroll(-inf)");
        MouseScrolledEvent zero(0.0f, 0.0f);
        cam.OnEvent(zero); unchanged("scroll(0)");
    }

    // The pure helpers: nonfinite inputs return the focus unchanged (the same
    // answer the existing zero-height / zero-zoom guards give).
    const glm::vec2 focus{ 1.0f, 2.0f };
    const glm::vec2 vpPos{ 0.0f, 0.0f }, vpSize{ 800.0f, 600.0f };
    CHECK(Camera2DController::ScreenToWorld({ kNaN, 10.0f }, vpPos, vpSize, focus, 5.0f) == focus);
    CHECK(Camera2DController::ScreenToWorld({ 10.0f, 10.0f }, vpPos, vpSize, focus, kNaN) == focus);
    CHECK(Camera2DController::ScreenToWorld({ 10.0f, 10.0f }, vpPos, { 800.0f, kInf }, focus, 5.0f) == focus);
    CHECK(Camera2DController::ScreenToWorld({ 10.0f, 10.0f }, vpPos, { 800.0f, 0.0f }, focus, 5.0f) == focus);
    CHECK(Camera2DController::ScreenToWorld({ 10.0f, 10.0f }, vpPos, vpSize, focus, 0.0f) == focus);
    CHECK(Camera2DController::ScreenToWorld({ 10.0f, 10.0f }, vpPos, vpSize, focus, -1.0f) == focus);
    CHECK(Camera2DController::PanBy(focus, { kNaN, 0.0f }, 5.0f, 600.0f) == focus);
    CHECK(Camera2DController::PanBy(focus, { 1.0f, 1.0f }, kInf, 600.0f) == focus);
    CHECK(Camera2DController::PanBy(focus, { 1.0f, 1.0f }, 5.0f, 0.0f) == focus);
    CHECK(Camera2DController::PanBy(focus, { 1.0f, 1.0f }, 5.0f, kNaN) == focus);
    CHECK(Camera2DController::ZoomAboutPoint(focus, { kNaN, 0.0f }, 5.0f, 2.0f) == focus);
    CHECK(Camera2DController::ZoomAboutPoint(focus, { 0.0f, 0.0f }, kNaN, 2.0f) == focus);
    CHECK(Camera2DController::ZoomAboutPoint(focus, { 0.0f, 0.0f }, 5.0f, kInf) == focus);
    CHECK(Camera2DController::ZoomAboutPoint(focus, { 0.0f, 0.0f }, 0.0f, 2.0f) == focus);
    CHECK(Camera2DController::ZoomAboutPoint(focus, { 0.0f, 0.0f }, -5.0f, 2.0f) == focus);
    // Finite inputs still work after all of that.
    CHECK(Finite(Camera2DController::ScreenToWorld({ 400.0f, 300.0f }, vpPos, vpSize, focus, 5.0f)));
    CHECK(Camera2DController::ScreenToWorld({ 400.0f, 300.0f }, vpPos, vpSize, focus, 5.0f) == focus);

    // A constructor given a nonfinite / nonpositive aspect falls back to 1.
    CHECK(Camera2DController(kNaN).GetAspect() == doctest::Approx(1.0f));
    CHECK(Camera2DController(kInf).GetAspect() == doctest::Approx(1.0f));
    CHECK(Camera2DController(0.0f).GetAspect() == doctest::Approx(1.0f));
    CHECK(Camera2DController(-2.0f).GetAspect() == doctest::Approx(1.0f));
}

TEST_CASE("WO-08 R04: FrameBounds on empty / zero-size / inverted boxes recentres and keeps a finite projection")
{
    Camera2DController cam(16.0f / 9.0f);
    cam.SetZoom(3.0f);
    cam.FrameBounds({ 5.0f, 5.0f }, { 5.0f, 5.0f });   // zero-size: recentre only
    CHECK(cam.GetFocus() == glm::vec2(5.0f, 5.0f));
    CHECK(cam.GetZoom() == doctest::Approx(3.0f));
    cam.FrameBounds({ 10.0f, 10.0f }, { 0.0f, 0.0f });   // inverted (min > max): size negative => recentre only
    CHECK(cam.GetFocus() == glm::vec2(5.0f, 5.0f));
    CHECK(cam.GetZoom() == doctest::Approx(3.0f));
    cam.FrameBounds({ -1e-7f, -1e-7f }, { 1e-7f, 1e-7f });   // below the 1e-6 threshold
    CHECK(cam.GetZoom() == doctest::Approx(3.0f));
    cam.FrameBounds({ 0.0f, 0.0f }, { 1e6f, 1e6f });          // huge: clamps to the max zoom
    CHECK(cam.GetZoom() == doctest::Approx(10000.0f));
    cam.FrameBounds({ 0.0f, 0.0f }, { 1e-4f, 1e-4f });        // tiny: clamps to the min zoom
    CHECK(cam.GetZoom() == doctest::Approx(0.01f));
    CHECK(Finite(cam.GetCamera().GetViewProjectionMatrix()));
}

TEST_CASE("WO-08 R04: a changing viewport — 1x1, 641x359, 1920x1080, 3x3, 1x1080, 0x0 (ignored) — never yields NaN or a divide-by-zero")
{
    Camera2DController cam(16.0f / 9.0f);
    cam.SetFocus({ 2.0f, -1.0f });
    cam.SetZoom(4.0f);
    const glm::vec2 sizes[] = { { 1, 1 }, { 641, 359 }, { 1920, 1080 }, { 3, 3 }, { 1, 1080 }, { 0, 0 }, { 1080, 1 }, { 641, 359 } };
    float lastAspect = cam.GetAspect();
    for (const glm::vec2& s : sizes)
    {
        cam.SetViewportRect({ 0.0f, 0.0f }, s);
        REQUIRE(Finite(cam.GetCamera().GetViewProjectionMatrix()));
        if (s.x > 0.0f && s.y > 0.0f)
        {
            CHECK(cam.GetAspect() == doctest::Approx(s.x / s.y));
            lastAspect = cam.GetAspect();
            // The focus projects to the viewport centre at every size.
            const glm::vec2 c = ProjectToScreen(cam, { 0.0f, 0.0f }, s, cam.GetFocus());
            CHECK(std::abs(c.x - s.x * 0.5f) <= 0.5f);
            CHECK(std::abs(c.y - s.y * 0.5f) <= 0.5f);
            // And a screen->world->screen trip through the statics at this size holds.
            const glm::vec2 px{ std::floor(s.x * 0.8f) + 0.5f, std::floor(s.y * 0.2f) + 0.5f };
            const glm::vec2 w = Camera2DController::ScreenToWorld(px, { 0.0f, 0.0f }, s, cam.GetFocus(), cam.GetZoom());
            CHECK(glm::length(WorldToScreen(w, { 0.0f, 0.0f }, s, cam.GetFocus(), cam.GetZoom()) - px) <= 0.5f);
        }
        else
        {
            CHECK(cam.GetAspect() == doctest::Approx(lastAspect));   // 0x0 is ignored, not applied
        }
    }
}

TEST_CASE("WO-08 R04: outside the declared envelope the float error is measured, not hidden")
{
    // Informational: a 0.01-unit zoom parked 1,000 world units from the origin
    // (1,000x the envelope) loses whole pixels to float ulps. Recorded so the
    // envelope in the header is a measured statement.
    const glm::vec2 vpPos{ 0.0f, 0.0f }, vpSize{ 1920.0f, 1080.0f };
    const float zoom = 0.01f;
    const glm::vec2 focus{ 1000.0f, 1000.0f };
    const glm::vec2 px{ 1234.5f, 567.5f };
    const glm::vec2 w = Camera2DController::ScreenToWorld(px, vpPos, vpSize, focus, zoom);
    const float errPx = glm::length(WorldToScreen(w, vpPos, vpSize, focus, zoom) - px);
    MESSAGE("R04 outside-envelope sample (zoom 0.01, focus 1000): round-trip error " << errPx << " px");
    CHECK(std::isfinite(errPx));
}
