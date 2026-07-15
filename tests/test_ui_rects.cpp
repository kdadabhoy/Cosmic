// test_ui_rects.cpp — in-game UI layout + interaction (Phase 17 / U1).
// Headless: pure rect math + button state machine + EventBus click routing.
// No GL (Render is not exercised here).
//
// Acceptance (plan doc 16 U1): anchor/pivot matrix (centered, stretched,
// corner-pinned, nested, scaled) each within +/-0.5 px; button hover / press /
// release-inside / release-outside transitions; a simulated click emits its
// signal on the scene EventBus and does not leak when released outside.

#include <doctest.h>

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/EventBus.h"
#include "scene/ui/UiComponents.h"
#include "scene/ui/UiSystem.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace Cosmic;

static bool Near(float a, float b, float eps = 0.5f) { return std::abs(a - b) <= eps; }
static bool RectNear(const UiRect& r, glm::vec2 mn, glm::vec2 mx)
{
    return Near(r.Min.x, mn.x) && Near(r.Min.y, mn.y) &&
           Near(r.Max.x, mx.x) && Near(r.Max.y, mx.y);
}

// ---------------------------------------------------------------------------
// ResolveRect — the anchor/offset matrix
// ---------------------------------------------------------------------------

TEST_CASE("U1: ResolveRect centered point-anchor")
{
    UiRect parent{ {0, 0}, {1920, 1080} };
    RectTransformComponent rt;
    rt.AnchorMin = rt.AnchorMax = { 0.5f, 0.5f };
    rt.OffsetMin = { -100, -50 };
    rt.OffsetMax = {  100,  50 };

    UiRect r = UiSystem::ResolveRect(parent, rt);
    CHECK(RectNear(r, { 860, 490 }, { 1060, 590 }));
    CHECK(Near(r.Width(), 200.0f));
    CHECK(Near(r.Height(), 100.0f));
}

TEST_CASE("U1: ResolveRect stretched (insets from edges)")
{
    UiRect parent{ {0, 0}, {1920, 1080} };
    RectTransformComponent rt;
    rt.AnchorMin = { 0.0f, 0.0f };
    rt.AnchorMax = { 1.0f, 1.0f };
    rt.OffsetMin = {  20,  20 };
    rt.OffsetMax = { -20, -20 };

    UiRect r = UiSystem::ResolveRect(parent, rt);
    CHECK(RectNear(r, { 20, 20 }, { 1900, 1060 }));
}

TEST_CASE("U1: ResolveRect corner-pinned (top-right)")
{
    UiRect parent{ {0, 0}, {1920, 1080} };
    RectTransformComponent rt;
    rt.AnchorMin = rt.AnchorMax = { 1.0f, 0.0f };
    rt.OffsetMin = { -110, 10 };
    rt.OffsetMax = {  -10, 60 };

    UiRect r = UiSystem::ResolveRect(parent, rt);
    CHECK(RectNear(r, { 1810, 10 }, { 1910, 60 }));
}

TEST_CASE("U1: ResolveRect nested resolves against the parent rect")
{
    UiRect root{ {0, 0}, {1920, 1080} };
    RectTransformComponent panel;
    panel.AnchorMin = { 0.0f, 0.0f };
    panel.AnchorMax = { 1.0f, 1.0f };
    panel.OffsetMin = {  20,  20 };
    panel.OffsetMax = { -20, -20 };
    UiRect panelRect = UiSystem::ResolveRect(root, panel);   // {20,20}-{1900,1060}

    RectTransformComponent child;
    child.AnchorMin = child.AnchorMax = { 0.0f, 0.0f };
    child.OffsetMin = { 0, 0 };
    child.OffsetMax = { 100, 40 };
    UiRect childRect = UiSystem::ResolveRect(panelRect, child);
    CHECK(RectNear(childRect, { 20, 20 }, { 120, 60 }));
}

TEST_CASE("U1: ResolveRect scale multiplies offsets (ScaleWithHeight)")
{
    UiRect parent{ {0, 0}, {1000, 1000} };
    RectTransformComponent rt;
    rt.AnchorMin = rt.AnchorMax = { 0.0f, 0.0f };
    rt.OffsetMin = { 10, 10 };
    rt.OffsetMax = { 50, 30 };

    UiRect r = UiSystem::ResolveRect(parent, rt, /*scale=*/2.0f);
    CHECK(RectNear(r, { 20, 20 }, { 100, 60 }));
}

