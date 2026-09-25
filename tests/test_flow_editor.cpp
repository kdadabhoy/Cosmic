// test_flow_editor.cpp — UX-01 (UX & Shipping): the headless halves of the flow-editor and
// Editors-host acceptance cases (docs/plans/ux-shipping-2026-09-24/03-Acceptance-Catalog.md).
//
//   FE01 U  NodeCanvas::RouteLink (Projects/Starforge/src/widgets/NodeCanvasRoute.cpp, the code
//           the editor ships): forward, backward same row, backward above, self-loop, vertical
//           stack — 64 samples of each cubic never enter the source node rect grown by 2 px
//           except at its own pin; backward / self-loop curves pass below the union of both
//           rects; bit-exact control points; forward links equal imgui-node-editor's own curve.
//   FE02 U  the trigger-kind selector's command path (editors/FlowTrigger.cpp, the code the
//           inspector runs): Event -> When -> Event restores the transition field for field
//           (no guard, no "if"); FlowAsset::Validate refuses `when` without a guard and reports
//           an empty guard on any transition; F-FLOWS — LoadFromString + SaveToString of every
//           tracked .cflow hashes (SHA-256) as at the base 6021822.
//   FE03 U  AssetEditorHost's draw rule: Open raises the View flag, a cleared flag hides the
//           window while documents stay open, a later Open raises it, CloseAll empties.
//   FE05 U  AssetEditorHost tab ids: per-document counters, stable when another document closes.
// The editor halves (FE03 ✕ / FE04 / FE05 dirty frames) run in UX01EditorSelfTest.cpp.

#include <doctest.h>

#include "../Projects/Starforge/src/widgets/NodeCanvas.h"
#include "../Projects/Starforge/src/editors/FlowTrigger.h"
#include "../Projects/Starforge/src/editors/AssetEditorHost.h"

#include "scene/FlowMachine.h"

#include <imgui_internal.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace Cosmic;
using Starforge::NodeCanvas;
namespace FT = Starforge::FlowTrigger;
namespace fs = std::filesystem;

namespace
{
    // ---- FE01 helpers ----------------------------------------------------------------------
    ImVec2 Bezier(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, float t)
    {
        const float u = 1.0f - t;
        const float a = u * u * u, b = 3.0f * u * u * t, c = 3.0f * u * t * t, d = t * t * t;
        return ImVec2(a * p0.x + b * p1.x + c * p2.x + d * p3.x, a * p0.y + b * p1.y + c * p2.y + d * p3.y);
    }

    // imgui-node-editor's Link::GetCurve before the UX-01 patch (imgui_node_editor.cpp:955-982 at
    // 6021822), for pins of strength `strength` and the default directions (1,0) / (-1,0).
    void TodayCurve(ImVec2 start, ImVec2 end, float pinStrength, ImVec2& cp0, ImVec2& cp1)
    {
        auto easeLinkStrength = [](const ImVec2& a, const ImVec2& b, float strength)
        {
            const auto distanceX    = b.x - a.x;
            const auto distanceY    = b.y - a.y;
            const auto distance     = ImSqrt(distanceX * distanceX + distanceY * distanceY);
            const auto halfDistance = distance * 0.5f;
            if (halfDistance < strength)
                strength = strength * ImSin(IM_PI * 0.5f * halfDistance / strength);
            return strength;
        };
        const ImVec2 startDir(1.0f, 0.0f), endDir(-1.0f, 0.0f);
        const float startStrength = easeLinkStrength(start, end, pinStrength);
        const float endStrength   = easeLinkStrength(start, end, pinStrength);
        cp0 = ImVec2(start.x + startDir.x * startStrength, start.y + startDir.y * startStrength);
        cp1 = ImVec2(end.x + endDir.x * endStrength, end.y + endDir.y * endStrength);
    }

    struct RouteCase
    {
        const char* Name;
        ImRect Src, Dst;
        ImVec2 Start, End;     // output pin pivot (right edge, 8 px node padding), input pin pivot
        bool   Forward;
    };

