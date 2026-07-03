#type vertex
#version 450 core

// Terrain — S8 quadtree-patch terrain shader. Every node draws the SAME shared
// 32x32-quad patch mesh; per-node uniforms place it in the world and the vertex
// stage texelFetches the packed height+normal texture (R,G = 16-bit height
// hi/lo byte; B,A = normal.xz * 0.5 + 0.5). Patch vertices land EXACTLY on
// heightfield texels (node sizes are power-of-two texel spans), so texelFetch
// needs no filtering and the surface matches Terrain::SampleHeight's triangle
// interpolation. a_Position = (u, skirtFlag, v): skirt vertices (flag = 1) drop
// by u_SkirtDepth to curtain LOD cracks between neighboring depths.

layout(location = 0) in vec3 a_Position;

layout(std140, binding = 1) uniform CameraBlock
{
    mat4 ViewProjection;
    vec4 CameraPosition;
} u_Camera;

uniform sampler2D u_HeightMap;

uniform vec2  u_NodeOrigin;        // world XZ of the node's (0,0) corner
uniform float u_NodeSize;          // world size of the node
uniform vec2  u_NodeTexelOrigin;   // heightfield texel of the node's corner
uniform float u_NodeTexels;        // texels spanned by the node (power of two)

uniform float u_HeightScale;
uniform float u_BaseHeight;
uniform float u_SkirtDepth;

out vec3 v_WorldPos;
out vec3 v_Normal;