TEST_CASE("U1: CanvasScale — ConstantPixel is 1, ScaleWithHeight is viewportH/refH")
{
    UiRect vp{ {0, 0}, {1920, 540} };

    CanvasComponent constant; constant.ScaleMode = UiScaleMode::ConstantPixel;
    CHECK(UiSystem::CanvasScale(constant, vp) == doctest::Approx(1.0f));

    CanvasComponent scaled; scaled.ScaleMode = UiScaleMode::ScaleWithHeight;
    scaled.ReferenceHeight = 1080.0f;
    CHECK(UiSystem::CanvasScale(scaled, vp) == doctest::Approx(0.5f));
}

TEST_CASE("U1: PivotPoint is rect min + size*pivot")
{
    UiRect r{ {100, 200}, {300, 400} };
    glm::vec2 p = UiSystem::PivotPoint(r, { 0.5f, 0.0f });
    CHECK(p.x == doctest::Approx(200.0f));
    CHECK(p.y == doctest::Approx(200.0f));
}

// ---------------------------------------------------------------------------
// Button state machine (pure)
// ---------------------------------------------------------------------------

TEST_CASE("U1: button hover / press / release-inside emits once")
{
    // Hover only.
    ButtonStep s = UiSystem::StepButtonState(UiButtonState::Normal, false, true,
                                             /*hovered=*/true, false, false, false);
    CHECK(s.State == UiButtonState::Hover);
    CHECK_FALSE(s.Armed);
    CHECK_FALSE(s.Emit);

    // Press inside.
    s = UiSystem::StepButtonState(UiButtonState::Hover, false, true,
                                  true, /*pressedEdge=*/true, false, /*down=*/true);
    CHECK(s.State == UiButtonState::Pressed);
    CHECK(s.Armed);
    CHECK_FALSE(s.Emit);

    // Release inside -> emit.
    s = UiSystem::StepButtonState(UiButtonState::Pressed, /*armedPrev=*/true, true,
                                  true, false, /*releasedEdge=*/true, /*down=*/false);
    CHECK(s.Emit);
    CHECK_FALSE(s.Armed);
    CHECK(s.State == UiButtonState::Hover);
}

TEST_CASE("U1: button release OUTSIDE does not emit")
{
    // Armed (pressed on the button), pointer moved off, released.
    ButtonStep s = UiSystem::StepButtonState(UiButtonState::Pressed, /*armedPrev=*/true, true,
                                             /*hovered=*/false, false, /*releasedEdge=*/true,
                                             /*down=*/false);
    CHECK_FALSE(s.Emit);
    CHECK_FALSE(s.Armed);
    CHECK(s.State == UiButtonState::Normal);
}

TEST_CASE("U1: non-interactable button is Disabled and never emits")
{
    ButtonStep s = UiSystem::StepButtonState(UiButtonState::Normal, true, /*interactable=*/false,
                                             true, true, true, true);
    CHECK(s.State == UiButtonState::Disabled);
    CHECK_FALSE(s.Emit);
    CHECK_FALSE(s.Armed);
}

// ---------------------------------------------------------------------------
// Scene-driven Update → EventBus (headless)
// ---------------------------------------------------------------------------

static Entity MakeButton(Scene& s, Entity canvas, const std::string& signal,
                         glm::vec2 offMin, glm::vec2 offMax, int z = 0)
{
    Entity b = s.CreateEntity("Button");
    auto& rt = b.AddComponent<RectTransformComponent>();
    rt.AnchorMin = rt.AnchorMax = { 0.0f, 0.0f };
    rt.OffsetMin = offMin; rt.OffsetMax = offMax; rt.ZOrder = z;
    auto& btn = b.AddComponent<UiButtonComponent>();
    btn.Signal = signal;
    b.AddComponent<UiImageComponent>();
    s.SetParent(b, canvas, /*keepWorldPose=*/false);
    return b;
}

