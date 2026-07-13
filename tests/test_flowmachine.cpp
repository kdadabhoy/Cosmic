// test_flowmachine.cpp — screen-flow runtime + `.cflow` (Phase 17 / U5).
// Headless: parse/serialize round-trip, structural validation, and a full
// menu -> game -> pause(push) -> pop -> timer -> quit run over a FAKE scene
// loader that records load order; plus a guard that reads a reflected field.

#include <doctest.h>

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/FlowMachine.h"

#include <string>
#include <unordered_map>
#include <vector>

using namespace Cosmic;

static const char* kFlow = R"({
  "cosmic_flow": 1,
  "start": "MainMenu",
  "states": [
    { "name": "MainMenu", "scene": "project://scenes/MainMenu.cscene",
      "onEnter": [ { "emit": "menu_shown" } ],
      "transitions": [
        { "on": "play_clicked", "to": "Game", "transition": "Fade" },
        { "on": "quit_clicked", "to": "@quit" } ] },
    { "name": "Game", "scene": "project://scenes/Main.cscene",
      "transitions": [
        { "on": "key:Escape", "to": "Pause", "push": true },
        { "on": "timer:5", "to": "TimeUp" } ] },
    { "name": "Pause", "overlay": true,
      "transitions": [ { "on": "resume_clicked", "to": "@pop" } ] },
    { "name": "TimeUp", "scene": "project://scenes/Win.cscene",
      "transitions": [ { "on": "ok", "to": "@quit" } ] }
  ]
})";

TEST_CASE("U5: .cflow parses, serializes, and round-trips")
{
    FlowAsset a;
    std::string err;
    REQUIRE(FlowAsset::LoadFromString(a, kFlow, &err));
    CHECK(err.empty());

    CHECK(a.Version == 1);
    CHECK(a.Start == "MainMenu");
    REQUIRE(a.States.size() == 4);

    const FlowState* menu = a.Find("MainMenu");
    REQUIRE(menu != nullptr);
    REQUIRE(menu->OnEnter.size() == 1);
    CHECK(menu->OnEnter[0].ActionType == FlowAction::Type::Emit);
    CHECK(menu->OnEnter[0].Signal == "menu_shown");
    REQUIRE(menu->Transitions.size() == 2);
    CHECK(menu->Transitions[0].On == "play_clicked");
    CHECK(menu->Transitions[0].To == "Game");
    CHECK(menu->Transitions[0].Transition == "Fade");

    const FlowState* game = a.Find("Game");
    REQUIRE(game != nullptr);
    CHECK(game->Transitions[0].Push == true);

    const FlowState* pause = a.Find("Pause");
    REQUIRE(pause != nullptr);
    CHECK(pause->Overlay == true);
    CHECK(pause->Scene.empty());

    // Round-trip: save and reload preserves structure.
    FlowAsset b;
    REQUIRE(FlowAsset::LoadFromString(b, a.SaveToString(), &err));
    REQUIRE(b.States.size() == 4);
    CHECK(b.Find("Game")->Transitions[0].Push == true);
    CHECK(b.Find("MainMenu")->OnEnter[0].Signal == "menu_shown");
}

TEST_CASE("U6: node layout (EditorPos) round-trips the .cflow and is runtime-inert")
{
    // The Flow Graph panel persists node positions into each state's
    // "editor": {"pos": [x,y]} block; the runtime must ignore them entirely.
    FlowAsset a;
    REQUIRE(FlowAsset::LoadFromString(a, kFlow, nullptr));
    a.States[0].EditorPos = {  40.0f,  60.0f };
    a.States[1].EditorPos = { 340.0f, 260.0f };

    FlowAsset b;
    std::string err;
    REQUIRE(FlowAsset::LoadFromString(b, a.SaveToString(), &err));
    CHECK(b.States[0].EditorPos.x == doctest::Approx(40.0f));
    CHECK(b.States[0].EditorPos.y == doctest::Approx(60.0f));
    CHECK(b.States[1].EditorPos.x == doctest::Approx(340.0f));
    CHECK(b.States[1].EditorPos.y == doctest::Approx(260.0f));

    // Same structure/validation with or without layout: the block is editor-only.
    CHECK(b.Validate().empty());
    CHECK(b.States.size() == a.States.size());
}

TEST_CASE("U5: Validate flags missing start and unknown transition targets")
{
    FlowAsset a;
    FlowAsset::LoadFromString(a, kFlow, nullptr);
    CHECK(a.Validate().empty());

    // Break a transition target.
    a.States[0].Transitions[0].To = "Nowhere";
    auto errs = a.Validate();
    CHECK_FALSE(errs.empty());

    // Missing start state.
    FlowAsset b;
    b.Start = "DoesNotExist";
    CHECK_FALSE(b.Validate().empty());
}

