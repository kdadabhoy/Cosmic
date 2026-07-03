#type vertex
#version 450 core

// PBR — Cook-Torrance metallic-roughness surface shader (S6.2). Direct lighting
// from the engine sun + point lights (binding-0 LightsBlock); image-based ambient
// (IBL) arrives in S6.3 (gated by u_HasIBL below — flat ambient until then).
// Follows the glTF 2.0 metallic-roughness model so S4.4b/S6.2 imports map 1:1.
// Reads the camera from the binding-1 CameraBlock. Renders LINEAR radiance into
// the HDR target — the S6.1 tonemap resolves it (highlights exceed 1.0 and roll
// off on the ACES shoulder / bloom in S6.6).
//
// S6.2 adds TEXTURES + normal mapping: albedo / normal / metal-rough / AO /
// emissive maps, each gated by a u_HasXMap float, sampled with the a_Tangent
// (location 3) TBN basis every Mesh now generates.
//
// Phase 11 (S11.1 / doc 10) adds the SNOW OVERLAY material feature: a scene-wide
// world-up + altitude snow blend with sparkle micro-glints and an optional
// accumulation coverage mask. Entirely gated by u_SnowEnabled (GL default 0 =
// byte-identical output); Renderer3D::SetSnow pushes the uniforms via
// ApplySceneBindings (F8). PBRInstanced.glsl carries the identical block —
// keep the two in sync.

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in vec4 a_Tangent;   // xyz = tangent, w = handedness sign (S6.2)

layout(std140, binding = 1) uniform CameraBlock
{
    mat4 ViewProjection;
    vec4 CameraPosition;   // xyz = camera world pos
} u_Camera;

uniform mat4 u_Model;
uniform mat3 u_NormalMatrix;   // transpose(inverse(mat3(model))) — engine convention

out vec3  v_WorldPos;
out vec3  v_WorldNormal;
out vec2  v_TexCoord;
out vec3  v_WorldTangent;
out float v_TangentW;

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
layout(location = 1) out int  o_EntityID;   // S4.6 entity-ID pick attachment

in vec3  v_WorldPos;
in vec3  v_WorldNormal;
in vec2  v_TexCoord;
in vec3  v_WorldTangent;
in float v_TangentW;

layout(std140, binding = 1) uniform CameraBlock
{
    mat4 ViewProjection;
    vec4 CameraPosition;
} u_Camera;

// Engine-wide scene lights (binding 0 = Bindings::LightsUbo). Same std140 block
// MeshLit reads — vec4-only packing, the literal 16s mirror kMaxPointLights.
layout(std140, binding = 0) uniform LightsBlock
{
    vec4 u_SunDirection_Ambient;     // xyz = dir the sun light TRAVELS, w = ambient
    vec4 u_SunColor_Intensity;       // rgb, w = intensity
    vec4 u_PointCount;               // x = active point count (as float)
    vec4 u_PointPos_Radius[16];      // xyz world pos, w = radius
    vec4 u_PointColor_Intensity[16]; // rgb, w = intensity
};

// Material-owned (glTF metallic-roughness factors).
uniform vec4  u_Albedo;     // base color (linear); a = alpha
uniform float u_Metallic;   // 0 = dielectric, 1 = metal
uniform float u_Roughness;  // 0 = mirror-smooth, 1 = fully rough
uniform float u_AO;         // ambient occlusion factor [0,1]
uniform vec3  u_Emissive;   // emissive radiance added on top
uniform int   u_EntityID;   // S4.6: -1 when not picking

// Material-owned textures (glTF maps). Each gated by a u_HasXMap float so an
// absent map costs nothing and never samples an unbound unit meaningfully.
uniform sampler2D u_AlbedoMap;      uniform float u_HasAlbedoMap;
uniform sampler2D u_NormalMap;      uniform float u_HasNormalMap;
uniform sampler2D u_MetalRoughMap;  uniform float u_HasMetalRoughMap;  // glTF: rough=G, metal=B
uniform sampler2D u_AOMap;          uniform float u_HasAOMap;          // occlusion in R
uniform sampler2D u_EmissiveMap;    uniform float u_HasEmissiveMap;

