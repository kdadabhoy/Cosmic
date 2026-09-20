// render_ap02_widgets.cpp — App Platform AP-02: the bound widgets on the GPU.
//
//   V03  one reviewed 320x180 golden per widget kind (F-WIDGETS: a ConstantPixel
//        canvas at literal pixel rects, pure sentinel colours, a DataBus pre-filled
//        with known values/history where the kind needs live data), compared with
//        the harness tolerance (2/255, 0.1 %) PLUS exact sentinel pixels/ROIs (fill
//        edge, knob centre, plot line pixel, texture halves, frame edge) so a whole-
//        frame tolerance cannot hide one wrong widget.
//   E05  the preview-vs-live A/B pair on a combined scene: Render(bus = nullptr)
//        against Render(bus) with the bus holding exactly the preview values is
//        byte-identical (in-process pair => BytesEqual); with different bus values
//        the diff is confined to the widget rects and every bound widget moved.
//
// Every draw under test is a Renderer2D verb inside UiSystem::Render; this file
// uses only engine verbs itself (the conformance scanner covers tests/).

#include "wo08_common.h"

#include "data/DataBus.h"
#include "scene/Components.h"
#include "scene/Entity.h"
#include "scene/Scene.h"
#include "scene/ui/UiComponents.h"
#include "scene/ui/UiSystem.h"
#include "utils/ImageIO.h"

#include <doctest.h>

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace Cosmic;
using namespace CosmicRender;
using namespace Wo08;

namespace
{
    constexpr uint32_t kW = kGoldenWidth;    // 320
    constexpr uint32_t kH = kGoldenHeight;   // 180
    const UiRect kCanvas{ { 0.0f, 0.0f }, { (float)kW, (float)kH } };

    // Pure sentinel colours (alpha 1 => exact bytes after the alpha-over blend).
    const glm::vec4 kGreen  { 0.0f, 1.0f, 0.0f, 1.0f };
    const glm::vec4 kBlue   { 0.0f, 0.0f, 1.0f, 1.0f };
    const glm::vec4 kRed    { 1.0f, 0.0f, 0.0f, 1.0f };
    const glm::vec4 kWhite  { 1.0f, 1.0f, 1.0f, 1.0f };
    const glm::vec4 kYellow { 1.0f, 1.0f, 0.0f, 1.0f };
    const glm::vec4 kMagenta{ 1.0f, 0.0f, 1.0f, 1.0f };
    const glm::vec4 kCyan   { 0.0f, 1.0f, 1.0f, 1.0f };
    const glm::vec4 kPlotBg { 0.0f, 0.0f, 0.2f, 1.0f };   // 51/255 exactly: no rounding ambiguity

    Entity MakeCanvas(Scene& s)
    {
        Entity canvas = s.CreateEntity("Canvas");
        canvas.AddComponent<CanvasComponent>().ScaleMode = UiScaleMode::ConstantPixel;
        return canvas;
    }

    Entity AddRect(Scene& s, Entity parent, const char* name, glm::vec2 min, glm::vec2 max, int32_t z = 0)
    {
        Entity e = s.CreateEntity(name);
        auto& rt = e.AddComponent<RectTransformComponent>();
        rt.AnchorMin = rt.AnchorMax = { 0.0f, 0.0f };
        rt.OffsetMin = min;
        rt.OffsetMax = max;
        rt.ZOrder = z;
        s.SetParent(e, parent, false);
        return e;
    }

    void RenderUi(Scene& s, const Ref<FrameBuffer>& fbo, Image& out, const DataBus* bus, bool preview)
    {
        BeginFrame(fbo);
        UiSystem::Render(s, kCanvas, nullptr, bus, preview);
        REQUIRE(Capture(fbo, out));
    }

    // Two flat-halves textures on disk (left / right colours), loaded through the
    // production AssetLibrary path exactly as an authored OnTexture/OffTexture is.
    struct HalfTextures
    {
        std::string OnPath, OffPath;
        static constexpr glm::u8vec4 OnLeft  { 0, 255, 0, 255 };
        static constexpr glm::u8vec4 OnRight { 255, 255, 0, 255 };
        static constexpr glm::u8vec4 OffLeft { 128, 128, 128, 255 };
        static constexpr glm::u8vec4 OffRight{ 64, 64, 64, 255 };
    };

