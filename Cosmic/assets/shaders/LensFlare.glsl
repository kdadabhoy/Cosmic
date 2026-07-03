#type vertex
#version 450 core

// LensFlare — Phase 11 (S7 upgrade / doc 10 F7): procedural screen-space lens
// flare, drawn ADDITIVELY (SetBlendMode(Additive), depth off) over the
// tonemapped LDR image as a fullscreen pass — ghosts along the sun-through-
// center axis, a halo ring, and an anamorphic streak, all analytic (no flare
// textures). Occlusion is resolved in-shader: a handful of scene-depth taps
// around the sun's screen position fade the whole effect as the sun goes
// behind geometry.
//
// Standard post-shader contract: fragment output `color`, varying `v_TexCoord`.

out vec2 v_TexCoord;

void main()
{
    vec2 uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    v_TexCoord = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;

in vec2 v_TexCoord;

uniform sampler2D u_Depth;        // scene depth (occlusion taps)
uniform vec2      u_SunScreenPos; // sun position in [0,1] screen UV
uniform float     u_SunInFront;   // 1 = sun is in front of the camera (else skip)
uniform float     u_Intensity;    // overall strength (F7 default ~0.35)
uniform vec3      u_Tint;         // flare tint (usually the sun color)
uniform float     u_Aspect;       // viewport width / height (round ghosts)

float Ghost(vec2 uv, vec2 center, float radius)
{
    vec2 d = (uv - center) * vec2(u_Aspect, 1.0);
    return pow(clamp(1.0 - length(d) / radius, 0.0, 1.0), 2.5);
}

void main()
{
    if (u_SunInFront < 0.5 || u_Intensity <= 0.0)
    {
        color = vec4(0.0);
        return;
    }

    // Occlusion: fraction of taps around the sun that see sky (far plane).
    float vis = 0.0;
    const vec2 taps[5] = vec2[5](vec2(0.0), vec2( 0.004,  0.004), vec2(-0.004,  0.004),
                                 vec2( 0.004, -0.004), vec2(-0.004, -0.004));
    for (int i = 0; i < 5; ++i)
    {
        vec2 suv = clamp(u_SunScreenPos + taps[i], vec2(0.001), vec2(0.999));
        vis += (texture(u_Depth, suv).r >= 0.9999) ? 1.0 : 0.0;
    }
    vis /= 5.0;
    // Fade near the screen edge so the flare doesn't pop when the sun leaves.
    vec2 edge = abs(u_SunScreenPos - 0.5) * 2.0;
    vis *= clamp(1.2 - max(edge.x, edge.y), 0.0, 1.0);

    if (vis <= 0.0)
    {
        color = vec4(0.0);
        return;
    }

    vec2 uv     = v_TexCoord;
    vec2 center = vec2(0.5);
    vec2 axis   = center - u_SunScreenPos;    // sun -> screen center

    vec3 flare = vec3(0.0);

    // Ghosts: reflections marching along the axis with alternating hue.
    const float ghostPos [6] = float[6](-0.4, 0.25, 0.55, 0.9, 1.35, 1.8);
    const float ghostSize[6] = float[6](0.055, 0.03, 0.07, 0.045, 0.10, 0.06);
    for (int i = 0; i < 6; ++i)
    {
        vec2  gp   = u_SunScreenPos + axis * (1.0 + ghostPos[i]);
        float g    = Ghost(uv, gp, ghostSize[i]);
        vec3  hue  = mix(vec3(0.9, 0.6, 0.35), vec3(0.35, 0.6, 0.9),
                         fract(float(i) * 0.37 + 0.2));
        flare += hue * g * 0.12;
    }

    // Halo: a thin ring at a fixed radius around the screen center.
    {
        vec2  d    = (uv - center) * vec2(u_Aspect, 1.0);
        float ring = 1.0 - smoothstep(0.0, 0.045, abs(length(d) - 0.42));
        // Brightest on the side opposite the sun (like a real coating halo).
        float side = clamp(dot(normalize(d + 1e-5), normalize(axis + 1e-5)) * 0.5 + 0.5, 0.0, 1.0);
        flare += vec3(0.45, 0.55, 0.9) * ring * side * 0.10;
    }

    // Anamorphic streak: a thin horizontal blue line through the sun.
    {
        float dy = abs(uv.y - u_SunScreenPos.y);
        float dx = abs(uv.x - u_SunScreenPos.x);
        float streak = exp(-dy * dy * 6000.0) * exp(-dx * dx * 18.0);
        flare += vec3(0.35, 0.5, 1.0) * streak * 0.5;
    }

    // Bright core glow right at the sun.
    flare += u_Tint * Ghost(uv, u_SunScreenPos, 0.16) * 0.35;

    color = vec4(flare * u_Tint * u_Intensity * vis, 1.0);
}