    // The FE01 oracle: sample the cubic at 64 points; inside the source rect grown by 2 px a
    // sample is allowed only in its own pin's zone (within 1.5x the pin-to-grown-edge distance
    // + 2 px of the start pin; for a self-loop also of the end pin, which sits in the same box).
    // Returns the number of offending samples; `maxY` gets the deepest sample.
    int SamplesInsideSource(const RouteCase& c, const ImVec2& cp0, const ImVec2& cp1, float& maxY, std::string& first)
    {
        ImRect g = c.Src;
        g.Expand(2.0f);
        const bool selfLoop = c.Src.Min.x == c.Dst.Min.x && c.Src.Min.y == c.Dst.Min.y &&
                              c.Src.Max.x == c.Dst.Max.x && c.Src.Max.y == c.Dst.Max.y;
        const float zone0 = 1.5f * (g.Max.x - c.Start.x) + 2.0f;
        const float zone1 = 1.5f * (c.End.x - g.Min.x) + 2.0f;
        int bad = 0;
        maxY = -1e30f;
        for (int i = 0; i < 64; ++i)
        {
            const float t = (float)i / 63.0f;
            const ImVec2 p = Bezier(c.Start, cp0, cp1, c.End, t);
            maxY = std::max(maxY, p.y);
            const bool inside = p.x > g.Min.x && p.x < g.Max.x && p.y > g.Min.y && p.y < g.Max.y;
            if (!inside) continue;
            const float d0 = std::hypot(p.x - c.Start.x, p.y - c.Start.y);
            const float d1 = std::hypot(p.x - c.End.x, p.y - c.End.y);
            if (d0 <= zone0) continue;
            if (selfLoop && d1 <= zone1) continue;
            if (!bad)
            {
                char buf[160];
                std::snprintf(buf, sizeof(buf), "t=%.3f (%.1f, %.1f), %.1f px from the pin", t, p.x, p.y, d0);
                first = buf;
            }
            ++bad;
        }
        return bad;
    }

    const RouteCase kCases[] = {
        // FlowEditor-sized nodes (160 x 80 / 120), pins 8 px inside the node (NodePadding).
        { "forward (target right of source)",
          ImRect(0, 0, 160, 80),     ImRect(300, 0, 460, 80),   ImVec2(152, 40),  ImVec2(308, 40),  true },
        { "backward, same row (target left)",
          ImRect(300, 0, 460, 80),   ImRect(0, 0, 160, 80),     ImVec2(452, 40),  ImVec2(8, 40),    false },
        { "backward, target left and above",
          ImRect(300, 200, 460, 280), ImRect(0, 0, 160, 80),    ImVec2(452, 240), ImVec2(8, 40),    false },
        { "self-loop",
          ImRect(0, 0, 160, 120),    ImRect(0, 0, 160, 120),    ImVec2(152, 60),  ImVec2(8, 30),    false },
        { "vertical stack (target directly below)",
          ImRect(0, 0, 160, 80),     ImRect(0, 200, 160, 280),  ImVec2(152, 40),  ImVec2(8, 240),   false },
    };

    // Bit-exact control points (hex floats) recorded from NodeCanvasRoute.cpp at UX-01; the
    // forward row is ALSO asserted equal to TodayCurve (the independent oracle).
    struct Expected { float Cp0x, Cp0y, Cp1x, Cp1y; };
    // (COSMIC_UX01_RECORD_ROUTES=1 prints them; Debug and Release agree bit for bit.)
    const Expected kExpected[] = {
        { 0x1.ec2d18p+7f,  0x1.4p+5f,       0x1.abd2e8p+7f,  0x1.4p+5f },        // 246.088, 40, 213.912, 40
        { 0x1.2cc66ap+9f,  0x1.7b19a8p+7f, -0x1.1b19a8p+7f,  0x1.7b19a8p+7f },   // 601.55, 189.55, -141.55, 189.55
        { 0x1.933104p+9f,  0x1.293104p+9f, -0x1.5a6208p+8f,  0x1.8a6208p+8f },   // 806.383, 594.383, -346.383, 394.383
        { 0x1.324ea6p+8f,  0x1.ac9d4cp+7f, -0x1.249d4cp+7f,  0x1.709d4cp+7f },   // 306.307, 214.307, -146.307, 184.307
        { 0x1.ad755p+8f,   0x1.3d755p+8f,  -0x1.0d755p+8f,   0x1.02baa8p+9f },   // 429.458, 317.458, -269.458, 517.458
    };

