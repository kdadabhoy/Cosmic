// test_ui_widgets.cpp — App Platform AP-02, acceptance V04 (bound widgets, headless).
//
// The pure helpers (FormatValue / GaugeFill / SliderValueAt), the UiSystem::Update
// state machine for sliders and toggles against a real DataBus and the scene
// EventBus, CollectHostedPanels (the engine half of V05), a SceneSerializer
// round-trip of a scene holding all seven widget components (field-exact), and
// the reflection registry listing the seven reflected names under "UI". Every
// case drives the production path (UiSystem::Update over a Scene) — no private
// state is poked to force a transition.

#include <doctest.h>

#include "data/DataBus.h"
#include "reflect/TypeRegistry.h"
#include "scene/Components.h"
#include "scene/Entity.h"
#include "scene/EventBus.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"
#include "scene/ui/UiComponents.h"
#include "scene/ui/UiSystem.h"

#include <glm/glm.hpp>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace Cosmic;

namespace
{
    const UiRect kViewport{ { 0.0f, 0.0f }, { 800.0f, 600.0f } };

    Entity MakeCanvas(Scene& s)
    {
        Entity canvas = s.CreateEntity("Canvas");
        canvas.AddComponent<CanvasComponent>().ScaleMode = UiScaleMode::ConstantPixel;   // scale 1: literal rects
        return canvas;
    }

    Entity MakeElement(Scene& s, Entity parent, const char* name, glm::vec2 offMin, glm::vec2 offMax, int32_t z = 0)
    {
        Entity e = s.CreateEntity(name);
        auto& rt = e.AddComponent<RectTransformComponent>();
        rt.AnchorMin = rt.AnchorMax = { 0.0f, 0.0f };
        rt.OffsetMin = offMin;
        rt.OffsetMax = offMax;
        rt.ZOrder = z;
        s.SetParent(e, parent, /*keepWorldPose=*/false);
        return e;
    }

    UiPointer Press(glm::vec2 at)   { UiPointer p; p.Position = at; p.Down = true;  p.PressedEdge = true;  return p; }
    UiPointer Hold(glm::vec2 at)    { UiPointer p; p.Position = at; p.Down = true;                         return p; }
    UiPointer Release(glm::vec2 at) { UiPointer p; p.Position = at; p.Down = false; p.ReleasedEdge = true; return p; }
    UiPointer Idle(glm::vec2 at)    { UiPointer p; p.Position = at;                                        return p; }

    std::string Fmt(const char* format, const DataValue* v, const char* prefix = "", const char* suffix = "", bool stale = false)
    {
        UiValueTextComponent c;
        c.Format = format;
        c.Prefix = prefix;
        c.Suffix = suffix;
        return UiSystem::FormatValue(c, v, stale);
    }
}

