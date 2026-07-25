// test_ui_anchor.cpp — world-anchored UI projection (Phase 29 W2 / §9.2).
//
// Headless (no GL): the two pure functions the X6 world-anchor path is built on.
//
//   UiSystem::ProjectToCanvas — world point -> canvas point (top-left origin,
//   +y DOWN), for BOTH an ortho 2D VP and a perspective 3D VP. The edges that
//   matter are the frustum boundary (a nameplate leaving the screen) and the
//   behind-camera case (clip.w <= 0), which must be reported, not wrapped.
//
//   UiRect::Contains — hit testing, exercised through NESTED ResolveRect chains
//   so an anchored child's hit area is proven to follow its ancestors.
//
// test_ui_rects.cpp covers the anchor matrix and the button state machine; this
// suite is the projection + nested-hit net.

#include <doctest.h>

#include "scene/ui/UiComponents.h"
#include "scene/ui/UiSystem.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

using namespace Cosmic;

namespace
{
    // A 1280x720 canvas at the origin — the shape UiSystem hands the projector.
    UiRect Canvas(float w = 1280.0f, float h = 720.0f)
    {
        UiRect r;
        r.Min = { 0.0f, 0.0f };
        r.Max = { w, h };
        return r;
    }

    bool Near(float a, float b, float eps = 0.01f) { return std::abs(a - b) <= eps; }
}

TEST_SUITE("X6: ProjectToCanvas — ortho (2D) camera")
{
    // A 2D camera looking down -Z at the origin, 20x11.25 world units wide.
    glm::mat4 OrthoVP()
    {
        return glm::ortho(-10.0f, 10.0f, -5.625f, 5.625f, -1.0f, 1.0f);
    }

    TEST_CASE("the world origin lands at the canvas centre")
    {
        glm::vec2 p{ -1.0f };
        REQUIRE(UiSystem::ProjectToCanvas({ 0.0f, 0.0f, 0.0f }, OrthoVP(), Canvas(), p));
        CHECK(Near(p.x, 640.0f));
        CHECK(Near(p.y, 360.0f));
    }

    TEST_CASE("+y is UP in the world and DOWN on the canvas")
    {
        glm::vec2 top{ 0.0f }, bottom{ 0.0f };
        REQUIRE(UiSystem::ProjectToCanvas({ 0.0f,  5.625f, 0.0f }, OrthoVP(), Canvas(), top));
        REQUIRE(UiSystem::ProjectToCanvas({ 0.0f, -5.625f, 0.0f }, OrthoVP(), Canvas(), bottom));
        CHECK(Near(top.y, 0.0f));        // world top edge  -> canvas y 0
        CHECK(Near(bottom.y, 720.0f));   // world bottom    -> canvas y = height
        CHECK(top.y < bottom.y);
    }

    TEST_CASE("the frustum edges map exactly to the canvas corners")
    {
        const UiRect c = Canvas();
        glm::vec2 tl{ 0.0f }, br{ 0.0f };
        REQUIRE(UiSystem::ProjectToCanvas({ -10.0f,  5.625f, 0.0f }, OrthoVP(), c, tl));
        REQUIRE(UiSystem::ProjectToCanvas({  10.0f, -5.625f, 0.0f }, OrthoVP(), c, br));
        CHECK(Near(tl.x, 0.0f));
        CHECK(Near(tl.y, 0.0f));
        CHECK(Near(br.x, 1280.0f));
        CHECK(Near(br.y, 720.0f));
        CHECK(c.Contains(tl));
        CHECK(c.Contains(br));
    }

    TEST_CASE("a point just outside the frustum projects OUTSIDE the canvas, not clamped")
    {
        const UiRect c = Canvas();
        glm::vec2 offLeft{ 0.0f }, offTop{ 0.0f };
        REQUIRE(UiSystem::ProjectToCanvas({ -12.0f, 0.0f, 0.0f }, OrthoVP(), c, offLeft));
        REQUIRE(UiSystem::ProjectToCanvas({ 0.0f, 8.0f, 0.0f },  OrthoVP(), c, offTop));

        CHECK(offLeft.x < 0.0f);          // the caller decides to hide it
        CHECK_FALSE(c.Contains(offLeft));
        CHECK(offTop.y < 0.0f);
        CHECK_FALSE(c.Contains(offTop));

        // The mapping stays linear past the edge (no clamping anywhere).
        CHECK(Near(offLeft.x, -128.0f));  // 2 units past a 10-unit half-width
    }

    TEST_CASE("the canvas rect's own origin and size are honoured")
    {
        // A canvas inset inside the window (a sub-viewport): the same world point
        // must land at the inset's centre, not the window's.
        UiRect inset;
        inset.Min = { 100.0f, 50.0f };
        inset.Max = { 500.0f, 250.0f };
        glm::vec2 p{ 0.0f };
        REQUIRE(UiSystem::ProjectToCanvas({ 0.0f, 0.0f, 0.0f }, OrthoVP(), inset, p));
        CHECK(Near(p.x, 300.0f));
        CHECK(Near(p.y, 150.0f));
        CHECK(inset.Contains(p));
    }
}

