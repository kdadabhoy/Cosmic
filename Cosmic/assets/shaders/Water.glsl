#type vertex
#version 450 core

// Water — S9.1 Tier 1 surface. The vertex stage displaces a flat grid by the
// SAME Gerstner sum the CPU queries evaluate (water/GerstnerWave.h) — the two
// must stay formula-identical so floating objects sit on the rendered surface.
// Wave constants (k, omega, effective steepness) are precomputed CPU-side:
//   u_WaveDirKA[i] = (dir.x, dir.y, k, amplitude)
//   u_WaveQOP[i]   = (qEffective, omega, phase, 0)

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
uniform vec4 u_WaveDirKA[4];
uniform vec4 u_WaveQOP[4];

out vec3 v_WorldPos;
out vec3 v_GerstnerNormal;
out vec4 v_ClipPos;

void main()
{
    vec2 p = u_Center + a_Position.xz * u_Extent;

    vec3  offset = vec3(0.0);
    vec2  nxz    = vec2(0.0);
    float ny     = 1.0;

    for (int i = 0; i < u_WaveCount && i < 4; ++i)
    {
        vec2  d   = u_WaveDirKA[i].xy;
        float k   = u_WaveDirKA[i].z;
        float A   = u_WaveDirKA[i].w;
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
    }

    v_WorldPos       = vec3(p.x + offset.x, u_SurfaceHeight + offset.y, p.y + offset.z);
    v_GerstnerNormal = normalize(vec3(-nxz.x, max(ny, 1e-3), -nxz.y));
    v_ClipPos        = u_Camera.ViewProjection * vec4(v_WorldPos, 1.0);
    gl_Position      = v_ClipPos;
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;
layout(location = 1) out int  o_EntityID;

in vec3 v_WorldPos;
in vec3 v_GerstnerNormal;
in vec4 v_ClipPos;

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
    vec3  refracted  = mix(refraction, waterTint, absorb * 0.85);

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

    // --- Shoreline foam: depth delta + scrolling noise (detail map red) ---
    float foamMask  = 1.0 - smoothstep(0.0, max(u_FoamDepth, 1e-3), vertDepth);
    float foamNoise = texture(u_DetailA, v_WorldPos.xz * 0.35 + u_Time * 0.03).r;
    vec3  foam      = vec3(1.0) * foamMask * (0.45 + 0.55 * smoothstep(0.35, 0.75, foamNoise));

    vec3 surface = mix(refracted, reflected, fresnel) + glint + foam;

    color      = vec4(surface, 1.0);
    o_EntityID = u_EntityID;
}
