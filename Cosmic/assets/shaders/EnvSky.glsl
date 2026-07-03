#type vertex
#version 450 core

// EnvSky — renders a procedural analytic sky into a cubemap face (S6.3 / S7.1).
// The same sky is the IBL environment source and (via the baked cube) the
// skybox, so lighting and background always agree. Driven by a sun DIRECTION
// (vector TO the sun); irradiance/prefilter convolve the result.
//
// Cube-render mode: u_ViewProjection is the per-face view-projection; the
// fragment turns the interpolated local cube position into a world direction.

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

    return max(sky, vec3(0.0)) * u_SkyIntensity;
}

void main()
{
    vec3 dir = normalize(v_LocalPos);
    color = vec4(ProceduralSky(dir, normalize(u_SunDirection)), 1.0);
}