TEST_SUITE("AP-02 V04 widgets")
{
    // ------------------------------------------------------------------------
    // FormatValue
    // ------------------------------------------------------------------------
    TEST_CASE("V04 FormatValue: one conversion / none / two (literal) / bool / string / missing / stale / non-finite / prefix+suffix")
    {
        const DataValue n = DataValue::MakeNumber(3.14159);
        CHECK(Fmt("%.2f", &n) == "3.14");
        CHECK(Fmt("%5.1f", &n) == "  3.1");
        CHECK(Fmt("%g", &n) == "3.14159");
        CHECK(Fmt("%.0f rpm", &n) == "3 rpm");
        CHECK(Fmt("%d", &n) == "3");                            // integer conversion: truncated, not UB
        CHECK(Fmt("%04d", &n) == "0003");
        const DataValue v255 = DataValue::MakeNumber(255.0), v1500 = DataValue::MakeNumber(1500.0);
        CHECK(Fmt("%x", &v255) == "ff");
        CHECK(Fmt("%e", &v1500) == "1.500000e+03");
        CHECK(Fmt("100%% at %.1f", &n) == "100% at 3.1");      // %% is not a conversion

        // No conversion, two conversions, a non-numeric conversion, a dangling '%':
        // the format is used LITERALLY (never passed to printf with a bogus arg list).
        CHECK(Fmt("literal", &n) == "literal");
        CHECK(Fmt("%.1f / %.1f", &n) == "%.1f / %.1f");
        CHECK(Fmt("%s", &n) == "%s");
        CHECK(Fmt("%", &n) == "%");
        CHECK(Fmt("%*d", &n) == "%*d");

        const DataValue t = DataValue::MakeBool(true), f = DataValue::MakeBool(false);
        CHECK(Fmt("%.2f", &t) == "true");
        CHECK(Fmt("%.2f", &f) == "false");

        const DataValue str = DataValue::MakeString("armed");
        CHECK(Fmt("%.2f", &str) == "armed");

        // Missing channel -> Placeholder (wrapped in Prefix/Suffix like a value).
        UiValueTextComponent c;
        c.Prefix = "T: "; c.Suffix = " C"; c.Placeholder = "--";
        CHECK(UiSystem::FormatValue(c, nullptr, false) == "T: -- C");
        c.Placeholder = "n/a";
        CHECK(UiSystem::FormatValue(c, nullptr, false) == "T: n/a C");

        // Stale never changes the text (the colour carries staleness).
        CHECK(Fmt("%.2f", &n, "", "", true) == Fmt("%.2f", &n, "", "", false));
        CHECK(Fmt("%.2f", nullptr, "", "", true) == "--");

        // Non-finite numbers show the placeholder (channels are data, §1).
        const DataValue nan = DataValue::MakeNumber(std::numeric_limits<double>::quiet_NaN());
        const DataValue inf = DataValue::MakeNumber(std::numeric_limits<double>::infinity());
        CHECK(Fmt("%.2f", &nan) == "--");
        CHECK(Fmt("%.2f", &inf) == "--");

        // Prefix/Suffix wrap every kind.
        CHECK(Fmt("%.1f", &n, "v=", "V") == "v=3.1V");
        CHECK(Fmt("%.1f", &t, "[", "]") == "[true]");
        CHECK(Fmt("%.1f", &str, "<", ">") == "<armed>");
    }

    // ------------------------------------------------------------------------
    // GaugeFill
    // ------------------------------------------------------------------------
    TEST_CASE("V04 GaugeFill: linear, clamped both ends, reversed range, degenerate range, non-finite -> 0")
    {
        CHECK(UiSystem::GaugeFill(0.0f, 100.0f, 50.0) == doctest::Approx(0.5f));
        CHECK(UiSystem::GaugeFill(0.0f, 100.0f, 0.0) == 0.0f);
        CHECK(UiSystem::GaugeFill(0.0f, 100.0f, 100.0) == 1.0f);
        CHECK(UiSystem::GaugeFill(0.0f, 100.0f, -5.0) == 0.0f);
        CHECK(UiSystem::GaugeFill(0.0f, 100.0f, 250.0) == 1.0f);
        CHECK(UiSystem::GaugeFill(-1.0f, 1.0f, 0.0) == doctest::Approx(0.5f));
        CHECK(UiSystem::GaugeFill(100.0f, 0.0f, 25.0) == doctest::Approx(0.75f));   // reversed range
        CHECK(UiSystem::GaugeFill(5.0f, 5.0f, 5.0) == 1.0f);                        // degenerate: at/above -> 1
        CHECK(UiSystem::GaugeFill(5.0f, 5.0f, 4.0) == 0.0f);
        CHECK(UiSystem::GaugeFill(0.0f, 100.0f, std::numeric_limits<double>::quiet_NaN()) == 0.0f);
        CHECK(UiSystem::GaugeFill(0.0f, 100.0f, std::numeric_limits<double>::infinity()) == 0.0f);
        CHECK(UiSystem::GaugeFill(0.0f, 100.0f, -std::numeric_limits<double>::infinity()) == 0.0f);
    }

    // ------------------------------------------------------------------------
    // SliderValueAt
    // ------------------------------------------------------------------------
    TEST_CASE("V04 SliderValueAt: horizontal / vertical, Step snapping, out-of-rect clamps, degenerate rect")
    {
        const UiRect r{ { 100.0f, 200.0f }, { 300.0f, 240.0f } };   // 200 wide, 40 tall

        // Horizontal: left = min, right = max.
        CHECK(UiSystem::SliderValueAt(r, UiSliderOrientation::Horizontal, { 100.0f, 220.0f }, 0.0f, 1.0f, 0.0f) == doctest::Approx(0.0));
        CHECK(UiSystem::SliderValueAt(r, UiSliderOrientation::Horizontal, { 200.0f, 220.0f }, 0.0f, 1.0f, 0.0f) == doctest::Approx(0.5));
        CHECK(UiSystem::SliderValueAt(r, UiSliderOrientation::Horizontal, { 300.0f, 220.0f }, 0.0f, 1.0f, 0.0f) == doctest::Approx(1.0));
        CHECK(UiSystem::SliderValueAt(r, UiSliderOrientation::Horizontal, { 150.0f, 220.0f }, 10.0f, 30.0f, 0.0f) == doctest::Approx(15.0));

        // Vertical: bottom = min, top = max.
        CHECK(UiSystem::SliderValueAt(r, UiSliderOrientation::Vertical, { 200.0f, 240.0f }, 0.0f, 1.0f, 0.0f) == doctest::Approx(0.0));
        CHECK(UiSystem::SliderValueAt(r, UiSliderOrientation::Vertical, { 200.0f, 220.0f }, 0.0f, 1.0f, 0.0f) == doctest::Approx(0.5));
        CHECK(UiSystem::SliderValueAt(r, UiSliderOrientation::Vertical, { 200.0f, 200.0f }, 0.0f, 1.0f, 0.0f) == doctest::Approx(1.0));
        CHECK(UiSystem::SliderValueAt(r, UiSliderOrientation::Vertical, { 200.0f, 230.0f }, -10.0f, 10.0f, 0.0f) == doctest::Approx(-5.0));

        // Step snapping (nearest multiple from Min), result stays in range.
        CHECK(UiSystem::SliderValueAt(r, UiSliderOrientation::Horizontal, { 100.0f + 200.0f * 0.33f, 220.0f }, 0.0f, 1.0f, 0.25f) == doctest::Approx(0.25));
        CHECK(UiSystem::SliderValueAt(r, UiSliderOrientation::Horizontal, { 100.0f + 200.0f * 0.40f, 220.0f }, 0.0f, 1.0f, 0.25f) == doctest::Approx(0.5));
        CHECK(UiSystem::SliderValueAt(r, UiSliderOrientation::Horizontal, { 100.0f + 200.0f * 0.44f, 220.0f }, 0.0f, 100.0f, 10.0f) == doctest::Approx(40.0));
        CHECK(UiSystem::SliderValueAt(r, UiSliderOrientation::Horizontal, { 100.0f + 200.0f * 0.99f, 220.0f }, 0.0f, 1.0f, 0.3f) == doctest::Approx(0.9));   // 3 steps; 1.2 would leave the range
        CHECK(UiSystem::SliderValueAt(r, UiSliderOrientation::Horizontal, { 100.0f + 200.0f * 0.7f, 220.0f }, 1.0f, 2.0f, 0.5f) == doctest::Approx(1.5));
        CHECK(UiSystem::SliderValueAt(r, UiSliderOrientation::Horizontal, { 100.0f + 200.0f * 0.9f, 220.0f }, 1.0f, 2.0f, 0.5f) == doctest::Approx(2.0));

        // Out of rect: clamped to the ends, on both axes.
        CHECK(UiSystem::SliderValueAt(r, UiSliderOrientation::Horizontal, { -50.0f, 220.0f }, 0.0f, 1.0f, 0.0f) == doctest::Approx(0.0));
        CHECK(UiSystem::SliderValueAt(r, UiSliderOrientation::Horizontal, { 900.0f, 220.0f }, 0.0f, 1.0f, 0.0f) == doctest::Approx(1.0));
        CHECK(UiSystem::SliderValueAt(r, UiSliderOrientation::Horizontal, { 200.0f, -500.0f }, 0.0f, 1.0f, 0.0f) == doctest::Approx(0.5));   // off-axis distance is irrelevant
        CHECK(UiSystem::SliderValueAt(r, UiSliderOrientation::Vertical,   { 200.0f, -50.0f }, 0.0f, 1.0f, 0.0f) == doctest::Approx(1.0));
        CHECK(UiSystem::SliderValueAt(r, UiSliderOrientation::Vertical,   { 200.0f, 900.0f }, 0.0f, 1.0f, 0.0f) == doctest::Approx(0.0));

        // Reversed Min/Max still clamps within the two.
        CHECK(UiSystem::SliderValueAt(r, UiSliderOrientation::Horizontal, { 900.0f, 220.0f }, 1.0f, 0.0f, 0.0f) == doctest::Approx(0.0));
        CHECK(UiSystem::SliderValueAt(r, UiSliderOrientation::Horizontal, { 100.0f, 220.0f }, 1.0f, 0.0f, 0.0f) == doctest::Approx(1.0));

        // Degenerate (zero-size) rect: t = 0 -> Min, no division blow-up.
        const UiRect z{ { 10.0f, 10.0f }, { 10.0f, 10.0f } };
        const double dz = UiSystem::SliderValueAt(z, UiSliderOrientation::Horizontal, { 10.0f, 10.0f }, 2.0f, 5.0f, 0.0f);
        CHECK(std::isfinite(dz));
        CHECK(dz == doctest::Approx(2.0));
    }

    // ------------------------------------------------------------------------
    // Update — slider
    // ------------------------------------------------------------------------
    TEST_CASE("V04 Update slider: press-drag-release writes the bus every frame; Signal only on release and only when changed; no double-emit")
    {
        Scene s;
        Entity canvas = MakeCanvas(s);
        Entity sliderE = MakeElement(s, canvas, "Slider", { 100.0f, 100.0f }, { 300.0f, 140.0f });
        auto& sl = sliderE.AddComponent<UiSliderComponent>();
        sl.Channel = "gain"; sl.Min = 0.0f; sl.Max = 1.0f; sl.Signal = "gain_set";

        DataBus bus;
        int fires = 0;
        entt::entity source = entt::null;
        s.Events().Connect("gain_set", [&](Entity src) { ++fires; source = (entt::entity)src; });

        // Idle over the slider: consumed (blocks picking), no write.
        CHECK(UiSystem::Update(s, kViewport, Idle({ 200.0f, 120.0f }), nullptr, &bus));
        CHECK_FALSE(bus.Has("gain"));
        CHECK_FALSE(UiSystem::Update(s, kViewport, Idle({ 50.0f, 50.0f }), nullptr, &bus));

        // Press at 25 %: arms + writes.
        CHECK(UiSystem::Update(s, kViewport, Press({ 150.0f, 120.0f }), nullptr, &bus));
        REQUIRE(bus.Has("gain"));
        CHECK(bus.GetNumber("gain") == doctest::Approx(0.25));
        CHECK(sl.Dragging);
        CHECK(fires == 0);
        std::vector<DataSample> hist;
        CHECK(bus.History("gain", hist) == 1);

        // Drag to 50 %, then 75 %: a write EVERY frame (history grows per Update), even when still.
        UiSystem::Update(s, kViewport, Hold({ 200.0f, 120.0f }), nullptr, &bus);
        CHECK(bus.GetNumber("gain") == doctest::Approx(0.5));
        UiSystem::Update(s, kViewport, Hold({ 250.0f, 120.0f }), nullptr, &bus);
        CHECK(bus.GetNumber("gain") == doctest::Approx(0.75));
        UiSystem::Update(s, kViewport, Hold({ 250.0f, 120.0f }), nullptr, &bus);
        CHECK(bus.History("gain", hist) == 4);
        CHECK(fires == 0);

        // Drag OUTSIDE the rect keeps steering (clamped) — the drag owns the pointer.
        CHECK(UiSystem::Update(s, kViewport, Hold({ 700.0f, 500.0f }), nullptr, &bus));
        CHECK(bus.GetNumber("gain") == doctest::Approx(1.0));

        // Release outside: final write, drag ends, ONE signal with the entity as source.
        UiSystem::Update(s, kViewport, Release({ 700.0f, 500.0f }), nullptr, &bus);
        CHECK_FALSE(sl.Dragging);
        CHECK(fires == 1);
        CHECK(source == (entt::entity)sliderE);

        // A second Update with the same release edge does not double-emit or write.
        const size_t histBefore = bus.History("gain", hist);
        UiSystem::Update(s, kViewport, Release({ 700.0f, 500.0f }), nullptr, &bus);
        CHECK(fires == 1);
        CHECK(bus.History("gain", hist) == histBefore);

        // Press + release at the SAME value the channel already holds: writes, but no signal.
        bus.Set("gain", 0.5);
        UiSystem::Update(s, kViewport, Press({ 200.0f, 120.0f }), nullptr, &bus);
        UiSystem::Update(s, kViewport, Release({ 200.0f, 120.0f }), nullptr, &bus);
        CHECK(bus.GetNumber("gain") == doctest::Approx(0.5));
        CHECK(fires == 1);

        // Two press edges in a row do not re-arm anything observable; a move + release emits once.
        UiSystem::Update(s, kViewport, Press({ 200.0f, 120.0f }), nullptr, &bus);
        UiSystem::Update(s, kViewport, Press({ 200.0f, 120.0f }), nullptr, &bus);
        UiSystem::Update(s, kViewport, Release({ 300.0f, 120.0f }), nullptr, &bus);
        CHECK(bus.GetNumber("gain") == doctest::Approx(1.0));
        CHECK(fires == 2);

        // Not dragging: the app's own write is what the slider would show (nothing overwritten).
        bus.Set("gain", 0.1);
        UiSystem::Update(s, kViewport, Idle({ 200.0f, 120.0f }), nullptr, &bus);
        CHECK(bus.GetNumber("gain") == doctest::Approx(0.1));

        // Step snapping through the production path.
        sl.Step = 0.25f;
        UiSystem::Update(s, kViewport, Press({ 100.0f + 200.0f * 0.6f, 120.0f }), nullptr, &bus);
        CHECK(bus.GetNumber("gain") == doctest::Approx(0.5));
        UiSystem::Update(s, kViewport, Release({ 100.0f + 200.0f * 0.6f, 120.0f }), nullptr, &bus);
        CHECK(fires == 3);

        // Empty Signal: value changes, nothing emitted.
        sl.Signal.clear();
        UiSystem::Update(s, kViewport, Press({ 100.0f, 120.0f }), nullptr, &bus);
        UiSystem::Update(s, kViewport, Release({ 100.0f, 120.0f }), nullptr, &bus);
        CHECK(bus.GetNumber("gain") == doctest::Approx(0.0));
        CHECK(fires == 3);
    }

    TEST_CASE("V04 Update slider: vertical orientation, a press that begins outside never drags, no bus => hit-tested but inert")
    {
        Scene s;
        Entity canvas = MakeCanvas(s);
        Entity sliderE = MakeElement(s, canvas, "VSlider", { 100.0f, 100.0f }, { 140.0f, 300.0f });
        auto& sl = sliderE.AddComponent<UiSliderComponent>();
        sl.Channel = "level"; sl.Min = 0.0f; sl.Max = 10.0f; sl.Orientation = UiSliderOrientation::Vertical; sl.Signal = "level_set";
        DataBus bus;
        int fires = 0;
        s.Events().Connect("level_set", [&](Entity) { ++fires; });

        // Press outside, drag into the rect, release inside: nothing.
        CHECK_FALSE(UiSystem::Update(s, kViewport, Press({ 50.0f, 50.0f }), nullptr, &bus));
        UiSystem::Update(s, kViewport, Hold({ 120.0f, 200.0f }), nullptr, &bus);
        UiSystem::Update(s, kViewport, Release({ 120.0f, 200.0f }), nullptr, &bus);
        CHECK_FALSE(bus.Has("level"));
        CHECK(fires == 0);

        // Press at the bottom quarter: 2.5 (bottom = Min).
        UiSystem::Update(s, kViewport, Press({ 120.0f, 250.0f }), nullptr, &bus);
        CHECK(bus.GetNumber("level") == doctest::Approx(2.5));
        UiSystem::Update(s, kViewport, Hold({ 120.0f, 100.0f }), nullptr, &bus);
        CHECK(bus.GetNumber("level") == doctest::Approx(10.0));
        UiSystem::Update(s, kViewport, Release({ 120.0f, 100.0f }), nullptr, &bus);
        CHECK(fires == 1);

        // Pointer released off-window (Down false, no edge) ends the drag too, emitting once.
        UiSystem::Update(s, kViewport, Press({ 120.0f, 300.0f }), nullptr, &bus);
        CHECK(sl.Dragging);
        UiSystem::Update(s, kViewport, Idle({ 120.0f, 300.0f }), nullptr, &bus);
        CHECK_FALSE(sl.Dragging);
        CHECK(fires == 2);
        CHECK(bus.GetNumber("level") == doctest::Approx(0.0));

        // No bus: the slider still consumes the pointer, but does not drag or emit.
        CHECK(UiSystem::Update(s, kViewport, Press({ 120.0f, 200.0f })));
        CHECK_FALSE(sl.Dragging);
        UiSystem::Update(s, kViewport, Release({ 120.0f, 200.0f }));
        CHECK(fires == 2);
        CHECK(bus.GetNumber("level") == doctest::Approx(0.0));
    }

    // ------------------------------------------------------------------------
    // Update — toggle
    // ------------------------------------------------------------------------
    TEST_CASE("V04 Update toggle: release-inside flips the bool and emits once; release-outside does not; no double-emit; Interactable=false removes it")
    {
        Scene s;
        Entity canvas = MakeCanvas(s);
        Entity toggleE = MakeElement(s, canvas, "Toggle", { 100.0f, 100.0f }, { 160.0f, 130.0f });
        toggleE.AddComponent<UiImageComponent>();
        auto& tg = toggleE.AddComponent<UiToggleComponent>();
        tg.Channel = "armed"; tg.Signal = "armed_toggled";
        DataBus bus;
        int fires = 0;
        entt::entity source = entt::null;
        s.Events().Connect("armed_toggled", [&](Entity src) { ++fires; source = (entt::entity)src; });

        // Missing channel reads false -> first flip writes true.
        CHECK(UiSystem::Update(s, kViewport, Press({ 130.0f, 115.0f }), nullptr, &bus));
        CHECK_FALSE(bus.Has("armed"));
        CHECK(tg.Armed);
        UiSystem::Update(s, kViewport, Release({ 130.0f, 115.0f }), nullptr, &bus);
        REQUIRE(bus.Has("armed"));
        CHECK(bus.GetBool("armed") == true);
        CHECK(bus.Get("armed").ValueKind == DataValue::Kind::Bool);
        CHECK(fires == 1);
        CHECK(source == (entt::entity)toggleE);
        CHECK_FALSE(tg.Armed);

        // Same release edge again: nothing.
        UiSystem::Update(s, kViewport, Release({ 130.0f, 115.0f }), nullptr, &bus);
        CHECK(bus.GetBool("armed") == true);
        CHECK(fires == 1);

        // Flip back.
        UiSystem::Update(s, kViewport, Press({ 130.0f, 115.0f }), nullptr, &bus);
        UiSystem::Update(s, kViewport, Release({ 130.0f, 115.0f }), nullptr, &bus);
        CHECK(bus.GetBool("armed") == false);
        CHECK(fires == 2);

        // Press inside, release outside: no flip, no signal.
        UiSystem::Update(s, kViewport, Press({ 130.0f, 115.0f }), nullptr, &bus);
        UiSystem::Update(s, kViewport, Hold({ 400.0f, 400.0f }), nullptr, &bus);
        UiSystem::Update(s, kViewport, Release({ 400.0f, 400.0f }), nullptr, &bus);
        CHECK(bus.GetBool("armed") == false);
        CHECK(fires == 2);

        // Press outside, release inside: no flip.
        UiSystem::Update(s, kViewport, Press({ 400.0f, 400.0f }), nullptr, &bus);
        UiSystem::Update(s, kViewport, Release({ 130.0f, 115.0f }), nullptr, &bus);
        CHECK(bus.GetBool("armed") == false);
        CHECK(fires == 2);

        // Drag off and back still fires (a press that began inside).
        UiSystem::Update(s, kViewport, Press({ 130.0f, 115.0f }), nullptr, &bus);
        UiSystem::Update(s, kViewport, Hold({ 400.0f, 400.0f }), nullptr, &bus);
        UiSystem::Update(s, kViewport, Hold({ 130.0f, 115.0f }), nullptr, &bus);
        UiSystem::Update(s, kViewport, Release({ 130.0f, 115.0f }), nullptr, &bus);
        CHECK(bus.GetBool("armed") == true);
        CHECK(fires == 3);

        // The app's own write is respected: flip from an app-set true goes to false.
        bus.SetBool("armed", true);
        UiSystem::Update(s, kViewport, Press({ 130.0f, 115.0f }), nullptr, &bus);
        UiSystem::Update(s, kViewport, Release({ 130.0f, 115.0f }), nullptr, &bus);
        CHECK(bus.GetBool("armed") == false);
        CHECK(fires == 4);

        // Interactable = false: not hit, never flips, never emits.
        tg.Interactable = false;
        CHECK_FALSE(UiSystem::Update(s, kViewport, Press({ 130.0f, 115.0f }), nullptr, &bus));
        UiSystem::Update(s, kViewport, Release({ 130.0f, 115.0f }), nullptr, &bus);
        CHECK(bus.GetBool("armed") == false);
        CHECK(fires == 4);
        tg.Interactable = true;

        // No bus: consumed, but nothing to flip and no signal.
        CHECK(UiSystem::Update(s, kViewport, Press({ 130.0f, 115.0f })));
        UiSystem::Update(s, kViewport, Release({ 130.0f, 115.0f }));
        CHECK(fires == 4);

        // Empty Signal: the flip still lands on the bus, nothing emitted.
        tg.Signal.clear();
        UiSystem::Update(s, kViewport, Press({ 130.0f, 115.0f }), nullptr, &bus);
        UiSystem::Update(s, kViewport, Release({ 130.0f, 115.0f }), nullptr, &bus);
        CHECK(bus.GetBool("armed") == true);
        CHECK(fires == 4);
    }

    // ------------------------------------------------------------------------
    // Update — precedence + the existing button
    // ------------------------------------------------------------------------
    TEST_CASE("V04 Update precedence: a UiImage over a slider does not block it; Interactable=false removes a slider; topmost of button/slider/toggle wins; Update returns true over any of them")
    {
        Scene s;
        Entity canvas = MakeCanvas(s);
        DataBus bus;

        // Slider with a plain image ON TOP (higher ZOrder): the image is not interactive.
        Entity sliderE = MakeElement(s, canvas, "Slider", { 100.0f, 100.0f }, { 300.0f, 140.0f }, 0);
        auto& sl = sliderE.AddComponent<UiSliderComponent>();
        sl.Channel = "a";
        Entity overlay = MakeElement(s, canvas, "Overlay", { 0.0f, 0.0f }, { 800.0f, 600.0f }, 10);
        overlay.AddComponent<UiImageComponent>();

        CHECK(UiSystem::Update(s, kViewport, Press({ 200.0f, 120.0f }), nullptr, &bus));
        CHECK(bus.GetNumber("a") == doctest::Approx(0.5));
        UiSystem::Update(s, kViewport, Release({ 200.0f, 120.0f }), nullptr, &bus);

        // Interactable = false: the slider is no longer hit and never writes.
        sl.Interactable = false;
        bus.Remove("a");
        CHECK_FALSE(UiSystem::Update(s, kViewport, Press({ 200.0f, 120.0f }), nullptr, &bus));
        UiSystem::Update(s, kViewport, Release({ 200.0f, 120.0f }), nullptr, &bus);
        CHECK_FALSE(bus.Has("a"));
        CHECK_FALSE(sl.Dragging);
        sl.Interactable = true;

        // A button UNDER the slider (lower ZOrder) never sees the click; a toggle ABOVE takes it.
        Entity buttonE = MakeElement(s, canvas, "Button", { 100.0f, 100.0f }, { 300.0f, 140.0f }, -5);
        buttonE.AddComponent<UiImageComponent>();
        buttonE.AddComponent<UiButtonComponent>().Signal = "btn";
        int btnFires = 0, tgFires = 0;
        s.Events().Connect("btn", [&](Entity) { ++btnFires; });
        s.Events().Connect("tgl", [&](Entity) { ++tgFires; });

        UiSystem::Update(s, kViewport, Press({ 200.0f, 120.0f }), nullptr, &bus);
        UiSystem::Update(s, kViewport, Release({ 200.0f, 120.0f }), nullptr, &bus);
        CHECK(btnFires == 0);
        CHECK(bus.GetNumber("a") == doctest::Approx(0.5));
        CHECK(buttonE.GetComponent<UiButtonComponent>().State == UiButtonState::Normal);   // not even hovered

        Entity toggleE = MakeElement(s, canvas, "Toggle", { 150.0f, 100.0f }, { 250.0f, 140.0f }, 20);
        toggleE.AddComponent<UiImageComponent>();
        auto& tg = toggleE.AddComponent<UiToggleComponent>();
        tg.Channel = "t"; tg.Signal = "tgl";
        bus.Remove("a");
        UiSystem::Update(s, kViewport, Press({ 200.0f, 120.0f }), nullptr, &bus);
        UiSystem::Update(s, kViewport, Release({ 200.0f, 120.0f }), nullptr, &bus);
        CHECK(tgFires == 1);
        CHECK(bus.GetBool("t") == true);
        CHECK_FALSE(bus.Has("a"));            // the slider under the toggle was not steered
        CHECK(btnFires == 0);

        // Beside the toggle, the slider wins again; the button below is still shielded.
        UiSystem::Update(s, kViewport, Press({ 110.0f, 120.0f }), nullptr, &bus);
        UiSystem::Update(s, kViewport, Release({ 110.0f, 120.0f }), nullptr, &bus);
        CHECK(bus.Has("a"));
        CHECK(btnFires == 0);
        CHECK(tgFires == 1);

        // A button on top of everything is the one that fires (existing behaviour, front-most wins).
        Entity topBtn = MakeElement(s, canvas, "TopButton", { 100.0f, 100.0f }, { 300.0f, 140.0f }, 30);
        topBtn.AddComponent<UiImageComponent>();
        topBtn.AddComponent<UiButtonComponent>().Signal = "btn";
        bus.Remove("a");
        CHECK(UiSystem::Update(s, kViewport, Press({ 200.0f, 120.0f }), nullptr, &bus));
        UiSystem::Update(s, kViewport, Release({ 200.0f, 120.0f }), nullptr, &bus);
        CHECK(btnFires == 1);
        CHECK(tgFires == 1);
        CHECK_FALSE(bus.Has("a"));

        // Update returns true over every interactive kind and false over none / plain images.
        CHECK(UiSystem::Update(s, kViewport, Idle({ 200.0f, 120.0f }), nullptr, &bus));   // top button
        CHECK(UiSystem::Update(s, kViewport, Idle({ 105.0f, 138.0f }), nullptr, &bus));   // slider (edge, under top button too)
        CHECK_FALSE(UiSystem::Update(s, kViewport, Idle({ 600.0f, 500.0f }), nullptr, &bus));   // overlay image only
        CHECK_FALSE(UiSystem::Update(s, kViewport, Idle({ 900.0f, 900.0f }), nullptr, &bus));   // nothing
    }

    TEST_CASE("V04 Update: the existing UiButton machine is unchanged with a bus present (hover/press/release-inside emits once; release-outside does not)")
    {
        Scene s;
        Entity canvas = MakeCanvas(s);
        Entity b = MakeElement(s, canvas, "Button", { 0.0f, 0.0f }, { 200.0f, 80.0f });
        b.AddComponent<UiImageComponent>();
        b.AddComponent<UiButtonComponent>().Signal = "play";
        auto& btn = b.GetComponent<UiButtonComponent>();
        DataBus bus;
        int fires = 0;
        s.Events().Connect("play", [&](Entity) { ++fires; });

        CHECK(UiSystem::Update(s, kViewport, Idle({ 100.0f, 40.0f }), nullptr, &bus));
        CHECK(btn.State == UiButtonState::Hover);
        UiSystem::Update(s, kViewport, Press({ 100.0f, 40.0f }), nullptr, &bus);
        CHECK(btn.State == UiButtonState::Pressed);
        CHECK(btn.Armed);
        UiSystem::Update(s, kViewport, Hold({ 100.0f, 40.0f }), nullptr, &bus);
        CHECK(fires == 0);
        UiSystem::Update(s, kViewport, Release({ 100.0f, 40.0f }), nullptr, &bus);
        CHECK(fires == 1);
        CHECK_FALSE(btn.Armed);
        UiSystem::Update(s, kViewport, Press({ 100.0f, 40.0f }), nullptr, &bus);
        UiSystem::Update(s, kViewport, Release({ 500.0f, 500.0f }), nullptr, &bus);
        CHECK(fires == 1);
        CHECK(btn.State == UiButtonState::Normal);
        CHECK(bus.ChannelCount() == 0);       // a button never touches the bus
    }

    // ------------------------------------------------------------------------
    // CollectHostedPanels (V05 engine half)
    // ------------------------------------------------------------------------
    TEST_CASE("V05 CollectHostedPanels: resolved rects in canvas (back-to-front) order with names, handles and the canvas scale; inactive-canvas and non-panel elements excluded; DrawnThisFrame cleared")
    {
        Scene s;
        Entity canvas = s.CreateEntity("Canvas");
        auto& c = canvas.AddComponent<CanvasComponent>();
        c.ScaleMode = UiScaleMode::ScaleWithHeight;
        c.ReferenceHeight = 300.0f;                 // viewport 600 tall => scale 2

        Entity front = MakeElement(s, canvas, "Front", { 10.0f, 10.0f }, { 110.0f, 60.0f }, 5);
        front.AddComponent<UiHostedPanelComponent>().PanelName = "front";
        Entity back = MakeElement(s, canvas, "Back", { 0.0f, 0.0f }, { 50.0f, 50.0f }, 0);
        back.AddComponent<UiHostedPanelComponent>().PanelName = "back";
        Entity image = MakeElement(s, canvas, "Image", { 0.0f, 0.0f }, { 20.0f, 20.0f }, 100);
        image.AddComponent<UiImageComponent>();
        // A panel under an INACTIVE canvas (T13: inactive canvas or ancestor => skipped).
        Entity hiddenCanvas = s.CreateEntity("HiddenCanvas");
        hiddenCanvas.AddComponent<CanvasComponent>().SortOrder = 50;
        hiddenCanvas.GetComponent<TagComponent>().Active = false;
        Entity hidden = MakeElement(s, hiddenCanvas, "Hidden", { 0.0f, 0.0f }, { 20.0f, 20.0f }, 200);
        hidden.AddComponent<UiHostedPanelComponent>().PanelName = "hidden";

        front.GetComponent<UiHostedPanelComponent>().DrawnThisFrame = true;   // a stale report from "last frame"

        std::vector<UiHostedPanelDraw> out;
        out.push_back({});                                                     // must be cleared
        UiSystem::CollectHostedPanels(s, kViewport, out);
        REQUIRE(out.size() == 2);
        CHECK(out[0].Name == "back");
        CHECK(out[0].Handle == (uint32_t)(entt::entity)back);
        CHECK(out[0].Rect.Min.x == doctest::Approx(0.0f));
        CHECK(out[0].Rect.Max.x == doctest::Approx(100.0f));                    // 50 * scale 2
        CHECK(out[0].Rect.Max.y == doctest::Approx(100.0f));
        CHECK(out[0].Scale == doctest::Approx(2.0f));
        CHECK(out[1].Name == "front");
        CHECK(out[1].Handle == (uint32_t)(entt::entity)front);
        CHECK(out[1].Rect.Min.x == doctest::Approx(20.0f));
        CHECK(out[1].Rect.Min.y == doctest::Approx(20.0f));
        CHECK(out[1].Rect.Max.x == doctest::Approx(220.0f));
        CHECK(out[1].Rect.Max.y == doctest::Approx(120.0f));
        CHECK_FALSE(front.GetComponent<UiHostedPanelComponent>().DrawnThisFrame);

        // The rects agree with CollectElements for the same entities.
        std::vector<UiElement> els;
        UiSystem::CollectElements(s, kViewport, els);
        for (const UiHostedPanelDraw& d : out)
        {
            bool found = false;
            for (const UiElement& el : els)
                if (el.Handle == d.Handle)
                {
                    found = true;
                    CHECK(el.Rect.Min == d.Rect.Min);
                    CHECK(el.Rect.Max == d.Rect.Max);
                }
            CHECK(found);
        }

        // A scene without a canvas yields nothing.
        Scene empty;
        empty.CreateEntity("Loose").AddComponent<UiHostedPanelComponent>().PanelName = "x";
        UiSystem::CollectHostedPanels(empty, kViewport, out);
        CHECK(out.empty());
    }

    // ------------------------------------------------------------------------
    // Reflection + serialization
    // ------------------------------------------------------------------------
    TEST_CASE("V04 reflection: the seven widget names are registered under category UI with their asset-path / colour / enum hints")
    {
        auto& reg = Reflect::GetRegistry();
        const char* names[] = { "UiValueText", "UiGauge", "UiIndicator", "UiPlot", "UiSlider", "UiToggle", "UiHostedPanel" };
        for (const char* n : names)
        {
            const Reflect::TypeDescriptor* d = reg.FindByName(n);
            REQUIRE_MESSAGE(d != nullptr, n);
            CHECK_MESSAGE(d->Category == "UI", n);
        }
        CHECK(reg.Find<UiValueTextComponent>()   == reg.FindByName("UiValueText"));
        CHECK(reg.Find<UiGaugeComponent>()       == reg.FindByName("UiGauge"));
        CHECK(reg.Find<UiIndicatorComponent>()   == reg.FindByName("UiIndicator"));
        CHECK(reg.Find<UiPlotComponent>()        == reg.FindByName("UiPlot"));
        CHECK(reg.Find<UiSliderComponent>()      == reg.FindByName("UiSlider"));
        CHECK(reg.Find<UiToggleComponent>()      == reg.FindByName("UiToggle"));
        CHECK(reg.Find<UiHostedPanelComponent>() == reg.FindByName("UiHostedPanel"));

        auto field = [&](const char* type, const char* name) -> const Reflect::FieldDescriptor* {
            const Reflect::TypeDescriptor* d = reg.FindByName(type);
            if (!d) return nullptr;
            for (const auto& f : d->Fields) if (f.Name == name) return &f;
            return nullptr;
        };
        // Texture slots are asset paths of type "texture"; colours are Color; enums carry their entries.
        for (auto [type, name] : { std::pair{ "UiIndicator", "OnTexture" }, std::pair{ "UiIndicator", "OffTexture" },
                                   std::pair{ "UiToggle", "OnTexture" },    std::pair{ "UiToggle", "OffTexture" } })
        {
            const auto* f = field(type, name);
            REQUIRE_MESSAGE(f != nullptr, type << "." << name);
            CHECK(f->Kind == Reflect::FieldKind::AssetPath);
            CHECK(f->Hints.AssetType == "texture");
        }
        for (auto [type, name] : { std::pair{ "UiValueText", "StaleColor" }, std::pair{ "UiGauge", "FillColor" }, std::pair{ "UiGauge", "TrackColor" },
                                   std::pair{ "UiIndicator", "OnTint" }, std::pair{ "UiPlot", "LineColor4" }, std::pair{ "UiPlot", "BackgroundColor" },
                                   std::pair{ "UiSlider", "KnobColor" }, std::pair{ "UiToggle", "OffTint" }, std::pair{ "UiHostedPanel", "FrameColor" } })
        {
            const auto* f = field(type, name);
            REQUIRE_MESSAGE(f != nullptr, type << "." << name);
            CHECK(f->Kind == Reflect::FieldKind::Color);
        }
        const auto* style = field("UiGauge", "Style");
        REQUIRE(style);
        CHECK(style->Kind == Reflect::FieldKind::Enum);
        REQUIRE(style->Hints.EnumEntries.size() == 2);
        CHECK(style->Hints.EnumEntries[0].Name == "Bar");
        CHECK(style->Hints.EnumEntries[1].Name == "Arc");
        const auto* dir = field("UiGauge", "Direction");
        REQUIRE(dir);
        REQUIRE(dir->Hints.EnumEntries.size() == 2);
        CHECK(dir->Hints.EnumEntries[1].Name == "BottomToTop");
        const auto* orient = field("UiSlider", "Orientation");
        REQUIRE(orient);
        REQUIRE(orient->Hints.EnumEntries.size() == 2);
        CHECK(orient->Hints.EnumEntries[1].Name == "Vertical");

        // Runtime-only members are NOT reflected.
        CHECK(field("UiSlider", "Dragging") == nullptr);
        CHECK(field("UiToggle", "Armed") == nullptr);
        CHECK(field("UiHostedPanel", "DrawnThisFrame") == nullptr);
        CHECK(field("UiIndicator", "ResolvedOn") == nullptr);
    }

    TEST_CASE("V04 serialization: a scene holding all seven widgets round-trips through SceneSerializer field-exact, and save->load->save is byte-identical")
    {
        Scene scene;
        Entity canvas = MakeCanvas(scene);

        Entity vt = MakeElement(scene, canvas, "Value", { 1.0f, 2.0f }, { 3.0f, 4.0f });
        {
            auto& t = vt.AddComponent<UiTextComponent>();
            t.Text = "unchanged"; t.SizePx = 24.0f; t.HAlign = UiHAlign::Right;
            auto& v = vt.AddComponent<UiValueTextComponent>();
            v.Channel = "pend.angle"; v.Format = "%+.3f"; v.Prefix = "θ="; v.Suffix = " rad"; v.Placeholder = "n/a";
            v.StaleAfter = 1.5f; v.StaleColor = { 0.1f, 0.2f, 0.3f, 0.4f }; v.PreviewValue = 0.75f;
        }
        Entity g = MakeElement(scene, canvas, "Gauge", { 0.0f, 0.0f }, { 10.0f, 10.0f });
        {
            auto& c = g.AddComponent<UiGaugeComponent>();
            c.Channel = "speed"; c.Min = -5.0f; c.Max = 250.0f; c.Style = UiGaugeStyle::Arc; c.Direction = UiGaugeDirection::BottomToTop;
            c.FillColor = { 0.9f, 0.1f, 0.2f, 1.0f }; c.TrackColor = { 0.05f, 0.06f, 0.07f, 0.5f }; c.Thickness = 0.4f; c.PreviewValue = 77.0f;
        }
        Entity ind = MakeElement(scene, canvas, "Indicator", { 0.0f, 0.0f }, { 10.0f, 10.0f });
        {
            auto& c = ind.AddComponent<UiIndicatorComponent>();
            c.Channel = "armed"; c.Op = ">="; c.Threshold = 0.5f; c.OnTint = { 1.0f, 0.0f, 0.0f, 1.0f }; c.OffTint = { 0.0f, 0.0f, 1.0f, 0.5f };
            c.OnTexture = "textures/on.png"; c.OffTexture = "textures/off.png"; c.PreviewOn = true;
        }
        Entity pl = MakeElement(scene, canvas, "Plot", { 0.0f, 0.0f }, { 10.0f, 10.0f });
        {
            auto& c = pl.AddComponent<UiPlotComponent>();
            c.Channel = "a"; c.Channel2 = "b"; c.Channel3 = "c"; c.Channel4 = "d"; c.WindowSeconds = 4.5f; c.AutoScaleY = false;
            c.YMin = -3.0f; c.YMax = 7.0f; c.LineColor = { 1, 0, 0, 1 }; c.LineColor2 = { 0, 1, 0, 1 }; c.LineColor3 = { 0, 0, 1, 1 }; c.LineColor4 = { 1, 1, 0, 1 };
            c.GridColor = { 0.5f, 0.5f, 0.5f, 0.25f }; c.BackgroundColor = { 0.1f, 0.1f, 0.1f, 0.9f }; c.GridDivisions = 7; c.LineWidth = 3.5f;
            c.ShowLabels = false; c.PreviewAmplitude = 2.5f;
        }
        Entity sl = MakeElement(scene, canvas, "Slider", { 0.0f, 0.0f }, { 10.0f, 10.0f });
        {
            auto& c = sl.AddComponent<UiSliderComponent>();
            c.Channel = "gain"; c.Min = 0.5f; c.Max = 9.5f; c.Step = 0.25f; c.Signal = "gain_set"; c.Orientation = UiSliderOrientation::Vertical;
            c.TrackColor = { 0.2f, 0.2f, 0.2f, 1.0f }; c.FillColor = { 0.3f, 0.3f, 0.9f, 1.0f }; c.KnobColor = { 1.0f, 1.0f, 1.0f, 0.8f };
            c.KnobSize = 22.0f; c.Interactable = false; c.PreviewValue = 4.0f;
            c.Dragging = true;   // runtime-only: must NOT survive the round-trip
        }
        Entity tg = MakeElement(scene, canvas, "Toggle", { 0.0f, 0.0f }, { 10.0f, 10.0f });
        {
            tg.AddComponent<UiImageComponent>();
            auto& c = tg.AddComponent<UiToggleComponent>();
            c.Channel = "enabled"; c.Signal = "enabled_toggled"; c.OnTint = { 0.0f, 1.0f, 0.0f, 1.0f }; c.OffTint = { 0.4f, 0.4f, 0.4f, 1.0f };
            c.OnTexture = "textures/toggle_on.png"; c.OffTexture = "textures/toggle_off.png"; c.Interactable = false; c.PreviewOn = true;
            c.Armed = true;      // runtime-only
        }
        Entity hp = MakeElement(scene, canvas, "Panel", { 0.0f, 0.0f }, { 10.0f, 10.0f });
        {
            auto& c = hp.AddComponent<UiHostedPanelComponent>();
            c.PanelName = "telemetry"; c.ShowFrame = false; c.FrameColor = { 0.2f, 0.4f, 0.6f, 0.8f }; c.PlaceholderText = "Telemetry panel";
            c.DrawnThisFrame = true;   // runtime-only
        }

        const UUID ids[7] = { vt.GetComponent<IDComponent>().ID, g.GetComponent<IDComponent>().ID, ind.GetComponent<IDComponent>().ID,
                              pl.GetComponent<IDComponent>().ID, sl.GetComponent<IDComponent>().ID, tg.GetComponent<IDComponent>().ID,
                              hp.GetComponent<IDComponent>().ID };

        const std::string save1 = SceneSerializer::SaveToString(scene);
        for (const char* n : { "\"UiValueText\"", "\"UiGauge\"", "\"UiIndicator\"", "\"UiPlot\"", "\"UiSlider\"", "\"UiToggle\"", "\"UiHostedPanel\"" })
            CHECK_MESSAGE(save1.find(n) != std::string::npos, n);
        CHECK(save1.find("Dragging") == std::string::npos);
        CHECK(save1.find("DrawnThisFrame") == std::string::npos);
        CHECK(save1.find("\"Armed\"") == std::string::npos);

        Scene loaded;
        REQUIRE(SceneSerializer::LoadFromString(loaded, save1));

        Entity vt2 = loaded.FindByUUID(ids[0]); REQUIRE(vt2);
        {
            REQUIRE(vt2.HasComponent<UiValueTextComponent>());
            const auto& v = vt2.GetComponent<UiValueTextComponent>();
            CHECK(v.Channel == "pend.angle"); CHECK(v.Format == "%+.3f"); CHECK(v.Prefix == "θ="); CHECK(v.Suffix == " rad");
            CHECK(v.Placeholder == "n/a"); CHECK(v.StaleAfter == 1.5f); CHECK(v.StaleColor == glm::vec4(0.1f, 0.2f, 0.3f, 0.4f)); CHECK(v.PreviewValue == 0.75f);
            CHECK(vt2.GetComponent<UiTextComponent>().Text == "unchanged");
            CHECK(vt2.GetComponent<UiTextComponent>().HAlign == UiHAlign::Right);
        }
        Entity g2 = loaded.FindByUUID(ids[1]); REQUIRE(g2);
        {
            REQUIRE(g2.HasComponent<UiGaugeComponent>());
            const auto& c = g2.GetComponent<UiGaugeComponent>();
            CHECK(c.Channel == "speed"); CHECK(c.Min == -5.0f); CHECK(c.Max == 250.0f); CHECK(c.Style == UiGaugeStyle::Arc);
            CHECK(c.Direction == UiGaugeDirection::BottomToTop); CHECK(c.FillColor == glm::vec4(0.9f, 0.1f, 0.2f, 1.0f));
            CHECK(c.TrackColor == glm::vec4(0.05f, 0.06f, 0.07f, 0.5f)); CHECK(c.Thickness == 0.4f); CHECK(c.PreviewValue == 77.0f);
        }
        Entity ind2 = loaded.FindByUUID(ids[2]); REQUIRE(ind2);
        {
            REQUIRE(ind2.HasComponent<UiIndicatorComponent>());
            const auto& c = ind2.GetComponent<UiIndicatorComponent>();
            CHECK(c.Channel == "armed"); CHECK(c.Op == ">="); CHECK(c.Threshold == 0.5f); CHECK(c.OnTint == glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
            CHECK(c.OffTint == glm::vec4(0.0f, 0.0f, 1.0f, 0.5f)); CHECK(c.OnTexture == "textures/on.png"); CHECK(c.OffTexture == "textures/off.png");
            CHECK(c.PreviewOn == true);
            CHECK(c.ResolvedOn == nullptr);
        }
        Entity pl2 = loaded.FindByUUID(ids[3]); REQUIRE(pl2);
        {
            REQUIRE(pl2.HasComponent<UiPlotComponent>());
            const auto& c = pl2.GetComponent<UiPlotComponent>();
            CHECK(c.Channel == "a"); CHECK(c.Channel2 == "b"); CHECK(c.Channel3 == "c"); CHECK(c.Channel4 == "d");
            CHECK(c.WindowSeconds == 4.5f); CHECK(c.AutoScaleY == false); CHECK(c.YMin == -3.0f); CHECK(c.YMax == 7.0f);
            CHECK(c.LineColor == glm::vec4(1, 0, 0, 1)); CHECK(c.LineColor2 == glm::vec4(0, 1, 0, 1)); CHECK(c.LineColor3 == glm::vec4(0, 0, 1, 1)); CHECK(c.LineColor4 == glm::vec4(1, 1, 0, 1));
            CHECK(c.GridColor == glm::vec4(0.5f, 0.5f, 0.5f, 0.25f)); CHECK(c.BackgroundColor == glm::vec4(0.1f, 0.1f, 0.1f, 0.9f));
            CHECK(c.GridDivisions == 7); CHECK(c.LineWidth == 3.5f); CHECK(c.ShowLabels == false); CHECK(c.PreviewAmplitude == 2.5f);
        }
        Entity sl2 = loaded.FindByUUID(ids[4]); REQUIRE(sl2);
        {
            REQUIRE(sl2.HasComponent<UiSliderComponent>());
            const auto& c = sl2.GetComponent<UiSliderComponent>();
            CHECK(c.Channel == "gain"); CHECK(c.Min == 0.5f); CHECK(c.Max == 9.5f); CHECK(c.Step == 0.25f); CHECK(c.Signal == "gain_set");
            CHECK(c.Orientation == UiSliderOrientation::Vertical); CHECK(c.TrackColor == glm::vec4(0.2f, 0.2f, 0.2f, 1.0f));
            CHECK(c.FillColor == glm::vec4(0.3f, 0.3f, 0.9f, 1.0f)); CHECK(c.KnobColor == glm::vec4(1.0f, 1.0f, 1.0f, 0.8f));
            CHECK(c.KnobSize == 22.0f); CHECK(c.Interactable == false); CHECK(c.PreviewValue == 4.0f);
            CHECK(c.Dragging == false);
        }
        Entity tg2 = loaded.FindByUUID(ids[5]); REQUIRE(tg2);
        {
            REQUIRE(tg2.HasComponent<UiToggleComponent>());
            const auto& c = tg2.GetComponent<UiToggleComponent>();
            CHECK(c.Channel == "enabled"); CHECK(c.Signal == "enabled_toggled"); CHECK(c.OnTint == glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
            CHECK(c.OffTint == glm::vec4(0.4f, 0.4f, 0.4f, 1.0f)); CHECK(c.OnTexture == "textures/toggle_on.png"); CHECK(c.OffTexture == "textures/toggle_off.png");
            CHECK(c.Interactable == false); CHECK(c.PreviewOn == true);
            CHECK(c.Armed == false);
            CHECK(tg2.HasComponent<UiImageComponent>());
        }
        Entity hp2 = loaded.FindByUUID(ids[6]); REQUIRE(hp2);
        {
            REQUIRE(hp2.HasComponent<UiHostedPanelComponent>());
            const auto& c = hp2.GetComponent<UiHostedPanelComponent>();
            CHECK(c.PanelName == "telemetry"); CHECK(c.ShowFrame == false); CHECK(c.FrameColor == glm::vec4(0.2f, 0.4f, 0.6f, 0.8f));
            CHECK(c.PlaceholderText == "Telemetry panel");
            CHECK(c.DrawnThisFrame == false);
        }

        // save -> load -> save is byte-identical.
        const std::string save2 = SceneSerializer::SaveToString(loaded);
        CHECK(save1 == save2);

        // Defaults round-trip too (a fresh component of each kind, untouched).
        Scene defaults;
        Entity dc = MakeCanvas(defaults);
        Entity d = MakeElement(defaults, dc, "Defaults", { 0.0f, 0.0f }, { 1.0f, 1.0f });
        d.AddComponent<UiTextComponent>(); d.AddComponent<UiValueTextComponent>(); d.AddComponent<UiGaugeComponent>(); d.AddComponent<UiIndicatorComponent>();
        d.AddComponent<UiPlotComponent>(); d.AddComponent<UiSliderComponent>(); d.AddComponent<UiToggleComponent>(); d.AddComponent<UiHostedPanelComponent>();
        const std::string dsave = SceneSerializer::SaveToString(defaults);
        Scene dloaded;
        REQUIRE(SceneSerializer::LoadFromString(dloaded, dsave));
        Entity d2 = dloaded.FindByUUID(d.GetComponent<IDComponent>().ID);
        REQUIRE(d2);
        CHECK(d2.GetComponent<UiValueTextComponent>().Format == "%.2f");
        CHECK(d2.GetComponent<UiValueTextComponent>().Placeholder == "--");
        CHECK(d2.GetComponent<UiGaugeComponent>().Max == 100.0f);
        CHECK(d2.GetComponent<UiGaugeComponent>().PreviewValue == 50.0f);
        CHECK(d2.GetComponent<UiIndicatorComponent>().Op == "==");
        CHECK(d2.GetComponent<UiPlotComponent>().WindowSeconds == 10.0f);
        CHECK(d2.GetComponent<UiPlotComponent>().GridDivisions == 4);
        CHECK(d2.GetComponent<UiSliderComponent>().KnobSize == 18.0f);
        CHECK(d2.GetComponent<UiSliderComponent>().PreviewValue == 0.5f);
        CHECK(d2.GetComponent<UiToggleComponent>().OffTint == glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
        CHECK(d2.GetComponent<UiHostedPanelComponent>().ShowFrame == true);
        CHECK(SceneSerializer::SaveToString(dloaded) == dsave);
    }
}

// ============================================================================
// KI-64 / KI-65 (fix/player-pointer, 2026-09-20) — the standalone pointer mapping
// and the slider knob as a grab target. Headless: the same UiSystem::Update the
// PlayerLayer / editor call, fed pointers produced by UiSystem::MapPointerToCanvas
// from SCREEN-space coordinates exactly as PlayerLayer::UpdateUI produces them.
// ============================================================================

namespace
{
    // A canvas that scales with height like every PendulumLab screen (ReferenceHeight 1080).
    Entity MakeScaledCanvas(Scene& s)
    {
        Entity canvas = s.CreateEntity("Canvas");
        auto& c = canvas.AddComponent<CanvasComponent>();
        c.ScaleMode       = UiScaleMode::ScaleWithHeight;
        c.ReferenceHeight = 1080.0f;
        return canvas;
    }

    // A centre-anchored element (AnchorMin == AnchorMax == anchor), sized by +-half offsets.
    Entity MakeAnchored(Scene& s, Entity parent, const char* name, glm::vec2 anchor, glm::vec2 half, int32_t z = 0)
    {
        Entity e = s.CreateEntity(name);
        auto& rt = e.AddComponent<RectTransformComponent>();
        rt.AnchorMin = rt.AnchorMax = anchor;
        rt.OffsetMin = -half;
        rt.OffsetMax = half;
        rt.ZOrder = z;
        s.SetParent(e, parent, /*keepWorldPose=*/false);
        return e;
    }

    UiRect RectOf(Scene& s, Entity e, const UiRect& viewport)
    {
        std::vector<UiElement> els;
        UiSystem::CollectElements(s, viewport, els);
        for (const UiElement& el : els)
            if (el.Handle == (uint32_t)(entt::entity)e) return el.Rect;
        return {};
    }

    // PendulumLab's Home: Start / Settings / Quit at 0.64 / 0.74 / 0.84, 280 x 60 canvas px.
    struct Home
    {
        Scene  s;
        Entity start, settings, quit;
        Home()
        {
            Entity canvas = MakeScaledCanvas(s);
            start    = MakeAnchored(s, canvas, "StartButton",    { 0.5f, 0.64f }, { 140.0f, 30.0f }, 2);
            settings = MakeAnchored(s, canvas, "SettingsButton", { 0.5f, 0.74f }, { 140.0f, 30.0f }, 2);
            quit     = MakeAnchored(s, canvas, "QuitButton",     { 0.5f, 0.84f }, { 140.0f, 30.0f }, 2);
            start.AddComponent<UiButtonComponent>().Signal    = "start_clicked";
            settings.AddComponent<UiButtonComponent>().Signal = "settings_clicked";
            quit.AddComponent<UiButtonComponent>().Signal     = "quit_clicked";
        }
        UiButtonState State(Entity e) { return e.GetComponent<UiButtonComponent>().State; }
    };

    // PendulumLab's Settings sliders: a 30 %-wide row at anchor y, 24 canvas px tall, 18 px knob.
    struct SettingsRow
    {
        Scene  s;
        Entity slider;
        SettingsRow()
        {
            Entity canvas = MakeScaledCanvas(s);
            slider = s.CreateEntity("GravitySlider");
            auto& rt = slider.AddComponent<RectTransformComponent>();
            rt.AnchorMin = { 0.36f, 0.29f };
            rt.AnchorMax = { 0.66f, 0.29f };
            rt.OffsetMin = { 0.0f, -12.0f };
            rt.OffsetMax = { 0.0f,  12.0f };
            s.SetParent(slider, canvas, false);
            auto& sl = slider.AddComponent<UiSliderComponent>();
            sl.Channel = "settings.gravity"; sl.Min = 1.0f; sl.Max = 25.0f; sl.KnobSize = 18.0f;
        }
        UiSliderComponent& Sl() { return slider.GetComponent<UiSliderComponent>(); }
    };

    // The host geometry of one pointer scenario: where the frame is presented on screen,
    // how big it is there, and how big its framebuffer is.
    struct Host
    {
        glm::vec2 FramePos, FrameSize, FbSize;
        // Screen position of a canvas point (the inverse of the mapping) — where the cursor
        // sits when it is exactly on that point's picture.
        glm::vec2 ScreenOf(glm::vec2 canvasPt) const { return FramePos + canvasPt * (FrameSize / FbSize); }
        glm::vec2 Map(glm::vec2 screen) const { return UiSystem::MapPointerToCanvas(screen, FramePos, FrameSize, FbSize); }
    };

    // (b) Kaden's maximized window: 2560 x 1392 client at (0,0), 54 px of menu bar + dock tab above the frame.
    const Host kChrome   { { 0.0f, 54.0f },   { 2560.0f, 1338.0f }, { 2560.0f, 1338.0f } };
    // (b') his fullscreen: 2560 x 1441 cover, only the 27 px dock tab bar above the frame.
    const Host kFullscreen{ { 0.0f, 27.0f },  { 2560.0f, 1414.0f }, { 2560.0f, 1414.0f } };
    // (a) a frame presented at 1280 x 720 whose framebuffer is 1920 x 1080 (1.5x), below 54 px of chrome.
    const Host kDpi150   { { 0.0f, 54.0f },   { 1280.0f, 720.0f },  { 1920.0f, 1080.0f } };
    // (c) a letterboxed host: the 1920 x 1080 target presented 1:1 at screen (100, 54); the canvas
    //     is laid out in a 4:3 band inside it (the editor's game band / UiSystem::Render band variant).
    const Host kBandHost { { 100.0f, 54.0f }, { 1920.0f, 1080.0f }, { 1920.0f, 1080.0f } };
    const UiRect kBand   { { 240.0f, 0.0f }, { 1680.0f, 1080.0f } };
}

TEST_SUITE("KI-64/65 pointer mapping")
{
    TEST_CASE("KI-64 MapPointerToCanvas: chrome offset, framebuffer scale, identity, degenerate sizes")
    {
        // Identity: frame at the origin, presented 1:1.
        CHECK(UiSystem::MapPointerToCanvas({ 10.0f, 20.0f }, { 0.0f, 0.0f }, { 800.0f, 600.0f }, { 800.0f, 600.0f }) == glm::vec2(10.0f, 20.0f));
        // Chrome / window position is subtracted (the window's own position cancels: both inputs are screen space).
        CHECK(UiSystem::MapPointerToCanvas({ 1280.0f, 1044.0f }, { 0.0f, 54.0f }, { 2560.0f, 1338.0f }, { 2560.0f, 1338.0f }) == glm::vec2(1280.0f, 990.0f));
        CHECK(UiSystem::MapPointerToCanvas({ 300.0f, 254.0f }, { 100.0f, 54.0f }, { 640.0f, 480.0f }, { 640.0f, 480.0f }) == glm::vec2(200.0f, 200.0f));
        // Framebuffer scale: presented at 2/3 of its framebuffer size -> canvas coordinates grow 1.5x.
        const glm::vec2 hi = UiSystem::MapPointerToCanvas({ 640.0f, 414.0f }, { 0.0f, 54.0f }, { 1280.0f, 720.0f }, { 1920.0f, 1080.0f });
        CHECK(hi.x == doctest::Approx(960.0f));
        CHECK(hi.y == doctest::Approx(540.0f));
        // A downscaled preview (framebuffer smaller than the presented image) shrinks instead.
        CHECK(UiSystem::MapPointerToCanvas({ 400.0f, 300.0f }, { 0.0f, 0.0f }, { 800.0f, 600.0f }, { 400.0f, 300.0f }) == glm::vec2(200.0f, 150.0f));
        // Degenerate presented size: no scaling (never a division by zero / inf).
        const glm::vec2 dg = UiSystem::MapPointerToCanvas({ 50.0f, 60.0f }, { 10.0f, 10.0f }, { 0.0f, 0.0f }, { 800.0f, 600.0f });
        CHECK(dg == glm::vec2(40.0f, 50.0f));
        CHECK(std::isfinite(dg.x));
    }

    TEST_CASE("KI-64 buttons: a pointer mapped from screen space hits the button it points at under a chrome offset, a 1.5x framebuffer scale and a letterbox band; the pre-fix window-client pointer hits the row below")
    {
        SUBCASE("(b) chrome offset — Kaden's maximized window, cursor on Settings' centre")
        {
            Home h;
            const UiRect viewport{ { 0.0f, 0.0f }, kChrome.FbSize };
            const UiRect settingsRect = RectOf(h.s, h.settings, viewport);
            CHECK(settingsRect.Center().y == doctest::Approx(0.74f * 1338.0f));

            // Cursor exactly on the Settings picture (screen y = 54 + canvas y).
            const glm::vec2 screen = kChrome.ScreenOf(settingsRect.Center());
            CHECK(screen.y == doctest::Approx(54.0f + 0.74f * 1338.0f));
            UiSystem::Update(h.s, viewport, Idle(kChrome.Map(screen)));
            CHECK(h.State(h.settings) == UiButtonState::Hover);
            CHECK(h.State(h.start)    == UiButtonState::Normal);
            CHECK(h.State(h.quit)     == UiButtonState::Normal);

            // The KI-61 expression (window-client pointer, chrome NOT subtracted) at the same
            // cursor lands in the gap under Settings: nothing hovers ("exactly on Settings does nothing")...
            UiSystem::Update(h.s, viewport, Idle(screen));
            CHECK(h.State(h.settings) == UiButtonState::Normal);
            CHECK(h.State(h.quit)     == UiButtonState::Normal);
            // ...and 16 px under Start's picture it hovers Settings ("the cursor under Start activates Settings").
            const UiRect startRect = RectOf(h.s, h.start, viewport);
            const glm::vec2 underStart = kChrome.ScreenOf({ startRect.Center().x, startRect.Max.y + 16.0f });
            UiSystem::Update(h.s, viewport, Idle(underStart));
            CHECK(h.State(h.settings) == UiButtonState::Hover);
            // Mapped, the same cursor hovers nothing (it is in the gap between the buttons).
            UiSystem::Update(h.s, viewport, Idle(kChrome.Map(underStart)));
            CHECK(h.State(h.settings) == UiButtonState::Normal);
            CHECK(h.State(h.start)    == UiButtonState::Normal);

            // A full press-release on the mapped Settings centre emits settings_clicked once.
            int settingsFires = 0, otherFires = 0;
            h.s.Events().Connect("settings_clicked", [&](Entity) { ++settingsFires; });
            h.s.Events().Connect("start_clicked",    [&](Entity) { ++otherFires; });
            h.s.Events().Connect("quit_clicked",     [&](Entity) { ++otherFires; });
            UiSystem::Update(h.s, viewport, Press(kChrome.Map(screen)));
            UiSystem::Update(h.s, viewport, Release(kChrome.Map(screen)));
            CHECK(settingsFires == 1);
            CHECK(otherFires == 0);
        }

        SUBCASE("(a) 1.5x framebuffer scale — the presented image is smaller than the framebuffer")
        {
            Home h;
            const UiRect viewport{ { 0.0f, 0.0f }, kDpi150.FbSize };
            const UiRect settingsRect = RectOf(h.s, h.settings, viewport);
            // Centre of the picture on screen: chrome + canvas / 1.5.
            const glm::vec2 screen = kDpi150.ScreenOf(settingsRect.Center());
            CHECK(screen.x == doctest::Approx(640.0f));
            UiSystem::Update(h.s, viewport, Idle(kDpi150.Map(screen)));
            CHECK(h.State(h.settings) == UiButtonState::Hover);
            CHECK(h.State(h.start)    == UiButtonState::Normal);
            // One framebuffer pixel above the button's top edge (on screen: 2/3 px) misses it.
            const glm::vec2 above = kDpi150.ScreenOf({ settingsRect.Center().x, settingsRect.Min.y - 1.0f });
            UiSystem::Update(h.s, viewport, Idle(kDpi150.Map(above)));
            CHECK(h.State(h.settings) == UiButtonState::Normal);
            // The unscaled screen delta (origin subtracted, no scale) lands on a different row entirely.
            UiSystem::Update(h.s, viewport, Idle(screen - kDpi150.FramePos));
            CHECK(h.State(h.settings) == UiButtonState::Normal);
        }

        SUBCASE("(c) letterbox band — canvases laid out in a 4:3 band of a 16:9 target")
        {
            Home h;
            // Elements resolve inside the band (absolute target pixels), like Render's band variant.
            const UiRect settingsRect = RectOf(h.s, h.settings, kBand);
            CHECK(settingsRect.Center().x == doctest::Approx(960.0f));
            CHECK(settingsRect.Width()    == doctest::Approx(280.0f));   // 1080-tall band -> scale 1
            const glm::vec2 screen = kBandHost.ScreenOf(settingsRect.Center());
            UiSystem::Update(h.s, kBand, Idle(kBandHost.Map(screen)));
            CHECK(h.State(h.settings) == UiButtonState::Hover);
            // A cursor over the black side band (target x = 100) hovers nothing at the same height.
            const glm::vec2 sideBand = kBandHost.ScreenOf({ 100.0f, settingsRect.Center().y });
            CHECK_FALSE(UiSystem::Update(h.s, kBand, Idle(kBandHost.Map(sideBand))));
            CHECK(h.State(h.settings) == UiButtonState::Normal);
        }
    }

    TEST_CASE("KI-64 slider knobs: the same three mappings grab the knob centre and refuse 1 px above the knob's top where the rect is not")
    {
        // A slider whose rect is exactly as tall as its knob, so "1 px above the knob" is
        // outside both: 200 x 18 px, knob 18 px, value 50 % -> knob centred at (200, 109).
        auto makeSlider = [](Scene& s, Entity canvas) -> Entity
        {
            Entity e = MakeElement(s, canvas, "Slider", { 100.0f, 100.0f }, { 300.0f, 118.0f });
            auto& sl = e.AddComponent<UiSliderComponent>();
            sl.Channel = "gain"; sl.Min = 0.0f; sl.Max = 1.0f; sl.KnobSize = 18.0f;
            return e;
        };
        auto check = [&](const Host& host, const UiRect& viewport, const char* label)
        {
            INFO(label);
            Scene s;
            Entity canvas = MakeCanvas(s);                       // ConstantPixel: literal rects
            Entity e = makeSlider(s, canvas);
            auto& sl = e.GetComponent<UiSliderComponent>();
            DataBus bus;
            bus.Set("gain", 0.5);
            const UiRect rect = RectOf(s, e, viewport);
            const UiRect knob = UiSystem::SliderKnobRect(rect, sl.Orientation, sl.KnobSize, sl.Min, sl.Max, 0.5);
            CHECK(knob.Center() == rect.Center());
            CHECK(knob.Height() == doctest::Approx(18.0f));

            // Press on the knob centre (mapped from its screen position): the drag begins and writes.
            UiSystem::Update(s, viewport, Press(host.Map(host.ScreenOf(knob.Center()))), nullptr, &bus);
            CHECK(sl.Dragging);
            CHECK(bus.GetNumber("gain") == doctest::Approx(0.5));
            // Drag 50 px right in framebuffer pixels: the value follows (0.75).
            UiSystem::Update(s, viewport, Hold(host.Map(host.ScreenOf(knob.Center() + glm::vec2(50.0f, 0.0f)))), nullptr, &bus);
            CHECK(bus.GetNumber("gain") == doctest::Approx(0.75));
            UiSystem::Update(s, viewport, Release(host.Map(host.ScreenOf(knob.Center() + glm::vec2(50.0f, 0.0f)))), nullptr, &bus);
            CHECK_FALSE(sl.Dragging);

            // 1 px above the knob's top (rect is not there either): no drag, no write.
            bus.Set("gain", 0.5);
            std::vector<DataSample> hist;
            const size_t before = bus.History("gain", hist);
            UiSystem::Update(s, viewport, Press(host.Map(host.ScreenOf({ knob.Center().x, knob.Min.y - 1.0f }))), nullptr, &bus);
            CHECK_FALSE(sl.Dragging);
            CHECK(bus.History("gain", hist) == before);
            UiSystem::Update(s, viewport, Release(host.Map(host.ScreenOf({ knob.Center().x, knob.Min.y - 1.0f }))), nullptr, &bus);

            // The KI-61 pointer (screen coordinates handed over unmapped) at the knob's picture misses it.
            UiSystem::Update(s, viewport, Press(host.ScreenOf(knob.Center())), nullptr, &bus);
            CHECK_FALSE(sl.Dragging);
            UiSystem::Update(s, viewport, Release(host.ScreenOf(knob.Center())), nullptr, &bus);
        };
        check(kChrome,     UiRect{ { 0.0f, 0.0f }, kChrome.FbSize },     "(b) chrome offset");
        check(kFullscreen, UiRect{ { 0.0f, 0.0f }, kFullscreen.FbSize }, "(b') fullscreen tab bar");
        check(kDpi150,     UiRect{ { 0.0f, 0.0f }, kDpi150.FbSize },     "(a) 1.5x framebuffer scale");
        check(kBandHost,   kBand,                                          "(c) letterbox band");

        SUBCASE("PendulumLab's Gravity row in fullscreen: the knob the draw shows is the one the mapped press grabs; the unmapped press (27 px low) misses, the unmapped press 21 px above it grabs")
        {
            SettingsRow r;
            const UiRect viewport{ { 0.0f, 0.0f }, kFullscreen.FbSize };
            DataBus bus;
            bus.Set("settings.gravity", 9.80665);
            const UiRect rect = RectOf(r.s, r.slider, viewport);
            const float scale = 1414.0f / 1080.0f;
            CHECK(rect.Height() == doctest::Approx(24.0f * scale));
            const UiRect knob = UiSystem::SliderKnobRect(rect, r.Sl().Orientation, r.Sl().KnobSize * scale, r.Sl().Min, r.Sl().Max, 9.80665);
            CHECK(knob.Height() == doctest::Approx(18.0f * scale));
            CHECK(knob.Center().y == doctest::Approx(rect.Center().y));

            const glm::vec2 onKnob = kFullscreen.ScreenOf(knob.Center());
            UiSystem::Update(r.s, viewport, Press(kFullscreen.Map(onKnob)), nullptr, &bus);
            CHECK(r.Sl().Dragging);
            UiSystem::Update(r.s, viewport, Release(kFullscreen.Map(onKnob)), nullptr, &bus);
            CHECK_FALSE(r.Sl().Dragging);

            // The stale package's mapping (window y used as frame y): the knob's own pixels miss...
            UiSystem::Update(r.s, viewport, Press(onKnob), nullptr, &bus);
            CHECK_FALSE(r.Sl().Dragging);
            UiSystem::Update(r.s, viewport, Release(onKnob), nullptr, &bus);
            // ...and 21 px above the knob grabs — the symptom in the fullscreen photo.
            UiSystem::Update(r.s, viewport, Press(onKnob - glm::vec2(0.0f, 21.0f)), nullptr, &bus);
            CHECK(r.Sl().Dragging);
            UiSystem::Update(r.s, viewport, Release(onKnob - glm::vec2(0.0f, 21.0f)), nullptr, &bus);
        }
    }

    TEST_CASE("KI-65 slider knob larger than its track: a press on the knob's overhang grabs and drags, 1 px beyond the knob does not; SliderKnobRect is the drawn geometry (horizontal + vertical)")
    {
        SUBCASE("horizontal: 200 x 8 track, 24 px knob at 50 %")
        {
            Scene s;
            Entity canvas = MakeCanvas(s);
            Entity e = MakeElement(s, canvas, "Slider", { 100.0f, 100.0f }, { 300.0f, 108.0f });
            auto& sl = e.AddComponent<UiSliderComponent>();
            sl.Channel = "gain"; sl.Min = 0.0f; sl.Max = 1.0f; sl.KnobSize = 24.0f;
            DataBus bus;
            bus.Set("gain", 0.5);

            const UiRect knob = UiSystem::SliderKnobRect({ { 100.0f, 100.0f }, { 300.0f, 108.0f } }, UiSliderOrientation::Horizontal, 24.0f, 0.0f, 1.0f, 0.5);
            CHECK(knob.Min == glm::vec2(188.0f, 92.0f));
            CHECK(knob.Max == glm::vec2(212.0f, 116.0f));
            // The knob follows the value: at min it is centred on the rect's left edge.
            CHECK(UiSystem::SliderKnobRect({ { 100.0f, 100.0f }, { 300.0f, 108.0f } }, UiSliderOrientation::Horizontal, 24.0f, 0.0f, 1.0f, 0.0).Center() == glm::vec2(100.0f, 104.0f));
            // Floor at 2 px like the draw.
            CHECK(UiSystem::SliderKnobRect({ { 100.0f, 100.0f }, { 300.0f, 108.0f } }, UiSliderOrientation::Horizontal, 0.0f, 0.0f, 1.0f, 0.0).Width() == doctest::Approx(2.0f));

            // On the knob, 9 px above the rect's centre line (outside the 8 px rect, inside the knob): grabs.
            UiSystem::Update(s, kViewport, Press({ 200.0f, 95.0f }), nullptr, &bus);
            CHECK(sl.Dragging);
            UiSystem::Update(s, kViewport, Hold({ 250.0f, 95.0f }), nullptr, &bus);
            CHECK(bus.GetNumber("gain") == doctest::Approx(0.75));
            UiSystem::Update(s, kViewport, Release({ 250.0f, 95.0f }), nullptr, &bus);
            CHECK_FALSE(sl.Dragging);

            // The knob moved with the value: its old overhang pixels are empty now, its new ones grab.
            bus.Set("gain", 0.75);
            UiSystem::Update(s, kViewport, Press({ 200.0f, 95.0f }), nullptr, &bus);
            CHECK_FALSE(sl.Dragging);
            UiSystem::Update(s, kViewport, Release({ 200.0f, 95.0f }), nullptr, &bus);
            UiSystem::Update(s, kViewport, Press({ 250.0f, 113.0f }), nullptr, &bus);   // below the rect, on the knob
            CHECK(sl.Dragging);
            UiSystem::Update(s, kViewport, Release({ 250.0f, 113.0f }), nullptr, &bus);

            // 1 px beyond the knob's top (y = 91 for a knob spanning 92..116) does not grab.
            bus.Set("gain", 0.5);
            UiSystem::Update(s, kViewport, Press({ 200.0f, 91.0f }), nullptr, &bus);
            CHECK_FALSE(sl.Dragging);
            UiSystem::Update(s, kViewport, Release({ 200.0f, 91.0f }), nullptr, &bus);
            // The track away from the knob still grabs (a press at 25 % steers there).
            UiSystem::Update(s, kViewport, Press({ 150.0f, 104.0f }), nullptr, &bus);
            CHECK(sl.Dragging);
            CHECK(bus.GetNumber("gain") == doctest::Approx(0.25));
            UiSystem::Update(s, kViewport, Release({ 150.0f, 104.0f }), nullptr, &bus);
            // Update reports the knob overhang as "over an interactable" (scene picking must yield).
            CHECK(UiSystem::Update(s, kViewport, Idle({ 150.0f, 95.0f }), nullptr, &bus));
            CHECK_FALSE(UiSystem::Update(s, kViewport, Idle({ 150.0f, 90.0f }), nullptr, &bus));
        }

        SUBCASE("vertical: 8 x 200 track, 24 px knob at 50 % (bottom = min)")
        {
            Scene s;
            Entity canvas = MakeCanvas(s);
            Entity e = MakeElement(s, canvas, "VSlider", { 100.0f, 100.0f }, { 108.0f, 300.0f });
            auto& sl = e.AddComponent<UiSliderComponent>();
            sl.Channel = "level"; sl.Min = 0.0f; sl.Max = 1.0f; sl.KnobSize = 24.0f;
            sl.Orientation = UiSliderOrientation::Vertical;
            DataBus bus;
            bus.Set("level", 0.5);

            const UiRect knob = UiSystem::SliderKnobRect({ { 100.0f, 100.0f }, { 108.0f, 300.0f } }, UiSliderOrientation::Vertical, 24.0f, 0.0f, 1.0f, 0.5);
            CHECK(knob.Center() == glm::vec2(104.0f, 200.0f));
            CHECK(UiSystem::SliderKnobRect({ { 100.0f, 100.0f }, { 108.0f, 300.0f } }, UiSliderOrientation::Vertical, 24.0f, 0.0f, 1.0f, 0.0).Center() == glm::vec2(104.0f, 300.0f));

            // Left of the rect, on the knob: grabs; drag up raises the value.
            UiSystem::Update(s, kViewport, Press({ 95.0f, 200.0f }), nullptr, &bus);
            CHECK(sl.Dragging);
            UiSystem::Update(s, kViewport, Hold({ 95.0f, 150.0f }), nullptr, &bus);
            CHECK(bus.GetNumber("level") == doctest::Approx(0.75));
            UiSystem::Update(s, kViewport, Release({ 95.0f, 150.0f }), nullptr, &bus);
            // 1 px beyond the knob's left edge (x = 91 for a knob spanning 92..116) does not.
            bus.Set("level", 0.5);
            UiSystem::Update(s, kViewport, Press({ 91.0f, 200.0f }), nullptr, &bus);
            CHECK_FALSE(sl.Dragging);
            UiSystem::Update(s, kViewport, Release({ 91.0f, 200.0f }), nullptr, &bus);
        }

        SUBCASE("without a bus the knob sits at PreviewValue (what a bus-less host draws) and the hit region follows it; the slider stays inert")
        {
            Scene s;
            Entity canvas = MakeCanvas(s);
            Entity e = MakeElement(s, canvas, "Slider", { 100.0f, 100.0f }, { 300.0f, 108.0f });
            auto& sl = e.AddComponent<UiSliderComponent>();
            sl.Min = 0.0f; sl.Max = 1.0f; sl.KnobSize = 24.0f; sl.PreviewValue = 1.0f;
            CHECK(UiSystem::Update(s, kViewport, Idle({ 300.0f, 95.0f })));        // knob at the right end
            CHECK_FALSE(UiSystem::Update(s, kViewport, Idle({ 200.0f, 95.0f })));  // no knob at 50 %
            UiSystem::Update(s, kViewport, Press({ 300.0f, 95.0f }));
            CHECK_FALSE(sl.Dragging);
        }
    }
}
