// test_events.cpp — event value types. Headless (no GL, no window).
//
// T3 (gap §14.5): WindowFileDropEvent carries the dropped paths through the
// generic Event dispatch. The GLFW glfwSetDropCallback plumbing in Window.cpp
// is exercised on-GPU (a manual drop → Starforge Console via T8); here we prove
// the event value type + dispatch are correct and headless-safe.

#include <doctest.h>

#include "events/ApplicationEvent.h"
#include "events/Event.h"

#include <string>
#include <vector>

using namespace Cosmic;

TEST_CASE("T3: WindowFileDropEvent carries paths with the right type/category")
{
    std::vector<std::string> paths = {
        "C:/models/rover.obj",
        "C:/textures/rust.png",
    };
    WindowFileDropEvent e(paths);

    CHECK(e.GetEventType() == EventType::WindowFileDrop);
    CHECK(WindowFileDropEvent::GetStaticType() == EventType::WindowFileDrop);
    CHECK(e.IsInCategory(EventCategoryApplication));
    REQUIRE(e.GetPaths().size() == 2);
    CHECK(e.GetPaths()[0] == "C:/models/rover.obj");
    CHECK(e.GetPaths()[1] == "C:/textures/rust.png");
    CHECK(e.ToString().find("2 file(s)") != std::string::npos);
}

TEST_CASE("T3: WindowFileDropEvent dispatches through EventDispatcher to the drop handler")
{
    WindowFileDropEvent e({ "C:/a.gltf" });

    // A non-matching dispatch must not fire; the matching one must, and receive
    // the concrete event with its paths intact.
    bool droppedFired = false, resizeFired = false;
    EventDispatcher dispatcher(e);

    dispatcher.Dispatch<WindowResizeEvent>([&](WindowResizeEvent&) { resizeFired = true; return true; });
    dispatcher.Dispatch<WindowFileDropEvent>([&](WindowFileDropEvent& drop)
    {
        droppedFired = true;
        CHECK(drop.GetPaths().size() == 1);
        CHECK(drop.GetPaths()[0] == "C:/a.gltf");
        return true;
    });

    CHECK_FALSE(resizeFired);
    CHECK(droppedFired);
    CHECK(e.Handled);   // the matching handler returned true
}

// WO-07 L04 (2D stability, KI-32): a listener removed from INSIDE a dispatch must not
// fire in that same dispatch (the EventBus rule). Listener A unsubscribes B; the
// dispatch that A runs in must not reach B. Headless twin of the L04 host probe.
#include "telemetry/EntitySelection.h"

TEST_CASE("WO-07 L04: EntitySelection does not invoke a listener unsubscribed during dispatch")
{
    using Cosmic::EntitySelection;
    int aFired = 0, bFired = 0, cFired = 0;
    EntitySelection::SubscriptionHandle b = 0;
    auto a = EntitySelection::OnChanged([&](const std::string&, const std::string&)
    {
        ++aFired;
        if (b) { EntitySelection::Unsubscribe(b); b = 0; }   // remove B mid-dispatch
    });
    b = EntitySelection::OnChanged([&](const std::string&, const std::string&) { ++bFired; });
    auto c = EntitySelection::OnChanged([&](const std::string&, const std::string&) { ++cFired; });

    EntitySelection::SetByName("wo07-l04-probe");
    CHECK(aFired == 1);
    CHECK(bFired == 0);   // removed before its turn: must not fire
    CHECK(cFired == 1);   // later listeners still fire (non-vacuous)

    EntitySelection::SetByName("wo07-l04-probe-2");
    CHECK(aFired == 2);
    CHECK(bFired == 0);
    CHECK(cFired == 2);

    EntitySelection::Unsubscribe(a);
    EntitySelection::Unsubscribe(c);
    EntitySelection::Clear();
}
