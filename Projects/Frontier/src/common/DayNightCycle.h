#pragma once

// DayNightCycle.h
//
// Time-of-day policy for Frontier (Phase 11, doc 10 F12a). App-side: the engine
// ships the generic sky/IBL/lens-flare verbs; the *palette* and the sun/moon
// choreography are app content, reusable by every world. Evaluate(hours) turns a
// 0..24 clock into a full lighting state — sun (or moonlight) direction/color,
// ambient, fog, and a filled-out Cosmic::SkyDetailDesc for the per-pixel sky.
//
// Sun path copies the Engine3DDemo / IslandWorld sun-from-hour math (sunrise 6,
// zenith 12, sunset 18). Below the horizon the sun light hands off to a cool,
// dim moonlight travelling opposite the sun's azimuth, and the night sky IBL +
// crisp moon disc + stars fade in. Header-only, pure (no GL, deterministic).

#include <Cosmic.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace Frontier
{
    /** Everything a world needs to light one frame for a given hour. */
    struct DayState
    {
        glm::vec3 ToSun{ 0.0f, 1.0f, 0.0f };       // direction TO the real sun — drives the
                                                   // sky/IBL appearance (env.SetSunDirection),
                                                   // NOT the key light (which is moonlit at night)
        glm::vec3 SunDir{ 0.0f, -1.0f, 0.0f };     // direction the key light TRAVELS
        glm::vec3 SunColor{ 1.0f };
        float     SunIntensity = 1.0f;
        float     Ambient      = 0.25f;

        glm::vec3 FogColor{ 0.72f, 0.82f, 0.95f };
        float     FogDensity   = 0.0025f;

        glm::vec3 MoonDir{ 0.0f, 1.0f, 0.0f };     // direction TO the moon (IBL night glow)
        float     MoonIntensity = 0.0f;            // baked night-sky moon glow (0..1)
        bool      Night = false;

        Cosmic::SkyDetailDesc Sky;                 // per-pixel detailed-sky params
    };

    class DayNightCycle
    {
    public:
        /** Map a 0..24 hour to a full lighting state. `timeSeconds` only drives the
         *  detailed sky's star twinkle (Sky.Time). */
        static DayState Evaluate(float hours, float timeSeconds = 0.0f)
        {
            const float pi = glm::pi<float>();

            // --- Sun path (Engine3DDemo math): sunrise 6, zenith 12, sunset 18.
            const float f   = (hours - 6.0f) / 12.0f;            // 0 at 06:00, 1 at 18:00
            const float alt = std::sin(f * pi) * (pi * 0.5f);    // < 0 outside daytime
            const float azi = f * pi;                            // east -> west
            const glm::vec3 toSun = glm::normalize(glm::vec3(
                std::cos(alt) * std::cos(azi), std::sin(alt), std::cos(alt) * std::sin(azi)));

            DayState s;
            s.ToSun = toSun;

            const float elev  = glm::clamp(toSun.y, 0.0f, 1.0f);
            // Night ramp: 0 by day, 1 once the sun is well below the horizon (matches
            // SkyDetail's day = smoothstep(-0.25, 0.10, sun.y) crossover).
            const float night = glm::clamp((0.10f - toSun.y) / 0.35f, 0.0f, 1.0f);
            s.Night = toSun.y < 0.0f;

            // --- Key light: sun by day, cool moonlight by night ---
            s.SunDir       = -toSun;
            s.SunColor     = glm::mix(glm::vec3(1.0f, 0.62f, 0.36f),   // warm at the terminator
                                      glm::vec3(1.0f, 0.97f, 0.92f),   // white at noon
                                      glm::clamp(elev * 2.0f, 0.0f, 1.0f));
            s.SunIntensity = glm::mix(0.0f, 3.2f, elev);
            s.Ambient      = glm::mix(0.05f, 0.30f, elev);

            // Moon: opposite the sun's azimuth, fixed +35 deg elevation.
            const float moonElev = glm::radians(35.0f);
            const float moonAzi  = azi + pi;
            const glm::vec3 toMoon = glm::normalize(glm::vec3(
                std::cos(moonElev) * std::cos(moonAzi),
                std::sin(moonElev),
                std::cos(moonElev) * std::sin(moonAzi)));
            s.MoonDir       = toMoon;
            s.MoonIntensity = night;                             // baked night-sky glow ramp

            if (s.Night)
            {
                // The key light becomes cool, dim moonlight travelling opposite the moon.
                s.SunDir       = -toMoon;
                s.SunColor     = glm::vec3(0.62f, 0.71f, 0.90f);
                s.SunIntensity = 0.12f * night;
            }

            // --- Fog palette: warm at the terminators, cool by day, dark at night ---
            const glm::vec3 dayFog { 0.72f, 0.82f, 0.95f };
            const glm::vec3 dusk   { 0.85f, 0.55f, 0.40f };
            const glm::vec3 nightFog{ 0.03f, 0.05f, 0.10f };
            if (toSun.y >= 0.0f)
                s.FogColor = glm::mix(dusk, dayFog, glm::clamp(toSun.y * 3.0f, 0.0f, 1.0f));
            else
                s.FogColor = glm::mix(dusk, nightFog, glm::clamp(-toSun.y * 3.0f, 0.0f, 1.0f));
            s.FogDensity = 0.0025f;

            // --- Detailed sky (SkyDetail.glsl): the shader gates the moon/stars by
            //     its own day/night ramp, so these can stay constant. The sun disc's
            //     direction comes from the env's stored sun (set alongside this). ---
            s.Sky.SkyIntensity      = 1.0f;
            s.Sky.SunDiscIntensity  = 40.0f;
            s.Sky.SunAngularRadius  = 0.00465f;
            s.Sky.MoonDirection     = toMoon;
            s.Sky.MoonIntensity     = 1.5f;
            s.Sky.MoonAngularRadius = 0.0087f;
            s.Sky.StarIntensity     = 1.0f;
            s.Sky.StarDensity       = 90.0f;
            s.Sky.MilkyWayIntensity = 0.35f;
            s.Sky.MilkyWayDir       = glm::normalize(glm::vec3(0.36f, 0.48f, 0.80f));
            s.Sky.Time              = timeSeconds;

            return s;
        }
    };

} // namespace Frontier
