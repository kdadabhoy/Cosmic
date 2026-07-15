#type vertex
#version 450 core

// Tonemap — the S6.1 HDR resolve pass. A single fullscreen triangle generated
// from gl_VertexID (0,1,2) — no vertex buffer or attributes are bound; the
// engine issues it via RenderCommand::DrawArrays(Triangles, 0, 3) over its
// private empty VAO. The triangle over-covers the screen (NDC corners
// (-1,-1),(3,-1),(-1,3)); v_TexCoord runs 0..1 across the visible region.

out vec2 v_TexCoord;

void main()
{
    vec2 uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);   // (0,0),(2,0),(0,2)
    v_TexCoord = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}

#type fragment
#version 450 core

// Resolve the linear HDR scene (RGBA16F) to the LDR target: exposure scale,
// ACES filmic tonemap (Narkowicz approximation), then linear->sRGB gamma.
// This is where overbright (>1.0) values roll off on the ACES shoulder instead
// of hard-clipping to flat white. UI/2D composites AFTER this pass (contract 7).
//
// NOTE (documented, S12.6): the engine's authored colors are not yet converted
// sRGB->linear on input, so the final gamma here makes HDR-on look brighter/more
// filmic than HDR-off. That A/B difference is expected for the foundation; a full
// sRGB-correctness audit rides S12.6.
//
// PREPROCESSOR CONTRACT (OpenGLShader::PreProcess): the fragment output MUST be
// named `color` and the varying `v_TexCoord` — the engine preamble injector
// pattern-matches those exact strings and injects its own declarations when
// absent. A differently-named location-0 output used to collide with the
// injected one (duplicate location 0 = compile error). Every post shader in the
// S6 stack follows this convention.

layout(location = 0) out vec4 color;

in vec2 v_TexCoord;

uniform sampler2D u_Scene;      // HDR scene color (slot 0)
uniform float     u_Exposure;   // linear exposure multiplier (1.0 = neutral)
uniform float     u_Gamma;      // output gamma (X2) — 2.2 = the shipped sRGB curve

// Vignette (Phase 25 / Q5) — post-tonemap edge darkening toward u_VignetteColor.
// u_VignetteAmount 0 (the GL/engine default) skips the block entirely, so the
// shipped output is byte-identical. Applied on the final LDR image.
uniform float u_VignetteAmount;   // 0 = off
uniform float u_VignetteRadius;   // normalized distance where darkening reaches full
uniform float u_VignetteFeather;  // falloff softness
uniform vec3  u_VignetteColor;    // edge color (usually black)

// SSAO (S6.5) — modulates the scene before tonemapping. Applied to the whole
// image (a documented simplification of "ambient only", which needs a depth
// prepass / forward AO fetch). u_UseAO gates it.
uniform sampler2D u_AO;
uniform float     u_UseAO;

// Bloom (S6.6) — additive HDR glow combined in before the ACES curve.
uniform sampler2D u_Bloom;
uniform float     u_UseBloom;
uniform float     u_BloomIntensity;

// Sun shafts (S10.3) — raymarched god rays, additive like bloom.
uniform sampler2D u_Shafts;
uniform float     u_UseShafts;

// Heat-haze (S10.5) — an RG offset field (distortion particles) that displaces
// every scene-space fetch below, so haze bends fog/AO/bloom consistently.
uniform sampler2D u_Distort;
uniform float     u_UseDistort;
uniform float     u_DistortStrength;

// Height fog + aerial perspective (S7.2) — depth-based world-space inscatter,
// reconstructed from the scene depth. Applied before AO/bloom so distant geometry
// fades into the sky. The far plane (sky) is skipped so the horizon reads clean.
uniform sampler2D u_Depth;
uniform float     u_UseFog;
uniform vec3      u_FogColor;
uniform float     u_FogDensity;
uniform float     u_FogHeightFalloff;
uniform float     u_FogBaseHeight;
uniform mat4      u_InvViewProj;
uniform vec3      u_CameraPos;

// Underwater medium (Phase 11 / S9.4-lite, doc 10) — when the camera is below
// a liquid plane, absorb toward the medium color with distance and tint the
// image. Gated by u_UseUnderwater (GL default 0 = the shipped output); the F6
// work order drives it via PostProcessStack::SetUnderwater.
uniform float u_UseUnderwater;
uniform float u_WaterlineY;         // world Y of the liquid surface
uniform vec3  u_UnderwaterColor;    // distance-fog color near the surface (shallow)
uniform float u_UnderwaterDensity;  // 1/m absorption at the surface
uniform vec3  u_UnderwaterTint;     // spectral multiplier (e.g. 0.55, 0.75, 0.90)
// Subnautica-style depth grading + seafloor caustics (Phase 11 Layer 2, doc water notes).
uniform vec3  u_DeepWaterColor;     // fog color once the camera is UnderwaterDepthRef deep
uniform float u_UnderwaterDepthRef; // camera depth (m) over which fog reaches the deep color
uniform float u_CausticStrength;    // animated light webs on submerged geometry (0 = off)
uniform float u_CausticScale;       // caustic world repeats per meter
uniform float u_Time;               // seconds (caustic animation)

