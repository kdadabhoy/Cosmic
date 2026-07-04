#pragma once

// LightningDirector.h
//
// Frontier storm choreography (Phase 11, doc 10 F16). Schedules periodic
// lightning strikes and exposes a decaying flash strength the world multiplies
// into the sun + sky intensity (a cold-white pulse), then fires a distance-
// delayed, distance-scaled `thunder` one-shot (F10 AudioEngine::Play). Purely
// app-side policy — the engine ships only the generic light/sky/audio verbs.
//
// Header-only, deterministic per seed, headless-safe (Play no-ops on a degraded
// audio device).

#include <Cosmic.h>

#include <glm/glm.hpp>

#include <algorithm>

namespace Frontier
{
    class LightningDirector
    {
    public:
        void Init(const Cosmic::Ref<Cosmic::Sound>& thunder, uint32_t seed = 0x11A6B70u)
        {
            m_Thunder      = thunder;
            m_Rng          = Cosmic::Random(seed);
            m_TimeToNext   = m_Rng.Range(3.0f, 8.0f);
            m_Flash        = 0.0f;
            m_ThunderDelay = -1.0f;
        }

        void Update(float dt)
        {
            // Decay the flash (strike = 1, fades over ~0.25 s = a snappy pulse).
            m_Flash = std::max(0.0f, m_Flash - dt / 0.25f);

            // Pending thunder: count down the acoustic travel delay, fire once.
            if (m_ThunderDelay >= 0.0f)
            {
                m_ThunderDelay -= dt;
                if (m_ThunderDelay <= 0.0f)
                {
                    Cosmic::AudioEngine::Play(m_Thunder, m_ThunderVolume);
                    m_ThunderDelay = -1.0f;
                }
            }

            m_TimeToNext -= dt;
            if (m_TimeToNext <= 0.0f)
            {
                Strike();
                m_TimeToNext = m_Rng.Range(6.0f, 18.0f);
            }
        }

        /** Flash now (also called by a panel button). Picks a bearing + distance,
         *  and schedules the thunder at distance / speed-of-sound. */
        void Strike()
        {
            m_Flash    = 1.0f;
            m_Azimuth  = m_Rng.Range(0.0f, 6.2831853f);
            const float distance = m_Rng.Range(300.0f, 4000.0f);   // metres to the bolt
            m_ThunderDelay  = distance / 340.0f;                   // sound travel time (s)
            m_ThunderVolume = glm::clamp(1.0f - distance / 4500.0f, 0.05f, 1.0f);
        }

        float FlashStrength() const { return m_Flash; }               // 0..1
        float SunMultiplier() const { return 1.0f + 5.0f * m_Flash; }  // ~x6 at peak
        float SkyMultiplier() const { return 1.0f + 2.0f * m_Flash; }  // ~x3 at peak
        float Azimuth()       const { return m_Azimuth; }

    private:
        Cosmic::Ref<Cosmic::Sound> m_Thunder;
        Cosmic::Random m_Rng{ 1u };
        float m_TimeToNext    = 6.0f;
        float m_Flash         = 0.0f;
        float m_Azimuth       = 0.0f;
        float m_ThunderDelay  = -1.0f;
        float m_ThunderVolume = 1.0f;
    };
}
