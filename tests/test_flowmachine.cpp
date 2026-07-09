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