// --- IBL (S6.3): image-based ambient. u_HasIBL stays 0 until an EnvironmentMap
//     is bound, so the flat ambient term below is the default. ---
uniform samplerCube u_IrradianceMap;    // diffuse irradiance
uniform samplerCube u_PrefilterMap;     // prefiltered specular (mip = roughness)
uniform sampler2D   u_BrdfLut;          // split-sum BRDF integration LUT
uniform float       u_HasIBL;           // 0 = flat ambient, 1 = IBL ambient
uniform float       u_PrefilterMaxLod;  // highest prefilter mip index

// --- Directional shadows (S6.4): 3x3 PCF against the sun's shadow map ---
uniform sampler2D u_ShadowMap;
uniform mat4      u_LightViewProj;
uniform float     u_HasShadow;      // 0 = no shadowing
uniform float     u_ShadowBias;

// --- Snow overlay (S11.1, Phase 11): scene-wide, pushed by Renderer3D::SetSnow
//     through ApplySceneBindings. u_SnowEnabled's GL default of 0 keeps every
//     pre-Phase-11 draw byte-identical. The coverage mask (optional) is the
//     CoverageCapture RG target: R = coverage [0,1], G = encoded top-surface Y
//     (worldY = G * u_SnowMaskYDecode.x + u_SnowMaskYDecode.y) so receivers
//     under cover (roof over a floor) reject snow. Reserved unit 12
//     (Bindings::TexUnitSnowMask). ---
uniform float     u_SnowEnabled;    // 0 = off (default)
uniform float     u_SnowAmount;     // global coverage scale [0,1]
uniform float     u_SnowLine;       // world Y where snow fades in
uniform float     u_SnowBlendH;     // altitude blend half-width (m)
uniform float     u_SnowSlopeSharp; // pow() exponent on N·up (higher = flatter-only)
uniform vec3      u_SnowColor;      // snow albedo (linear)
uniform float     u_SnowSparkle;    // micro-glint strength (0 = off)
uniform sampler2D u_SnowMaskMap;    // coverage mask (unit 12)
uniform float     u_HasSnowMask;    // 0 = uniform coverage
uniform vec4      u_SnowMaskRect;   // xy = world min corner, zw = 1 / world size
uniform vec2      u_SnowMaskYDecode;// worldY = g * x + y
uniform float     u_SnowMaskYTol;   // receiver-vs-top tolerance (m)

const float PI = 3.14159265359;

// Returns 1.0 fully shadowed, 0.0 fully lit (slope-scaled bias + 3x3 PCF).
float ShadowFactor(vec3 worldPos, vec3 N, vec3 L)
{
    vec4 lp   = u_LightViewProj * vec4(worldPos, 1.0);
    vec3 proj = lp.xyz / lp.w * 0.5 + 0.5;          // clip → [0,1]
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 0.0;                                  // outside the map: treat as lit

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

// sRGB -> linear (authored color textures are sRGB; a full input-decode audit is
// S12.6, but decoding albedo/emissive here keeps the PBR math physically sane).
vec3 SrgbToLinear(vec3 c) { return pow(c, vec3(2.2)); }

// Trowbridge-Reitz GGX normal distribution.
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
    float ndv = max(dot(N, V), 0.0);
    float ndl = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(ndv, rough) * GeometrySchlickGGX(ndl, rough);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Fresnel with a roughness term — used for the IBL specular ambient so rough
// surfaces don't get an over-bright rim.
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

    vec3  numerator   = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * ndl + 1e-4;
    vec3  specular    = numerator / denominator;

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    return (kD * albedo / PI + specular) * radiance * ndl;
}

// Snow coverage for this fragment (0 when the feature is off) — shared logic
// with PBRInstanced.glsl / Terrain.glsl (keep in sync).
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
        // Exposed = at/above the captured top surface; under cover = no snow.
        float exposed = smoothstep(-u_SnowMaskYTol * 2.0, -u_SnowMaskYTol * 0.5, worldPos.y - topY);
        mask = m.r * exposed;
    }

    return clamp(u_SnowAmount * slopeW * altW * mask, 0.0, 1.0);
}