void main()
{
    ivec2 texel = ivec2(u_NodeTexelOrigin + a_Position.xz * u_NodeTexels + vec2(0.5));
    vec4  s     = texelFetch(u_HeightMap, texel, 0);

    float h01 = (s.r * 65280.0 + s.g * 255.0) / 65535.0;   // R = hi byte, G = lo byte
    float nx  = s.b * 2.0 - 1.0;
    float nz  = s.a * 2.0 - 1.0;
    float ny  = sqrt(max(1.0 - nx * nx - nz * nz, 0.0));   // terrain normals point up

    vec2  xz = u_NodeOrigin + a_Position.xz * u_NodeSize;
    float y  = u_BaseHeight + h01 * u_HeightScale - a_Position.y * u_SkirtDepth;

    v_WorldPos  = vec3(xz.x, y, xz.y);
    v_Normal    = vec3(nx, ny, nz);
    gl_Position = u_Camera.ViewProjection * vec4(v_WorldPos, 1.0);
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;
layout(location = 1) out int  o_EntityID;   // MRT-safe (S4.6 pick pass)

in vec3 v_WorldPos;
in vec3 v_Normal;

layout(std140, binding = 1) uniform CameraBlock
{
    mat4 ViewProjection;
    vec4 CameraPosition;
} u_Camera;

// Engine scene lights (binding 0 = Bindings::LightsUbo) — same block as MeshLit/PBR.
layout(std140, binding = 0) uniform LightsBlock
{
    vec4 u_SunDirection_Ambient;     // xyz = dir the sun light TRAVELS, w = ambient
    vec4 u_SunColor_Intensity;       // rgb, w = intensity
    vec4 u_PointCount;               // x = active point count
    vec4 u_PointPos_Radius[16];
    vec4 u_PointColor_Intensity[16];
};

// --- Splat layers (S8.2): 0 = base, 1 = slope, 2 = high, 3 = low ---
uniform sampler2D u_LayerTex0;  uniform vec3 u_LayerColor0;  uniform float u_LayerTiling0;
uniform sampler2D u_LayerTex1;  uniform vec3 u_LayerColor1;  uniform float u_LayerTiling1;
uniform sampler2D u_LayerTex2;  uniform vec3 u_LayerColor2;  uniform float u_LayerTiling2;
uniform sampler2D u_LayerTex3;  uniform vec3 u_LayerColor3;  uniform float u_LayerTiling3;

uniform float u_SlopeThreshold;      // normal.y below this -> slope layer
uniform float u_SlopeBlend;
uniform float u_HighHeight;          // high layer (snow) fade-in altitude
uniform float u_HighBlend;
uniform float u_LowHeight;           // low layer (sand/ash) fade-out altitude
uniform float u_LowBlend;
uniform float u_TriplanarSharpness;

uniform int u_EntityID;

// --- Phase 11 (S11.1 / doc 10) terrain extras. All default-0 = the shipped S8
//     look, byte-identical, until the F4/F8 work orders feed them. ---
uniform float u_WetLine;             // world Y at/below which the surface reads wet
uniform float u_WetBand;             // fade band above the line (m)
uniform float u_WetDarken;           // 0 = off; 1 = full wet darkening
uniform float u_SnowSparkle;         // micro-glint strength on the high (snow) layer
uniform float u_SnowOverlayAmount;   // mask-driven extra snow below the snow line
// Coverage mask — same CoverageCapture RG contract as PBR.glsl (R = coverage,
// G = encoded top-surface Y). Reserved unit 12 (Bindings::TexUnitSnowMask).
uniform sampler2D u_SnowMaskMap;
uniform float     u_HasSnowMask;
uniform vec4      u_SnowMaskRect;    // xy = world min corner, zw = 1 / world size
uniform vec2      u_SnowMaskYDecode; // worldY = g * x + y
uniform float     u_SnowMaskYTol;    // receiver-vs-top tolerance (m)

// --- Scene IBL ambient (S6.3) + sun shadow (S6.4), injected by
//     Renderer3D::ApplySceneBindings on reserved units ---
uniform samplerCube u_IrradianceMap;
uniform samplerCube u_PrefilterMap;   // unused here; declared so the reserved
uniform sampler2D   u_BrdfLut;        // units stay type-consistent
uniform float       u_HasIBL;
uniform float       u_PrefilterMaxLod;

uniform sampler2D u_ShadowMap;
uniform mat4      u_LightViewProj;
uniform float     u_HasShadow;
uniform float     u_ShadowBias;

// 3x3 PCF, identical convention to PBR.glsl: 1 = fully shadowed.
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

// Triplanar sample (S8.2): project on the three world planes and blend by the
// normal, sharpened so flat ground stays a single cheap-looking XZ projection.
vec3 Triplanar(sampler2D tex, vec3 worldPos, float tiling, vec3 N)
{
    vec3 w = pow(abs(N), vec3(u_TriplanarSharpness));
    w /= max(w.x + w.y + w.z, 1e-5);
    vec3 sx = texture(tex, worldPos.zy * tiling).rgb;
    vec3 sy = texture(tex, worldPos.xz * tiling).rgb;
    vec3 sz = texture(tex, worldPos.xy * tiling).rgb;
    return sx * w.x + sy * w.y + sz * w.z;
}

void main()
{
    vec3 N = normalize(v_Normal);

    // --- Auto-splat weights (S8.2, parameterized — no scenario constants) ---
    float slopeW = 1.0 - smoothstep(u_SlopeThreshold - u_SlopeBlend,
                                    u_SlopeThreshold + u_SlopeBlend, N.y);
    float highW  = smoothstep(u_HighHeight - u_HighBlend,
                              u_HighHeight + u_HighBlend, v_WorldPos.y);
    highW *= 1.0 - slopeW * 0.85;                       // snow slides off steep rock

    // --- Phase 11 coverage mask + accumulation overlay (inert by default:
    // u_HasSnowMask = 0 -> mask = 1; u_SnowOverlayAmount = 0 -> no overlay) ---
    float snowMask = 1.0;
    if (u_HasSnowMask > 0.5)
    {
        vec2  muv  = clamp((v_WorldPos.xz - u_SnowMaskRect.xy) * u_SnowMaskRect.zw, 0.0, 1.0);
        vec2  m    = texture(u_SnowMaskMap, muv).rg;
        float topY = m.g * u_SnowMaskYDecode.x + u_SnowMaskYDecode.y;
        float exposed = smoothstep(-u_SnowMaskYTol * 2.0, -u_SnowMaskYTol * 0.5, v_WorldPos.y - topY);
        snowMask = m.r * exposed;
    }
    highW *= mix(1.0, snowMask, u_HasSnowMask);          // accumulation gates the snow band
    // Accumulated snow spreading below the snow line (blizzard build-up).
    float overlayW = u_SnowOverlayAmount * snowMask
                   * pow(clamp(N.y, 0.0, 1.0), 3.0) * (1.0 - slopeW);
    highW = clamp(max(highW, overlayW), 0.0, 1.0);

    float lowW   = (1.0 - smoothstep(u_LowHeight - u_LowBlend,
                                     u_LowHeight + u_LowBlend, v_WorldPos.y))
                 * (1.0 - slopeW);
    float baseW  = max(1.0 - slopeW - highW - lowW, 0.0);

    float wSum = baseW + slopeW + highW + lowW;
    baseW /= wSum;  slopeW /= wSum;  highW /= wSum;  lowW /= wSum;

    vec3 albedo = baseW  * u_LayerColor0 * Triplanar(u_LayerTex0, v_WorldPos, u_LayerTiling0, N) * 2.0
                + slopeW * u_LayerColor1 * Triplanar(u_LayerTex1, v_WorldPos, u_LayerTiling1, N) * 2.0
                + highW  * u_LayerColor2 * Triplanar(u_LayerTex2, v_WorldPos, u_LayerTiling2, N) * 2.0
                + lowW   * u_LayerColor3 * Triplanar(u_LayerTex3, v_WorldPos, u_LayerTiling3, N) * 2.0;
    // (x2: the shared procedural detail texture is authored mid-gray = 0.5.)

    // --- Phase 11 wetness band: darken at/below the waterline (beach wet-sand,
    // lakeshores). u_WetDarken = 0 (default) is a no-op. ---
    if (u_WetDarken > 0.0)
    {
        float wetW = 1.0 - smoothstep(u_WetLine, u_WetLine + max(u_WetBand, 1e-3), v_WorldPos.y);
        albedo *= mix(1.0, 0.45, wetW * u_WetDarken);
    }

    // --- Direct sun (Lambert; shadowed) + point lights ---
    vec3  L      = normalize(-u_SunDirection_Ambient.xyz);
    float ndl    = max(dot(N, L), 0.0);
    float shadow = (u_HasShadow > 0.5) ? ShadowFactor(v_WorldPos, N, L) : 0.0;
    vec3  direct = u_SunColor_Intensity.rgb * u_SunColor_Intensity.w * ndl * (1.0 - shadow);

    int count = int(u_PointCount.x);
    for (int i = 0; i < count && i < 16; ++i)
    {
        vec3  toL    = u_PointPos_Radius[i].xyz - v_WorldPos;
        float radius = max(u_PointPos_Radius[i].w, 1e-3);
        float dist   = length(toL);
        float att    = pow(clamp(1.0 - pow(dist / radius, 4.0), 0.0, 1.0), 2.0) / (dist * dist + 1.0);
        direct += u_PointColor_Intensity[i].rgb * u_PointColor_Intensity[i].w * att
                * max(dot(N, toL / max(dist, 1e-4)), 0.0);
    }

    // --- Ambient: IBL irradiance when bound, else the LightsBlock ambient knob ---
    vec3 ambient = (u_HasIBL > 0.5)
        ? texture(u_IrradianceMap, N).rgb
        : vec3(u_SunDirection_Ambient.w);

    vec3 outColor = albedo * (direct + ambient);

    // --- Phase 11 snow sparkle: twinkling micro-glints on the snow layer ---
    if (u_SnowSparkle > 0.0 && highW > 0.0)
    {
        vec3  V       = normalize(u_Camera.CameraPosition.xyz - v_WorldPos);
        vec3  H       = normalize(V + L);
        vec2  cell    = floor(v_WorldPos.xz * 24.0);
        float h       = fract(sin(dot(cell, vec2(127.1, 311.7))) * 43758.5453);
        float twinkle = smoothstep(0.97, 1.0, fract(h + dot(V, vec3(3.1, 5.2, 7.3))));
        outColor += u_SunColor_Intensity.rgb * u_SunColor_Intensity.w
                  * pow(max(dot(N, H), 0.0), 48.0)
                  * twinkle * u_SnowSparkle * highW * (1.0 - shadow);
    }

    color      = vec4(outColor, 1.0);
    o_EntityID = u_EntityID;
}
