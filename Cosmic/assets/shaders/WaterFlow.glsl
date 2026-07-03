#type vertex
#version 450 core

// WaterFlow — Phase 11 (S9 adjunct / doc 10 F6): a generic FLOWING water sheet
// for rivers, waterfalls, spillways — anywhere the S9 planar Water (Y-constant
// plane) cannot go. The mesh's UVs are authored so +U runs along the flow
// (arclength) and V spans it [0,1]; the fragment stage scrolls procedural
// normal perturbation and foam downstream. Translucent (engine-default alpha
// blend); draw AFTER opaque geometry with depth WRITES off, depth test on —
// same contract as the S9 water surface. Lit by the LightsBlock sun + the
// ApplySceneBindings IBL set (reflection fallback), so it sits in any scene.
//
// No refraction grab — sheets are thin; the u_Opacity alpha over the already-
// drawn scene reads as transmission at a fraction of the cost (documented
// tier choice; the S9 Water keeps the true refraction).

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in vec4 a_Tangent;    // TBN for the flow perturbation

layout(std140, binding = 1) uniform CameraBlock
{
    mat4 ViewProjection;
    vec4 CameraPosition;
} u_Camera;

uniform mat4 u_Model;
uniform mat3 u_NormalMatrix;

out vec3  v_WorldPos;
out vec3  v_WorldNormal;
out vec3  v_WorldTangent;
out float v_TangentW;
out vec2  v_TexCoord;

void main()
{
    vec4 world     = u_Model * vec4(a_Position, 1.0);
    v_WorldPos     = world.xyz;
    v_WorldNormal  = u_NormalMatrix * a_Normal;
    v_WorldTangent = u_NormalMatrix * a_Tangent.xyz;
    v_TangentW     = a_Tangent.w;
    v_TexCoord     = a_TexCoord;
    gl_Position    = u_Camera.ViewProjection * world;
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;
layout(location = 1) out int  o_EntityID;

in vec3  v_WorldPos;
in vec3  v_WorldNormal;
in vec3  v_WorldTangent;
in float v_TangentW;
in vec2  v_TexCoord;

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

uniform float u_Time;
uniform int   u_EntityID;

// Material parameters (suggested river / waterfall values in parens).
uniform float u_FlowSpeed;      // UV/s downstream (river 0.25 / falls 1.6)
uniform vec2  u_Tiling;         // noise repeats along U, V (4.0, 2.0)
uniform vec3  u_TintColor;      // water tint, linear (0.10, 0.30, 0.35)
uniform float u_Opacity;        // base alpha (river 0.55 / falls 0.8)
uniform float u_FoamStrength;   // aeration foam (river 0.35 / falls 1.0)
uniform float u_NormalStrength; // surface perturbation (0.5)
uniform float u_SpecularPower;  // sun glint tightness (180)
uniform float u_StreakStretch;  // U:V anisotropy of the streak noise (6.0)

// Scene set (Renderer3D::ApplySceneBindings) — IBL fallback reflections.
uniform samplerCube u_IrradianceMap;
uniform samplerCube u_PrefilterMap;
uniform sampler2D   u_BrdfLut;
uniform float       u_HasIBL;
uniform float       u_PrefilterMaxLod;

uniform sampler2D u_ShadowMap;      // declared for unit-type consistency
uniform mat4      u_LightViewProj;  // (sheets don't self-shadow at this tier)
uniform float     u_HasShadow;
uniform float     u_ShadowBias;

float WHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float WNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(WHash(i),              WHash(i + vec2(1, 0)), u.x),
               mix(WHash(i + vec2(0, 1)), WHash(i + vec2(1, 1)), u.x), u.y);
}

void main()
{
    vec2 uv = v_TexCoord;

    // --- Downstream-scrolled surface perturbation in tangent space ---
    vec2  s1 = vec2(uv.x * u_Tiling.x       - u_Time * u_FlowSpeed,       uv.y * u_Tiling.y);
    vec2  s2 = vec2(uv.x * u_Tiling.x * 1.7 - u_Time * u_FlowSpeed * 1.4, uv.y * u_Tiling.y * 1.9 + 3.7);
    float px = (WNoise(s1) - 0.5) + (WNoise(s2 + 11.3) - 0.5);
    float py = (WNoise(s1 + 5.1) - 0.5) + (WNoise(s2) - 0.5);

    vec3 Ng = normalize(v_WorldNormal);
    vec3 T  = normalize(v_WorldTangent - Ng * dot(Ng, v_WorldTangent));
    vec3 B  = cross(Ng, T) * v_TangentW;
    vec3 N  = normalize(mat3(T, B, Ng) * normalize(vec3(px * u_NormalStrength,
                                                        py * u_NormalStrength, 1.0)));

    vec3 V = normalize(u_Camera.CameraPosition.xyz - v_WorldPos);

    // --- Streaks: anisotropic noise elongated along the flow ---
    float streak = WNoise(vec2(uv.x * u_Tiling.x * 0.6 - u_Time * u_FlowSpeed * 2.0,
                               uv.y * u_Tiling.y * max(u_StreakStretch, 1.0)));
    streak = smoothstep(0.55, 0.9, streak);

    // --- Foam: fast-scrolled aeration + band edges (bank contact) ---
    float foamN = WNoise(vec2(uv.x * u_Tiling.x * 2.4 - u_Time * u_FlowSpeed * 2.6,
                              uv.y * u_Tiling.y * 3.1));
    float edges = 1.0 - smoothstep(0.0, 0.18, uv.y) * smoothstep(1.0, 0.82, uv.y);
    float foam  = clamp((smoothstep(0.5, 0.85, foamN) + edges * 0.7), 0.0, 1.0)
                * u_FoamStrength;

    // --- Fresnel reflection (IBL fallback) + shadowless sun glint ---
    float fresnel = 0.02 + 0.98 * pow(clamp(1.0 - max(dot(N, V), 0.0), 0.0, 1.0), 5.0);
    vec3  reflected = (u_HasIBL > 0.5)
        ? textureLod(u_PrefilterMap, reflect(-V, N), 1.5).rgb
        : u_TintColor * 1.3;

    vec3  L     = normalize(-u_SunDirection_Ambient.xyz);
    vec3  H     = normalize(L + V);
    vec3  glint = u_SunColor_Intensity.rgb * u_SunColor_Intensity.w
                * pow(max(dot(N, H), 0.0), max(u_SpecularPower, 1.0));

    vec3 ambient = (u_HasIBL > 0.5) ? texture(u_IrradianceMap, N).rgb
                                    : vec3(u_SunDirection_Ambient.w);

    vec3 body    = u_TintColor * (ambient + u_SunColor_Intensity.rgb * u_SunColor_Intensity.w
                                            * max(dot(N, L), 0.0) * 0.5);
    vec3 surface = mix(body, reflected, fresnel)
                 + glint
                 + vec3(0.9) * streak * 0.15
                 + vec3(1.0) * foam;

    float alpha = clamp(u_Opacity + foam * 0.5 + fresnel * 0.25, 0.0, 1.0);

    color      = vec4(surface, alpha);
    o_EntityID = u_EntityID;
}
