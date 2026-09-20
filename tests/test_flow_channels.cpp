// test_flow_channels.cpp — AP-01 V06: the flow additions (design contract §5).
//
// Headless, over a fake scene loader. Channel guards for all six ops on number /
// bool / string channels (through EvaluateFlowGuard and through a running
// FlowMachine with SetDataBus), a missing channel and a bus-less machine evaluating
// false with ONE warning per guard, "on": "when" transitions firing at most once per
// OnUpdate — after the signal drain, before timers, never without a guard (which
// Validate reports) — StartAt entering the named state and falling back to Start,
// FlowKeyBridge::KeyCodeFor, rising edges per key over an injected probe, unknown
// key names warning once, KeySignals as a distinct first-occurrence list, and the
// JSON contract: a v1 and a v2 file save BYTE-IDENTICAL to what the AP-05B binary
// produced (pinned below from that binary), "channel" is written only when non-empty
// and "on": "when" verbatim.

#include <doctest.h>

#include "data/DataBus.h"
#include "scene/FlowMachine.h"
#include "scene/FlowKeyBridge.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "codes/KeyCodes.h"
#include "core/Log.h"

#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace Cosmic;

namespace
{
    // Counts engine WARN lines containing `needle` while installed.
    struct WarnCounter
    {
        std::shared_ptr<CallbackSink> Sink;
        std::atomic<int> Count{ 0 };
        std::string Needle;
        explicit WarnCounter(std::string needle) : Needle(std::move(needle))
        {
            Sink = std::make_shared<CallbackSink>([this](spdlog::level::level_enum lvl, const std::string& text)
            {
                if (lvl == spdlog::level::warn && text.find(Needle) != std::string::npos) ++Count;
            });
            Log::AddSink(Sink);
        }
        ~WarnCounter() { Log::RemoveSink(Sink); }
    };

    FlowMachine::SceneLoader FakeLoader(std::vector<std::string>* loads = nullptr)
    {
        return [loads](const std::string& path) -> Ref<Scene>
        {
            if (loads) loads->push_back(path);
            return Scene::Create();
        };
    }

    FlowAsset Parse(const char* text)
    {
        FlowAsset a;
        std::string err;
        REQUIRE_MESSAGE(FlowAsset::LoadFromString(a, text, &err), err);
        return a;
    }

