#type vertex
#version 450 core

// EnvSky — renders a procedural analytic sky into a cubemap face (S6.3 / S7.1).
// The same sky is the IBL environment source and (via the baked cube) the
// skybox, so lighting and background always agree. Driven by a sun DIRECTION
// (vector TO the sun); irradiance/prefilter convolve the result.
//
// Cube-render mode: u_ViewProjection is the per-face view-projection; the
// fragment turns the interpolated local cube position into a world direction.
//
// Phase 11 (S11 / doc 10): optional NIGHT tier, gated by u_NightSky (GL default
// 0 = the shipped S7 behavior, byte-identical). With it on, the sky darkens
// through twilight as the sun sets and a moon glow term takes over so the IBL
// bake produces plausible moon-lit ambient. Fine detail (stars, moon disc,
// craters) deliberately does NOT live here — the cube is far too low-res; the
// per-pixel SkyDetail.glsl background pass owns it. Keep the shared palette
// math in sync with SkyDetail.glsl (documented pairing).

layout(location = 0) in vec3 a_Position;

uniform mat4 u_ViewProjection;

out vec3 v_LocalPos;

void main()
{
    v_LocalPos  = a_Position;
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;

in vec3 v_LocalPos;

uniform vec3  u_SunDirection;   // direction TO the sun (normalized)
uniform float u_SkyIntensity;   // overall multiplier (HDR)

// --- Phase 11 night tier (all default 0 = legacy output; F7 feeds them) ---
uniform float u_NightSky;       // 0 = legacy day-only palette, 1 = enable night
uniform vec3  u_MoonDirection;  // direction TO the moon (normalized)
uniform float u_MoonIntensity;  // moonlight strength (0 = no moon term)

// A cheap but plausible daytime sky: a zenith→horizon gradient, a warm ground
// hemisphere, a sun disk + glow, horizon haze that brightens toward the sun, and
// a sunset reddening as the sun nears the horizon. Not Hosek — good enough to
// light a scene and read as sky; S7 tunes it further.
vec3 ProceduralSky(vec3 dir, vec3 sun)
{
    float up      = clamp(dir.y, -1.0, 1.0);
    float sunDot  = max(dot(dir, sun), 0.0);
    float sunElev = clamp(sun.y, 0.0, 1.0);

    // Base gradient (day palette), warmed at the horizon as the sun sets.
    vec3 zenith  = mix(vec3(0.10, 0.16, 0.42), vec3(0.16, 0.30, 0.62), sunElev);
    vec3 horizon = mix(vec3(0.95, 0.55, 0.32), vec3(0.72, 0.83, 0.95), sunElev);
    vec3 sky     = mix(horizon, zenith, pow(clamp(up, 0.0, 1.0), 0.55));

    // Ground hemisphere (below the horizon).
    vec3 ground  = vec3(0.19, 0.18, 0.17);
    sky = mix(sky, ground, clamp(-up * 4.0, 0.0, 1.0));

    // Sun disk + glow.
    vec3  sunCol = mix(vec3(1.0, 0.45, 0.20), vec3(1.0, 0.96, 0.88), sunElev);
    float glow   = pow(sunDot, 8.0) * 0.5 + pow(sunDot, 350.0) * 12.0;
    sky += sunCol * glow;

    // Horizon haze brightening toward the sun.
    float haze = pow(sunDot, 2.0) * (1.0 - clamp(up, 0.0, 1.0)) * 0.5;
    sky += sunCol * haze;

    // --- Night tier: darken through twilight; moon glow takes over so the
    // IBL convolution yields cool moon-lit ambient. ---
    if (u_NightSky > 0.5)
    {
        // 1 at sun elev >= +0.10, 0 at <= -0.25 (astronomical-ish twilight ramp).
        float day = smoothstep(-0.25, 0.10, sun.y);
        vec3 nightZenith  = vec3(0.004, 0.007, 0.016);
        vec3 nightHorizon = vec3(0.012, 0.016, 0.030);
        vec3 nightSky     = mix(nightHorizon, nightZenith, pow(clamp(up, 0.0, 1.0), 0.55));
        nightSky = mix(nightSky, ground * 0.03, clamp(-up * 4.0, 0.0, 1.0));

        if (u_MoonIntensity > 0.0)
        {
            float moonDot  = max(dot(dir, normalize(u_MoonDirection)), 0.0);
            vec3  moonCol  = vec3(0.62, 0.71, 0.90);
            float moonGlow = pow(moonDot, 12.0) * 0.35 + pow(moonDot, 300.0) * 6.0;
            nightSky += moonCol * moonGlow * u_MoonIntensity;
        }

        sky = mix(nightSky, sky, day);
    }

    return max(sky, vec3(0.0)) * u_SkyIntensity;
}

void main()
{
    vec3 dir = normalize(v_LocalPos);
    color = vec4(ProceduralSky(dir, normalize(u_SunDirection)), 1.0);
}
