// test_story.cpp — Starforge Story Graph runtime (Phase 25 / Q3). Headless (no
// GL): a 5-node branching dialogue with a variable-guarded option + a Once
// option walks every path correctly, and OnEnter signals land on the scene bus.

#include <doctest.h>

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/EventBus.h"
#include "scene/StoryGraph.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace Cosmic;

static const char* kStory = R"({
  "cosmic_story": 1,
  "start": "Intro",
  "variables": [ { "name": "Gold", "type": "number", "default": 5 } ],
  "nodes": [
    { "name": "Intro", "speaker": "Guard", "text": "Halt.",
      "options": [
        { "text": "Pay 10 gold", "next": "Paid", "if": { "var": "Gold", "op": ">=", "value": 10 } },
        { "text": "Fight",       "next": "Fight" },
        { "text": "Ask the secret", "next": "Secret", "once": true } ] },
    { "name": "Paid",   "onEnter": ["paid"],        "options": [ { "text": "Go",   "next": "End" } ] },
    { "name": "Fight",  "onEnter": ["fought"],      "options": [ { "text": "Go",   "next": "End" } ] },
    { "name": "Secret", "onEnter": ["secret_told"], "options": [ { "text": "Back", "next": "Intro" } ] },
    { "name": "End",    "text": "Done.",            "options": [ { "text": "Finish", "next": "@end" } ] }
  ]
})";

static bool Saw(const std::vector<std::string>& log, const std::string& sig)
{
    return std::find(log.begin(), log.end(), sig) != log.end();
}

TEST_CASE("Q3: .cstory parses (nodes, options, guard, once, variables)")
{
    StoryGraph g;
    std::string err;
    REQUIRE(StoryGraph::LoadFromString(g, kStory, &err));
    CHECK(err.empty());
    CHECK(g.Start == "Intro");
    REQUIRE(g.Nodes.size() == 5);
    REQUIRE(g.Variables.size() == 1);
    CHECK(g.Variables[0].Name == "Gold");

    const StoryNode* intro = g.Find("Intro");
    REQUIRE(intro);
    CHECK(intro->Speaker == "Guard");
    REQUIRE(intro->Options.size() == 3);
    CHECK(intro->Options[0].HasGuard);
    CHECK(intro->Options[0].Guard.Var == "Gold");
    CHECK(intro->Options[2].Once == true);
    CHECK(g.Validate().empty());
}

TEST_CASE("Q3: a guarded option + a Once option + signals walk every path")
{
    StoryGraph g;
    REQUIRE(StoryGraph::LoadFromString(g, kStory, nullptr));

    Ref<Scene> scene = Scene::Create();
    std::vector<std::string> signals;
    scene->Events().ConnectAny([&](const std::string& s, Entity) { signals.push_back(s); });

    StoryRunner r;
    r.Start(g, scene.get());
    REQUIRE(r.Current());
    CHECK(r.Current()->Name == "Intro");

    // Gold = 5 < 10 → "Pay" hidden; "Fight" + "Ask the secret" (Once) valid.
    REQUIRE(r.ValidOptions().size() == 2);
    CHECK(r.Current()->Options[r.ValidOptions()[0]].Next == "Fight");
    CHECK(r.Current()->Options[r.ValidOptions()[1]].Next == "Secret");

    // Raise Gold so the guard passes on the NEXT Intro entry (rebuild-on-enter).
    r.SetVar("Gold", FlowValue::MakeNumber(10.0));

    // Choose the Once "Ask the secret" (valid index 1).
    r.Choose(1);
    CHECK(r.Current()->Name == "Secret");
    CHECK(Saw(signals, "secret_told"));

    // Back to Intro: "Pay" now valid (Gold 10), "Ask the secret" gone (Once).
    r.Choose(0);
    CHECK(r.Current()->Name == "Intro");
    REQUIRE(r.ValidOptions().size() == 2);
    CHECK(r.Current()->Options[r.ValidOptions()[0]].Next == "Paid");   // the guarded option
    CHECK(r.Current()->Options[r.ValidOptions()[1]].Next == "Fight");
    for (int idx : r.ValidOptions())
        CHECK(r.Current()->Options[idx].Next != "Secret");            // consumed Once

    // Take the (now valid) paid path to the end.
    r.Choose(0);
    CHECK(r.Current()->Name == "Paid");
    CHECK(Saw(signals, "paid"));
    r.Choose(0);
    CHECK(r.Current()->Name == "End");
    r.Choose(0);   // "@end"
    CHECK(r.IsEnded());
    CHECK(r.Current() == nullptr);
}

TEST_CASE("Q3: the Fight branch + serialize round-trip")
{
    StoryGraph g;
    REQUIRE(StoryGraph::LoadFromString(g, kStory, nullptr));

    Ref<Scene> scene = Scene::Create();
    std::vector<std::string> signals;
    scene->Events().ConnectAny([&](const std::string& s, Entity) { signals.push_back(s); });

    StoryRunner r;
    r.Start(g, scene.get());
    // valid = [Fight, Secret]; Fight is valid index 0.
    r.Choose(0);
    CHECK(r.Current()->Name == "Fight");
    CHECK(Saw(signals, "fought"));

    // Round-trip the graph.
    StoryGraph b;
    REQUIRE(StoryGraph::LoadFromString(b, g.SaveToString(), nullptr));
    REQUIRE(b.Nodes.size() == 5);
    CHECK(b.Find("Intro")->Options[0].Guard.Var == "Gold");
    CHECK(b.Find("Intro")->Options[2].Once == true);
    CHECK(b.Find("Secret")->OnEnter.size() == 1);
    CHECK(b.Find("Secret")->OnEnter[0] == "secret_told");
    CHECK(b.Variables.size() == 1);
}

TEST_CASE("Q3: a story shares the flow blackboard when run under a flow")
{
    // A guard reads "Score" which lives in a FlowMachine's blackboard; the runner
    // is handed the machine as its shared store.
    const char* story = R"({
      "cosmic_story": 1, "start": "Gate",
      "nodes": [
        { "name": "Gate", "options": [
            { "text": "Enter", "next": "@end", "if": { "var": "Score", "op": ">=", "value": 1 } } ] }
      ]
    })";
    StoryGraph g;
    REQUIRE(StoryGraph::LoadFromString(g, story, nullptr));

    FlowAsset fa;
    fa.Start = "S";
    FlowVariable v; v.Name = "Score"; v.Default = FlowValue::MakeNumber(0.0);
    fa.Variables = { v };
    FlowState s; s.Name = "S"; fa.States.push_back(s);
    FlowMachine fm;
    fm.Start(fa);   // seeds Score = 0

    StoryRunner r;
    r.Start(g, nullptr, &fm);
    CHECK(r.ValidOptions().empty());   // Score 0 < 1 → the gate option is hidden

    fm.SetVar("Score", FlowValue::MakeNumber(1.0));   // flow-side change
    r.Start(g, nullptr, &fm);                          // re-enter → rebuild
    REQUIRE(r.ValidOptions().size() == 1);
    r.Choose(0);
    CHECK(r.IsEnded());
}
