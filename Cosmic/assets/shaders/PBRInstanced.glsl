#type vertex
#version 450 core

// PBRInstanced — Phase 11 (S12.3-lite / doc 10 F5): the PBR surface shader for
// Renderer3D::DrawMeshInstanced. Per-instance transforms + tints live in an
// std430 SSBO on binding 9 (Bindings::InstancesSsbo — claimed in
// BindingPoints.h by F5) indexed by gl_InstanceID; everything else matches
// PBR.glsl, including the S11.1 snow overlay (instanced pines get snow like
// everything else). KEEP THE FRAGMENT STAGE IN SYNC WITH PBR.glsl.
//
// Normal transform note: normals go through mat3(Model) + normalize — exact
// for the rigid + UNIFORM-scale transforms instancing is for (trees, rocks,
// debris). Non-uniform instance scales would need a per-instance inverse-
// transpose (not worth 48 bytes/instance here; documented limitation).

struct InstanceData
{
    mat4 Model;   // world transform
    vec4 Tint;    // rgb multiplies albedo, a unused (reserved)
};

layout(std430, binding = 9) readonly buffer InstancePool
{
    InstanceData instances[];
};

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in vec4 a_Tangent;

layout(std140, binding = 1) uniform CameraBlock
{
    mat4 ViewProjection;
    vec4 CameraPosition;
} u_Camera;

out vec3  v_WorldPos;
out vec3  v_WorldNormal;
out vec2  v_TexCoord;
out vec3  v_WorldTangent;
out float v_TangentW;
out vec4  v_Tint;

void main()
{
    InstanceData inst = instances[gl_InstanceID];

    vec4 world     = inst.Model * vec4(a_Position, 1.0);
    mat3 nrm       = mat3(inst.Model);   // uniform-scale assumption (see header)
    v_WorldPos     = world.xyz;
    v_WorldNormal  = normalize(nrm * a_Normal);
    v_WorldTangent = normalize(nrm * a_Tangent.xyz);
    v_TangentW     = a_Tangent.w;
    v_TexCoord     = a_TexCoord;
    v_Tint         = inst.Tint;
    gl_Position    = u_Camera.ViewProjection * world;
}

#type fragment
#version 450 core

// ======= Fragment stage: PBR.glsl with a per-instance albedo tint. =======
// Kept in sync with PBR.glsl (Cook-Torrance + IBL + shadows + snow overlay).

layout(location = 0) out vec4 color;
layout(location = 1) out int  o_EntityID;

in vec3  v_WorldPos;
in vec3  v_WorldNormal;
in vec2  v_TexCoord;
in vec3  v_WorldTangent;
in float v_TangentW;
in vec4  v_Tint;

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

uniform vec4  u_Albedo;
uniform float u_Metallic;
uniform float u_Roughness;
uniform float u_AO;
uniform vec3  u_Emissive;
uniform int   u_EntityID;

uniform sampler2D u_AlbedoMap;      uniform float u_HasAlbedoMap;
uniform sampler2D u_NormalMap;      uniform float u_HasNormalMap;
uniform sampler2D u_MetalRoughMap;  uniform float u_HasMetalRoughMap;
uniform sampler2D u_AOMap;          uniform float u_HasAOMap;
uniform sampler2D u_EmissiveMap;    uniform float u_HasEmissiveMap;

uniform samplerCube u_IrradianceMap;
uniform samplerCube u_PrefilterMap;
uniform sampler2D   u_BrdfLut;
uniform float       u_HasIBL;
uniform float       u_PrefilterMaxLod;

uniform sampler2D u_ShadowMap;
uniform mat4      u_LightViewProj;
uniform float     u_HasShadow;
uniform float     u_ShadowBias;