    HalfTextures WriteHalfTextures()
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "cosmic-ap02-fixtures";
        std::error_code ec;
        fs::create_directories(dir, ec);
        auto write = [&](const char* name, glm::u8vec4 l, glm::u8vec4 r) {
            const int w = 16, h = 16;
            std::vector<uint8_t> px((size_t)w * h * 4);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const glm::u8vec4 c = x < w / 2 ? l : r;
                    const size_t i = ((size_t)y * w + x) * 4;
                    px[i] = c.r; px[i + 1] = c.g; px[i + 2] = c.b; px[i + 3] = c.a;
                }
            const std::string path = (dir / name).string();
            REQUIRE(ImageIO::WritePNG(path, w, h, 4, px.data()));
            return path;
        };
        HalfTextures t;
        t.OnPath  = write("ap02_ind_on.png",  HalfTextures::OnLeft,  HalfTextures::OnRight);
        t.OffPath = write("ap02_ind_off.png", HalfTextures::OffLeft, HalfTextures::OffRight);
        return t;
    }

    // ---- F-WIDGETS scenes ---------------------------------------------------------

    struct ValueTextScene
    {
        Ref<Scene> S;
        std::unique_ptr<DataBus> Bus = std::make_unique<DataBus>();
        Entity     Fresh, Stale, Missing;
    };

    ValueTextScene MakeValueTextScene()
    {
        ValueTextScene f;
        f.S = Scene::Create();
        Scene& s = *f.S;
        Entity canvas = MakeCanvas(s);
        f.Bus->Set("temp", 21.5);
        f.Bus->Advance(5.0);               // "temp" is now 5 s old
        f.Bus->Set("volts", 12.345);       // fresh

        auto text = [](Entity e, glm::vec4 color) {
            auto& t = e.AddComponent<UiTextComponent>();
            t.Text = "SHOULD NOT SHOW"; t.SizePx = 28.0f; t.Color = color; t.HAlign = UiHAlign::Left; t.VAlign = UiVAlign::Middle;
        };
        f.Fresh = AddRect(s, canvas, "Fresh", { 10, 10 }, { 310, 60 });
        text(f.Fresh, kWhite);
        { auto& v = f.Fresh.AddComponent<UiValueTextComponent>(); v.Channel = "volts"; v.Format = "%.2f"; v.Prefix = "V "; v.Suffix = " V"; v.StaleAfter = 1.0f; v.StaleColor = kRed; }
        f.Stale = AddRect(s, canvas, "Stale", { 10, 65 }, { 310, 115 });
        text(f.Stale, kWhite);
        { auto& v = f.Stale.AddComponent<UiValueTextComponent>(); v.Channel = "temp"; v.Format = "%.1f"; v.Suffix = " C"; v.StaleAfter = 1.0f; v.StaleColor = kRed; }
        f.Missing = AddRect(s, canvas, "Missing", { 10, 120 }, { 310, 170 });
        text(f.Missing, kCyan);
        { auto& v = f.Missing.AddComponent<UiValueTextComponent>(); v.Channel = "nothing"; v.Prefix = "T "; v.Placeholder = "--"; v.StaleColor = kRed; }
        return f;
    }

    Ref<Scene> MakeGaugeScene()
    {
        Ref<Scene> scene = Scene::Create();
        Scene& s = *scene;
        Entity canvas = MakeCanvas(s);
        auto gauge = [&](const char* name, glm::vec2 min, glm::vec2 max, UiGaugeStyle style, UiGaugeDirection dir, float pv) {
            Entity e = AddRect(s, canvas, name, min, max);
            auto& g = e.AddComponent<UiGaugeComponent>();
            g.Channel = "g"; g.Min = 0.0f; g.Max = 100.0f; g.Style = style; g.Direction = dir;
            g.FillColor = kGreen; g.TrackColor = kBlue; g.Thickness = 0.3f; g.PreviewValue = pv;
            return e;
        };
        gauge("Bar0",   { 10, 10 },  { 110, 30 },  UiGaugeStyle::Bar, UiGaugeDirection::LeftToRight, 0.0f);
        gauge("Bar50",  { 120, 10 }, { 220, 30 },  UiGaugeStyle::Bar, UiGaugeDirection::LeftToRight, 50.0f);
        gauge("Bar100", { 230, 10 }, { 310, 30 },  UiGaugeStyle::Bar, UiGaugeDirection::LeftToRight, 100.0f);
        gauge("VBar50", { 10, 40 },  { 30, 120 },  UiGaugeStyle::Bar, UiGaugeDirection::BottomToTop, 50.0f);
        gauge("Arc0",   { 60, 110 }, { 120, 170 }, UiGaugeStyle::Arc, UiGaugeDirection::LeftToRight, 0.0f);
        gauge("Arc50",  { 130, 110 }, { 190, 170 }, UiGaugeStyle::Arc, UiGaugeDirection::LeftToRight, 50.0f);
        gauge("Arc100", { 200, 110 }, { 260, 170 }, UiGaugeStyle::Arc, UiGaugeDirection::LeftToRight, 100.0f);
        return scene;
    }

    Ref<Scene> MakeIndicatorScene(const HalfTextures& tex)
    {
        Ref<Scene> scene = Scene::Create();
        Scene& s = *scene;
        Entity canvas = MakeCanvas(s);
        auto ind = [&](const char* name, glm::vec2 min, glm::vec2 max, bool on, bool textured) {
            Entity e = AddRect(s, canvas, name, min, max);
            auto& i = e.AddComponent<UiIndicatorComponent>();
            i.Channel = "i"; i.PreviewOn = on;
            if (textured) { i.OnTexture = tex.OnPath; i.OffTexture = tex.OffPath; i.OnTint = kWhite; i.OffTint = kWhite; }
            else          { i.OnTint = kRed; i.OffTint = kBlue; }
            return e;
        };
        ind("TexOn",   { 10, 10 },  { 90, 90 },  true,  true);
        ind("TexOff",  { 100, 10 }, { 180, 90 }, false, true);
        ind("SolidOn", { 190, 10 }, { 250, 70 }, true,  false);
        ind("SolidOff",{ 260, 10 }, { 310, 60 }, false, false);
        return scene;
    }

    struct PlotScene
    {
        Ref<Scene> S;
        std::unique_ptr<DataBus> Bus = std::make_unique<DataBus>();
        Entity     Plot;
    };

    PlotScene MakePlotScene()
    {
        PlotScene f;
        f.S = Scene::Create();
        Scene& s = *f.S;
        Entity canvas = MakeCanvas(s);
        // 10 s of 60 Hz history: "a" = 0.8 sin(2 cycles), "b" = 0.6 with a NaN gap in (3, 7).
        for (int k = 0; k <= 600; ++k)
        {
            const double t = k / 60.0;
            f.Bus->Set("a", 0.8 * std::sin(2.0 * glm::pi<double>() * 2.0 * t / 10.0));
            f.Bus->Set("b", (t < 3.0 || t > 7.0) ? 0.6 : std::numeric_limits<double>::quiet_NaN());
            if (k < 600) f.Bus->Advance(1.0 / 60.0);
        }
        f.Plot = AddRect(s, canvas, "Plot", { 10, 10 }, { 310, 170 });
        auto& p = f.Plot.AddComponent<UiPlotComponent>();
        p.Channel = "a"; p.Channel2 = "b"; p.WindowSeconds = 10.0f; p.AutoScaleY = false; p.YMin = -1.0f; p.YMax = 1.0f;
        p.LineColor = kYellow; p.LineColor2 = kMagenta; p.GridColor = { 1.0f, 1.0f, 1.0f, 0.12f }; p.BackgroundColor = kPlotBg;
        p.GridDivisions = 4; p.LineWidth = 2.0f; p.ShowLabels = true;
        return f;
    }

    Ref<Scene> MakeSliderScene()
    {
        Ref<Scene> scene = Scene::Create();
        Scene& s = *scene;
        Entity canvas = MakeCanvas(s);
        auto slider = [&](const char* name, glm::vec2 min, glm::vec2 max, UiSliderOrientation o, float pv) {
            Entity e = AddRect(s, canvas, name, min, max);
            auto& sl = e.AddComponent<UiSliderComponent>();
            sl.Channel = "s"; sl.Min = 0.0f; sl.Max = 1.0f; sl.Orientation = o; sl.PreviewValue = pv;
            sl.TrackColor = kBlue; sl.FillColor = kGreen; sl.KnobColor = kWhite; sl.KnobSize = 16.0f;
            return e;
        };
        slider("H0",   { 10, 10 },  { 210, 40 },  UiSliderOrientation::Horizontal, 0.0f);
        slider("H50",  { 10, 50 },  { 210, 80 },  UiSliderOrientation::Horizontal, 0.5f);
        slider("H100", { 10, 90 },  { 210, 120 }, UiSliderOrientation::Horizontal, 1.0f);
        slider("V0",   { 225, 10 }, { 245, 170 }, UiSliderOrientation::Vertical, 0.0f);
        slider("V50",  { 255, 10 }, { 275, 170 }, UiSliderOrientation::Vertical, 0.5f);
        slider("V100", { 285, 10 }, { 305, 170 }, UiSliderOrientation::Vertical, 1.0f);
        return scene;
    }

    Ref<Scene> MakeToggleScene(const HalfTextures& tex)
    {
        Ref<Scene> scene = Scene::Create();
        Scene& s = *scene;
        Entity canvas = MakeCanvas(s);
        auto toggle = [&](const char* name, glm::vec2 min, glm::vec2 max, bool on, bool image, bool textured, glm::vec4 onTint, glm::vec4 offTint) {
            Entity e = AddRect(s, canvas, name, min, max);
            if (image) e.AddComponent<UiImageComponent>().Tint = kWhite;
            auto& t = e.AddComponent<UiToggleComponent>();
            t.Channel = "t"; t.PreviewOn = on; t.OnTint = onTint; t.OffTint = offTint;
            if (textured) { t.OnTexture = tex.OnPath; t.OffTexture = tex.OffPath; }
            return e;
        };
        toggle("On",      { 10, 10 },  { 90, 50 },   true,  true,  false, kGreen, kRed);
        toggle("Off",     { 100, 10 }, { 180, 50 },  false, true,  false, kGreen, kRed);
        toggle("TexOn",   { 190, 10 }, { 250, 50 },  true,  true,  true,  kWhite, kWhite);
        toggle("TexOff",  { 260, 10 }, { 310, 50 },  false, true,  true,  kWhite, kWhite);
        toggle("NoImage", { 10, 60 },  { 90, 100 },  true,  false, false, kYellow, kRed);
        return scene;
    }

    Ref<Scene> MakeHostedPanelScene()
    {
        Ref<Scene> scene = Scene::Create();
        Scene& s = *scene;
        Entity canvas = MakeCanvas(s);
        Entity a = AddRect(s, canvas, "Panel", { 20, 20 }, { 300, 125 });
        { auto& hp = a.AddComponent<UiHostedPanelComponent>(); hp.PanelName = "telemetry"; hp.ShowFrame = true; hp.FrameColor = kWhite; }
        Entity b = AddRect(s, canvas, "NoFrame", { 200, 130 }, { 310, 175 }, 1);
        { auto& hp = b.AddComponent<UiHostedPanelComponent>(); hp.PanelName = "x"; hp.ShowFrame = false; hp.FrameColor = kRed; hp.PlaceholderText = "custom"; }
        return scene;
    }

    // The E05 combined scene: one of each bound widget, preview values that a bus
    // can hold exactly. The plot's preview is the engine's sine (two cycles, 128
    // steps over the window); a bus replicates it sample-for-sample with exact
    // binary times (10 s / 128 = 0.078125 is a power-of-two fraction).
    struct ComboScene
    {
        Ref<Scene> S;
        Entity Value, Gauge, Indicator, Plot, Slider, Toggle, Panel;
    };

    ComboScene MakeComboScene()
    {
        ComboScene f;
        f.S = Scene::Create();
        Scene& s = *f.S;
        Entity canvas = MakeCanvas(s);
        f.Value = AddRect(s, canvas, "Value", { 10, 5 }, { 200, 45 });
        { auto& t = f.Value.AddComponent<UiTextComponent>(); t.SizePx = 24.0f; t.HAlign = UiHAlign::Left; t.Color = kWhite;
          auto& v = f.Value.AddComponent<UiValueTextComponent>(); v.Channel = "v"; v.Format = "%.1f"; v.Suffix = " V"; v.PreviewValue = 12.5f; }
        f.Gauge = AddRect(s, canvas, "Gauge", { 10, 50 }, { 200, 70 });
        { auto& g = f.Gauge.AddComponent<UiGaugeComponent>(); g.Channel = "g"; g.FillColor = kGreen; g.TrackColor = kBlue; g.PreviewValue = 50.0f; }
        f.Indicator = AddRect(s, canvas, "Indicator", { 210, 5 }, { 250, 45 });
        { auto& i = f.Indicator.AddComponent<UiIndicatorComponent>(); i.Channel = "i"; i.Op = "=="; i.Threshold = 1.0f; i.OnTint = kGreen; i.OffTint = kRed; i.PreviewOn = true; }
        f.Toggle = AddRect(s, canvas, "Toggle", { 260, 5 }, { 310, 45 });
        { f.Toggle.AddComponent<UiImageComponent>().Tint = kWhite;
          auto& t = f.Toggle.AddComponent<UiToggleComponent>(); t.Channel = "t"; t.OnTint = kGreen; t.OffTint = kRed; t.PreviewOn = false; }
        f.Slider = AddRect(s, canvas, "Slider", { 10, 75 }, { 200, 100 });
        { auto& sl = f.Slider.AddComponent<UiSliderComponent>(); sl.Channel = "s"; sl.TrackColor = kBlue; sl.FillColor = kGreen; sl.KnobColor = kWhite; sl.PreviewValue = 0.5f; }
        f.Plot = AddRect(s, canvas, "Plot", { 10, 105 }, { 200, 175 });
        { auto& p = f.Plot.AddComponent<UiPlotComponent>(); p.Channel = "p"; p.WindowSeconds = 10.0f; p.AutoScaleY = true; p.LineColor = kYellow;
          p.BackgroundColor = kPlotBg; p.PreviewAmplitude = 1.0f; }
        f.Panel = AddRect(s, canvas, "Panel", { 210, 50 }, { 310, 175 });
        { auto& hp = f.Panel.AddComponent<UiHostedPanelComponent>(); hp.PanelName = "panel"; hp.FrameColor = kWhite; }
        return f;
    }

    // Fill `bus` so every widget of the combo scene reads exactly its preview value.
    void FillComboBusWithPreview(DataBus& bus, double plotAmp = 1.0)
    {
        bus.Set("v", 12.5);
        bus.Set("g", 50.0);
        bus.SetBool("i", true);
        bus.SetBool("t", false);
        bus.Set("s", 0.5);
        constexpr int kPreviewPoints = 128;
        for (int k = 0; k <= kPreviewPoints; ++k)
        {
            const double x = (double)k / (double)kPreviewPoints;
            bus.Set("p", plotAmp * std::sin(x * 4.0 * glm::pi<double>() + 0 * glm::half_pi<double>()));
            if (k < kPreviewPoints) bus.Advance(10.0 / kPreviewPoints);
        }
    }

    // Union-of-rects containment for the E05 diff mask.
    struct DiffStats
    {
        size_t Differing = 0;
        size_t Outside   = 0;
        uint32_t FirstOutsideX = 0, FirstOutsideY = 0;
    };

    DiffStats DiffWithin(const Image& a, const Image& b, const std::vector<UiRect>& rects)
    {
        DiffStats d;
        REQUIRE(a.Width == b.Width);
        REQUIRE(a.Height == b.Height);
        for (uint32_t y = 0; y < a.Height; ++y)
            for (uint32_t x = 0; x < a.Width; ++x)
            {
                if (PixelAt(a, x, y) == PixelAt(b, x, y)) continue;
                ++d.Differing;
                const glm::vec2 c{ x + 0.5f, y + 0.5f };
                bool inside = false;
                for (const UiRect& r : rects) if (r.Contains(c)) { inside = true; break; }
                if (!inside)
                {
                    if (d.Outside == 0) { d.FirstOutsideX = x; d.FirstOutsideY = y; }
                    ++d.Outside;
                }
            }
        return d;
    }

    size_t CountDiffIn(const Image& a, const Image& b, const UiRect& r)
    {
        size_t n = 0;
        for (uint32_t y = (uint32_t)std::max(0.0f, r.Min.y); y < (uint32_t)r.Max.y && y < a.Height; ++y)
            for (uint32_t x = (uint32_t)std::max(0.0f, r.Min.x); x < (uint32_t)r.Max.x && x < a.Width; ++x)
                if (PixelAt(a, x, y) != PixelAt(b, x, y)) ++n;
        return n;
    }

    UiRect RectOf(Scene& s, Entity e)
    {
        std::vector<UiElement> els;
        UiSystem::CollectElements(s, kCanvas, els);
        for (const UiElement& el : els)
            if (el.Handle == (uint32_t)(entt::entity)e) return el.Rect;
        FAIL("element not laid out");
        return {};
    }
}