TEST_SUITE("X6: ProjectToCanvas — perspective (3D) camera")
{
    // A camera at +Z looking at the origin down -Z.
    glm::mat4 PerspVP()
    {
        const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
        const glm::mat4 view = glm::lookAt(glm::vec3{ 0.0f, 0.0f, 10.0f },
                                           glm::vec3{ 0.0f, 0.0f, 0.0f },
                                           glm::vec3{ 0.0f, 1.0f, 0.0f });
        return proj * view;
    }

    TEST_CASE("a point in front of the camera projects; the centre is the canvas centre")
    {
        glm::vec2 p{ 0.0f };
        REQUIRE(UiSystem::ProjectToCanvas({ 0.0f, 0.0f, 0.0f }, PerspVP(), Canvas(), p));
        CHECK(Near(p.x, 640.0f, 0.05f));
        CHECK(Near(p.y, 360.0f, 0.05f));
    }

    TEST_CASE("a point BEHIND the camera is reported as unprojectable")
    {
        const UiRect c = Canvas();
        glm::vec2 p{ 12345.0f, 54321.0f };

        // Behind the eye (z > 10 puts it on the far side of the camera).
        CHECK_FALSE(UiSystem::ProjectToCanvas({ 0.0f, 0.0f, 20.0f }, PerspVP(), c, p));
        // Exactly ON the camera plane: clip.w == 0, also refused (no divide).
        CHECK_FALSE(UiSystem::ProjectToCanvas({ 0.0f, 0.0f, 10.0f }, PerspVP(), c, p));

        // outPoint is left untouched on failure, so a caller that ignores the
        // return value cannot silently draw at a garbage position.
        CHECK(p.x == 12345.0f);
        CHECK(p.y == 54321.0f);
    }

    TEST_CASE("perspective divide: the same object is smaller and nearer the centre when far")
    {
        const UiRect c = Canvas();
        glm::vec2 near2{ 0.0f }, far2{ 0.0f };
        REQUIRE(UiSystem::ProjectToCanvas({ 2.0f, 0.0f,  8.0f }, PerspVP(), c, near2));   // 2 units away
        REQUIRE(UiSystem::ProjectToCanvas({ 2.0f, 0.0f, -20.0f }, PerspVP(), c, far2));   // 30 units away

        const float nearOffset = std::abs(near2.x - 640.0f);
        const float farOffset  = std::abs(far2.x  - 640.0f);
        CHECK(farOffset < nearOffset);          // foreshortening
        CHECK(farOffset > 0.0f);
    }

    TEST_CASE("a nameplate offset above an entity tracks it across the screen")
    {
        // The X6 usage: project the entity, then add a pixel ScreenOffset. Moving
        // the entity right must move the anchor right by a positive amount and
        // keep the vertical offset exactly constant.
        const UiRect c = Canvas();
        const glm::vec2 screenOffset{ 0.0f, -48.0f };   // 48 px above the head

        glm::vec2 a{ 0.0f }, b{ 0.0f };
        REQUIRE(UiSystem::ProjectToCanvas({ -3.0f, 1.0f, 0.0f }, PerspVP(), c, a));
        REQUIRE(UiSystem::ProjectToCanvas({  3.0f, 1.0f, 0.0f }, PerspVP(), c, b));
        CHECK(b.x > a.x);
        CHECK(Near(a.y, b.y));                          // same height, same row

        CHECK(Near((a + screenOffset).y, a.y - 48.0f));
        CHECK((a + screenOffset).y < a.y);              // "above" is a SMALLER y
    }
}

