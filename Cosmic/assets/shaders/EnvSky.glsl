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

// --- Phase 27 X1 physical atmosphere (u_SkyMode default 0 = legacy, byte-identical) ---
uniform float u_SkyMode;        // 0 = ProceduralSky gradient, 1 = PhysicalSky scattering
uniform float u_Turbidity;      // scales Mie density (haze)
uniform float u_RayleighScale;  // scales Rayleigh (blue) scattering
uniform float u_MieScale;       // scales Mie (white haze / sun halo) scattering
uniform float u_MieG;           // Mie phase asymmetry (0..0.99)
uniform float u_SunAngularRadius; // sun-disc radius in radians (X2); 0 = no crisp disc

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

// ---------------------------------------------------------------------------
// X1 — analytic Rayleigh+Mie single-scattering atmosphere. A pure function of
// the view + sun directions (no camera position), so the six cube faces bake a
// consistent sky that the skybox draws and the irradiance/prefilter passes
// convolve — lighting always matches the visible sky. Turbidity scales the Mie
// (haze) density; RayleighScale/MieScale weight the two terms; MieG tightens the
// forward sun halo. Ray-marched (16 view x 8 light samples) — cheap enough for a
// 256^2 cube that only rebakes when the sun moves.
// ---------------------------------------------------------------------------
vec3 PhysicalSky(vec3 dir, vec3 sun)
{
    const float PI     = 3.141592653589793;
    const float Rg     = 6360e3;                            // ground radius (m)
    const float Ra     = 6420e3;                            // atmosphere top (m)
    const vec3  betaR0 = vec3(5.8e-6, 13.5e-6, 33.1e-6);    // Rayleigh @ sea level
    const float betaM0 = 21e-6;                             // Mie @ sea level
    const float Hr     = 8000.0;                            // Rayleigh scale height
    const float Hm     = 1200.0;                            // Mie scale height
    const int   STEPS  = 16;
    const int   LSTEPS = 8;
    const float kSunIntensity = 22.0;                       // maps scattering into the HDR range

    vec3  betaR = betaR0 * max(u_RayleighScale, 0.0);
    float betaM = betaM0 * max(u_MieScale, 0.0) * max(u_Turbidity, 0.0);

    vec3  origin = vec3(0.0, Rg + 2.0, 0.0);                // eye ~2 m above ground

    // Primary ray: far intersection with the atmosphere shell, clipped at the
    // ground for downward rays (gives a real horizon instead of integrating
    // through the planet).
    float b      = dot(origin, dir);
    float atmFar = -b + sqrt(max(b * b - (dot(origin, origin) - Ra * Ra), 0.0));
    float tMax   = atmFar;
    float discg  = b * b - (dot(origin, origin) - Rg * Rg);
    if (discg > 0.0)
    {
        float tg = -b - sqrt(discg);
        if (tg > 0.0) tMax = min(tMax, tg);
    }
    if (tMax <= 0.0)
        return vec3(0.0);

    float mu     = dot(dir, sun);
    float phaseR = 3.0 / (16.0 * PI) * (1.0 + mu * mu);
    float g      = clamp(u_MieG, 0.0, 0.99);
    float phaseM = 3.0 / (8.0 * PI) * ((1.0 - g * g) * (1.0 + mu * mu)) /
                   ((2.0 + g * g) * pow(max(1.0 + g * g - 2.0 * g * mu, 1e-4), 1.5));

    float segLen = tMax / float(STEPS);
    vec3  sumR = vec3(0.0);
    vec3  sumM = vec3(0.0);
    float odR = 0.0, odM = 0.0;                             // accumulated view-ray optical depth
    float t   = segLen * 0.5;
    for (int i = 0; i < STEPS; ++i)
    {
        vec3  p  = origin + dir * t;
        float h  = length(p) - Rg;
        float hr = exp(-h / Hr) * segLen;
        float hm = exp(-h / Hm) * segLen;
        odR += hr;
        odM += hm;

        // Light ray toward the sun: optical depth to the atmosphere top.
        float bl   = dot(p, sun);
        float lFar = -bl + sqrt(max(bl * bl - (dot(p, p) - Ra * Ra), 0.0));
        float lSeg = lFar / float(LSTEPS);
        float odLR = 0.0, odLM = 0.0;
        float tl   = lSeg * 0.5;
        bool  ground = false;
        for (int j = 0; j < LSTEPS; ++j)
        {
            vec3  pl = p + sun * tl;
            float hl = length(pl) - Rg;
            if (hl < 0.0) { ground = true; break; }         // sun below the local horizon
            odLR += exp(-hl / Hr) * lSeg;
            odLM += exp(-hl / Hm) * lSeg;
            tl   += lSeg;
        }
        if (!ground)
        {
            vec3 tau = betaR * (odR + odLR) + betaM * 1.1 * (odM + odLM);
            vec3 att = exp(-tau);
            sumR += att * hr;
            sumM += att * hm;
        }
        t += segLen;
    }

    vec3 col = kSunIntensity * (sumR * betaR * phaseR + sumM * betaM * phaseM);

    // Sun disc (X2) — a crisp limb-darkened disc sized by u_SunAngularRadius,
    // attenuated by the view ray's own transmittance so it reddens/dims as it
    // sinks toward the horizon (longer optical path scatters out the blue).
    if (u_SunAngularRadius > 1e-6 && mu > 0.0)
    {
        float sinT = length(cross(dir, sun));
        float r    = sinT / u_SunAngularRadius;
        if (r < 1.0)
        {
            float limb = 1.0 - 0.6 * (1.0 - sqrt(max(1.0 - r * r, 0.0)));
            vec3  ext  = exp(-(betaR * odR + betaM * 1.1 * odM));   // view-ray transmittance
            col += 20.0 * limb * ext;
        }
    }

    return max(col, vec3(0.0)) * u_SkyIntensity;
}

void main()
{
    vec3 dir = normalize(v_LocalPos);
    vec3 sun = normalize(u_SunDirection);
    // u_SkyMode default 0 selects the shipped gradient — byte-identical output.
    vec3 c = (u_SkyMode > 0.5) ? PhysicalSky(dir, sun) : ProceduralSky(dir, sun);
    color = vec4(c, 1.0);
}
