// test_scenemanager.cpp — scene transition state machine (Phase 13 / E5).
// Headless: fake loaders, no GL, no file I/O.
//
// Acceptance (plan doc 11 E5): state-machine unit test (fake loader) — the fade
// cycle, no-fade immediate load, mid-transition queuing, alpha/progress bounds,
// and a failed load keeping the current scene.

#include <doctest.h>

#include "scene/Scene.h"
#include "scene/SceneManager.h"

using namespace Cosmic;

TEST_CASE("E5: a fade transition runs FadeOut -> Loading -> FadeIn -> Idle once")
{
    SceneManager mgr(0.4f);
    CHECK(mgr.GetState() == SceneLoadState::Idle);
    CHECK_FALSE(mgr.IsLoading());

    int loads = 0;
    mgr.Request([&]() -> Ref<Scene> { ++loads; return Scene::Create(); },
                SceneTransition::Fade);

    CHECK(mgr.IsLoading());
    CHECK(mgr.GetState() == SceneLoadState::FadeOut);
    CHECK(mgr.FadeAlpha() == doctest::Approx(0.0f));   // just started fading out

    // Drive to completion in small steps.
    for (int i = 0; i < 100 && mgr.IsLoading(); ++i)
        mgr.OnUpdate(0.05f);

    CHECK_FALSE(mgr.IsLoading());
    CHECK(mgr.GetState() == SceneLoadState::Idle);
    CHECK(loads == 1);
    CHECK(mgr.GetActiveScene() != nullptr);
    CHECK(mgr.LastLoadSucceeded());
    CHECK(mgr.FadeAlpha() == doctest::Approx(0.0f));
}

TEST_CASE("E5: a no-fade transition loads on the next Loading frame")
{
    SceneManager mgr(0.4f);
    int loads = 0;
    mgr.Request([&]() -> Ref<Scene> { ++loads; return Scene::Create(); },
                SceneTransition::None);

    CHECK(mgr.GetState() == SceneLoadState::Loading);   // straight to load, no fade
    CHECK(mgr.FadeAlpha() == doctest::Approx(0.0f));

    mgr.OnUpdate(0.0f);   // runs the Loading frame
    CHECK(loads == 1);
    CHECK(mgr.GetState() == SceneLoadState::Idle);
}

TEST_CASE("E5: a request made mid-transition is queued and honored after")
{
    SceneManager mgr(0.4f);
    int a = 0, b = 0;

    mgr.Request([&]() -> Ref<Scene> { ++a; return Scene::Create(); }, SceneTransition::Fade);
    // Second request arrives while the first is still fading out.
    mgr.Request([&]() -> Ref<Scene> { ++b; return Scene::Create(); }, SceneTransition::Fade);
    CHECK(mgr.GetState() == SceneLoadState::FadeOut);   // still the first transition

    for (int i = 0; i < 500; ++i)
        mgr.OnUpdate(0.05f);

    CHECK(a == 1);
    CHECK(b == 1);
    CHECK(mgr.GetState() == SceneLoadState::Idle);
}

TEST_CASE("E5: alpha and progress stay in [0,1] across a transition")
{
    SceneManager mgr(0.4f);
    mgr.Request([&]() -> Ref<Scene> { return Scene::Create(); }, SceneTransition::Fade);

    for (int i = 0; i < 100 && mgr.IsLoading(); ++i)
    {
        const float alpha = mgr.FadeAlpha();
        const float prog  = mgr.Progress();
        CHECK(alpha >= 0.0f);
        CHECK(alpha <= 1.0f);
        CHECK(prog  >= 0.0f);
        CHECK(prog  <= 1.0f);
        mgr.OnUpdate(0.05f);
    }
}

TEST_CASE("E5: a failed load keeps the current scene")
{
    SceneManager mgr(0.0f);   // no fade -> immediate Loading
    Ref<Scene> initial = Scene::Create();
    mgr.SetActiveScene(initial);

    mgr.Request([]() -> Ref<Scene> { return nullptr; }, SceneTransition::None);
    mgr.OnUpdate(0.0f);   // Loading frame -> loader returns null

    CHECK(mgr.GetActiveScene() == initial);   // unchanged
    CHECK_FALSE(mgr.LastLoadSucceeded());
    CHECK(mgr.GetState() == SceneLoadState::Idle);
}