// Krzysztof Narkowicz's ACES filmic curve fit.
vec3 ACESFilmic(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Cheap self-contained caustic (no texture): the coincidence of two scrolling
// value-noise fields reads as animated cellular light webs on the seafloor.
float uwHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float uwNoise(vec2 p)
{
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(uwHash(i),              uwHash(i + vec2(1, 0)), u.x),
               mix(uwHash(i + vec2(0, 1)), uwHash(i + vec2(1, 1)), u.x), u.y);
}
float uwCaustic(vec2 p, float t)
{
    float n1 = uwNoise(p + vec2(t * 0.13, t * 0.11));
    float n2 = uwNoise(p * 1.37 + vec2(-t * 0.09, t * 0.15));
    float c  = 1.0 - abs(n1 - n2) * 2.0;
    return pow(clamp(c, 0.0, 1.0), 8.0);
}

void main()
{
    // Heat-haze (S10.5): one displaced coordinate feeds every scene-space
    // sample so the distortion stays coherent across fog, AO, bloom and shafts.
    vec2 uv = v_TexCoord;
    if (u_UseDistort > 0.5)
        uv += texture(u_Distort, v_TexCoord).xy * u_DistortStrength;

    vec3 hdr = texture(u_Scene, uv).rgb;

    if (u_UseUnderwater > 0.5)                         // underwater medium (Phase 11 Layer 2)
    {
        // Ramp across the surface so crossing the waterline isn't a hard pop.
        float uw = smoothstep(u_WaterlineY + 0.6, u_WaterlineY - 0.6, u_CameraPos.y);
        if (uw > 0.001)
        {
            float d     = texture(u_Depth, uv).r;
            bool  isGeo = d < 0.9999;
            vec3  world = u_CameraPos;
            float dist  = 200.0;                        // sky/far: fully fogged medium
            if (isGeo)
            {
                vec4 clip = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
                vec4 wp   = u_InvViewProj * clip;
                world     = wp.xyz / wp.w;
                dist      = length(world - u_CameraPos);
            }

            // Seafloor caustics: animated webs on submerged geometry, faded with the
            // lit point's depth and with view distance (they live in the shallows).
            if (u_CausticStrength > 0.0 && isGeo && world.y < u_WaterlineY)
            {
                float caust      = uwCaustic(world.xz * u_CausticScale, u_Time);
                float depthUnder = u_WaterlineY - world.y;
                float cfade      = exp(-depthUnder * 0.03) * exp(-dist * u_UnderwaterDensity * 0.6);
                hdr += hdr * caust * u_CausticStrength * cfade * uw;
            }

            // Depth-graded fog: denser + bluer the deeper the CAMERA descends.
            float depthBelow = max(u_WaterlineY - u_CameraPos.y, 0.0);
            float grade      = clamp(depthBelow / max(u_UnderwaterDepthRef, 1e-3), 0.0, 1.0);
            float dens       = u_UnderwaterDensity * (1.0 + 2.0 * grade);
            vec3  fogCol     = mix(u_UnderwaterColor, u_DeepWaterColor, grade);
            float f          = 1.0 - exp(-dist * max(dens, 1e-4));
            vec3  fogged     = mix(hdr * u_UnderwaterTint, fogCol, clamp(f, 0.0, 1.0));
            hdr              = mix(hdr, fogged, uw);
        }
    }

    if (u_UseFog > 0.5)                                // height fog (S7.2)
    {
        float d = texture(u_Depth, uv).r;
        if (d < 0.9999)                               // skip the sky / far plane
        {
            vec4 clip  = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
            vec4 world = u_InvViewProj * clip;
            world /= world.w;

            float dist   = length(world.xyz - u_CameraPos);
            float height = exp(-u_FogHeightFalloff * max(world.y - u_FogBaseHeight, 0.0));
            float f      = 1.0 - exp(-dist * u_FogDensity * height);
            hdr = mix(hdr, u_FogColor, clamp(f, 0.0, 1.0));
        }
    }

    if (u_UseAO > 0.5)
        hdr *= texture(u_AO, uv).r;                   // contact darkening (S6.5)
    if (u_UseBloom > 0.5)
        hdr += texture(u_Bloom, uv).rgb * u_BloomIntensity;   // additive glow (S6.6)
    if (u_UseShafts > 0.5)                            // god rays (S10.3)
    {
        vec3 shaft = texture(u_Shafts, uv).rgb;
        // Underwater the shafts read as light through the water medium, not white sun.
        if (u_UseUnderwater > 0.5 && u_CameraPos.y < u_WaterlineY)
            shaft *= u_UnderwaterTint;
        hdr += shaft;
    }

    vec3 mapped = ACESFilmic(hdr * u_Exposure);
    mapped      = pow(mapped, vec3(1.0 / max(u_Gamma, 0.01)));   // linear -> sRGB (X2: u_Gamma, default 2.2)

    if (u_VignetteAmount > 0.0)                        // Q5 vignette (post-tonemap)
    {
        float d     = distance(v_TexCoord, vec2(0.5)) * 1.41421356;   // 0 center → ~1 corner
        float inner = u_VignetteRadius - max(u_VignetteFeather, 1e-3);
        float v     = 1.0 - smoothstep(inner, u_VignetteRadius, d);    // 1 center → 0 edge
        float f     = mix(1.0, v, clamp(u_VignetteAmount, 0.0, 1.0));
        mapped      = mix(u_VignetteColor, mapped, f);
    }

    color = vec4(mapped, 1.0);
}
