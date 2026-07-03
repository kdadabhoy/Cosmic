#type vertex
#version 450 core

// Water — S9.1 Tier 1 surface, upgraded to v2 in Phase 11 (S11 / doc 10).
// The vertex stage displaces a flat grid by the SAME Gerstner sum the CPU
// queries evaluate (water/GerstnerWave.h) — the two must stay formula-identical
// so floating objects sit on the rendered surface. Wave constants (k, omega,
// effective steepness) are precomputed CPU-side:
//   u_WaveDirKA[i] = (dir.x, dir.y, k, amplitude)
//   u_WaveQOP[i]   = (qEffective, omega, phase, 0)
//
// v2 additions (all default-off — every new uniform's GL default of 0 makes
// this file render byte-identically to the shipped S9.1 tier until the F6
// work order feeds it):
//   - 8 waves (was 4). The CPU query path must use the same resolved set.
//   - SHORE AWARENESS: an optional packed terrain height texture (the S8
//     hi/lo-byte format) lets waves flatten in shallow water and the fragment
//     stage add breaker foam. texelFetch only — the hi/lo byte packing does
//     not survive bilinear filtering.
//   - v_CrestNorm: normalized crest height for whitecaps/breakers.

layout(location = 0) in vec3 a_Position;   // grid in [-0.5, 0.5]^2 on XZ

layout(std140, binding = 1) uniform CameraBlock
{
    mat4 ViewProjection;
    vec4 CameraPosition;
} u_Camera;

uniform vec2  u_Center;
uniform vec2  u_Extent;
uniform float u_SurfaceHeight;
uniform float u_Time;

uniform int  u_WaveCount;
uniform vec4 u_WaveDirKA[8];
uniform vec4 u_WaveQOP[8];

// --- v2: shore-aware wave attenuation (0 = off) ---
uniform sampler2D u_ShoreHeightTex;   // S8 packed height texture (R hi, G lo byte)
uniform float     u_HasShoreTex;      // 0 = no shore data (default; legacy path)
uniform vec4      u_ShoreRect;        // xy = terrain world min corner, zw = 1 / world size
uniform vec2      u_ShoreHeight;      // x = height scale, y = base height (world Y of sample 0)
uniform float     u_ShoreDepthRange;  // water depth (m) over which waves regain full amplitude

out vec3  v_WorldPos;
out vec3  v_GerstnerNormal;
out vec4  v_ClipPos;
out float v_ShoreFactor;              // 1 = deep water (legacy), -> 0 at the shoreline
out float v_CrestNorm;                // vertical offset / total amplitude, ~[-1, 1]

