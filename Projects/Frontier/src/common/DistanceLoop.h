#pragma once

// DistanceLoop.h
//
// Frontier ambience helper (Phase 11, doc 10 F10). Owns ONE looping voice and
// attenuates its volume by the listener↔source distance each frame — a volcano
// rumble that swells as you fly toward the caldera, water babble near a fall,
// wind that fades with altitude. Header-only, app-side (Frontier owns audio
// MEANING; the engine ships only the generic AudioEngine verbs).
//
// Headless-safe: when audio failed to initialize (CI/RDP) PlayLooping returns
// InvalidSoundHandle and every call no-ops — no crashes, no branches at call sites.

#include <Cosmic.h>

#include <glm/glm.hpp>
#include <algorithm>

namespace Frontier
{
    class DistanceLoop
    {
    public:
        DistanceLoop() = default;
        ~DistanceLoop() { Stop(); }

        DistanceLoop(const DistanceLoop&)            = delete;
        DistanceLoop& operator=(const DistanceLoop&) = delete;

        /** Start the looping voice muted (the first Update sets its distance volume).
         *  Idempotent; safe when audio is degraded (handle stays invalid). */
        void Start(const Cosmic::Ref<Cosmic::Sound>& sound,
                   Cosmic::AudioGroup group = Cosmic::AudioGroup::Sfx)
        {
            if (m_Handle != Cosmic::InvalidSoundHandle)
                return;
            m_Handle = Cosmic::AudioEngine::PlayLooping(sound, 0.0f, 1.0f, group);
        }

        void Stop()
        {
            if (m_Handle != Cosmic::InvalidSoundHandle)
            {
                Cosmic::AudioEngine::Stop(m_Handle);
                m_Handle = Cosmic::InvalidSoundHandle;
            }
        }

        bool IsPlaying() const { return m_Handle != Cosmic::InvalidSoundHandle; }

        /** Volume = maxVol * clamp(1 - dist/radius, 0, 1)^2 (quadratic falloff:
         *  full at the source, silent at `radius`). No-op when not started. */
        void Update(const glm::vec3& listener, const glm::vec3& source,
                    float radius, float maxVol = 1.0f)
        {
            if (m_Handle == Cosmic::InvalidSoundHandle)
                return;
            const float dist = glm::length(listener - source);
            const float t    = radius > 1e-3f ? std::clamp(1.0f - dist / radius, 0.0f, 1.0f) : 0.0f;
            Cosmic::AudioEngine::SetVolume(m_Handle, maxVol * t * t);
        }

    private:
        Cosmic::SoundHandle m_Handle = Cosmic::InvalidSoundHandle;
    };
}
