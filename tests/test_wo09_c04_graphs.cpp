// test_wo09_c04_graphs.cpp — WO-09 (2D stability) C04: Flow / Story / EventBus,
// headless.
//
//   EventBus   listener add / remove during emit, nested emit, clear during
//              emit, exact listener counts and lifetimes, 10,000 listeners.
//   Flow       cycles (bounded by the source's 100,000-iteration cascade guard,
//              asserted), dangling targets, missing entry, @pop with no
//              overlay, malformed timers, push cycles (stack growth pinned).
//   Story      dangling next, missing start, cycles by choice, once semantics.
//   Parsers    seeded structural + byte fuzz of FlowAsset::LoadFromString and
//              StoryGraph::LoadFromString (2,000 cases each in the PR profile,
//              COSMIC_WO09_FUZZ_CASES overrides for nightly), every committed
//              F-CORRUPT fixture under tests/fixtures/wo09/corrupt/, and a per-
//              case parser deadline. The oracle: a loader returns false, or
//              returns true with an asset whose Validate() does not throw and
//              whose SaveToString() re-parses — never a crash, never an
//              uncaught exception (which ends the process = the evidence).

#include <doctest.h>

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/EventBus.h"
#include "scene/FlowMachine.h"
#include "scene/StoryGraph.h"
#include "wo09_fuzz.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Cosmic;

namespace
{
    std::string EvidenceDir()
    {
        if (const char* e = std::getenv("COSMIC_WO09_EVIDENCE_DIR")) return e[0] ? e : "";
        return "";
    }
    int FuzzCases()
    {
        if (const char* n = std::getenv("COSMIC_WO09_FUZZ_CASES")) return std::max(1, std::atoi(n));
        return 2000;
    }
    std::filesystem::path CorruptDir()
    {
#ifdef COSMIC_WO09_FIXTURES
        return std::filesystem::path(COSMIC_WO09_FIXTURES) / "corrupt";
#else
        return std::filesystem::path("tests/fixtures/wo09/corrupt");
#endif
    }

    const char* kFlow = R"({
  "cosmic_flow": 2,
  "start": "Menu",
  "variables": [ { "name": "Coins", "type": "number", "default": 0 },
                 { "name": "Mode", "type": "enum", "default": "easy", "options": ["easy", "hard"] } ],
  "states": [
    { "name": "Menu", "scene": "project://scenes/Menu.cscene",
      "onEnter": [ { "emit": "menu_shown" }, { "setVar": { "var": "Coins", "add": true, "value": 1 } } ],
      "transitions": [ { "on": "play", "to": "Game", "transition": "Fade" },
                       { "on": "cheat", "to": "Win", "if": { "var": "Coins", "op": ">=", "value": 3 } },
                       { "on": "quit", "to": "@quit" } ],
      "editor": { "pos": [10, 20] } },
    { "name": "Game", "scene": "project://scenes/Main.cscene",
      "transitions": [ { "on": "key:Escape", "to": "Pause", "push": true },
                       { "on": "timer:2.5", "to": "Win" },
                       { "on": "hurt", "to": "Game", "if": { "entity": "Player", "component": "Tag", "field": "Tag", "op": "==", "value": "Player" } } ] },
    { "name": "Pause", "overlay": true, "transitions": [ { "on": "resume", "to": "@pop" } ] },
    { "name": "Win", "scene": "project://scenes/Win.cscene",
      "onEnter": [ { "setField": { "entity": "Hud", "component": "Tag", "field": "Tag", "value": "Winner" } } ],
      "transitions": [ { "on": "ok", "to": "@quit" } ] }
  ]
})";

    const char* kStory = R"({
  "cosmic_story": 1,
  "start": "Intro",
  "variables": [ { "name": "Gold", "type": "number", "default": 5 } ],
  "nodes": [
    { "name": "Intro", "speaker": "Guard", "text": "Halt.", "portrait": "p.png", "background": "b.png", "audio": "a.wav",
      "onEnter": ["intro"], "onExit": ["left"],
      "options": [ { "text": "Pay", "next": "Paid", "if": { "var": "Gold", "op": ">=", "value": 10 } },
                   { "text": "Fight", "next": "Fight" },
                   { "text": "Secret", "next": "Secret", "once": true } ],
      "editor": { "pos": [1, 2] } },
    { "name": "Paid",   "onEnter": ["paid"],   "options": [ { "text": "Go", "next": "End" } ] },
    { "name": "Fight",  "onEnter": ["fought"], "options": [ { "text": "Go", "next": "End" } ] },
    { "name": "Secret", "options": [ { "text": "Back", "next": "Intro" } ] },
    { "name": "End",    "text": "Done.", "options": [ { "text": "Finish", "next": "@end" } ] }
  ]
})";

    // Flow oracle: accepted ⇒ Validate() and a re-parse of SaveToString() work.
    bool ParseFlow(const std::string& text)
    {
        FlowAsset a; std::string err;
        if (!FlowAsset::LoadFromString(a, text, &err)) return false;
        (void)a.Validate();
        FlowAsset again;
        if (!FlowAsset::LoadFromString(again, a.SaveToString(), &err)) FAIL("a loaded flow re-saved into something unloadable: " << err);
        return true;
    }
    bool ParseStory(const std::string& text)
    {
        StoryGraph g; std::string err;
        if (!StoryGraph::LoadFromString(g, text, &err)) return false;
        (void)g.Validate();
        StoryGraph again;
        if (!StoryGraph::LoadFromString(again, g.SaveToString(), &err)) FAIL("a loaded story re-saved into something unloadable: " << err);
        return true;
    }
}