void main()
{
    vec2 p = u_Center + a_Position.xz * u_Extent;

    // Shore factor: how deep the calm-water column is under this grid point.
    float shore = 1.0;
    if (u_HasShoreTex > 0.5)
    {
        vec2  uv01 = clamp((p - u_ShoreRect.xy) * u_ShoreRect.zw, 0.0, 1.0);
        ivec2 ts   = textureSize(u_ShoreHeightTex, 0);
        ivec2 tx   = ivec2(uv01 * vec2(ts - 1) + 0.5);
        vec4  hs   = texelFetch(u_ShoreHeightTex, tx, 0);
        float terrainY = u_ShoreHeight.y
                       + ((hs.r * 65280.0 + hs.g * 255.0) / 65535.0) * u_ShoreHeight.x;
        float depth = u_SurfaceHeight - terrainY;
        shore = smoothstep(0.0, 1.0, clamp(depth / max(u_ShoreDepthRange, 1e-3), 0.0, 1.0));
    }

    vec3  offset = vec3(0.0);
    vec2  nxz    = vec2(0.0);
    float ny     = 1.0;
    float ampSum = 0.0;

    for (int i = 0; i < u_WaveCount && i < 8; ++i)
    {
        vec2  d   = u_WaveDirKA[i].xy;
        float k   = u_WaveDirKA[i].z;
        float A   = u_WaveDirKA[i].w * shore;   // waves flatten in the shallows
        float q   = u_WaveQOP[i].x;
        float om  = u_WaveQOP[i].y;
        float ph  = u_WaveQOP[i].z;

        float phi = k * dot(d, p) - om * u_Time + ph;
        float c   = cos(phi);
        float s   = sin(phi);

        offset.xz += q * A * d * c;
        offset.y  += A * s;
        nxz       += d * k * A * c;
        ny        -= q * k * A * s;
        ampSum    += A;
    }

    v_WorldPos       = vec3(p.x + offset.x, u_SurfaceHeight + offset.y, p.y + offset.z);
    v_GerstnerNormal = normalize(vec3(-nxz.x, max(ny, 1e-3), -nxz.y));
    v_ClipPos        = u_Camera.ViewProjection * vec4(v_WorldPos, 1.0);
    v_ShoreFactor    = shore;
    v_CrestNorm      = offset.y / max(ampSum, 1e-4);
    gl_Position      = v_ClipPos;
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;
layout(location = 1) out int  o_EntityID;

in vec3  v_WorldPos;
in vec3  v_GerstnerNormal;
in vec4  v_ClipPos;
in float v_ShoreFactor;
in float v_CrestNorm;

layout(std140, binding = 1) uniform CameraBlock
{
    mat4 ViewProjection;
    vec4 CameraPosition;
} u_Camera;

layout(std140, binding = 0) uniform LightsBlock
{
    vec4 u_SunDirection_Ambient;
    vec4 u_SunColor_Intensity;
    vec4 u_PointCount;
    vec4 u_PointPos_Radius[16];
    vec4 u_PointColor_Intensity[16];
};

// Screen-space inputs (grabbed/attached by Water::Render).
uniform sampler2D u_Refraction;     // scene color BEFORE the water draw
uniform sampler2D u_SceneDepth;     // the bound HDR target's depth
uniform sampler2D u_DetailA;        // scrolling tangent-space detail normals
uniform sampler2D u_DetailB;
uniform sampler2D u_Reflection;     // planar reflection (when captured)
uniform float     u_HasReflection;
uniform mat4      u_ReflectionViewProj;
uniform mat4      u_InvViewProj;

uniform float u_Time;
uniform vec3  u_ShallowColor;
uniform vec3  u_DeepColor;
uniform float u_DepthFadeDistance;
uniform float u_FoamDepth;
uniform float u_RefractionStrength;
uniform float u_ReflectionStrength;
uniform float u_DetailTiling;
uniform float u_DetailSpeed;
uniform float u_DetailStrength;
uniform float u_SpecularPower;
uniform int   u_EntityID;

// --- v2 optics (all default 0 = off; F6 feeds them) ---
uniform float u_CausticStrength;    // animated light webs on the seen floor
uniform float u_CausticScale;       // world repeats per meter (F6 default ~0.15)
uniform float u_SparkleStrength;    // micro-glint twinkle on top of the sun glint
uniform float u_WhitecapStrength;   // crest whitecaps (storm seas)

// Scene set (Renderer3D::ApplySceneBindings): shadowed sun + IBL fallback.
uniform samplerCube u_IrradianceMap;
uniform samplerCube u_PrefilterMap;
uniform sampler2D   u_BrdfLut;
uniform float       u_HasIBL;
uniform float       u_PrefilterMaxLod;

uniform sampler2D u_ShadowMap;
uniform mat4      u_LightViewProj;
uniform float     u_HasShadow;
uniform float     u_ShadowBias;

float ShadowFactor(vec3 worldPos, vec3 N, vec3 L)
{
    vec4 lp   = u_LightViewProj * vec4(worldPos, 1.0);
    vec3 proj = lp.xyz / lp.w * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 0.0;
    float bias  = max(u_ShadowBias * (1.0 - dot(N, L)), u_ShadowBias * 0.2);
    vec2  texel = 1.0 / vec2(textureSize(u_ShadowMap, 0));
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            shadow += (proj.z - bias > texture(u_ShadowMap, proj.xy + vec2(x, y) * texel).r) ? 1.0 : 0.0;
    return shadow / 9.0;
}

vec3 WorldFromDepth(vec2 uv, float depth)
{
    vec4 clip  = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = u_InvViewProj * clip;
    return world.xyz / world.w;
}

void main()
{
    vec2 screenUV = v_ClipPos.xy / v_ClipPos.w * 0.5 + 0.5;

    // --- Normal: Gerstner base + two counter-scrolling detail maps. The water
    // plane's tangent frame is world-aligned (T = +X, B = +Z), so the detail
    // xy perturbs world xz directly. ---
    vec2 uvA = v_WorldPos.xz * u_DetailTiling + u_Time * u_DetailSpeed * vec2( 1.0,  0.6);
    vec2 uvB = v_WorldPos.xz * u_DetailTiling * 0.71 - u_Time * u_DetailSpeed * vec2( 0.8, -1.0);
    vec2 detail = (texture(u_DetailA, uvA).xy * 2.0 - 1.0)
                + (texture(u_DetailB, uvB).xy * 2.0 - 1.0);
    vec3 N = normalize(vec3(v_GerstnerNormal.x + detail.x * u_DetailStrength,
                            v_GerstnerNormal.y,
                            v_GerstnerNormal.z + detail.y * u_DetailStrength));

    vec3 V = normalize(u_Camera.CameraPosition.xyz - v_WorldPos);

    // --- Depth-based absorption + foam inputs (S9.1: soft shorelines) ---
    float sceneD     = texture(u_SceneDepth, screenUV).r;
    vec3  sceneWorld = WorldFromDepth(screenUV, sceneD);
    float viewDepth  = (sceneD < 0.9999)
        ? max(length(sceneWorld - u_Camera.CameraPosition.xyz) - length(v_WorldPos - u_Camera.CameraPosition.xyz), 0.0)
        : u_DepthFadeDistance * 20.0;                       // sky behind: fully deep
    float vertDepth  = (sceneD < 0.9999) ? max(v_WorldPos.y - sceneWorld.y, 0.0) : u_FoamDepth * 20.0;

    float absorb   = 1.0 - exp(-viewDepth / max(u_DepthFadeDistance, 1e-3));
    vec3  waterTint = mix(u_ShallowColor, u_DeepColor, absorb);

    // --- Refraction: distorted scene-color grab, faded out at the shoreline so
    // above-water pixels don't smear into the edge (tier-1 leak guard). ---
    float distortScale = clamp(vertDepth / max(u_FoamDepth, 1e-3), 0.0, 1.0);
    vec2  refrUV = clamp(screenUV + N.xz * u_RefractionStrength * distortScale, vec2(0.001), vec2(0.999));
    vec3  refraction = texture(u_Refraction, refrUV).rgb;

    // --- v2 caustics: animated light webs on the seen floor, faded with water
    // depth so they live in the shallows. Pattern = coincidence of the two
    // scrolling detail fields (cheap cellular-web look, no extra textures). ---
    if (u_CausticStrength > 0.0 && sceneD < 0.9999)
    {
        vec2  cuv   = sceneWorld.xz * u_CausticScale;
        float n1    = dot(texture(u_DetailA, cuv + u_Time * 0.017).rg, vec2(0.5));
        float n2    = dot(texture(u_DetailB, cuv * 1.37 - u_Time * 0.023).rg, vec2(0.5));
        float caust = pow(clamp(1.0 - abs(n1 - n2) * 2.2, 0.0, 1.0), 6.0);
        float fade  = exp(-viewDepth / max(u_DepthFadeDistance, 1e-3))
                    * clamp(vertDepth / max(u_FoamDepth, 1e-3), 0.0, 1.0);   // not on dry edges
        refraction += u_SunColor_Intensity.rgb * u_SunColor_Intensity.w
                    * caust * fade * u_CausticStrength;
    }

    vec3 refracted = mix(refraction, waterTint, absorb * 0.85);

    // --- Reflection: planar capture when available, IBL sky fallback else ---
    vec3 reflected;
    if (u_HasReflection > 0.5)
    {
        vec4 rp = u_ReflectionViewProj * vec4(v_WorldPos, 1.0);
        vec2 reflUV = clamp(rp.xy / rp.w * 0.5 + 0.5 + N.xz * u_ReflectionStrength, vec2(0.001), vec2(0.999));
        reflected = texture(u_Reflection, reflUV).rgb;
    }
    else if (u_HasIBL > 0.5)
    {
        reflected = textureLod(u_PrefilterMap, reflect(-V, N), 1.0).rgb;
    }
    else
    {
        reflected = waterTint * 1.3;
    }

    // --- Fresnel blend + shadowed sun glint ---
    float fresnel = 0.02 + 0.98 * pow(clamp(1.0 - max(dot(N, V), 0.0), 0.0, 1.0), 5.0);
    vec3  L       = normalize(-u_SunDirection_Ambient.xyz);
    float shadow  = (u_HasShadow > 0.5) ? ShadowFactor(v_WorldPos, vec3(0.0, 1.0, 0.0), L) : 0.0;
    vec3  H       = normalize(L + V);
    vec3  glint   = u_SunColor_Intensity.rgb * u_SunColor_Intensity.w
                  * pow(max(dot(N, H), 0.0), u_SpecularPower) * (1.0 - shadow);

    // --- v2 sparkle: per-cell twinkling micro-glints riding the sun glint ---
    if (u_SparkleStrength > 0.0)
    {
        vec2  cell    = floor(v_WorldPos.xz * 6.0);
        float h       = fract(sin(dot(cell, vec2(127.1, 311.7))) * 43758.5453);
        float phase   = fract(h + u_Time * mix(0.2, 1.0, fract(h * 7.0)));
        float twinkle = smoothstep(0.75, 1.0, sin(phase * 6.28318) * 0.5 + 0.5);
        glint += u_SunColor_Intensity.rgb * u_SunColor_Intensity.w
               * pow(max(dot(N, H), 0.0), u_SpecularPower * 4.0)
               * twinkle * u_SparkleStrength * (1.0 - shadow);
    }

    // --- Shoreline foam: depth delta + scrolling noise (detail map red) ---
    float foamMask  = 1.0 - smoothstep(0.0, max(u_FoamDepth, 1e-3), vertDepth);
    // v2 breakers: waves entering the shallows foam at their crests. Inert when
    // no shore texture is bound (v_ShoreFactor == 1).
    foamMask = max(foamMask, (1.0 - v_ShoreFactor) * smoothstep(0.15, 0.75, max(v_CrestNorm, 0.0)));
    // v2 whitecaps: open-water crest foam for heavy seas (default 0).
    float whitecap  = u_WhitecapStrength * smoothstep(0.55, 0.92, max(v_CrestNorm, 0.0));
    foamMask = clamp(foamMask + whitecap, 0.0, 1.0);

    float foamNoise = texture(u_DetailA, v_WorldPos.xz * 0.35 + u_Time * 0.03).r;
    vec3  foam      = vec3(1.0) * foamMask * (0.45 + 0.55 * smoothstep(0.35, 0.75, foamNoise));

    vec3 surface = mix(refracted, reflected, fresnel) + glint + foam;

    color      = vec4(surface, 1.0);
    o_EntityID = u_EntityID;
}
