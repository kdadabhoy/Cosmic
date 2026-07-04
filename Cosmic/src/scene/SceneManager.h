#pragma once
// scene/SceneManager.h
//
// ============================================================================
// Cosmic scene transitions — async-friendly load + fade state machine (E5).
// ============================================================================
//
// A plain engine service (NOT a global singleton — owned + ticked by whoever
// runs the frame, exactly like SerialLink). Drives scene swaps with an optional
// fade so a load frame never shows as a hitch:
//
//     Idle --Request--> FadeOut --> Loading --> FadeIn --> Idle
//
// The owner ticks OnUpdate(dt) and reads IsLoading()/Progress()/FadeAlpha() to
// draw a LoadingScreen-style overlay; the actual scene build runs on the MAIN
// thread during the Loading frame (GL resources are main-thread only — the
// JobSystem CPU-prepass split is a documented follow-up; the fade hides the one
// load frame). Scripts (E11) and the editor's File>Open (sync Load) both drive
// this. Pure logic — no GL — so the state machine is headless-tested.
// ============================================================================

#include "core/Core.h"

#include <functional>
#include <string>

namespace Cosmic
{
    class Scene;

    enum class SceneTransition { None, Fade };
    enum class SceneLoadState  { Idle, FadeOut, Loading, FadeIn };

    class COSMIC_API SceneManager
    {
    public:
        // Builds the next scene. May do its own CPU-side work, but any GL
        // resource creation must happen on the calling (main) thread — it is
        // invoked from OnUpdate during the Loading frame. Return null on failure
        // (the active scene is then kept and OnLoadFailed-style state is set).
        using SceneLoader = std::function<Ref<Scene>()>;

        explicit SceneManager(float fadeSeconds = 0.4f);

        // Queue a transition to a .cscene file (parsed via SceneSerializer into a
        // fresh Scene). Queue a transition to a custom-built scene. A Request made
        // mid-transition is stored and honored when the current one finishes
        // (latest pending wins — no re-entrancy).
        void Request(const std::string& path, SceneTransition transition = SceneTransition::Fade);
        void Request(SceneLoader loader,       SceneTransition transition = SceneTransition::Fade);

        // Synchronous load with no fade (editor File>Open): parse, swap, done.
        // Returns false (and keeps the current scene) on parse failure.
        bool Load(const std::string& path);

        // Advance the state machine by dt seconds.
        void OnUpdate(float dt);

        bool            IsLoading() const { return m_State != SceneLoadState::Idle; }
        SceneLoadState  GetState()  const { return m_State; }
        float           Progress()  const;    // 0..1 across the whole transition
        float           FadeAlpha() const;    // 0 = clear .. 1 = opaque overlay
        bool            LastLoadSucceeded() const { return m_LastLoadOk; }

        const Ref<Scene>& GetActiveScene() const { return m_Active; }
        void SetActiveScene(const Ref<Scene>& scene) { m_Active = scene; }

        void SetFadeSeconds(float s) { m_FadeSeconds = (s > 0.0f) ? s : 0.0f; }
        float GetFadeSeconds() const { return m_FadeSeconds; }

    private:
        void BeginPending();   // move the queued request into the active one
        void DoLoad();         // run the loader on the main thread, swap on success

        Ref<Scene>      m_Active;
        SceneLoadState  m_State = SceneLoadState::Idle;
        float           m_FadeSeconds;
        float           m_Elapsed = 0.0f;

        SceneLoader     m_CurrentLoader;
        SceneTransition m_CurrentTransition = SceneTransition::Fade;
        bool            m_LastLoadOk = true;

        bool            m_HasPending = false;
        SceneLoader     m_PendingLoader;
        SceneTransition m_PendingTransition = SceneTransition::Fade;
    };
}