TEST_SUITE("AP-02 V03")
{
    TEST_CASE("V03 value text — fresh (text colour), stale (StaleColor), missing (placeholder); UiText.Text untouched (golden ap02_valuetext)")
    {
        Ref<FrameBuffer> fbo = MakeRgba8Target(kW, kH);
        REQUIRE(fbo != nullptr);
        ValueTextScene f = MakeValueTextScene();
        StatsScope stats;
        Image frame;
        RenderUi(*f.S, fbo, frame, f.Bus.get(), false);
        WriteEvidence("ap02-valuetext", frame);

        // Fresh row: white ink; no red. Stale row: red ink; no white. Missing row: cyan ink ("T --").
        CHECK(CountColor(frame, 10, 10, 300, 50, ToU8(kWhite), 8) >= 40);
        CHECK(CountColor(frame, 10, 10, 300, 50, ToU8(kRed), 8) == 0);
        CHECK(CountColor(frame, 10, 65, 300, 50, ToU8(kRed), 8) >= 40);
        CHECK(CountColor(frame, 10, 65, 300, 50, ToU8(kWhite), 8) == 0);
        CHECK(CountColor(frame, 10, 120, 300, 50, ToU8(kCyan), 8) >= 10);
        // The placeholder row is short ("T --"): nothing drawn in its right half.
        CHECK(CountInk(frame, 160, 120, 150, 50, kClearU8) == 0);
        // Fresh row's ink is left-aligned and longer than the placeholder row's.
        CHECK(CountInk(frame, 10, 10, 60, 50, kClearU8) > 0);
        CHECK(CountInk(frame, 10, 10, 300, 50, kClearU8) > CountInk(frame, 10, 120, 300, 50, kClearU8));

        // The sibling UiText's own Text is never modified.
        CHECK(f.Fresh.GetComponent<UiTextComponent>().Text == "SHOULD NOT SHOW");
        CHECK(f.Stale.GetComponent<UiTextComponent>().Text == "SHOULD NOT SHOW");
        CHECK(f.Missing.GetComponent<UiTextComponent>().Text == "SHOULD NOT SHOW");
        // Only glyphs were drawn (3 strings, no widget quads), all in one flush.
        const Renderer2D::Statistics st = stats.Get();
        CHECK(st.GlyphCount == st.QuadCount);
        CHECK(st.GlyphCount >= 12);
        CHECK(st.DrawCalls == 1u);

        CHECK(CheckGolden("ap02_valuetext", frame));
    }

    TEST_CASE("V03 gauge — bar at 0/50/100 %, bottom-to-top bar, arc at 0/50/100 % with the bottom gap (golden ap02_gauge)")
    {
        Ref<FrameBuffer> fbo = MakeRgba8Target(kW, kH);
        REQUIRE(fbo != nullptr);
        Ref<Scene> scene = MakeGaugeScene();
        Image frame;
        RenderUi(*scene, fbo, frame, nullptr, false);   // no bus => preview values
        WriteEvidence("ap02-gauge", frame);

        const glm::u8vec4 fill = ToU8(kGreen), track = ToU8(kBlue);
        // Bar 0 %: track everywhere.
        CHECK(PixelAt(frame, 11, 20) == track);
        CHECK(PixelAt(frame, 109, 20) == track);
        // Bar 50 %: the fill edge sits at x = 170 (pixels 120..169 fill, 170.. track).
        CHECK(PixelAt(frame, 121, 20) == fill);
        CHECK(PixelAt(frame, 169, 20) == fill);
        CHECK(PixelAt(frame, 170, 20) == track);
        CHECK(PixelAt(frame, 219, 20) == track);
        // Bar 100 %: fill everywhere.
        CHECK(PixelAt(frame, 231, 20) == fill);
        CHECK(PixelAt(frame, 309, 20) == fill);
        // Bottom-to-top 50 %: the fill edge at y = 80 (rows 80..119 fill).
        CHECK(PixelAt(frame, 20, 79) == track);
        CHECK(PixelAt(frame, 20, 80) == fill);
        CHECK(PixelAt(frame, 20, 119) == fill);
        CHECK(PixelAt(frame, 20, 41) == track);
        // Arcs: centre (cx, 140), radius 30, ring 9 px at mid radius 25.5. The fill
        // sweeps from the bottom-left (135 deg) clockwise; 50 % ends at the top.
        auto ring = [](float cx, float deg) {
            const float a = glm::radians(deg);
            return glm::uvec2((uint32_t)std::floor(cx + 25.5f * std::cos(a)), (uint32_t)std::floor(140.0f + 25.5f * std::sin(a)));
        };
        for (float cx : { 90.0f, 160.0f, 230.0f })
        {
            const glm::uvec2 gap = ring(cx, 90.0f);          // the bottom: the 90-degree gap
            CHECK_MESSAGE(PixelAt(frame, gap.x, gap.y) == kClearU8, "arc gap at the bottom, cx=" << cx);
            const glm::uvec2 mid = ring(cx, 225.0f);         // upper-left, t = 1/3
            const glm::u8vec4 c = PixelAt(frame, mid.x, mid.y);
            CHECK_MESSAGE((c == fill || c == track), "ring pixel is fill or track, cx=" << cx << " got " << Describe(c));
        }
        CHECK(PixelAt(frame, ring(90.0f, 225.0f).x, ring(90.0f, 225.0f).y) == track);    // 0 %: all track
        CHECK(PixelAt(frame, ring(90.0f, 315.0f).x, ring(90.0f, 315.0f).y) == track);
        CHECK(PixelAt(frame, ring(160.0f, 180.0f).x, ring(160.0f, 180.0f).y) == fill);   // 50 %: left is filled ...
        CHECK(PixelAt(frame, ring(160.0f, 225.0f).x, ring(160.0f, 225.0f).y) == fill);
        CHECK(PixelAt(frame, ring(160.0f, 315.0f).x, ring(160.0f, 315.0f).y) == track);  // ... upper-right is not
        CHECK(PixelAt(frame, ring(160.0f, 0.0f).x, ring(160.0f, 0.0f).y) == track);
        CHECK(PixelAt(frame, ring(230.0f, 225.0f).x, ring(230.0f, 225.0f).y) == fill);   // 100 %: all fill
        CHECK(PixelAt(frame, ring(230.0f, 315.0f).x, ring(230.0f, 315.0f).y) == fill);
        CHECK(PixelAt(frame, ring(230.0f, 0.0f).x, ring(230.0f, 0.0f).y) == fill);
        // Inside the ring is empty (the hole).
        CHECK(PixelAt(frame, 160, 140) == kClearU8);

        CHECK(CheckGolden("ap02_gauge", frame));
    }

    TEST_CASE("V03 indicator — on/off with half-and-half textures and solid tints (golden ap02_indicator)")
    {
        Ref<FrameBuffer> fbo = MakeRgba8Target(kW, kH);
        REQUIRE(fbo != nullptr);
        const HalfTextures tex = WriteHalfTextures();
        Ref<Scene> scene = MakeIndicatorScene(tex);
        Image frame;
        RenderUi(*scene, fbo, frame, nullptr, false);
        WriteEvidence("ap02-indicator", frame);

        // Textured ON: left half green, right half yellow (the texture, not a tint).
        CHECK(Near(PixelAt(frame, 25, 50), HalfTextures::OnLeft, 2));
        CHECK(Near(PixelAt(frame, 75, 50), HalfTextures::OnRight, 2));
        // Textured OFF: the other texture.
        CHECK(Near(PixelAt(frame, 115, 50), HalfTextures::OffLeft, 2));
        CHECK(Near(PixelAt(frame, 165, 50), HalfTextures::OffRight, 2));
        // Solid ON red, solid OFF blue.
        CHECK(PixelAt(frame, 220, 40) == ToU8(kRed));
        CHECK(PixelAt(frame, 285, 35) == ToU8(kBlue));
        CHECK(PixelAt(frame, 5, 5) == kClearU8);

        // The textures were resolved through the production slot cache.
        auto& reg = scene->GetRegistry();
        int resolved = 0;
        for (auto e : reg.view<UiIndicatorComponent>())
        {
            const auto& i = reg.get<UiIndicatorComponent>(e);
            if (!i.OnTexture.empty()) { CHECK(i.ResolvedOn != nullptr); CHECK(i.ResolvedOff != nullptr); ++resolved; }
            else                      { CHECK(i.ResolvedOn == nullptr); CHECK(i.ResolvedOff == nullptr); }
        }
        CHECK(resolved == 2);

        CHECK(CheckGolden("ap02_indicator", frame));
    }

    TEST_CASE("V03 plot — two channels over a 10 s window with a non-finite gap, fixed Y range, grid + labels (golden ap02_plot)")
    {
        Ref<FrameBuffer> fbo = MakeRgba8Target(kW, kH);
        REQUIRE(fbo != nullptr);
        PlotScene f = MakePlotScene();
        StatsScope stats;
        Image frame;
        RenderUi(*f.S, fbo, frame, f.Bus.get(), false);
        WriteEvidence("ap02-plot", frame);

        // Channel "b" = 0.6 => y = 170 - 0.8 * 160 = 42; drawn at t in [0,3] and [7,10].
        CHECK(PixelAt(frame, 55, 42) == ToU8(kMagenta));      // t = 1.5 (x = 0.15)
        CHECK(PixelAt(frame, 280, 42) == ToU8(kMagenta));     // t = 9.0
        // The NaN gap (3, 7): background at t = 4.5 on the same row, no line.
        CHECK(PixelAt(frame, 145, 42) == ToU8(kPlotBg));
        CHECK(CountColor(frame, 110, 40, 100, 5, ToU8(kMagenta), 2) == 0);
        // Channel "a" peaks (+0.8 => y = 26) at t = 1.25 (x = 47.5) and troughs (-0.8 => y = 154) at t = 3.75.
        CHECK(PixelAt(frame, 47, 26) == ToU8(kYellow));
        CHECK(PixelAt(frame, 122, 154) == ToU8(kYellow));
        // Background where nothing is drawn (between grid lines / away from traces).
        CHECK(PixelAt(frame, 30, 120) == ToU8(kPlotBg));
        // Grid lines (white at 12 % over the background) at x = 85 and y = 130.
        {
            const glm::u8vec4 g = PixelAt(frame, 85, 100);
            CHECK(g.r > ToU8(kPlotBg).r + 10);
            CHECK(g.r < 120);
            CHECK(PixelAt(frame, 200, 130).r > ToU8(kPlotBg).r + 10);
        }
        // Labels: ink in the top-left ("1"), bottom-left ("-1") and bottom-right ("10s") corners.
        CHECK(CountInk(frame, 11, 11, 30, 14, ToU8(kPlotBg)) > 0);
        CHECK(CountInk(frame, 11, 156, 30, 13, ToU8(kPlotBg)) > 0);
        CHECK(CountInk(frame, 270, 156, 39, 13, ToU8(kPlotBg)) > 0);
        // Decimation: 601 samples per channel => at most 512 segments each; the line
        // batch carries nothing (traces and grid are quads) so MaxLines is never reached.
        const Renderer2D::Statistics st = stats.Get();
        CHECK(st.LineCount == 0u);
        CHECK(st.QuadCount - st.GlyphCount <= 1u + 2u * 5u + 2u * 512u);
        CHECK(st.QuadCount - st.GlyphCount >= 1u + 2u * 5u + 300u);       // both traces really drawn
        CHECK(st.DrawCalls == 2u);                                         // one quad draw + one glyph draw, one flush
        CHECK(st.Flushes == 1u);

        CHECK(CheckGolden("ap02_plot", frame));
    }

    TEST_CASE("V03 slider — horizontal and vertical at 0 / 0.5 / 1 with the knob centre sentinels (golden ap02_slider)")
    {
        Ref<FrameBuffer> fbo = MakeRgba8Target(kW, kH);
        REQUIRE(fbo != nullptr);
        Ref<Scene> scene = MakeSliderScene();
        Image frame;
        RenderUi(*scene, fbo, frame, nullptr, false);
        WriteEvidence("ap02-slider", frame);

        const glm::u8vec4 knob = ToU8(kWhite), fill = ToU8(kGreen), track = ToU8(kBlue);
        // H0: knob at x = 10, the whole track blue.
        CHECK(PixelAt(frame, 10, 25) == knob);
        CHECK(PixelAt(frame, 100, 25) == track);
        CHECK(PixelAt(frame, 205, 25) == track);
        // H50: knob centre at (110, 65); fill left of it, track right of it.
        CHECK(PixelAt(frame, 110, 65) == knob);
        CHECK(PixelAt(frame, 50, 65) == fill);
        CHECK(PixelAt(frame, 180, 65) == track);
        CHECK(PixelAt(frame, 50, 55) == kClearU8);                 // above the thin track
        // H100: knob at x = 210 (pixel 209), fill everywhere left.
        CHECK(PixelAt(frame, 209, 105) == knob);
        CHECK(PixelAt(frame, 20, 105) == fill);
        // V0: knob at the bottom; V50: knob centre at y = 90 with fill below and track above; V100: knob at the top.
        CHECK(PixelAt(frame, 235, 165) == knob);
        CHECK(PixelAt(frame, 235, 40) == track);
        CHECK(PixelAt(frame, 265, 90) == knob);
        CHECK(PixelAt(frame, 265, 140) == fill);
        CHECK(PixelAt(frame, 265, 40) == track);
        CHECK(PixelAt(frame, 295, 12) == knob);
        CHECK(PixelAt(frame, 295, 100) == fill);

        CHECK(CheckGolden("ap02_slider", frame));
    }

    TEST_CASE("V03 toggle — on/off tints into the sibling image, on/off textures, and the image-less fallback (golden ap02_toggle)")
    {
        Ref<FrameBuffer> fbo = MakeRgba8Target(kW, kH);
        REQUIRE(fbo != nullptr);
        const HalfTextures tex = WriteHalfTextures();
        Ref<Scene> scene = MakeToggleScene(tex);
        Image frame;
        RenderUi(*scene, fbo, frame, nullptr, false);
        WriteEvidence("ap02-toggle", frame);

        CHECK(PixelAt(frame, 50, 30) == ToU8(kGreen));             // On: image tint * OnTint
        CHECK(PixelAt(frame, 140, 30) == ToU8(kRed));              // Off: image tint * OffTint
        CHECK(Near(PixelAt(frame, 200, 30), HalfTextures::OnLeft, 2));    // textured on (left half)
        CHECK(Near(PixelAt(frame, 240, 30), HalfTextures::OnRight, 2));
        CHECK(Near(PixelAt(frame, 270, 30), HalfTextures::OffLeft, 2));   // textured off
        CHECK(Near(PixelAt(frame, 300, 30), HalfTextures::OffRight, 2));
        CHECK(PixelAt(frame, 50, 80) == ToU8(kYellow));            // no sibling image: own quad
        CHECK(PixelAt(frame, 150, 80) == kClearU8);

        CHECK(CheckGolden("ap02_toggle", frame));
    }

    TEST_CASE("V03 / V05 hosted panel — frame + placeholder in preview mode; no frame when ShowFrame is off (golden ap02_hostedpanel)")
    {
        Ref<FrameBuffer> fbo = MakeRgba8Target(kW, kH);
        REQUIRE(fbo != nullptr);
        Ref<Scene> scene = MakeHostedPanelScene();
        Image frame;
        RenderUi(*scene, fbo, frame, nullptr, false);
        WriteEvidence("ap02-hostedpanel", frame);

        const glm::u8vec4 frameCol = ToU8(kWhite);
        // 1-px frame on all four edges of {20,20}-{300,125}.
        CHECK(PixelAt(frame, 20, 90) == frameCol);
        CHECK(PixelAt(frame, 299, 90) == frameCol);
        CHECK(PixelAt(frame, 150, 20) == frameCol);
        CHECK(PixelAt(frame, 150, 124) == frameCol);
        CHECK(PixelAt(frame, 21, 21) == kClearU8);                 // just inside the corner
        CHECK(PixelAt(frame, 19, 90) == kClearU8);                 // just outside
        // Placeholder label ("telemetry") centred: ink around the centre, none near the corners.
        CHECK(CountInk(frame, 110, 62, 100, 20, kClearU8) > 20);
        CHECK(CountInk(frame, 25, 25, 60, 30, kClearU8) == 0);
        // Second panel: no frame (its red FrameColor appears only as the label's colour), label present.
        CHECK(PixelAt(frame, 200, 150) == kClearU8);
        CHECK(PixelAt(frame, 255, 130) == kClearU8);
        CHECK(CountInk(frame, 205, 135, 100, 35, kClearU8) > 10);

        // The engine half of V05: the same scene collects both panels back-to-front
        // with the rects the frame was drawn at.
        std::vector<UiHostedPanelDraw> panels;
        UiSystem::CollectHostedPanels(*scene, kCanvas, panels);
        REQUIRE(panels.size() == 2);
        CHECK(panels[0].Name == "telemetry");
        CHECK(panels[0].Rect.Min == glm::vec2(20.0f, 20.0f));
        CHECK(panels[0].Rect.Max == glm::vec2(300.0f, 125.0f));
        CHECK(panels[1].Name == "x");
        CHECK(panels[1].Rect.Min == glm::vec2(200.0f, 130.0f));

        // Live mode with the host reporting a draw: the frame stays, the label goes.
        {
            DataBus bus;
            auto& reg = scene->GetRegistry();
            for (auto e : reg.view<UiHostedPanelComponent>()) reg.get<UiHostedPanelComponent>(e).DrawnThisFrame = true;
            Image live;
            RenderUi(*scene, fbo, live, &bus, false);
            CHECK(PixelAt(live, 20, 90) == frameCol);
            CHECK(CountInk(live, 110, 62, 100, 20, kClearU8) == 0);
            CHECK(CountInk(live, 205, 135, 100, 35, kClearU8) == 0);
            // ... and without the report the placeholder is back (unregistered name).
            for (auto e : reg.view<UiHostedPanelComponent>()) reg.get<UiHostedPanelComponent>(e).DrawnThisFrame = false;
            Image unreg;
            RenderUi(*scene, fbo, unreg, &bus, false);
            CHECK(BytesEqual(unreg, frame));
        }

        CHECK(CheckGolden("ap02_hostedpanel", frame));
    }
}

