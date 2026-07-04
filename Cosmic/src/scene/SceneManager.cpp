// scene/SceneManager.cpp — async-friendly scene transition state machine (E5).

#include "scene/SceneManager.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"
#include "core/Log.h"

#include <algorithm>

namespace Cosmic
{
    SceneManager::SceneManager(float fadeSeconds)
        : m_FadeSeconds(fadeSeconds > 0.0f ? fadeSeconds : 0.0f)
    {
    }

    void SceneManager::Request(const std::string& path, SceneTransition transition)
    {
        // Wrap the path load in a main-thread loader (invoked during Loading).
        Request([path]() -> Ref<Scene>
        {
            Ref<Scene> scene = Scene::Create();
            if (!SceneSerializer::Load(*scene, path))
                return nullptr;
            return scene;
        }, transition);
    }

    void SceneManager::Request(SceneLoader loader, SceneTransition transition)
    {
        if (m_State == SceneLoadState::Idle)
        {
            m_CurrentLoader     = std::move(loader);
            m_CurrentTransition = transition;
            BeginPending();   // starts immediately from Idle (loader already staged below)
            return;
        }

        // Mid-transition: stash as the pending request (latest wins).
        m_PendingLoader     = std::move(loader);
        m_PendingTransition = transition;
        m_HasPending        = true;
    }

    bool SceneManager::Load(const std::string& path)
    {
        Ref<Scene> scene = Scene::Create();
        if (!SceneSerializer::Load(*scene, path))
        {
            m_LastLoadOk = false;
            return false;
        }
        m_Active     = scene;
        m_State      = SceneLoadState::Idle;
        m_Elapsed    = 0.0f;
        m_HasPending = false;
        m_LastLoadOk = true;
        return true;
    }

    void SceneManager::BeginPending()
    {
        // Enter the transition for whatever loader is currently staged.
        m_Elapsed = 0.0f;
        if (m_CurrentTransition == SceneTransition::Fade && m_FadeSeconds > 0.0f)
            m_State = SceneLoadState::FadeOut;
        else
            m_State = SceneLoadState::Loading;   // no fade → straight to load
    }

    void SceneManager::DoLoad()
    {
        Ref<Scene> next = m_CurrentLoader ? m_CurrentLoader() : nullptr;
        if (next)
        {
            m_Active     = next;
            m_LastLoadOk = true;
        }
        else
        {
            m_LastLoadOk = false;
            CS_CORE_ERROR("SceneManager: scene load failed — keeping the current scene.");
        }
    }

    void SceneManager::OnUpdate(float dt)
    {
        switch (m_State)
        {
        case SceneLoadState::Idle:
            if (m_HasPending)
            {
                m_CurrentLoader     = std::move(m_PendingLoader);
                m_CurrentTransition = m_PendingTransition;
                m_HasPending        = false;
                BeginPending();
            }
            break;

        case SceneLoadState::FadeOut:
            m_Elapsed += dt;
            if (m_Elapsed >= m_FadeSeconds)
            {
                m_Elapsed = 0.0f;
                m_State   = SceneLoadState::Loading;
            }
            break;

        case SceneLoadState::Loading:
            // Single main-thread load frame (hidden by the fade when fading).
            DoLoad();
            if (m_CurrentTransition == SceneTransition::Fade && m_FadeSeconds > 0.0f)
            {
                m_Elapsed = 0.0f;
                m_State   = SceneLoadState::FadeIn;
            }
            else
            {
                m_State = SceneLoadState::Idle;
                if (m_HasPending)   // a request arrived during the no-fade load
                {
                    m_CurrentLoader     = std::move(m_PendingLoader);
                    m_CurrentTransition = m_PendingTransition;
                    m_HasPending        = false;
                    BeginPending();
                }
            }
            break;

        case SceneLoadState::FadeIn:
            m_Elapsed += dt;
            if (m_Elapsed >= m_FadeSeconds)
            {
                m_Elapsed = 0.0f;
                m_State   = SceneLoadState::Idle;
                if (m_HasPending)
                {
                    m_CurrentLoader     = std::move(m_PendingLoader);
                    m_CurrentTransition = m_PendingTransition;
                    m_HasPending        = false;
                    BeginPending();
                }
            }
            break;
        }
    }

    float SceneManager::FadeAlpha() const
    {
        if (m_CurrentTransition == SceneTransition::None || m_FadeSeconds <= 0.0f)
            return 0.0f;

        const float t = std::clamp(m_Elapsed / m_FadeSeconds, 0.0f, 1.0f);
        switch (m_State)
        {
        case SceneLoadState::FadeOut: return t;          // 0 -> 1 (screen covers)
        case SceneLoadState::Loading: return 1.0f;       // fully covered during the swap
        case SceneLoadState::FadeIn:  return 1.0f - t;   // 1 -> 0 (screen reveals)
        default:                      return 0.0f;
        }
    }

    float SceneManager::Progress() const
    {
        const float t = (m_FadeSeconds > 0.0f) ? std::clamp(m_Elapsed / m_FadeSeconds, 0.0f, 1.0f) : 1.0f;
        switch (m_State)
        {
        case SceneLoadState::Idle:    return 1.0f;
        case SceneLoadState::FadeOut: return 0.5f * t;
        case SceneLoadState::Loading: return 0.5f;
        case SceneLoadState::FadeIn:  return 0.5f + 0.5f * t;
        }
        return 1.0f;
    }
}