TEST_CASE("U1: Update routes a click to the scene EventBus")
{
    Scene s;
    Entity canvas = s.CreateEntity("Canvas");
    auto& c = canvas.AddComponent<CanvasComponent>();
    c.ScaleMode = UiScaleMode::ConstantPixel;   // scale = 1 so the rect is literal

    Entity play = MakeButton(s, canvas, "play_clicked", { 0, 0 }, { 200, 80 });

    int fires = 0;
    entt::entity source = entt::null;
    s.Events().Connect("play_clicked", [&](Entity src) { ++fires; source = (entt::entity)src; });

    const UiRect vp{ {0, 0}, {800, 600} };

    // Press inside.
    UiPointer p; p.Position = { 100, 40 }; p.Down = true; p.PressedEdge = true;
    bool consumed = UiSystem::Update(s, vp, p);
    CHECK(consumed);
    CHECK(fires == 0);

    // Hold.
    p.PressedEdge = false;
    UiSystem::Update(s, vp, p);
    CHECK(fires == 0);

    // Release inside -> one emit.
    p.Down = false; p.ReleasedEdge = true;
    UiSystem::Update(s, vp, p);
    CHECK(fires == 1);
    CHECK(source == (entt::entity)play);
}

TEST_CASE("U1: click that releases outside the button never emits")
{
    Scene s;
    Entity canvas = s.CreateEntity("Canvas");
    canvas.AddComponent<CanvasComponent>().ScaleMode = UiScaleMode::ConstantPixel;
    MakeButton(s, canvas, "play_clicked", { 0, 0 }, { 200, 80 });

    int fires = 0;
    s.Events().Connect("play_clicked", [&](Entity) { ++fires; });

    const UiRect vp{ {0, 0}, {800, 600} };

    UiPointer p; p.Position = { 100, 40 }; p.Down = true; p.PressedEdge = true;
    UiSystem::Update(s, vp, p);                 // press inside

    p.PressedEdge = false; p.Position = { 500, 500 };  // drag off
    UiSystem::Update(s, vp, p);

    p.Down = false; p.ReleasedEdge = true;      // release outside
    bool consumed = UiSystem::Update(s, vp, p);
    CHECK(fires == 0);
    CHECK_FALSE(consumed);                       // pointer not over the button
}

TEST_CASE("U1: overlapping buttons — topmost ZOrder wins the click")
{
    Scene s;
    Entity canvas = s.CreateEntity("Canvas");
    canvas.AddComponent<CanvasComponent>().ScaleMode = UiScaleMode::ConstantPixel;

    MakeButton(s, canvas, "back",  { 0, 0 }, { 200, 80 }, /*z=*/0);
    MakeButton(s, canvas, "front", { 0, 0 }, { 200, 80 }, /*z=*/10);

    int back = 0, front = 0;
    s.Events().Connect("back",  [&](Entity) { ++back; });
    s.Events().Connect("front", [&](Entity) { ++front; });

    const UiRect vp{ {0, 0}, {800, 600} };
    UiPointer p; p.Position = { 100, 40 };

    p.Down = true; p.PressedEdge = true;  UiSystem::Update(s, vp, p);
    p.PressedEdge = false; p.Down = false; p.ReleasedEdge = true; UiSystem::Update(s, vp, p);

    CHECK(front == 1);
    CHECK(back == 0);
}

TEST_CASE("U1: HitTest returns the topmost element under a point (editor select)")
{
    Scene s;
    Entity canvas = s.CreateEntity("Canvas");
    canvas.AddComponent<CanvasComponent>().ScaleMode = UiScaleMode::ConstantPixel;

    Entity back  = MakeButton(s, canvas, "back",  { 0, 0 },     { 200, 80 }, /*z=*/0);
    Entity front = MakeButton(s, canvas, "front", { 50, 20 },   { 150, 60 }, /*z=*/10);

    const UiRect vp{ {0, 0}, {800, 600} };

    // Overlap region -> the higher ZOrder element.
    uint32_t hit = 0;
    CHECK(UiSystem::HitTest(s, vp, { 100, 40 }, hit));
    CHECK((entt::entity)hit == (entt::entity)front);

    // Back-only region.
    CHECK(UiSystem::HitTest(s, vp, { 10, 10 }, hit));
    CHECK((entt::entity)hit == (entt::entity)back);

    // Empty space -> no hit.
    CHECK_FALSE(UiSystem::HitTest(s, vp, { 500, 500 }, hit));
}

// ---------------------------------------------------------------------------
// EventBus basics (U2 dividend — tested here since U1 rides it)
// ---------------------------------------------------------------------------