    // The v1 fixture and the exact bytes FlowAsset::SaveToString produced for it at
    // AP-05B (HEAD 84a8075, before any AP-01 change) — captured from that binary.
    const char* kV1 = R"({
  "cosmic_flow": 1,
  "start": "Home",
  "states": [
    { "name": "Home", "scene": "project://scenes/Home.cscene",
      "onEnter": [ { "emit": "home_shown" },
                   { "setField": { "entity": "Title", "component": "UiText", "field": "Text", "value": "Hi" } } ],
      "transitions": [
        { "on": "start_clicked", "to": "Lab", "transition": "Fade" },
        { "on": "key:Escape", "to": "@quit" },
        { "on": "timer:3", "to": "Lab", "if": { "entity": "Player", "component": "Transform", "field": "Position", "op": ">", "value": 2 } } ] },
    { "name": "Lab", "scene": "project://scenes/Lab.cscene",
      "transitions": [ { "on": "key:Escape", "to": "Home" }, { "on": "settings", "to": "Settings", "push": true } ],
      "editor": { "pos": [ 320, 40 ] } },
    { "name": "Settings", "overlay": true, "transitions": [ { "on": "back", "to": "@pop" } ] }
  ]
})";

    const char* kExpectedV1 = R"({
  "cosmic_flow": 1,
  "start": "Home",
  "states": [
    {
      "editor": {
        "pos": [
          0.0,
          0.0
        ]
      },
      "name": "Home",
      "onEnter": [
        {
          "emit": "home_shown"
        },
        {
          "setField": {
            "component": "UiText",
            "entity": "Title",
            "field": "Text",
            "value": "Hi"
          }
        }
      ],
      "scene": "project://scenes/Home.cscene",
      "transitions": [
        {
          "on": "start_clicked",
          "to": "Lab",
          "transition": "Fade"
        },
        {
          "on": "key:Escape",
          "to": "@quit"
        },
        {
          "if": {
            "component": "Transform",
            "entity": "Player",
            "field": "Position",
            "op": ">",
            "value": 2.0
          },
          "on": "timer:3",
          "to": "Lab"
        }
      ]
    },
    {
      "editor": {
        "pos": [
          320.0,
          40.0
        ]
      },
      "name": "Lab",
      "scene": "project://scenes/Lab.cscene",
      "transitions": [
        {
          "on": "key:Escape",
          "to": "Home"
        },
        {
          "on": "settings",
          "push": true,
          "to": "Settings"
        }
      ]
    },
    {
      "editor": {
        "pos": [
          0.0,
          0.0
        ]
      },
      "name": "Settings",
      "overlay": true,
      "transitions": [
        {
          "on": "back",
          "to": "@pop"
        }
      ]
    }
  ]
})";

    const char* kV2 = R"({
  "cosmic_flow": 2,
  "start": "A",
  "variables": [ { "name": "Lives", "type": "number", "default": 3 },
                 { "name": "Mood", "type": "enum", "default": "Calm", "options": [ "Calm", "Angry" ] } ],
  "states": [
    { "name": "A", "scene": "project://scenes/A.cscene",
      "onEnter": [ { "setVar": { "var": "Lives", "add": true, "value": -1 } } ],
      "transitions": [ { "on": "go", "to": "B", "if": { "var": "Lives", "op": "<=", "value": 0 } } ] },
    { "name": "B", "transitions": [ { "on": "key:Space", "to": "A" } ] }
  ]
})";

    const char* kExpectedV2 = R"({
  "cosmic_flow": 2,
  "start": "A",
  "states": [
    {
      "editor": {
        "pos": [
          0.0,
          0.0
        ]
      },
      "name": "A",
      "onEnter": [
        {
          "setVar": {
            "add": true,
            "value": -1.0,
            "var": "Lives"
          }
        }
      ],
      "scene": "project://scenes/A.cscene",
      "transitions": [
        {
          "if": {
            "op": "<=",
            "value": 0.0,
            "var": "Lives"
          },
          "on": "go",
          "to": "B"
        }
      ]
    },
    {
      "editor": {
        "pos": [
          0.0,
          0.0
        ]
      },
      "name": "B",
      "transitions": [
        {
          "on": "key:Space",
          "to": "A"
        }
      ]
    }
  ],
  "variables": [
    {
      "default": 3.0,
      "name": "Lives",
      "type": "number"
    },
    {
      "default": "Calm",
      "name": "Mood",
      "options": [
        "Calm",
        "Angry"
      ],
      "type": "enum"
    }
  ]
})";

    // A flow with channel guards, "when" transitions, timers and keys.
    const char* kChannels = R"({
  "cosmic_flow": 1,
  "start": "Lab",
  "states": [
    { "name": "Lab", "scene": "project://scenes/Lab.cscene",
      "transitions": [
        { "on": "when", "to": "Stopped", "push": true, "if": { "channel": "pendulum.energy", "op": "<", "value": 0.01 } },
        { "on": "when", "to": "Alarm", "if": { "channel": "alarm", "op": "==", "value": true } },
        { "on": "settings_clicked", "to": "Settings", "if": { "channel": "ui.locked", "op": "==", "value": false } },
        { "on": "timer:1", "to": "TimeUp" },
        { "on": "key:Escape", "to": "@quit" },
        { "on": "key:Space", "to": "Alarm" },
        { "on": "key:Escape", "to": "Alarm" } ] },
    { "name": "Stopped", "overlay": true,
      "transitions": [ { "on": "resume_clicked", "to": "@pop" } ] },
    { "name": "Alarm", "scene": "project://scenes/Alarm.cscene",
      "transitions": [ { "on": "key:F3", "to": "Lab" }, { "on": "key:Bogus", "to": "Lab" }, { "on": "key:Space", "to": "Lab" } ] },
    { "name": "Settings", "scene": "project://scenes/Settings.cscene",
      "transitions": [ { "on": "when", "to": "Lab", "if": { "channel": "mode", "op": "==", "value": "lab" } } ] },
    { "name": "TimeUp", "scene": "project://scenes/TimeUp.cscene" }
  ]
})";
}

