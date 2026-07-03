#type vertex
#version 450 core

// SkyDetail — Phase 11 (S7 upgrade / doc 10 F7): the PER-PIXEL analytic sky
// background pass. The baked EnvSky cube stays the IBL source (lighting), but
// its per-face resolution is far too low for crisp detail — this shader
// re-evaluates the same sky analytically every frame and adds what a cube bake
// cannot hold: a physically-sized limb-darkened sun disc, a hash-based star
// field with twinkle, a milky-way band, and a moon whose PHASE emerges
// naturally from lighting its visible hemisphere by the real sun direction.
//
// Drawn exactly like Skybox.glsl (fullscreen triangle at the far plane, depth
// test/write OFF, background-first). The gradient palette below is a copy of
// EnvSky.glsl's — KEEP THE TWO IN SYNC or the horizon will pop when switching
// between the baked skybox and this pass.
//
// All intensities default to 0 (GL uniform default): the pass renders black
// until the F7 work order drives it — it is additive-new, no legacy impact.

out vec2 v_TexCoord;

void main()
{
    vec2 uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    v_TexCoord = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 1.0, 1.0);   // z = 1 → far plane
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;
layout(location = 1) out int  o_EntityID;   // MRT-safe: sky is never pickable

in vec2 v_TexCoord;

layout(std140, binding = 1) uniform CameraBlock
{
    mat4 ViewProjection;
    vec4 CameraPosition;
} u_Camera;

uniform mat4  u_InvViewProj;      // inverse(ViewProjection); set per-frame

uniform vec3  u_SunDirection;     // direction TO the sun (normalized)
uniform float u_SkyIntensity;     // overall HDR multiplier (F7 default 1.0)
uniform float u_Time;             // seconds (star twinkle)

// Sun disc (the F7 defaults in parentheses).
uniform float u_SunDiscIntensity; // HDR disc radiance (~40 — bloom does the rest)
uniform float u_SunAngularRadius; // radians (~0.00465 = the real 0.53° disc)

// Moon.
uniform vec3  u_MoonDirection;    // direction TO the moon (normalized)
uniform float u_MoonIntensity;    // disc brightness (~1.5)
uniform float u_MoonAngularRadius;// radians (~0.0087 — slightly oversized reads better)

// Stars.
uniform float u_StarIntensity;    // (~1.0)
uniform float u_StarDensity;      // cells per cube-face axis (~90)
uniform float u_MilkyWayIntensity;// (~0.35)
uniform vec3  u_MilkyWayDir;      // normal of the galactic band's great circle

// ---------------------------------------------------------------------------
// Hash / noise helpers (self-contained; no textures).
// ---------------------------------------------------------------------------
float Hash1(vec2 p)  { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
vec2  Hash2(vec2 p)  { return fract(sin(vec2(dot(p, vec2(127.1, 311.7)),
                                             dot(p, vec2(269.5, 183.3)))) * 43758.5453); }
float ValueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(Hash1(i),                Hash1(i + vec2(1, 0)), u.x),
               mix(Hash1(i + vec2(0, 1)),   Hash1(i + vec2(1, 1)), u.x), u.y);
}

// Direction -> (cube face id, face UV in [-1,1]^2). Dominant-axis mapping keeps
// star cells uniform across the sphere (no pole pinching).
vec2 CubeFaceUV(vec3 d, out float face)
{
    vec3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z) { face = d.x > 0.0 ? 0.0 : 1.0; return d.yz / a.x; }
    if (a.y >= a.z)               { face = d.y > 0.0 ? 2.0 : 3.0; return d.xz / a.y; }
    face = d.z > 0.0 ? 4.0 : 5.0; return d.xy / a.z;
}

// ---------------------------------------------------------------------------
// Base gradient — COPY of EnvSky.glsl's ProceduralSky (minus its disc/glow,
// re-added at full per-pixel quality below). Keep in sync with EnvSky.glsl.
// ---------------------------------------------------------------------------
vec3 BaseSky(vec3 dir, vec3 sun, float day)
{
    float up      = clamp(dir.y, -1.0, 1.0);
    float sunDot  = max(dot(dir, sun), 0.0);
    float sunElev = clamp(sun.y, 0.0, 1.0);

    vec3 zenith  = mix(vec3(0.10, 0.16, 0.42), vec3(0.16, 0.30, 0.62), sunElev);
    vec3 horizon = mix(vec3(0.95, 0.55, 0.32), vec3(0.72, 0.83, 0.95), sunElev);
    vec3 sky     = mix(horizon, zenith, pow(clamp(up, 0.0, 1.0), 0.55));

    vec3 ground = vec3(0.19, 0.18, 0.17);
    sky = mix(sky, ground, clamp(-up * 4.0, 0.0, 1.0));

    // Wide glow + haze (the crisp disc itself is separate, below).
    vec3  sunCol = mix(vec3(1.0, 0.45, 0.20), vec3(1.0, 0.96, 0.88), sunElev);
    sky += sunCol * pow(sunDot, 8.0) * 0.5;
    sky += sunCol * pow(sunDot, 2.0) * (1.0 - clamp(up, 0.0, 1.0)) * 0.5;

    // Night palette (same twilight ramp as EnvSky's night tier).
    vec3 nightZenith  = vec3(0.004, 0.007, 0.016);
    vec3 nightHorizon = vec3(0.012, 0.016, 0.030);
    vec3 nightSky     = mix(nightHorizon, nightZenith, pow(clamp(up, 0.0, 1.0), 0.55));
    nightSky = mix(nightSky, ground * 0.03, clamp(-up * 4.0, 0.0, 1.0));

    return mix(nightSky, sky, day);
}