TEST_CASE("U2: EventBus disconnect stops delivery; ConnectAny sees every signal")
{
    Scene s;
    Entity e = s.CreateEntity("E");

    int named = 0, any = 0;
    auto h = s.Events().Connect("ping", [&](Entity) { ++named; });
    s.Events().ConnectAny([&](const std::string&, Entity) { ++any; });

    s.Events().Emit("ping", e);
    CHECK(named == 1);
    CHECK(any == 1);

    s.Events().Disconnect(h);
    s.Events().Emit("ping", e);
    CHECK(named == 1);   // named listener gone
    CHECK(any == 2);     // any-listener still fires
}

// ---------------------------------------------------------------------------
// X6 — world-anchored UI: ProjectToCanvas + CollectElements projection
// ---------------------------------------------------------------------------

static glm::mat4 TestCameraVP()
{
    // Camera at (0,0,5) looking down -z; 60° vertical FOV, 1:1 aspect.
    const glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
    return proj * view;
}

TEST_CASE("X6: ProjectToCanvas maps world points to canvas (top-left, +y down)")
{
    const glm::mat4 vp = TestCameraVP();
    const UiRect canvas{ { 0, 0 }, { 800, 600 } };
    glm::vec2 p;

    // On-axis origin -> canvas center.
    REQUIRE(UiSystem::ProjectToCanvas({ 0, 0, 0 }, vp, canvas, p));
    CHECK(Near(p.x, 400.0f));
    CHECK(Near(p.y, 300.0f));

    // Above the axis -> UPPER half (+y is DOWN in canvas space).
    REQUIRE(UiSystem::ProjectToCanvas({ 0, 1, 0 }, vp, canvas, p));
    CHECK(p.y < 300.0f);
    CHECK(Near(p.x, 400.0f));

    // Right of the axis -> RIGHT half.
    REQUIRE(UiSystem::ProjectToCanvas({ 1, 0, 0 }, vp, canvas, p));
    CHECK(p.x > 400.0f);
    CHECK(Near(p.y, 300.0f));
}

TEST_CASE("X6: ProjectToCanvas reports behind-camera points as hidden")
{
    const glm::mat4 vp = TestCameraVP();
    const UiRect canvas{ { 0, 0 }, { 800, 600 } };
    glm::vec2 p;
    // (0,0,10) is 5 units BEHIND the eye at z=5 -> not projectable.
    CHECK_FALSE(UiSystem::ProjectToCanvas({ 0, 0, 10 }, vp, canvas, p));
}

TEST_CASE("X6: CollectElements positions a world-anchored element at the projected point")
{
    Scene scene;
    const glm::mat4 vp = TestCameraVP();
    const UiRect viewport{ { 0, 0 }, { 800, 600 } };

    // A tracked world entity at the origin.
    Entity world = scene.CreateEntity("Unit");
    const uint64_t worldId = (uint64_t)world.GetComponent<IDComponent>().ID;

    // Canvas + a world-anchored nameplate child (100x20 centered on the anchor).
    Entity canvas = scene.CreateEntity("Canvas");
    canvas.AddComponent<CanvasComponent>().ScaleMode = UiScaleMode::ConstantPixel;
    Entity plate = scene.CreateEntity("Nameplate");
    auto& rt = plate.AddComponent<RectTransformComponent>();
    rt.AnchorMin = rt.AnchorMax = { 0, 0 };
    rt.OffsetMin = { -50, -10 };
    rt.OffsetMax = {  50,  10 };
    plate.AddComponent<UiImageComponent>();
    auto& anchor = plate.AddComponent<UiWorldAnchorComponent>();
    anchor.TargetEntity = worldId;
    scene.SetParent(plate, canvas, /*keepWorldPose=*/false);

    std::vector<UiElement> out;
    UiSystem::CollectElements(scene, viewport, out, &vp);
    REQUIRE(out.size() == 1);
    // Origin projects to (400,300); the 100x20 box centers there.
    CHECK(Near(out[0].Rect.Center().x, 400.0f));
    CHECK(Near(out[0].Rect.Center().y, 300.0f));
    CHECK(Near(out[0].Rect.Width(), 100.0f));

    // Move the tracked entity behind the camera -> the nameplate is hidden.
    world.GetComponent<TransformComponent>().Position = { 0, 0, 10 };
    UiSystem::CollectElements(scene, viewport, out, &vp);
    CHECK(out.empty());
}