TEST_SUITE("AP-01 V06 Flow channels")
{
    TEST_CASE("V06: channel guards — all six ops on number, bool and string channels (EvaluateFlowGuard)")
    {
        DataBus bus;
        bus.Set("n", 5.0);
        bus.SetBool("b", true);
        bus.SetString("s", "lab");
        bus.SetString("numstr", "7.5");
        bus.Set("nan", std::numeric_limits<double>::quiet_NaN());
        bus.Set("inf", std::numeric_limits<double>::infinity());
        auto lookup = [&](const std::string& ch, DataValue& out) { if (!bus.Has(ch)) return false; out = bus.Get(ch); return true; };
        auto eval = [&](const char* channel, const char* op, FlowValue value)
        {
            FlowGuard g; g.Channel = channel; g.Op = op; g.Value = value;
            return EvaluateFlowGuard(g, nullptr, {}, {}, lookup);
        };
        const auto N = [](double v) { return FlowValue::MakeNumber(v); };

        // Number channel, every op.
        CHECK(eval("n", "==", N(5)));  CHECK_FALSE(eval("n", "==", N(6)));
        CHECK(eval("n", "!=", N(6)));  CHECK_FALSE(eval("n", "!=", N(5)));
        CHECK(eval("n", "<",  N(6)));  CHECK_FALSE(eval("n", "<",  N(5)));
        CHECK(eval("n", ">",  N(4)));  CHECK_FALSE(eval("n", ">",  N(5)));
        CHECK(eval("n", "<=", N(5)));  CHECK_FALSE(eval("n", "<=", N(4)));
        CHECK(eval("n", ">=", N(5)));  CHECK_FALSE(eval("n", ">=", N(6)));
        CHECK_FALSE(eval("n", "?", N(5)));                                   // unknown op

        // Bool channel: == / != compare as bools; ordering ops are false.
        CHECK(eval("b", "==", FlowValue::MakeBool(true)));
        CHECK_FALSE(eval("b", "==", FlowValue::MakeBool(false)));
        CHECK(eval("b", "!=", FlowValue::MakeBool(false)));
        CHECK_FALSE(eval("b", "<", FlowValue::MakeBool(true)));
        CHECK_FALSE(eval("b", ">", FlowValue::MakeBool(false)));
        CHECK_FALSE(eval("b", "<=", FlowValue::MakeBool(true)));
        CHECK_FALSE(eval("b", ">=", FlowValue::MakeBool(true)));
        CHECK(eval("b", "==", N(1)));                                        // a bool channel read as a number is 0/1
        CHECK(eval("n", "==", FlowValue::MakeBool(true)));                   // a non-zero number read as a bool is true

        // String channel: == / != compare as strings; ordering ops are false.
        CHECK(eval("s", "==", FlowValue::MakeString("lab")));
        CHECK_FALSE(eval("s", "==", FlowValue::MakeString("home")));
        CHECK(eval("s", "!=", FlowValue::MakeString("home")));
        CHECK_FALSE(eval("s", "<", FlowValue::MakeString("zzz")));
        CHECK_FALSE(eval("s", ">", FlowValue::MakeString("aaa")));
        CHECK_FALSE(eval("s", "<=", FlowValue::MakeString("lab")));
        CHECK_FALSE(eval("s", ">=", FlowValue::MakeString("lab")));
        CHECK(eval("s", "==", FlowValue::MakeEnum("lab")));                   // enum literals compare like strings
        CHECK(eval("numstr", ">", N(7)));                                    // a numeric string read as a number
        CHECK(eval("n", "==", FlowValue::MakeString("5")));                  // a number read as a string ("%g")

        // Non-finite numbers evaluate false under every op; a missing channel is false.
        for (const char* op : { "==", "!=", "<", ">", "<=", ">=" })
        {
            CHECK_FALSE(eval("nan", op, N(0)));
            CHECK_FALSE(eval("inf", op, N(0)));
        }
        CHECK_FALSE(eval("missing", "==", N(0)));
        CHECK_FALSE(eval("missing", "!=", N(0)));

        // No lookup callback at all (the default) => false, with the reason reported.
        std::vector<std::string> reasons;
        FlowGuard g; g.Channel = "n"; g.Op = "=="; g.Value = N(5);
        CHECK_FALSE(EvaluateFlowGuard(g, nullptr, {}, [&](const std::string& r) { reasons.push_back(r); }));
        REQUIRE(reasons.size() == 1);
        CHECK(reasons[0] == "no channel 'n'");

        // Channel has the highest precedence over Var and Field.
        g.Var = "someVar"; g.Entity = "E"; g.Component = "C"; g.Field = "F";
        CHECK(EvaluateFlowGuard(g, nullptr, [](const std::string&, FlowValue&) { return false; }, {}, lookup));
    }

    TEST_CASE("V06: a running machine reads channel guards from SetDataBus; missing channel and no bus are false with ONE warning per guard")
    {
        WarnCounter warns("flow guard");
        FlowAsset a = Parse(kChannels);
        CHECK(a.Validate().empty());
        DataBus bus;
        bus.SetBool("ui.locked", true);
        FlowMachine m;
        m.SetSceneLoader(FakeLoader());
        m.SetDataBus(&bus);
        m.Start(a);
        REQUIRE(m.CurrentState() == "Lab");

        // Signal transition guarded by a channel: locked => stays.
        m.FeedSignal("settings_clicked");
        m.OnUpdate(0.01f);
        CHECK(m.CurrentState() == "Lab");
        bus.SetBool("ui.locked", false);
        m.FeedSignal("settings_clicked");
        m.OnUpdate(0.01f);
        CHECK(m.CurrentState() == "Settings");

        // The Settings state's `when` reads a string channel.
        m.OnUpdate(0.01f);
        CHECK(m.CurrentState() == "Settings");
        bus.SetString("mode", "lab");
        m.OnUpdate(0.01f);
        CHECK(m.CurrentState() == "Lab");

        // Missing channels ("pendulum.energy", "alarm") evaluated false each update
        // — one warning per guard, not per update.
        const int before = warns.Count;
        m.OnUpdate(0.01f);
        m.OnUpdate(0.01f);
        m.OnUpdate(0.01f);
        CHECK(m.CurrentState() == "Lab");
        CHECK(warns.Count - before <= 2);           // at most one per (missing) guard
        CHECK(warns.Count >= 2);                    // both missing guards did warn once

        // A machine with no bus: channel guards are false, warned once per guard.
        WarnCounter noBus("no data bus");
        FlowMachine n;
        n.SetSceneLoader(FakeLoader());
        n.Start(a);
        bus.Set("pendulum.energy", 0.0);
        for (int i = 0; i < 5; ++i) n.OnUpdate(0.01f);
        CHECK(n.CurrentState() == "Lab");
        CHECK(noBus.Count == 2);                    // the two `when` guards on Lab, once each
        CHECK(n.StackDepth() == 1);
    }

    TEST_CASE("V06: 'when' fires at most once per OnUpdate, after the signal drain, before timers, never without a guard")
    {
        FlowAsset a = Parse(kChannels);
        DataBus bus;
        std::vector<std::string> loads;
        FlowMachine m;
        m.SetSceneLoader(FakeLoader(&loads));
        m.SetDataBus(&bus);
        m.Start(a);
        REQUIRE(m.CurrentState() == "Lab");

        // Both `when` guards true: only the FIRST in declaration order fires this update.
        bus.Set("pendulum.energy", 0.0);
        bus.SetBool("alarm", true);
        m.OnUpdate(0.01f);
        CHECK(m.CurrentState() == "Stopped");
        CHECK(m.StackDepth() == 2);                 // pushed (Stopped is an overlay)
        m.OnUpdate(0.01f);
        CHECK(m.CurrentState() == "Stopped");       // Stopped has no `when`; Lab's are not evaluated under the overlay
        m.FeedSignal("resume_clicked");
        m.OnUpdate(0.01f);                          // drain pops back to Lab, then Lab's `when` fires in the SAME update (after the drain)
        CHECK(m.CurrentState() == "Stopped");
        CHECK(m.StackDepth() == 2);

        // After the drain: a signal that leaves Lab first wins over Lab's `when`.
        m.FeedSignal("resume_clicked");
        bus.Set("pendulum.energy", 1.0);            // first `when` false
        bus.SetBool("ui.locked", false);
        m.FeedSignal("settings_clicked");           // queued: pop, then settings
        m.OnUpdate(0.01f);
        CHECK(m.CurrentState() == "Settings");      // the drained signal transitioned; the `when` (alarm) never ran on Settings' behalf
        bus.SetString("mode", "lab");
        m.OnUpdate(0.01f);
        CHECK(m.CurrentState() == "Lab");           // Settings' `when` -> Lab; Lab's alarm `when` (true) waits: one per update
        m.OnUpdate(0.01f);
        CHECK(m.CurrentState() == "Alarm");
    }

    TEST_CASE("V06: 'when' one-per-update ordering, timer ordering and the guard-less 'when'")
    {
        FlowAsset a = Parse(kChannels);
        DataBus bus;
        FlowMachine m;
        m.SetSceneLoader(FakeLoader());
        m.SetDataBus(&bus);

        // Exactly one `when` per update: Settings -> Lab (when mode == lab), then Lab's
        // alarm `when` only on the NEXT update.
        m.StartAt(a, "Settings");
        REQUIRE(m.CurrentState() == "Settings");
        bus.SetString("mode", "lab");
        bus.SetBool("alarm", true);
        bus.Set("pendulum.energy", 1.0);
        m.OnUpdate(0.01f);
        CHECK(m.CurrentState() == "Lab");
        m.OnUpdate(0.01f);
        CHECK(m.CurrentState() == "Alarm");

        // Before timers: with the timer (1 s) elapsed AND a true `when` in the same
        // update, the `when` wins.
        m.Start(a);
        bus.SetBool("alarm", false);
        m.OnUpdate(2.0f);                           // timer:1 due; both `when` false => the timer fires
        CHECK(m.CurrentState() == "TimeUp");
        m.Start(a);
        bus.SetBool("alarm", true);
        m.OnUpdate(2.0f);                           // timer due AND the alarm `when` true => `when` first
        CHECK(m.CurrentState() == "Alarm");

        // A `when` without a guard never fires, and Validate reports it.
        FlowAsset bad = Parse(kChannels);
        FlowTransition t; t.On = "when"; t.To = "Alarm"; t.HasGuard = false;
        bad.States[0].Transitions.insert(bad.States[0].Transitions.begin(), t);
        const auto errs = bad.Validate();
        REQUIRE(errs.size() == 1);
        CHECK(errs[0] == "state 'Lab' has a 'when' transition without an 'if' guard");
        FlowMachine b;
        b.SetSceneLoader(FakeLoader());
        b.SetDataBus(&bus);
        bus.SetBool("alarm", false);
        b.Start(bad);
        b.OnUpdate(0.01f);
        b.OnUpdate(0.01f);
        CHECK(b.CurrentState() == "Lab");
        // ...and a literal "when" signal fed by someone does not drive it either.
        b.FeedSignal("when");
        b.OnUpdate(0.01f);
        CHECK(b.CurrentState() == "Lab");
    }

    TEST_CASE("V06: StartAt enters the named state and falls back to Start for an unknown name")
    {
        WarnCounter warns("StartAt");
        FlowAsset a = Parse(kV1);
        std::vector<std::string> loads;
        FlowMachine m;
        m.SetSceneLoader(FakeLoader(&loads));

        m.StartAt(a, "Lab");
        CHECK(m.IsRunning());
        CHECK(m.CurrentState() == "Lab");
        REQUIRE(loads.size() == 1);
        CHECK(loads[0] == "project://scenes/Lab.cscene");
        CHECK(m.StackDepth() == 1);
        CHECK(warns.Count == 0);

        m.StartAt(a, "Nowhere");
        CHECK(m.IsRunning());
        CHECK(m.CurrentState() == "Home");          // fell back to the asset's start
        CHECK(loads.back() == "project://scenes/Home.cscene");
        CHECK(warns.Count == 1);

        m.Start(a);                                 // Start == StartAt(asset, asset.Start)
        CHECK(m.CurrentState() == "Home");
        CHECK(warns.Count == 1);

        // Resuming into an overlay-only state adopts it as the base frame (no scene => no load).
        const size_t n = loads.size();
        m.StartAt(a, "Settings");
        CHECK(m.CurrentState() == "Settings");
        CHECK(loads.size() == n);
        CHECK(m.ActiveScene() == nullptr);
    }

    TEST_CASE("V06: FlowKeyBridge::KeyCodeFor table")
    {
        CHECK(FlowKeyBridge::KeyCodeFor("Escape") == CS_KEY_ESCAPE);
        CHECK(FlowKeyBridge::KeyCodeFor("Space") == CS_KEY_SPACE);
        CHECK(FlowKeyBridge::KeyCodeFor("Enter") == CS_KEY_ENTER);
        CHECK(FlowKeyBridge::KeyCodeFor("Tab") == CS_KEY_TAB);
        CHECK(FlowKeyBridge::KeyCodeFor("Backspace") == CS_KEY_BACKSPACE);
        CHECK(FlowKeyBridge::KeyCodeFor("Up") == CS_KEY_UP);
        CHECK(FlowKeyBridge::KeyCodeFor("Down") == CS_KEY_DOWN);
        CHECK(FlowKeyBridge::KeyCodeFor("Left") == CS_KEY_LEFT);
        CHECK(FlowKeyBridge::KeyCodeFor("Right") == CS_KEY_RIGHT);
        CHECK(FlowKeyBridge::KeyCodeFor("F1") == CS_KEY_F1);
        CHECK(FlowKeyBridge::KeyCodeFor("F9") == CS_KEY_F9);
        CHECK(FlowKeyBridge::KeyCodeFor("F12") == CS_KEY_F12);
        CHECK(FlowKeyBridge::KeyCodeFor("A") == CS_KEY_A);
        CHECK(FlowKeyBridge::KeyCodeFor("M") == CS_KEY_M);
        CHECK(FlowKeyBridge::KeyCodeFor("Z") == CS_KEY_Z);
        CHECK(FlowKeyBridge::KeyCodeFor("0") == CS_KEY_0);
        CHECK(FlowKeyBridge::KeyCodeFor("9") == CS_KEY_9);
        // Unknown names (and the ones outside the table) are -1.
        CHECK(FlowKeyBridge::KeyCodeFor("") == -1);
        CHECK(FlowKeyBridge::KeyCodeFor("F13") == -1);
        CHECK(FlowKeyBridge::KeyCodeFor("F0") == -1);
        CHECK(FlowKeyBridge::KeyCodeFor("Fx") == -1);
        CHECK(FlowKeyBridge::KeyCodeFor("a") == -1);
        CHECK(FlowKeyBridge::KeyCodeFor("Esc") == -1);
        CHECK(FlowKeyBridge::KeyCodeFor("Shift") == -1);
        CHECK(FlowKeyBridge::KeyCodeFor("Bogus") == -1);
        CHECK(FlowKeyBridge::KeyCodeFor("10") == -1);
    }

    TEST_CASE("V06: KeySignals is a distinct first-occurrence list across all transitions")
    {
        FlowAsset a = Parse(kChannels);
        const auto keys = FlowMachine::KeySignals(a);
        REQUIRE(keys.size() == 4);
        CHECK(keys[0] == "key:Escape");             // listed twice in Lab, once in first position
        CHECK(keys[1] == "key:Space");
        CHECK(keys[2] == "key:F3");
        CHECK(keys[3] == "key:Bogus");
        CHECK(FlowMachine::KeySignals(FlowAsset{}).empty());
        FlowAsset v1 = Parse(kV1);
        const auto k1 = FlowMachine::KeySignals(v1);
        REQUIRE(k1.size() == 1);
        CHECK(k1[0] == "key:Escape");
    }

    TEST_CASE("V06: FlowKeyBridge feeds one signal per rising edge per key over an injected probe; unknown names warn once")
    {
        WarnCounter warns("FlowKeyBridge");
        FlowAsset a = Parse(kChannels);
        DataBus bus;
        FlowMachine m;
        m.SetSceneLoader(FakeLoader());
        m.SetDataBus(&bus);
        m.Start(a);

        std::vector<int> down;                      // the "held" key codes
        int probes = 0;
        FlowKeyBridge bridge;
        bridge.Bind(a, [&](int code) { ++probes; for (int d : down) if (d == code) return true; return false; });
        CHECK(bridge.BoundCount() == 3);            // Escape, Space, F3 — "Bogus" not bound
        CHECK(warns.Count == 1);
        bridge.Bind(a, [&](int code) { ++probes; for (int d : down) if (d == code) return true; return false; });
        CHECK(warns.Count == 1);                    // the unknown name warns once per bridge

        // Nothing held: no signal.
        bridge.Poll(m); m.OnUpdate(0.01f);
        CHECK(m.CurrentState() == "Lab");
        CHECK(probes == 3);

        // Space pressed and HELD for three polls: exactly one signal (Lab -> Alarm), no repeat.
        down = { CS_KEY_SPACE };
        bridge.Poll(m); m.OnUpdate(0.01f);
        CHECK(m.CurrentState() == "Alarm");
        bridge.Poll(m); m.OnUpdate(0.01f);
        bridge.Poll(m); m.OnUpdate(0.01f);
        CHECK(m.CurrentState() == "Alarm");         // Alarm has key:Space -> Lab; a held key is not a new edge

        // Release, press again: a new edge (Alarm -> Lab).
        down.clear();
        bridge.Poll(m); m.OnUpdate(0.01f);
        CHECK(m.CurrentState() == "Alarm");
        down = { CS_KEY_SPACE };
        bridge.Poll(m); m.OnUpdate(0.01f);
        CHECK(m.CurrentState() == "Lab");

        // Two keys rising in the same poll: both signals, in bound order (Escape first => @quit).
        down = { CS_KEY_ESCAPE, CS_KEY_F3 };
        bridge.Poll(m);
        CHECK(m.IsRunning());
        m.OnUpdate(0.01f);
        CHECK(m.QuitRequested());
        CHECK_FALSE(m.IsRunning());

        // Polling a stopped machine is harmless; Clear forgets the bindings.
        bridge.Poll(m);
        bridge.Clear();
        CHECK(bridge.BoundCount() == 0);
        bridge.Poll(m);
    }

    TEST_CASE("V06: JSON — v1 and v2 files save byte-identical to the pre-AP-01 binary; 'channel' only when non-empty; 'when' verbatim")
    {
        FlowAsset v1 = Parse(kV1);
        CHECK(v1.SaveToString() == std::string(kExpectedV1));
        FlowAsset v2 = Parse(kV2);
        CHECK(v2.SaveToString() == std::string(kExpectedV2));
        // Loading the saved text and saving again is a fixed point.
        FlowAsset again;
        REQUIRE(FlowAsset::LoadFromString(again, v1.SaveToString()));
        CHECK(again.SaveToString() == std::string(kExpectedV1));
        // No transition of a v1/v2 file carries a Channel.
        for (const FlowState& s : v1.States)
            for (const FlowTransition& t : s.Transitions)
                CHECK(t.Guard.Channel.empty());
        CHECK(v1.SaveToString().find("channel") == std::string::npos);
        CHECK(v2.SaveToString().find("channel") == std::string::npos);

        // Channel guards + "when" round-trip: the key is written exactly once per
        // channel guard, "on": "when" verbatim, and the reloaded asset is structurally equal.
        FlowAsset c = Parse(kChannels);
        const FlowState* lab = c.Find("Lab");
        REQUIRE(lab != nullptr);
        REQUIRE(lab->Transitions.size() == 7);
        CHECK(lab->Transitions[0].On == "when");
        CHECK(lab->Transitions[0].HasGuard);
        CHECK(lab->Transitions[0].Guard.Channel == "pendulum.energy");
        CHECK(lab->Transitions[0].Guard.Op == "<");
        CHECK(lab->Transitions[0].Guard.Value.ValueKind == FlowValue::Kind::Number);
        CHECK(lab->Transitions[0].Guard.Value.Number == 0.01);
        CHECK(lab->Transitions[0].Push);
        CHECK(lab->Transitions[1].Guard.Value.ValueKind == FlowValue::Kind::Bool);
        CHECK(lab->Transitions[2].Guard.Channel == "ui.locked");
        const std::string saved = c.SaveToString();
        size_t count = 0;
        for (size_t pos = saved.find("\"channel\""); pos != std::string::npos; pos = saved.find("\"channel\"", pos + 1)) ++count;
        CHECK(count == 4);                          // the four channel guards, nothing else
        CHECK(saved.find("\"on\": \"when\"") != std::string::npos);
        FlowAsset back;
        std::string err;
        REQUIRE(FlowAsset::LoadFromString(back, saved, &err));
        CHECK(back.SaveToString() == saved);
        const FlowState* lab2 = back.Find("Lab");
        REQUIRE(lab2 != nullptr);
        REQUIRE(lab2->Transitions.size() == 7);
        CHECK(lab2->Transitions[0].On == "when");
        CHECK(lab2->Transitions[0].Guard.Channel == "pendulum.energy");
        CHECK(lab2->Transitions[0].Guard.Var.empty());
        CHECK(lab2->Transitions[3].On == "timer:1");
        CHECK(back.Version == 1);                   // channel guards do not bump the version
        CHECK(back.Validate().empty());

        // A channel guard beats var/field on save (only the channel form is written).
        FlowAsset mixed = Parse(kV1);
        mixed.States[0].Transitions[2].Guard.Channel = "x";
        const std::string ms = mixed.SaveToString();
        CHECK(ms.find("\"channel\": \"x\"") != std::string::npos);
        CHECK(ms.find("\"entity\": \"Player\"") == std::string::npos);
    }
}