TEST_SUITE("WO-09 C04 graphs (headless)")
{
    TEST_CASE("WO-09 C04: EventBus — add/remove during emit, nested emit, clear during emit, exact counts")
    {
        Ref<Scene> s = Scene::Create();
        EventBus& bus = s->Events();
        CHECK(bus.TotalListeners() == 0);

        // Remove-during-emit: A removes B (later in order) ⇒ B must NOT fire; A
        // removes itself ⇒ still fires this once (it was live when reached).
        int aFired = 0, bFired = 0, cFired = 0;
        EventBus::Handle hB = 0;
        EventBus::Handle hA = bus.Connect("x", [&](Entity) { ++aFired; bus.Disconnect(hB); });
        hB = bus.Connect("x", [&](Entity) { ++bFired; });
        EventBus::Handle hC = bus.Connect("x", [&](Entity) { ++cFired; });
        CHECK(bus.ListenerCount("x") == 3);
        bus.Emit("x", Entity());
        CHECK(aFired == 1); CHECK(bFired == 0); CHECK(cFired == 1);
        CHECK(bus.ListenerCount("x") == 2);
        bus.Disconnect(hB);                                           // double-disconnect: no-op
        CHECK(bus.ListenerCount("x") == 2);

        // Add-during-emit: a listener connected inside a handler does not fire in
        // THIS dispatch (the snapshot rule) but does on the next.
        int dFired = 0;
        EventBus::Handle hAdder = bus.Connect("x", [&](Entity) { bus.Connect("x", [&](Entity) { ++dFired; }); });
        bus.Emit("x", Entity());
        CHECK(dFired == 0);
        CHECK(bus.ListenerCount("x") == 4);
        bus.Disconnect(hAdder);
        bus.Emit("x", Entity());
        CHECK(dFired == 1);

        // Nested emit: a handler emitting ANOTHER signal is delivered synchronously,
        // in order, and a bounded self-re-emit chain (depth 50) terminates with an
        // exact count. (An unbounded self-emit is a stack recursion by design —
        // the FlowMachine, not the bus, is the queued/bounded layer.)
        int yFired = 0, depth = 0, maxDepth = 0;
        bus.Connect("y", [&](Entity) { ++yFired; });
        bus.Connect("z", [&](Entity) { ++depth; maxDepth = std::max(maxDepth, depth); if (depth < 50) bus.Emit("z", Entity()); bus.Emit("y", Entity()); --depth; });
        bus.Emit("z", Entity());
        CHECK(maxDepth == 50);
        CHECK(yFired == 50);

        // Clear during emit: the remaining snapshotted listeners are NOT invoked.
        int e1 = 0, e2 = 0;
        bus.Connect("w", [&](Entity) { ++e1; bus.Clear(); });
        bus.Connect("w", [&](Entity) { ++e2; });
        bus.ConnectAny([&](const std::string&, Entity) { ++e2; });
        bus.Emit("w", Entity());
        CHECK(e1 == 1); CHECK(e2 == 0);
        CHECK(bus.TotalListeners() == 0);
        bus.Emit("w", Entity());                                      // nothing left: a no-op
        CHECK(e1 == 1);
        (void)hA; (void)hC;

        // Null handlers are refused (handle 0), and 0 is never a live handle.
        CHECK(bus.Connect("n", nullptr) == 0);
        CHECK(bus.ConnectAny(nullptr) == 0);
        bus.Disconnect(0);
        CHECK(bus.TotalListeners() == 0);
    }

    TEST_CASE("WO-09 C04: EventBus — 10,000 listeners: unique handles, exact delivery, exact removal, timing")
    {
        Ref<Scene> s = Scene::Create();
        EventBus& bus = s->Events();
        std::vector<EventBus::Handle> handles;
        int fired = 0;
        for (int i = 0; i < 10000; ++i)
            handles.push_back(bus.Connect(i % 2 ? "a" : "b", [&](Entity) { ++fired; }));
        std::vector<EventBus::Handle> sorted = handles;
        std::sort(sorted.begin(), sorted.end());
        CHECK(std::unique(sorted.begin(), sorted.end()) == sorted.end());   // all unique
        CHECK(bus.ListenerCount("a") == 5000);
        CHECK(bus.ListenerCount("b") == 5000);
        CHECK(bus.TotalListeners() == 10000);
        const auto t0 = std::chrono::steady_clock::now();
        bus.Emit("a", Entity());
        const double msEmit = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(fired == 5000);
        for (size_t i = 0; i < handles.size(); i += 2) bus.Disconnect(handles[i]);   // every "b"
        CHECK(bus.ListenerCount("b") == 0);
        CHECK(bus.ListenerCount("a") == 5000);
        fired = 0;
        bus.Emit("b", Entity());
        CHECK(fired == 0);
        MESSAGE("C04 EventBus: 5,000-listener emit = " << msEmit << " ms (each call re-checks liveness)");
        CHECK(msEmit < 10000.0);
    }

    TEST_CASE("WO-09 C04: FlowMachine — cycles are bounded by the 100,000-iteration cascade guard; push cycles grow the stack (pinned)")
    {
        // A ping-pong: A --go--> B, B --go--> A, both emitting "go" on enter.
        FlowAsset a; std::string err;
        REQUIRE(FlowAsset::LoadFromString(a, R"({ "cosmic_flow": 1, "start": "A", "states": [
            { "name": "A", "onEnter": [ { "emit": "go" } ], "transitions": [ { "on": "go", "to": "B" } ] },
            { "name": "B", "onEnter": [ { "emit": "go" } ], "transitions": [ { "on": "go", "to": "A" } ] } ] })", &err));
        Ref<Scene> scene = Scene::Create();
        int loads = 0;
        FlowMachine fm;
        fm.SetSceneLoader([&](const std::string&) { ++loads; return scene; });
        fm.Start(a);
        CHECK(fm.CurrentState() == "A");
        const auto t0 = std::chrono::steady_clock::now();
        fm.OnUpdate(0.016f);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(fm.IsRunning());
        CHECK(fm.StackDepth() == 1);                                  // plain transitions replace the stack
        MESSAGE("C04 flow ping-pong: one OnUpdate hit the cascade guard in " << ms << " ms");
        CHECK(ms < 10000.0);
        // Deterministic: the guard trips after 100,000 iterations; A is entered on odd
        // iterations, so the state after 100,001 fires (1 + 100,000) is B... pinned as
        // whichever it is, identically on a second run.
        const std::string after1 = fm.CurrentState();
        FlowMachine fm2;
        fm2.SetSceneLoader([&](const std::string&) { return scene; });
        fm2.Start(a);
        fm2.OnUpdate(0.016f);
        CHECK(fm2.CurrentState() == after1);
        fm.OnUpdate(0.016f);                                          // the queue was cleared: no further cascade
        CHECK(fm.CurrentState() == after1);

        // Push cycle: A --go--> B (push), B --go--> A (push): the stack grows by one
        // frame per iteration until the guard. PINNED: 100,002 frames after one update.
        FlowAsset p;
        REQUIRE(FlowAsset::LoadFromString(p, R"({ "cosmic_flow": 1, "start": "A", "states": [
            { "name": "A", "onEnter": [ { "emit": "go" } ], "transitions": [ { "on": "go", "to": "B", "push": true } ] },
            { "name": "B", "onEnter": [ { "emit": "go" } ], "transitions": [ { "on": "go", "to": "A", "push": true } ] } ] })", &err));
        FlowMachine fp;
        fp.SetSceneLoader([&](const std::string&) { return scene; });
        fp.Start(p);
        fp.OnUpdate(0.016f);
        // PINNED: the initial frame + 100,001 pushes (the guard trips AFTER iteration 100,001).
        MESSAGE("C04 flow push cycle: stack depth after one OnUpdate = " << fp.StackDepth());
        CHECK(fp.StackDepth() == 100002u);
        fp.Stop();
        CHECK(fp.StackDepth() == 0);
        CHECK(scene->Events().TotalListeners() == 0);                 // Stop released the bus subscription
    }

    TEST_CASE("WO-09 C04: FlowMachine — a double-emit self-loop (exponential queue) stays inside the deadline")
    {
        FlowAsset a; std::string err;
        REQUIRE(FlowAsset::LoadFromString(a, R"({ "cosmic_flow": 1, "start": "A", "states": [
            { "name": "A", "onEnter": [ { "emit": "go" }, { "emit": "go" } ], "transitions": [ { "on": "go", "to": "A" } ] } ] })", &err));
        Ref<Scene> scene = Scene::Create();
        FlowMachine fm;
        fm.SetSceneLoader([&](const std::string&) { return scene; });
        fm.Start(a);
        const auto t0 = std::chrono::steady_clock::now();
        fm.OnUpdate(0.016f);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        MESSAGE("C04 flow double-emit self-loop: one OnUpdate = " << ms << " ms (queue grows by one per iteration to the guard)");
        CHECK(fm.IsRunning());
        CHECK(ms < 10000.0);
        fm.OnUpdate(0.016f);                                          // cleared queue: instant
    }

    TEST_CASE("WO-09 C04: FlowMachine — dangling target, missing entry, @pop on a bare stack, malformed timers, unknown signal")
    {
        std::string err;
        // Missing entry: Start never runs, the machine is not running, no crash.
        FlowAsset missing;
        REQUIRE(FlowAsset::LoadFromString(missing, R"({ "cosmic_flow": 1, "start": "Nope", "states": [ { "name": "A" } ] })", &err));
        CHECK(missing.Validate().size() == 1);
        FlowMachine fm;
        fm.Start(missing);
        CHECK_FALSE(fm.IsRunning());
        CHECK(fm.CurrentState().empty());
        fm.FeedSignal("x"); fm.OnUpdate(1.0f);
        CHECK_FALSE(fm.IsRunning());

        // Dangling target + @pop on a bare stack + malformed timers.
        FlowAsset d;
        REQUIRE(FlowAsset::LoadFromString(d, R"({ "cosmic_flow": 1, "start": "A", "states": [
            { "name": "A", "scene": "project://scenes/A.cscene", "transitions": [ { "on": "go", "to": "Ghost" }, { "on": "pop", "to": "@pop" }, { "on": "tc", "to": "C" } ] },
            { "name": "C", "transitions": [ { "on": "timer:", "to": "B" }, { "on": "timer:nan", "to": "B" } ] },
            { "name": "B", "transitions": [ { "on": "timer:-1", "to": "A" }, { "on": "timer:1e999", "to": "A" } ] } ] })", &err));
        CHECK(d.Validate().size() == 1);                              // the Ghost target
        Ref<Scene> scene = Scene::Create();
        FlowMachine m;
        m.SetSceneLoader([&](const std::string&) { return scene; });
        m.Start(d);
        REQUIRE(m.IsRunning());
        m.FeedSignal("go"); m.OnUpdate(0.0f);
        CHECK(m.CurrentState() == "A");                               // stays put on a dangling target
        CHECK(m.IsRunning());
        m.FeedSignal("pop"); m.OnUpdate(0.0f);
        CHECK(m.CurrentState() == "A");                               // nothing to pop
        CHECK(m.StackDepth() == 1);
        m.FeedSignal("never-declared"); m.OnUpdate(0.0f);
        CHECK(m.CurrentState() == "A");
        // "timer:" parses as 0 seconds ⇒ it fires in the SAME update that entered C
        // (elapsed 0 >= 0); "timer:nan" would compare false forever. The FIRST matching
        // transition wins, so C is left for B before the update returns.
        m.FeedSignal("tc"); m.OnUpdate(0.0f);
        CHECK(m.CurrentState() == "B");
        // In B: "timer:-1" fires on the next update (elapsed >= -1), back to A;
        // "timer:1e999" (inf) could never fire.
        m.OnUpdate(0.0f);
        CHECK(m.CurrentState() == "A");
        // Stop + restart is clean: no residual listeners on the scene bus.
        m.Stop();
        CHECK(scene->Events().TotalListeners() == 0);
        m.Start(d);
        CHECK(scene->Events().TotalListeners() == 1);
        m.Stop();
        CHECK(scene->Events().TotalListeners() == 0);
    }

    TEST_CASE("WO-09 C04: StoryRunner — dangling next ends, missing start ends, chosen cycles terminate by choice, once is exact")
    {
        StoryGraph g; std::string err;
        REQUIRE(StoryGraph::LoadFromString(g, R"({ "cosmic_story": 1, "start": "A", "nodes": [
            { "name": "A", "options": [ { "text": "loop", "next": "A" }, { "text": "ghost", "next": "Ghost" }, { "text": "one", "next": "A", "once": true } ] } ] })", &err));
        CHECK(g.Validate().size() == 1);
        Ref<Scene> scene = Scene::Create();
        StoryRunner r;
        r.Start(g, scene.get());
        REQUIRE(r.Current() != nullptr);
        CHECK(r.ValidOptions().size() == 3);
        for (int i = 0; i < 1000; ++i) { r.Choose(0); REQUIRE(r.Current() != nullptr); }   // a chosen cycle is just choices
        CHECK(r.ValidOptions().size() == 3);
        r.Choose(2);                                                  // the once option: consumed after this choice
        CHECK(r.ValidOptions().size() == 2);
        r.Choose(5);                                                  // out of range: ignored
        CHECK(r.Current() != nullptr);
        r.Choose(-1);
        CHECK(r.Current() != nullptr);
        r.Choose(1);                                                  // -> Ghost: ends
        CHECK(r.IsEnded());
        CHECK(r.Current() == nullptr);
        r.Choose(0);                                                  // after the end: no-op

        StoryGraph none;
        REQUIRE(StoryGraph::LoadFromString(none, R"({ "cosmic_story": 1, "start": "Nope", "nodes": [] })", &err));
        StoryRunner r2;
        r2.Start(none, scene.get());
        CHECK(r2.IsEnded());
        CHECK(r2.Current() == nullptr);
        CHECK(scene->Events().TotalListeners() == 0);
    }

    TEST_CASE("WO-09 C04: parser fuzz — FlowAsset and StoryGraph over seeded structural + byte mutations, with a per-case deadline")
    {
        const int cases = FuzzCases();
        const double deadlineMs = 2000.0;                             // per parse; the runner's 10 s is the outer limit
        REQUIRE(ParseFlow(kFlow));
        REQUIRE(ParseStory(kStory));
        const Wo09Fuzz::Stats f = Wo09Fuzz::Run("flow",  kFlow,  0x0904F10Au, cases, deadlineMs, ParseFlow,  EvidenceDir(), true);
        const Wo09Fuzz::Stats s = Wo09Fuzz::Run("story", kStory, 0x09045708u, cases, deadlineMs, ParseStory, EvidenceDir(), true);
        MESSAGE("C04 fuzz flow:  seed 0x0904F10A cases=" << f.cases << " accepted=" << f.accepted << " rejected=" << f.rejected << " max=" << f.maxMs << " ms (case " << f.maxCase << ")");
        MESSAGE("C04 fuzz story: seed 0x09045708 cases=" << s.cases << " accepted=" << s.accepted << " rejected=" << s.rejected << " max=" << s.maxMs << " ms (case " << s.maxCase << ")");
        CHECK(f.cases == cases); CHECK(s.cases == cases);
        CHECK(f.maxMs <= deadlineMs); CHECK(s.maxMs <= deadlineMs);
        CHECK(f.accepted > 0); CHECK(f.rejected > 0);                 // both branches exercised
        CHECK(s.accepted > 0); CHECK(s.rejected > 0);
    }

    TEST_CASE("WO-09 C04: committed F-CORRUPT fixtures — every .cflow / .cstory under tests/fixtures/wo09/corrupt is handled")
    {
        const std::filesystem::path dir = CorruptDir();
        REQUIRE_MESSAGE(std::filesystem::exists(dir), "fixture dir missing: " << dir.string());
        int flows = 0, stories = 0;
        for (const auto& entry : std::filesystem::directory_iterator(dir))
        {
            const std::string ext = entry.path().extension().string();
            const std::string text = Wo09Fuzz::ReadFile(entry.path());
            const auto t0 = std::chrono::steady_clock::now();
            if (ext == ".cflow")       { ++flows;   (void)ParseFlow(text); }
            else if (ext == ".cstory") { ++stories; (void)ParseStory(text); }
            else continue;
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            CHECK_MESSAGE(ms < 2000.0, entry.path().filename().string() << " took " << ms << " ms");
        }
        CHECK(flows >= 1);
        CHECK(stories >= 1);
        MESSAGE("C04 corrupt fixtures: " << flows << " .cflow + " << stories << " .cstory handled");
    }
}