void main()
{
    vec2 uv = v_TexCoord;

    // --- Surface normal (with optional tangent-space normal map) ---
    vec3 N = normalize(v_WorldNormal);
    if (u_HasNormalMap > 0.5)
    {
        vec3 T  = normalize(v_WorldTangent - N * dot(N, v_WorldTangent));
        vec3 B  = cross(N, T) * v_TangentW;
        vec3 nTS = texture(u_NormalMap, uv).xyz * 2.0 - 1.0;
        N = normalize(mat3(T, B, N) * nTS);
    }

    // --- Material parameters (factor * texture, glTF convention) ---
    vec4 albedo4 = u_Albedo;
    if (u_HasAlbedoMap > 0.5)
    {
        vec4 s   = texture(u_AlbedoMap, uv);
        albedo4.rgb *= SrgbToLinear(s.rgb);
        albedo4.a   *= s.a;
    }
    vec3 albedo = albedo4.rgb;

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

    // --- Snow overlay (S11.1): world-up + altitude blend BEFORE F0 so the
    // snow's dielectric response feeds the whole BRDF. ---
    float snowCover = SnowCover(N, v_WorldPos);
    if (snowCover > 0.0)
    {
        albedo   = mix(albedo, u_SnowColor, snowCover);
        rough    = mix(rough, 0.6, snowCover);
        metallic = metallic * (1.0 - snowCover);
    }

    vec3 V  = normalize(u_Camera.CameraPosition.xyz - v_WorldPos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // --- Direct lighting: sun (shadowed) + point lights ---
    vec3  Lsun      = normalize(-u_SunDirection_Ambient.xyz);
    float sunShadow = (u_HasShadow > 0.5) ? ShadowFactor(v_WorldPos, N, Lsun) : 0.0;

    vec3 Lo = vec3(0.0);
    {
        vec3 radiance = u_SunColor_Intensity.rgb * u_SunColor_Intensity.w;
        Lo += (1.0 - sunShadow) * CookTorrance(N, V, Lsun, radiance, albedo, metallic, rough, F0);
    }

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

    // --- Ambient term: image-based (S6.3) when available, else a flat stand-in ---
    vec3 ambient;
    if (u_HasIBL > 0.5)
    {
        vec3  F     = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, rough);
        vec3  kD    = (vec3(1.0) - F) * (1.0 - metallic);
        vec3  irr   = texture(u_IrradianceMap, N).rgb;
        vec3  diff  = irr * albedo;

        vec3  R       = reflect(-V, N);
        vec3  prefilt = textureLod(u_PrefilterMap, R, rough * u_PrefilterMaxLod).rgb;
        vec2  brdf    = texture(u_BrdfLut, vec2(max(dot(N, V), 0.0), rough)).rg;
        vec3  spec    = prefilt * (F * brdf.x + brdf.y);

        ambient = (kD * diff + spec) * ao;
    }
    else
    {
        ambient = u_SunDirection_Ambient.w * albedo * ao;
    }

    vec3 outColor = ambient + Lo + emissive;

    // --- Snow sparkle (S11.1): view-dependent twinkling micro-glints ---
    if (snowCover > 0.0 && u_SnowSparkle > 0.0)
    {
        vec3  Hs      = normalize(V + Lsun);
        vec2  cell    = floor(v_WorldPos.xz * 24.0);   // ~4 cm glint cells
        float h       = fract(sin(dot(cell, vec2(127.1, 311.7))) * 43758.5453);
        float twinkle = smoothstep(0.97, 1.0, fract(h + dot(V, vec3(3.1, 5.2, 7.3))));
        outColor += u_SunColor_Intensity.rgb * u_SunColor_Intensity.w
                  * pow(max(dot(N, Hs), 0.0), 48.0)
                  * twinkle * u_SnowSparkle * snowCover * (1.0 - sunShadow);
    }

    color      = vec4(outColor, albedo4.a);
    o_EntityID = u_EntityID;
}