TEST_SUITE("X6: UiRect::Contains under nested anchors")
{
    TEST_CASE("Contains is inclusive on every edge and rejects just outside")
    {
        UiRect r;
        r.Min = { 10.0f, 20.0f };
        r.Max = { 110.0f, 60.0f };

        CHECK(r.Contains({ 10.0f, 20.0f }));      // corners are inside
        CHECK(r.Contains({ 110.0f, 60.0f }));
        CHECK(r.Contains({ 10.0f, 60.0f }));
        CHECK(r.Contains({ 60.0f, 40.0f }));      // centre

        CHECK_FALSE(r.Contains({ 9.99f, 40.0f }));
        CHECK_FALSE(r.Contains({ 110.01f, 40.0f }));
        CHECK_FALSE(r.Contains({ 60.0f, 19.99f }));
        CHECK_FALSE(r.Contains({ 60.0f, 60.01f }));
    }

    TEST_CASE("a hit inside a deeply nested child is a hit in every ancestor")
    {
        const UiRect root = Canvas();

        // Panel: stretched with a 40 px inset on all sides.
        RectTransformComponent panelRt;
        panelRt.AnchorMin = { 0.0f, 0.0f };
        panelRt.AnchorMax = { 1.0f, 1.0f };
        panelRt.OffsetMin = { 40.0f, 40.0f };
        panelRt.OffsetMax = { -40.0f, -40.0f };
        const UiRect panel = UiSystem::ResolveRect(root, panelRt);
        CHECK(Near(panel.Min.x, 40.0f));
        CHECK(Near(panel.Max.x, 1240.0f));

        // Row: pinned to the panel's top-left, 300x80.
        RectTransformComponent rowRt;
        rowRt.AnchorMin = { 0.0f, 0.0f };
        rowRt.AnchorMax = { 0.0f, 0.0f };
        rowRt.OffsetMin = { 20.0f, 20.0f };
        rowRt.OffsetMax = { 320.0f, 100.0f };
        const UiRect row = UiSystem::ResolveRect(panel, rowRt);

        // Button: centred point anchor inside the row, 100x30.
        RectTransformComponent btnRt;
        btnRt.AnchorMin = { 0.5f, 0.5f };
        btnRt.AnchorMax = { 0.5f, 0.5f };
        btnRt.OffsetMin = { -50.0f, -15.0f };
        btnRt.OffsetMax = {  50.0f,  15.0f };
        const UiRect button = UiSystem::ResolveRect(row, btnRt);

        const glm::vec2 centre = button.Center();
        CHECK(button.Contains(centre));
        CHECK(row.Contains(centre));
        CHECK(panel.Contains(centre));
        CHECK(root.Contains(centre));

        // A point in the row but outside the button hits only the ancestors.
        const glm::vec2 rowEdge{ row.Min.x + 2.0f, row.Center().y };
        CHECK_FALSE(button.Contains(rowEdge));
        CHECK(row.Contains(rowEdge));
        CHECK(panel.Contains(rowEdge));
    }

    TEST_CASE("canvas scale moves a nested hit area with the layout")
    {
        // ScaleWithHeight: the SAME authored offsets at 2x scale put the child
        // twice as far from its anchor, so a point that hit at 1x misses at 2x.
        UiRect root = Canvas(2560.0f, 1440.0f);

        RectTransformComponent rt;
        rt.AnchorMin = { 0.0f, 0.0f };
        rt.AnchorMax = { 0.0f, 0.0f };
        rt.OffsetMin = { 100.0f, 100.0f };
        rt.OffsetMax = { 200.0f, 140.0f };

        const UiRect at1 = UiSystem::ResolveRect(root, rt, 1.0f);
        const UiRect at2 = UiSystem::ResolveRect(root, rt, 2.0f);

        CHECK(Near(at1.Min.x, 100.0f));
        CHECK(Near(at2.Min.x, 200.0f));
        CHECK(Near(at2.Width(), at1.Width() * 2.0f));

        const glm::vec2 p = at1.Center();
        CHECK(at1.Contains(p));
        CHECK_FALSE(at2.Contains(p));

        // And the canvas scale a 1440p viewport reports for a 1080p design.
        CanvasComponent canvas;
        canvas.ScaleMode       = UiScaleMode::ScaleWithHeight;
        canvas.ReferenceHeight = 1080.0f;
        CHECK(UiSystem::CanvasScale(canvas, root) == doctest::Approx(1440.0f / 1080.0f));

        canvas.ScaleMode = UiScaleMode::ConstantPixel;
        CHECK(UiSystem::CanvasScale(canvas, root) == doctest::Approx(1.0f));
    }
}
