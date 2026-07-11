// test_camera2d.cpp — 2D camera rig pure math (Phase 17 / U3).
// Headless: ScreenToWorld / PanBy / ZoomAboutPoint invariants (no GL, no input).

#include <doctest.h>

#include "camera/Camera2DController.h"

#include <glm/glm.hpp>

using namespace Cosmic;

static bool Near2(const glm::vec2& a, const glm::vec2& b, float eps = 1e-4f)
{
    return std::abs(a.x - b.x) <= eps && std::abs(a.y - b.y) <= eps;
}

TEST_CASE("U3: ScreenToWorld — viewport center maps to focus, +y screen is -y world")
{
    const glm::vec2 vpPos{ 100.0f, 50.0f };
    const glm::vec2 vpSize{ 800.0f, 600.0f };
    const glm::vec2 focus{ 3.0f, -2.0f };
    const float zoom = 5.0f;   // half-height => 600 px spans 10 world units

    // Center pixel -> focus.
    CHECK(Near2(Camera2DController::ScreenToWorld(vpPos + vpSize * 0.5f,
                                                  vpPos, vpSize, focus, zoom), focus));

    // 60 px right + 60 px DOWN = +1 world x, -1 world y (10 units / 600 px).
    const glm::vec2 p = Camera2DController::ScreenToWorld(vpPos + vpSize * 0.5f + glm::vec2(60.0f, 60.0f),
                                                          vpPos, vpSize, focus, zoom);
    CHECK(Near2(p, { focus.x + 1.0f, focus.y - 1.0f }));
}

TEST_CASE("U3: PanBy — the world point under the cursor follows the drag")
{
    const glm::vec2 vpPos{ 0.0f, 0.0f };
    const glm::vec2 vpSize{ 800.0f, 600.0f };
    const float zoom = 5.0f;
    glm::vec2 focus{ 0.0f, 0.0f };

    // The world point under some pixel before the drag...
    const glm::vec2 pixel{ 200.0f, 200.0f };
    const glm::vec2 anchor = Camera2DController::ScreenToWorld(pixel, vpPos, vpSize, focus, zoom);

    // ...must appear under (pixel + delta) after panning by delta.
    const glm::vec2 delta{ 37.0f, -12.0f };
    focus = Camera2DController::PanBy(focus, delta, zoom, vpSize.y);
    const glm::vec2 after = Camera2DController::ScreenToWorld(pixel + delta, vpPos, vpSize, focus, zoom);
    CHECK(Near2(after, anchor));
}

TEST_CASE("U3: ZoomAboutPoint keeps the anchor's screen position fixed")
{
    const glm::vec2 vpPos{ 10.0f, 20.0f };
    const glm::vec2 vpSize{ 1024.0f, 768.0f };
    const float zoomBefore = 8.0f;
    const float zoomAfter  = 2.0f;   // 4x zoom in
    const glm::vec2 focus{ 5.0f, 7.0f };

    // Anchor = world under an off-center pixel at the old zoom.
    const glm::vec2 pixel{ 300.0f, 500.0f };
    const glm::vec2 anchor = Camera2DController::ScreenToWorld(pixel, vpPos, vpSize, focus, zoomBefore);

    const glm::vec2 focusAfter = Camera2DController::ZoomAboutPoint(focus, anchor, zoomBefore, zoomAfter);

    // The anchor must map to the SAME pixel at the new zoom.
    const glm::vec2 anchorAfter = Camera2DController::ScreenToWorld(pixel, vpPos, vpSize, focusAfter, zoomAfter);
    CHECK(Near2(anchorAfter, anchor, 1e-3f));
}

TEST_CASE("U3: FrameBounds centers the box and fits the larger extent")
{
    Camera2DController cam(2.0f);   // aspect 2:1
    cam.FrameBounds({ -10.0f, -1.0f }, { 10.0f, 1.0f });   // wide box: 20 x 2

    CHECK(Near2(cam.GetFocus(), { 0.0f, 0.0f }));
    // Fit by width: halfW needed = 10 => halfH = 10/aspect = 5, padded 10% = 5.5.
    CHECK(cam.GetZoom() == doctest::Approx(5.5f).epsilon(0.01));

    glm::vec2 mn, mx;
    cam.VisibleRect(mn, mx);
    CHECK(mn.x <= -10.0f);
    CHECK(mx.x >=  10.0f);
}