    // ---- FE02 helpers ----------------------------------------------------------------------
    const char* kFlow = R"({
  "cosmic_flow": 1,
  "start": "Home",
  "states": [
    { "name": "Home", "scene": "project://scenes/Home.cscene",
      "transitions": [ { "on": "start_clicked", "to": "Lab" }, { "on": "key:Escape", "to": "@quit" } ] },
    { "name": "Lab", "scene": "project://scenes/Lab.cscene",
      "transitions": [ { "on": "timer:2.5", "to": "Home" } ] }
  ]
})";

    void RequireSameTransition(const FlowTransition& a, const FlowTransition& b)
    {
        CHECK(a.On == b.On);
        CHECK(a.To == b.To);
        CHECK(a.Transition == b.Transition);
        CHECK(a.Push == b.Push);
        CHECK(a.HasGuard == b.HasGuard);
        CHECK(a.Guard.Channel == b.Guard.Channel);
        CHECK(a.Guard.Var == b.Guard.Var);
        CHECK(a.Guard.Entity == b.Guard.Entity);
        CHECK(a.Guard.Component == b.Guard.Component);
        CHECK(a.Guard.Field == b.Guard.Field);
        CHECK(a.Guard.Op == b.Guard.Op);
        CHECK((int)a.Guard.Value.ValueKind == (int)b.Guard.Value.ValueKind);
        CHECK(a.Guard.Value.Number == b.Guard.Value.Number);
        CHECK(a.Guard.Value.Bool == b.Guard.Value.Bool);
        CHECK(a.Guard.Value.String == b.Guard.Value.String);
    }

    bool AnyErrorContains(const std::vector<std::string>& errs, const char* needle)
    {
        for (const auto& e : errs)
            if (e.find(needle) != std::string::npos)
                return true;
        return false;
    }

    // Minimal SHA-256 (FIPS 180-4) for the F-FLOWS hashes.
    std::string Sha256Hex(const std::string& data)
    {
        static const uint32_t k[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
        uint32_t h[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
        auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
        std::string m = data;
        const uint64_t bits = (uint64_t)data.size() * 8ull;
        m.push_back((char)0x80);
        while (m.size() % 64 != 56) m.push_back('\0');
        for (int i = 7; i >= 0; --i) m.push_back((char)((bits >> (i * 8)) & 0xff));
        for (size_t off = 0; off < m.size(); off += 64)
        {
            uint32_t w[64];
            for (int i = 0; i < 16; ++i)
                w[i] = ((uint32_t)(uint8_t)m[off + i * 4] << 24) | ((uint32_t)(uint8_t)m[off + i * 4 + 1] << 16) |
                       ((uint32_t)(uint8_t)m[off + i * 4 + 2] << 8) | (uint32_t)(uint8_t)m[off + i * 4 + 3];
            for (int i = 16; i < 64; ++i)
            {
                const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
                const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
            }
            uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
            for (int i = 0; i < 64; ++i)
            {
                const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
                const uint32_t ch = (e & f) ^ (~e & g);
                const uint32_t t1 = hh + S1 + ch + k[i] + w[i];
                const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
                const uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
                const uint32_t t2 = S0 + mj;
                hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
            }
            h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
        }
        char out[65];
        for (int i = 0; i < 8; ++i) std::snprintf(out + i * 8, 9, "%08x", h[i]);
        return std::string(out, 64);
    }

    std::string ReadFile(const fs::path& p)
    {
        std::ifstream in(p, std::ios::binary);
        std::stringstream ss; ss << in.rdbuf();
        return ss.str();
    }

    fs::path RepoRoot() { return fs::path(COSMIC_PENDULUMLAB_DIR).parent_path().parent_path(); }

    // ---- FE03 / FE05 helpers ----------------------------------------------------------------
    struct FakeDoc final : Starforge::IAssetEditor
    {
        std::string P; bool D = false;
        FakeDoc(std::string p, bool dirty) : P(std::move(p)), D(dirty) {}
        const std::string& Path() const override { return P; }
        std::string Title() const override { return P; }
        bool Dirty() const override { return D; }
        void OnImGuiRender(Starforge::EditorContext&) override {}
    };
    Starforge::AssetEditorHost::Factory Make(const std::string& path, bool dirty = false)
    {
        return [path, dirty]() { return std::make_unique<FakeDoc>(path, dirty); };
    }
}

TEST_SUITE("UX-01 flow editor")
{
    TEST_CASE("UX-01 FE01 RouteLink: forward links keep the stock curve bit for bit; backward, self-loop and vertical-stack links stay out of the source rect and pass below both nodes")
    {
        const float strength = 100.0f;   // imgui-node-editor's default LinkStrength (every NodeCanvas pin)
        const bool record = std::getenv("COSMIC_UX01_RECORD_ROUTES") != nullptr;
        for (size_t n = 0; n < std::size(kCases); ++n)
        {
            const RouteCase& c = kCases[n];
            const std::string name = c.Name;
            CAPTURE(name);
            ImVec2 cp0, cp1;
            NodeCanvas::RouteLink(c.Start, c.End, c.Src, c.Dst, strength, cp0, cp1);
            if (record)
                std::printf("RECORD %s: { %af, %af, %af, %af }\n", c.Name, (double)cp0.x, (double)cp0.y, (double)cp1.x, (double)cp1.y);

            float maxY = 0.0f; std::string first;
            const int bad = SamplesInsideSource(c, cp0, cp1, maxY, first);
            CAPTURE(first);
            CHECK(bad == 0);

            if (c.Forward)
            {
                ImVec2 t0, t1;
                TodayCurve(c.Start, c.End, strength, t0, t1);
                CHECK(std::memcmp(&cp0, &t0, sizeof(ImVec2)) == 0);   // bit for bit
                CHECK(std::memcmp(&cp1, &t1, sizeof(ImVec2)) == 0);
            }
            else
            {
                const float unionBottom = std::max(c.Src.Max.y, c.Dst.Max.y);
                CHECK(maxY > unionBottom + 2.0f);   // below the union of both rects
            }
            if (!record)
            {
                CHECK(cp0.x == kExpected[n].Cp0x);
                CHECK(cp0.y == kExpected[n].Cp0y);
                CHECK(cp1.x == kExpected[n].Cp1x);
                CHECK(cp1.y == kExpected[n].Cp1y);
            }
        }

        // A long forward link (no easing) and a short one (eased) equal the stock curve too.
        const ImVec2 starts[] = { ImVec2(152, 40), ImVec2(152, 40), ImVec2(-3.25f, 17.5f) };
        const ImVec2 ends[]   = { ImVec2(908, 440), ImVec2(160, 44), ImVec2(12.0f, -300.0f) };
        for (int i = 0; i < 3; ++i)
        {
            ImVec2 cp0, cp1, t0, t1;
            NodeCanvas::RouteLink(starts[i], ends[i], ImRect(0, 0, 160, 80), ImRect(900, 400, 1000, 480), strength, cp0, cp1);
            TodayCurve(starts[i], ends[i], strength, t0, t1);
            CHECK(std::memcmp(&cp0, &t0, sizeof(ImVec2)) == 0);
            CHECK(std::memcmp(&cp1, &t1, sizeof(ImVec2)) == 0);
        }
    }

    TEST_CASE("UX-01 FE02 trigger kinds: Event -> When -> Event through SetKind restores the transition field for field, with no guard and no \"if\"")
    {
        FlowAsset a;
        std::string err;
        REQUIRE(FlowAsset::LoadFromString(a, kFlow, &err));
        const std::string before = a.SaveToString();
        const FlowTransition original = a.States[0].Transitions[0];
        FlowTransition& tr = a.States[0].Transitions[0];
        FT::Memory mem;

        CHECK(FT::KindOf(tr.On) == FT::Kind::Event);
        CHECK(FT::KindOf("key:Escape") == FT::Kind::Key);
        CHECK(FT::KindOf("timer:2.5") == FT::Kind::Timer);
        CHECK(FT::KindOf("when") == FT::Kind::When);

        REQUIRE(FT::SetKind(tr, FT::Kind::When, mem));
        CHECK(tr.On == "when");
        CHECK(tr.HasGuard);                       // "Guard (if)" switched on ...
        CHECK(FT::IsEmptyGuard(tr.Guard));        // ... with the empty skeleton, which Validate flags
        CHECK(AnyErrorContains(a.Validate(), "empty 'if' guard"));
        CHECK_FALSE(FT::SetKind(tr, FT::Kind::When, mem));   // no-op on the same kind

        REQUIRE(FT::SetKind(tr, FT::Kind::Event, mem));
        RequireSameTransition(tr, original);
        CHECK(a.SaveToString() == before);
        CHECK(a.SaveToString().find("\"if\"") == std::string::npos);
        CHECK(a.Validate().empty());

        // Event -> Key -> Timer -> Event: defaults for kinds never seen, the event restored.
        REQUIRE(FT::SetKind(tr, FT::Kind::Key, mem));
        CHECK(tr.On == "key:Escape");
        tr.On = "key:Space";                      // the key picker
        REQUIRE(FT::SetKind(tr, FT::Kind::Timer, mem));
        CHECK(tr.On == "timer:1");
        REQUIRE(FT::SetKind(tr, FT::Kind::Key, mem));
        CHECK(tr.On == "key:Space");              // remembered for this transition
        REQUIRE(FT::SetKind(tr, FT::Kind::Event, mem));
        CHECK(tr.On == "start_clicked");
        CHECK(a.SaveToString() == before);

        // A guard the user filled in while on When survives the switch back.
        REQUIRE(FT::SetKind(tr, FT::Kind::When, mem));
        tr.Guard.Channel = "pendulum.energy"; tr.Guard.Op = "<"; tr.Guard.Value = FlowValue::MakeNumber(0.01);
        REQUIRE(FT::SetKind(tr, FT::Kind::Event, mem));
        CHECK(tr.On == "start_clicked");
        CHECK(tr.HasGuard);
        CHECK(tr.Guard.Channel == "pendulum.energy");

        // A transition that already had a guard keeps it through When and back.
        FlowTransition& t2 = a.States[1].Transitions[0];
        t2.HasGuard = true; t2.Guard.Var = "Score"; t2.Guard.Op = ">"; t2.Guard.Value = FlowValue::MakeNumber(3.0);
        const FlowTransition t2Before = t2;
        FT::Memory mem2;
        REQUIRE(FT::SetKind(t2, FT::Kind::When, mem2));
        CHECK(t2.HasGuard);
        CHECK(t2.Guard.Var == "Score");
        REQUIRE(FT::SetKind(t2, FT::Kind::Timer, mem2));
        RequireSameTransition(t2, t2Before);

        // Timer helpers.
        float s = 0.0f;
        CHECK(FT::TimerSeconds("timer:2.5", s));
        CHECK(s == 2.5f);
        CHECK_FALSE(FT::TimerSeconds("timer:", s));
        CHECK(FT::TimerOn(0.75f) == "timer:0.75");
    }

    TEST_CASE("UX-01 FE02 FlowAsset::Validate: a when without a guard is refused and an empty guard on any transition is reported")
    {
        FlowAsset a;
        REQUIRE(FlowAsset::LoadFromString(a, kFlow, nullptr));
        REQUIRE(a.Validate().empty());

        FlowTransition when; when.On = "when"; when.To = "Home";
        a.States[1].Transitions.push_back(when);
        CHECK(AnyErrorContains(a.Validate(), "'when' transition without an 'if' guard"));
        a.States[1].Transitions.pop_back();

        // An event transition with "Guard (if)" on and no source: reported (KI-68).
        a.States[0].Transitions[0].HasGuard = true;
        const auto errs = a.Validate();
        CHECK(errs.size() == 1);
        CHECK(AnyErrorContains(errs, "empty 'if' guard"));

        // Any named source makes it a real guard.
        a.States[0].Transitions[0].Guard.Channel = "app.uptime";
        CHECK(a.Validate().empty());
        a.States[0].Transitions[0].Guard = FlowGuard{}; a.States[0].Transitions[0].Guard.Var = "Score";
        CHECK(a.Validate().empty());
        a.States[0].Transitions[0].Guard = FlowGuard{}; a.States[0].Transitions[0].Guard.Entity = "Player";
        CHECK(a.Validate().empty());

        // A when with an empty guard is reported as the empty guard (it has one; it names nothing).
        a.States[0].Transitions[0].Guard = FlowGuard{};
        a.States[0].Transitions[0].On = "when";
        CHECK(AnyErrorContains(a.Validate(), "empty 'if' guard"));
    }

    TEST_CASE("UX-01 FE02 F-FLOWS: LoadFromString + SaveToString of every tracked .cflow hashes (SHA-256) as at the base 6021822")
    {
        CHECK(Sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

        const fs::path repo = RepoRoot();
        const fs::path list = repo / "docs/plans/ux-shipping-2026-09-24/evidence/UX-01/fflows-sha256.txt";
        const char* out = std::getenv("COSMIC_UX01_FFLOWS_OUT");   // (re)generate the list at the base

        // The tracked set at 6021822 (`git ls-files *.cflow`).
        static const char* kTracked[] = {
            "Projects/PendulumLab/flows/Main.cflow",
            "Projects/Starforge/assets/templates/app/flows/Main.cflow",
            "Projects/Starforge/assets/templates/samples/FlowDemo/flows/Main.cflow",
            "Projects/Starforge/assets/templates/samples/ForgePong/flows/Main.cflow",
            "docs/plans/app-platform-2026-09-18/evidence/AP-03/ap03-Debug-dev/ap03-Main.cflow",
            "docs/plans/app-platform-2026-09-18/evidence/AP-03/ap03-Debug/ap03-Main.cflow",
            "docs/plans/app-platform-2026-09-18/evidence/AP-03/ap03-Release/ap03-Main.cflow",
            "docs/plans/app-platform-2026-09-18/evidence/GUIDE/project/flows/Main.cflow",
            "tests/fixtures/wo09/corrupt/emit-not-string.cflow",
            "tests/fixtures/wo09/corrupt/fuzz-flow-0x0904F10A-0.cflow",
            "tests/fixtures/wo09/corrupt/root-number.cflow",
            "tests/fixtures/wo09/corrupt/typed-version.cflow",
        };
        std::ostringstream computed;
        for (const char* rel : kTracked)
        {
            const fs::path p = repo / rel;
            REQUIRE_MESSAGE(fs::exists(p), rel);
            FlowAsset a;
            std::string err;
            const bool ok = FlowAsset::LoadFromString(a, ReadFile(p), &err);
            computed << (ok ? Sha256Hex(a.SaveToString()) : std::string("LOAD_REFUSED")) << "  " << rel << "\n";
        }
        if (out && *out)
        {
            std::ofstream o(out, std::ios::binary | std::ios::trunc);
            o << computed.str();
            MESSAGE("F-FLOWS list written to " << out);
        }
        REQUIRE_MESSAGE(fs::exists(list), "missing " << list.generic_string());
        const std::string expected = ReadFile(list);
        // The list is committed with LF; tolerate a CRLF checkout.
        std::string norm; for (char ch : expected) if (ch != '\r') norm.push_back(ch);
        CHECK(norm == computed.str());
    }

    TEST_CASE("UX-01 FE03 Editors host: Open raises the View flag, a cleared flag hides the window while documents stay open, a later Open raises it again, CloseAll empties")
    {
        Starforge::AssetEditorHost host;
        bool show = false;
        const std::string a = "project://flows/Main.cflow";
        REQUIRE(host.Open(a, Make(a), &show) != nullptr);
        CHECK(show);
        CHECK(host.ShouldDraw(show));

        show = false;                              // the window's ✕ (ImGui clears *p_open)
        CHECK(host.AnyOpen());
        CHECK_FALSE(host.ShouldDraw(show));        // KI-71: open documents alone never keep it drawn

        REQUIRE(host.Open(a, Make(a), &show) != nullptr);   // Screens ▸ Flow graph again
        CHECK(show);
        CHECK(host.ShouldDraw(show));
        CHECK(host.Count() == 1);                  // re-focused, not duplicated

        const std::string b = "project://flows/Dirty.cflow";
        REQUIRE(host.Open(b, Make(b, true), &show) != nullptr);
        CHECK(host.AnyDirty());
        CHECK(host.Find(b) != nullptr);

        host.CloseAll();
        CHECK_FALSE(host.AnyOpen());
        CHECK_FALSE(host.AnyDirty());
        CHECK(host.Count() == 0);
        CHECK(host.Find(a) == nullptr);
    }

    TEST_CASE("UX-01 FE05 Editors host: tab ids are per-document counters — closing a document never re-keys another, a reopened document gets a fresh id")
    {
        Starforge::AssetEditorHost host;
        bool show = false;
        const std::string a = "project://flows/A.cflow", b = "project://flows/B.cflow", c = "project://flows/C.cflow";
        host.Open(a, Make(a), &show);
        host.Open(b, Make(b), &show);
        host.Open(c, Make(c), &show);
        const uint32_t idA = host.TabId(a), idB = host.TabId(b), idC = host.TabId(c);
        CHECK(idA != 0); CHECK(idB != 0); CHECK(idC != 0);
        CHECK(idA != idB); CHECK(idB != idC); CHECK(idA != idC);

        REQUIRE(host.Close(a));                    // KI-70: index ids shifted B and C here
        CHECK(host.TabId(a) == 0);
        CHECK(host.TabId(b) == idB);
        CHECK(host.TabId(c) == idC);

        host.Open(a, Make(a), &show);              // reopened: a fresh id, colliding with nobody
        const uint32_t idA2 = host.TabId(a);
        CHECK(idA2 != 0); CHECK(idA2 != idB); CHECK(idA2 != idC);

        // The catalog's sequence: close and reopen a second document; the first keeps its id.
        REQUIRE(host.Close(c));
        host.Open(c, Make(c), &show);
        CHECK(host.TabId(b) == idB);
        CHECK(host.TabId(a) == idA2);
        CHECK(host.TabId(c) != idC);
        CHECK_FALSE(host.Close("project://flows/None.cflow"));
    }
}