TEST_CASE("U5: full run — menu -> game -> pause(push) -> pop -> timer -> quit")
{
    FlowAsset a;
    REQUIRE(FlowAsset::LoadFromString(a, kFlow, nullptr));

    std::vector<std::string> loadLog;
    std::unordered_map<std::string, Ref<Scene>> scenes;
    auto loader = [&](const std::string& path) -> Ref<Scene>
    {
        loadLog.push_back(path);
        auto& s = scenes[path];
        if (!s) s = Scene::Create();
        return s;
    };

    FlowMachine fm;
    fm.SetSceneLoader(loader);
    fm.Start(a);

    CHECK(fm.IsRunning());
    CHECK(fm.CurrentState() == "MainMenu");
    CHECK(fm.StackDepth() == 1);

    // Click Play (queued, applied in OnUpdate).
    fm.FeedSignal("play_clicked");
    fm.OnUpdate(0.0f);
    CHECK(fm.CurrentState() == "Game");
    CHECK(fm.StackDepth() == 1);

    // Escape pushes the Pause overlay (no scene of its own -> reuses Game's scene).
    fm.FeedSignal("key:Escape");
    fm.OnUpdate(0.0f);
    CHECK(fm.CurrentState() == "Pause");
    CHECK(fm.StackDepth() == 2);
    CHECK(fm.ActiveScene() == scenes["project://scenes/Main.cscene"]);

    // Resume pops back to Game.
    fm.FeedSignal("resume_clicked");
    fm.OnUpdate(0.0f);
    CHECK(fm.CurrentState() == "Game");
    CHECK(fm.StackDepth() == 1);

    // Time passes (pop reset the timer) -> timer:5 fires -> TimeUp.
    fm.OnUpdate(6.0f);
    CHECK(fm.CurrentState() == "TimeUp");

    // Quit.
    fm.FeedSignal("ok");
    fm.OnUpdate(0.0f);
    CHECK(fm.QuitRequested());
    CHECK_FALSE(fm.IsRunning());

    // Scene loads happened in order (Pause loaded nothing).
    REQUIRE(loadLog.size() == 3);
    CHECK(loadLog[0] == "project://scenes/MainMenu.cscene");
    CHECK(loadLog[1] == "project://scenes/Main.cscene");
    CHECK(loadLog[2] == "project://scenes/Win.cscene");
}

TEST_CASE("U5: a guard reads a reflected field and blocks/allows the transition")
{
    const char* flow = R"({
      "cosmic_flow": 1, "start": "Menu",
      "states": [
        { "name": "Menu", "scene": "mem://menu",
          "transitions": [
            { "on": "continue", "to": "Game",
              "if": { "entity": "SaveSlot", "component": "Camera",
                      "field": "Primary", "op": "==", "value": true } } ] },
        { "name": "Game", "scene": "mem://game", "transitions": [] }
      ]
    })";

    FlowAsset a;
    REQUIRE(FlowAsset::LoadFromString(a, flow, nullptr));

    // A menu scene carrying the guard's target entity.
    Ref<Scene> menu = Scene::Create();
    Entity slot = menu->CreateEntity("SaveSlot");
    auto& cam = slot.AddComponent<CameraComponent>();   // Primary defaults true
    Ref<Scene> game = Scene::Create();

    auto loader = [&](const std::string& path) -> Ref<Scene>
    {
        if (path == "mem://menu") return menu;
        return game;
    };

    // Guard TRUE -> transition allowed.
    {
        FlowMachine fm;
        fm.SetSceneLoader(loader);
        fm.Start(a);
        fm.FeedSignal("continue");
        fm.OnUpdate(0.0f);
        CHECK(fm.CurrentState() == "Game");
    }

    // Guard FALSE -> transition blocked, stays on Menu.
    cam.Primary = false;
    {
        FlowMachine fm;
        fm.SetSceneLoader(loader);
        fm.Start(a);
        fm.FeedSignal("continue");
        fm.OnUpdate(0.0f);
        CHECK(fm.CurrentState() == "Menu");
    }
}

// ===========================================================================
// Q2 — flow variables (typed blackboard)
// ===========================================================================

TEST_CASE("Q2: a variable-gated transition fires only after three increments")
{
    // Score starts at 0; entering Room adds 1 (a setVar action); the guarded
    // "leave" transition needs Score >= 3. Scene-less states (variable guards
    // need no scene).
    const char* flow = R"({
      "cosmic_flow": 2, "start": "Room",
      "variables": [ { "name": "Score", "type": "number", "default": 0 } ],
      "states": [
        { "name": "Room",
          "onEnter": [ { "setVar": { "var": "Score", "add": true, "value": 1 } } ],
          "transitions": [
            { "on": "leave", "to": "Exit", "if": { "var": "Score", "op": ">=", "value": 3 } },
            { "on": "loop",  "to": "Room" } ] },
        { "name": "Exit", "transitions": [] }
      ]
    })";

    FlowAsset a;
    std::string err;
    REQUIRE(FlowAsset::LoadFromString(a, flow, &err));
    CHECK(err.empty());
    REQUIRE(a.Variables.size() == 1);
    CHECK(a.Variables[0].Name == "Score");
    CHECK(a.Variables[0].Default.ValueKind == FlowValue::Kind::Number);

    FlowMachine fm;
    fm.Start(a);
    CHECK(fm.CurrentState() == "Room");
    CHECK(fm.GetVar("Score").Number == doctest::Approx(1.0));   // onEnter ran once

    // Score = 1 < 3 -> "leave" blocked.
    fm.FeedSignal("leave");
    fm.OnUpdate(0.0f);
    CHECK(fm.CurrentState() == "Room");

    // Re-enter Room twice via "loop" -> Score climbs to 3.
    fm.FeedSignal("loop"); fm.OnUpdate(0.0f);
    CHECK(fm.GetVar("Score").Number == doctest::Approx(2.0));
    fm.FeedSignal("loop"); fm.OnUpdate(0.0f);
    CHECK(fm.GetVar("Score").Number == doctest::Approx(3.0));

    // Score = 3 >= 3 -> "leave" now fires.
    fm.FeedSignal("leave");
    fm.OnUpdate(0.0f);
    CHECK(fm.CurrentState() == "Exit");
}