TEST_SUITE("AP-02 E05")
{
    TEST_CASE("E05 preview vs live: identical values => byte-identical frames; different values => the diff is confined to the widget rects and every bound widget moved")
    {
        Ref<FrameBuffer> fbo = MakeRgba8Target(kW, kH);
        REQUIRE(fbo != nullptr);
        ComboScene f = MakeComboScene();

        // A: preview (no bus). Also: preview == Render(bus, preview = true) with the same bus.
        Image preview;
        RenderUi(*f.S, fbo, preview, nullptr, false);
        WriteEvidence("ap02-e05-preview", preview);
        REQUIRE(CountInk(preview, 0, 0, kW, kH, kClearU8) > 500);   // something was drawn

        // B: live, bus holding exactly the preview values.
        DataBus same;
        FillComboBusWithPreview(same);
        Image live;
        RenderUi(*f.S, fbo, live, &same, false);
        WriteEvidence("ap02-e05-live-same", live);
        CHECK(BytesEqual(preview, live));

        Image previewWithBus;
        RenderUi(*f.S, fbo, previewWithBus, &same, true);
        CHECK(BytesEqual(preview, previewWithBus));

        // A second preview render is itself byte-stable (the pair is meaningful).
        Image preview2;
        RenderUi(*f.S, fbo, preview2, nullptr, false);
        CHECK(BytesEqual(preview, preview2));

        // B': live with DIFFERENT values: the diff is inside the union of widget rects.
        DataBus other;
        other.Set("v", 99.9);
        other.Set("g", 80.0);
        other.SetBool("i", false);
        other.SetBool("t", true);
        other.Set("s", 0.1);
        for (int k = 0; k <= 128; ++k)
        {
            other.Set("p", 0.5 * std::sin((double)k / 128.0 * 4.0 * glm::pi<double>()) + 0.25);
            if (k < 128) other.Advance(10.0 / 128.0);
        }
        Image liveOther;
        RenderUi(*f.S, fbo, liveOther, &other, false);
        WriteEvidence("ap02-e05-live-other", liveOther);

        Scene& s = *f.S;
        const std::vector<Entity> bound = { f.Value, f.Gauge, f.Indicator, f.Toggle, f.Slider, f.Plot };
        std::vector<UiRect> rects;
        for (Entity e : bound) rects.push_back(RectOf(s, e));
        const DiffStats d = DiffWithin(preview, liveOther, rects);
        CHECK(d.Differing > 200);
        CHECK_MESSAGE(d.Outside == 0, "first pixel outside the widget rects: (" << d.FirstOutsideX << ", " << d.FirstOutsideY << ")");
        for (Entity e : bound)
            CHECK_MESSAGE(CountDiffIn(preview, liveOther, RectOf(s, e)) > 0, "widget did not react to the bus: " << e.GetComponent<TagComponent>().Tag);
        // The hosted panel (not bus-bound) did not change.
        CHECK(CountDiffIn(preview, liveOther, RectOf(s, f.Panel)) == 0);

        // Spot values: gauge fill edge moved from 50 % (x = 105) to 80 % (x = 162); indicator red; toggle green; slider knob at 10 %.
        CHECK(PixelAt(preview, 100, 60) == ToU8(kGreen));
        CHECK(PixelAt(preview, 110, 60) == ToU8(kBlue));
        CHECK(PixelAt(liveOther, 150, 60) == ToU8(kGreen));
        CHECK(PixelAt(liveOther, 170, 60) == ToU8(kBlue));
        CHECK(PixelAt(preview, 230, 25) == ToU8(kGreen));
        CHECK(PixelAt(liveOther, 230, 25) == ToU8(kRed));
        CHECK(PixelAt(preview, 285, 25) == ToU8(kRed));
        CHECK(PixelAt(liveOther, 285, 25) == ToU8(kGreen));
        CHECK(PixelAt(preview, 105, 87) == ToU8(kWhite));          // knob at 0.5 => x = 10 + 0.5 * 190 = 105
        CHECK(PixelAt(liveOther, 29, 87) == ToU8(kWhite));         // knob at 0.1 => x = 29
    }
}