// Snow overlay (S11.1) — identical contract to PBR.glsl (keep in sync).
uniform float     u_SnowEnabled;
uniform float     u_SnowAmount;
uniform float     u_SnowLine;
uniform float     u_SnowBlendH;
uniform float     u_SnowSlopeSharp;
uniform vec3      u_SnowColor;
uniform float     u_SnowSparkle;
uniform sampler2D u_SnowMaskMap;
uniform float     u_HasSnowMask;
uniform vec4      u_SnowMaskRect;
uniform vec2      u_SnowMaskYDecode;
uniform float     u_SnowMaskYTol;

const float PI = 3.14159265359;

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
        {
            float d = texture(u_ShadowMap, proj.xy + vec2(x, y) * texel).r;
            shadow += (proj.z - bias > d) ? 1.0 : 0.0;
        }
    return shadow / 9.0;
}

vec3 SrgbToLinear(vec3 c) { return pow(c, vec3(2.2)); }

float DistributionGGX(vec3 N, vec3 H, float rough)
{
    float a   = rough * rough;
    float a2  = a * a;
    float ndh = max(dot(N, H), 0.0);
    float d   = (ndh * ndh) * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-6);
}

float GeometrySchlickGGX(float ndv, float rough)
{
    float r = rough + 1.0;
    float k = (r * r) / 8.0;
    return ndv / (ndv * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float rough)
{
    return GeometrySchlickGGX(max(dot(N, V), 0.0), rough)
         * GeometrySchlickGGX(max(dot(N, L), 0.0), rough);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float rough)
{
    vec3 fr = max(vec3(1.0 - rough), F0);
    return F0 + (fr - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 CookTorrance(vec3 N, vec3 V, vec3 L, vec3 radiance,
                  vec3 albedo, float metallic, float rough, vec3 F0)
{
    float ndl = max(dot(N, L), 0.0);
    if (ndl <= 0.0)
        return vec3(0.0);

    vec3  H   = normalize(V + L);
    float NDF = DistributionGGX(N, H, rough);
    float G   = GeometrySmith(N, V, L, rough);
    vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3  specular = (NDF * G * F) / (4.0 * max(dot(N, V), 0.0) * ndl + 1e-4);
    vec3  kD       = (vec3(1.0) - F) * (1.0 - metallic);
    return (kD * albedo / PI + specular) * radiance * ndl;
}

float SnowCover(vec3 N, vec3 worldPos)
{
    if (u_SnowEnabled < 0.5)
        return 0.0;

    float up     = clamp(N.y, 0.0, 1.0);
    float slopeW = pow(up, max(u_SnowSlopeSharp, 1e-3));
    float altW   = smoothstep(u_SnowLine - u_SnowBlendH, u_SnowLine + u_SnowBlendH, worldPos.y);

    float mask = 1.0;
    if (u_HasSnowMask > 0.5)
    {
        vec2  muv  = clamp((worldPos.xz - u_SnowMaskRect.xy) * u_SnowMaskRect.zw, 0.0, 1.0);
        vec2  m    = texture(u_SnowMaskMap, muv).rg;
        float topY = m.g * u_SnowMaskYDecode.x + u_SnowMaskYDecode.y;
        float exposed = smoothstep(-u_SnowMaskYTol * 2.0, -u_SnowMaskYTol * 0.5, worldPos.y - topY);
        mask = m.r * exposed;
    }

    return clamp(u_SnowAmount * slopeW * altW * mask, 0.0, 1.0);
}

void main()
{
    vec2 uv = v_TexCoord;

    vec3 N = normalize(v_WorldNormal);
    if (u_HasNormalMap > 0.5)
    {
        vec3 T  = normalize(v_WorldTangent - N * dot(N, v_WorldTangent));
        vec3 B  = cross(N, T) * v_TangentW;
        vec3 nTS = texture(u_NormalMap, uv).xyz * 2.0 - 1.0;
        N = normalize(mat3(T, B, N) * nTS);
    }

    vec4 albedo4 = u_Albedo;
    if (u_HasAlbedoMap > 0.5)
    {
        vec4 s   = texture(u_AlbedoMap, uv);
        albedo4.rgb *= SrgbToLinear(s.rgb);
        albedo4.a   *= s.a;
    }
    vec3 albedo = albedo4.rgb * v_Tint.rgb;   // per-instance variation

    float metallic = clamp(u_Metallic, 0.0, 1.0);
    float rough    = clamp(u_Roughness, 0.04, 1.0);
    if (u_HasMetalRoughMap > 0.5)
    {
        vec3 mr  = texture(u_MetalRoughMap, uv).rgb;
        rough    = clamp(rough * mr.g, 0.04, 1.0);
        metallic = metallic * mr.b;
    }

    float ao = clamp(u_AO, 0.0, 1.0);
    if (u_HasAOMap > 0.5)
        ao *= texture(u_AOMap, uv).r;

    vec3 emissive = u_Emissive;
    if (u_HasEmissiveMap > 0.5)
        emissive *= SrgbToLinear(texture(u_EmissiveMap, uv).rgb);

    float snowCover = SnowCover(N, v_WorldPos);
    if (snowCover > 0.0)
    {
        albedo   = mix(albedo, u_SnowColor, snowCover);
        rough    = mix(rough, 0.6, snowCover);
        metallic = metallic * (1.0 - snowCover);
    }

    vec3 V  = normalize(u_Camera.CameraPosition.xyz - v_WorldPos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3  Lsun      = normalize(-u_SunDirection_Ambient.xyz);
    float sunShadow = (u_HasShadow > 0.5) ? ShadowFactor(v_WorldPos, N, Lsun) : 0.0;

    vec3 Lo = (1.0 - sunShadow)
            * CookTorrance(N, V, Lsun, u_SunColor_Intensity.rgb * u_SunColor_Intensity.w,
                           albedo, metallic, rough, F0);

    int count = int(u_PointCount.x);
    for (int i = 0; i < count && i < 16; ++i)
    {
        vec3  lp     = u_PointPos_Radius[i].xyz;
        float radius = max(u_PointPos_Radius[i].w, 1e-3);
        vec3  toL    = lp - v_WorldPos;
        float dist   = length(toL);
        vec3  L      = toL / max(dist, 1e-4);

        float att      = pow(clamp(1.0 - pow(dist / radius, 4.0), 0.0, 1.0), 2.0) / (dist * dist + 1.0);
        vec3  radiance = u_PointColor_Intensity[i].rgb * u_PointColor_Intensity[i].w * att;
        Lo += CookTorrance(N, V, L, radiance, albedo, metallic, rough, F0);
    }

    vec3 ambient;
    if (u_HasIBL > 0.5)
    {
        vec3  F     = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, rough);
        vec3  kD    = (vec3(1.0) - F) * (1.0 - metallic);
        vec3  diff  = texture(u_IrradianceMap, N).rgb * albedo;
        vec3  prefilt = textureLod(u_PrefilterMap, reflect(-V, N), rough * u_PrefilterMaxLod).rgb;
        vec2  brdf    = texture(u_BrdfLut, vec2(max(dot(N, V), 0.0), rough)).rg;
        ambient = (kD * diff + prefilt * (F * brdf.x + brdf.y)) * ao;
    }
    else
    {
        ambient = u_SunDirection_Ambient.w * albedo * ao;
    }

    vec3 outColor = ambient + Lo + emissive;

    if (snowCover > 0.0 && u_SnowSparkle > 0.0)
    {
        vec3  Hs      = normalize(V + Lsun);
        vec2  cell    = floor(v_WorldPos.xz * 24.0);
        float h       = fract(sin(dot(cell, vec2(127.1, 311.7))) * 43758.5453);
        float twinkle = smoothstep(0.97, 1.0, fract(h + dot(V, vec3(3.1, 5.2, 7.3))));
        outColor += u_SunColor_Intensity.rgb * u_SunColor_Intensity.w
                  * pow(max(dot(N, Hs), 0.0), 48.0)
                  * twinkle * u_SnowSparkle * snowCover * (1.0 - sunShadow);
    }

    color      = vec4(outColor, albedo4.a);
    o_EntityID = u_EntityID;
}