void main()
{
    vec4 clip  = vec4(v_TexCoord * 2.0 - 1.0, 1.0, 1.0);
    vec4 world = u_InvViewProj * clip;
    world /= world.w;
    vec3 dir = normalize(world.xyz - u_Camera.CameraPosition.xyz);

    vec3  sun = normalize(u_SunDirection);
    float day = smoothstep(-0.25, 0.10, sun.y);   // 0 = deep night, 1 = day

    vec3 sky = BaseSky(dir, sun, day);

    // --- Sun disc with limb darkening. sin(theta) via the cross product is
    // stable where acos() is not (dot ~ 1). Extinction dims and reddens the
    // disc as it sits on the horizon. ---
    if (u_SunDiscIntensity > 0.0 && dot(dir, sun) > 0.0)
    {
        float sinT = length(cross(dir, sun));
        float r    = sinT / max(u_SunAngularRadius, 1e-5);
        if (r < 1.5)
        {
            float mu   = sqrt(max(1.0 - min(r, 1.0) * min(r, 1.0), 0.0));
            float limb = 1.0 - 0.6 * (1.0 - mu);                    // solar limb darkening
            float disc = smoothstep(1.0, 0.92, r) * limb;
            float ext  = smoothstep(-0.10, 0.25, sun.y);            // horizon extinction
            vec3  discCol = mix(vec3(1.0, 0.30, 0.08), vec3(1.0, 0.96, 0.90), ext);
            sky += discCol * disc * u_SunDiscIntensity * mix(0.15, 1.0, ext);
        }
    }

    float night = 1.0 - day;

    // --- Moon: disc whose visible hemisphere is lit by the REAL sun direction,
    // so the phase (new -> crescent -> full) falls out of the geometry. ---
    if (u_MoonIntensity > 0.0 && night > 0.01)
    {
        vec3  moon = normalize(u_MoonDirection);
        float cosM = dot(dir, moon);
        if (cosM > 0.0)
        {
            float sinT = length(cross(dir, moon));
            float r    = sinT / max(u_MoonAngularRadius, 1e-5);
            if (r < 1.0)
            {
                // Local disc frame -> visible-hemisphere surface normal.
                vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), moon));
                vec3 up2   = cross(moon, right);
                vec3 rel   = dir - moon * cosM;
                vec2 lc    = vec2(dot(rel, right), dot(rel, up2))
                           / max(u_MoonAngularRadius, 1e-5);        // [-1,1] on the disc
                float lz   = sqrt(max(1.0 - dot(lc, lc), 0.0));
                vec3  n    = normalize(right * lc.x + up2 * lc.y - moon * lz);

                float lit  = max(dot(n, sun), 0.0);                 // phase!
                // Procedural maria/craters.
                float craters = 0.72 + 0.28 * ValueNoise(lc * 5.7 + 13.1)
                              * (0.6 + 0.4 * ValueNoise(lc * 17.3));
                float edge = smoothstep(1.0, 0.94, r);
                vec3  moonCol = vec3(0.92, 0.94, 0.98) * craters;
                sky += moonCol * lit * edge * u_MoonIntensity * night;
                // Earthshine keeps the dark side barely readable.
                sky += moonCol * 0.012 * edge * u_MoonIntensity * night;
            }
            // Soft glow halo around the moon.
            sky += vec3(0.62, 0.71, 0.90) * pow(max(cosM, 0.0), 900.0)
                 * 0.6 * u_MoonIntensity * night;
        }
    }

    // --- Stars (night only, above the horizon): one candidate star per cube
    // cell; a per-cell hash gates existence, position, brightness and twinkle
    // phase. Milky-way band = distance from a great circle * noise. ---
    if (u_StarIntensity > 0.0 && night > 0.01 && dir.y > -0.05)
    {
        float face;
        vec2  fuv   = CubeFaceUV(dir, face);
        vec2  cells = fuv * max(u_StarDensity, 1.0);
        vec2  cell  = floor(cells);
        vec2  f     = fract(cells);
        vec2  seed  = cell + face * 61.0;

        float gate = Hash1(seed);
        if (gate > 0.70)
        {
            vec2  pos    = Hash2(seed + 3.7) * 0.8 + 0.1;           // keep off cell edges
            float d      = length(f - pos);
            float bright = pow(Hash1(seed + 9.1), 4.0);             // few bright, many dim
            float tw     = 0.75 + 0.25 * sin(u_Time * (1.0 + Hash1(seed + 5.3) * 4.0)
                                             + Hash1(seed + 7.7) * 6.28318);
            float star   = smoothstep(0.10, 0.0, d) * bright * tw;
            // Subtle color temperature spread (blue-white .. warm).
            vec3 starCol = mix(vec3(1.0, 0.85, 0.7), vec3(0.75, 0.85, 1.0), Hash1(seed + 11.3));
            sky += starCol * star * u_StarIntensity * night * smoothstep(-0.05, 0.15, dir.y);
        }

        if (u_MilkyWayIntensity > 0.0)
        {
            float band  = abs(dot(dir, normalize(u_MilkyWayDir)));  // 0 on the great circle
            float core  = exp(-band * band * 28.0);
            float cloud = ValueNoise(fuv * 9.0 + face * 23.0)
                        * ValueNoise(fuv * 23.0 - face * 7.0);
            sky += vec3(0.55, 0.60, 0.75) * core * (0.35 + 0.65 * cloud)
                 * u_MilkyWayIntensity * night * smoothstep(-0.05, 0.2, dir.y);
        }
    }

    color      = vec4(max(sky, vec3(0.0)) * u_SkyIntensity, 1.0);
    o_EntityID = -1;
}