TEST_CASE("Q2: runtime SetVar/GetVar drives a variable guard (the Flow() proxy path)")
{
    const char* flow = R"({
      "cosmic_flow": 2, "start": "A",
      "variables": [ { "name": "Score", "type": "number", "default": 0 } ],
      "states": [
        { "name": "A", "transitions": [
            { "on": "go", "to": "B", "if": { "var": "Score", "op": ">=", "value": 3 } } ] },
        { "name": "B", "transitions": [] }
      ]
    })";
    FlowAsset a;
    REQUIRE(FlowAsset::LoadFromString(a, flow, nullptr));

    FlowMachine fm;
    fm.Start(a);
    fm.FeedSignal("go"); fm.OnUpdate(0.0f);
    CHECK(fm.CurrentState() == "A");   // Score 0

    // Three script-style increments (what Flow().AddNumber does under the hood).
    for (int i = 0; i < 3; ++i)
        fm.SetVar("Score", FlowValue::MakeNumber(fm.GetVar("Score").Number + 1.0));
    CHECK(fm.GetVar("Score").Number == doctest::Approx(3.0));

    fm.FeedSignal("go"); fm.OnUpdate(0.0f);
    CHECK(fm.CurrentState() == "B");

    // A missing variable => the guard is false (unknown var never crashes).
    CHECK(fm.GetVar("Nope").Bool == false);
}

TEST_CASE("Q2: old v1 .cflow loads unchanged (no variables, stays v1 on save)")
{
    FlowAsset a;
    REQUIRE(FlowAsset::LoadFromString(a, kFlow, nullptr));
    CHECK(a.Version == 1);
    CHECK(a.Variables.empty());

    // A variable-free flow re-saves at v1 with no "variables" block (byte-compat).
    const std::string out = a.SaveToString();
    CHECK(out.find("\"cosmic_flow\": 1") != std::string::npos);
    CHECK(out.find("variables") == std::string::npos);
    CHECK(out.find("setVar") == std::string::npos);
}

TEST_CASE("Q2: variables + enum + setVar round-trip through the serializer")
{
    FlowAsset a;
    a.Start = "S";
    FlowVariable score;  score.Name = "Score"; score.Group = "Player"; score.Default = FlowValue::MakeNumber(2.0);
    FlowVariable mood;   mood.Name = "Mood"; mood.Default = FlowValue::MakeEnum("Calm"); mood.EnumOptions = { "Calm", "Angry" };
    a.Variables = { score, mood };

    FlowState s; s.Name = "S";
    FlowAction inc; inc.ActionType = FlowAction::Type::SetVar; inc.Var = "Score"; inc.VarAdd = true; inc.Value = FlowValue::MakeNumber(1.0);
    s.OnEnter.push_back(inc);
    FlowTransition t; t.On = "x"; t.To = "S"; t.HasGuard = true;
    t.Guard.Var = "Mood"; t.Guard.Op = "=="; t.Guard.Value = FlowValue::MakeString("Angry");
    s.Transitions.push_back(t);
    a.States.push_back(s);

    const std::string json = a.SaveToString();
    CHECK(json.find("\"cosmic_flow\": 2") != std::string::npos);

    FlowAsset b;
    REQUIRE(FlowAsset::LoadFromString(b, json, nullptr));
    REQUIRE(b.Variables.size() == 2);
    CHECK(b.Variables[0].Name == "Score");
    CHECK(b.Variables[0].Group == "Player");
    CHECK(b.Variables[0].Default.Number == doctest::Approx(2.0));
    CHECK(b.Variables[1].Default.ValueKind == FlowValue::Kind::Enum);
    REQUIRE(b.Variables[1].EnumOptions.size() == 2);
    CHECK(b.Variables[1].EnumOptions[1] == "Angry");
    REQUIRE(b.States.size() == 1);
    CHECK(b.States[0].OnEnter[0].ActionType == FlowAction::Type::SetVar);
    CHECK(b.States[0].OnEnter[0].VarAdd == true);
    CHECK(b.States[0].Transitions[0].Guard.Var == "Mood");
}
