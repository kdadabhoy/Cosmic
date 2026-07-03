#type vertex
#version 450 core

// FlowEmissive — Phase 11 (S11.2 / doc 10 F9-content): the generic "glowing
// flowing surface" material feature — lava flows, neon conduits, tron floors.
// A mesh whose UVs are authored so +U runs ALONG the flow (arclength) and V
// spans it [0,1] (the Frontier lava-strip generator produces exactly this)
// scrolls a procedural temperature field downstream; a piecewise temperature
// ramp turns it into HDR emissive that blooms (S6.6), while cool areas form a
// lit dark crust broken by glowing cracks.
//
// Everything is a parameter — the ENGINE knows nothing about volcanoes
// (master-roadmap rule 8). All intensity uniforms default to 0, so an unfed
// material renders as a dark lit crust; the app's Material sets the look.

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in vec4 a_Tangent;    // canonical layout; unused here

layout(std140, binding = 1) uniform CameraBlock
{
    mat4 ViewProjection;
    vec4 CameraPosition;
} u_Camera;

uniform mat4  u_Model;
uniform mat3  u_NormalMatrix;
uniform float u_Time;
uniform float u_FlowSpeed;     // UV units per second along -U (downstream scroll)
uniform float u_RippleAmp;     // meters of slow normal-direction swell (0 = off)

out vec3 v_WorldPos;
out vec3 v_WorldNormal;
out vec2 v_TexCoord;

float VHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float VNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(VHash(i),              VHash(i + vec2(1, 0)), u.x),
               mix(VHash(i + vec2(0, 1)), VHash(i + vec2(1, 1)), u.x), u.y);
}

void main()
{
    vec3 pos = a_Position;
    if (u_RippleAmp > 0.0)
    {
        float swell = VNoise(vec2(a_TexCoord.x * 2.0 - u_Time * u_FlowSpeed * 1.7,
                                  a_TexCoord.y * 3.0)) - 0.5;
        pos += a_Normal * swell * u_RippleAmp;
    }

    vec4 world     = u_Model * vec4(pos, 1.0);
    v_WorldPos     = world.xyz;
    v_WorldNormal  = u_NormalMatrix * a_Normal;
    v_TexCoord     = a_TexCoord;
    gl_Position    = u_Camera.ViewProjection * world;
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;
layout(location = 1) out int  o_EntityID;

in vec3 v_WorldPos;
in vec3 v_WorldNormal;
in vec2 v_TexCoord;

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

// Material parameters (set via Material::Set; suggested lava values in parens).
uniform float u_FlowSpeed;          // UV/s downstream scroll (0.05)
uniform vec2  u_NoiseScale;         // temperature-field repeats along U, V (3.0, 1.5)
uniform float u_Heat;               // overall temperature 0..1 (0.85)
uniform float u_EmissiveIntensity;  // HDR multiplier on the ramp (6.0 — blooms)
uniform vec3  u_CrustColor;         // cooled-surface albedo (0.035, 0.025, 0.025)
uniform float u_EdgeCool;           // 0..1 — how much the V edges cool (0.8)
uniform float u_CoolAlongLength;    // 1/UV-units — exponential cooling downstream (0.15)
uniform float u_CrackScale;         // crack-noise frequency (14.0)

float FHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float FNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(FHash(i),              FHash(i + vec2(1, 0)), u.x),
               mix(FHash(i + vec2(0, 1)), FHash(i + vec2(1, 1)), u.x), u.y);
}
float Fbm(vec2 p)
{
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 3; ++i) { v += a * FNoise(p); p *= 2.03; a *= 0.5; }
    return v;   // ~[0, 1)
}

// Procedural temperature LUT: black crust -> deep red -> orange -> yellow ->
// white-hot. Piecewise so the bands read like real incandescence.
vec3 TempRamp(float t)
{
    t = clamp(t, 0.0, 1.0);
    vec3 c = mix(vec3(0.0),               vec3(0.35, 0.015, 0.0), smoothstep(0.00, 0.25, t));
    c      = mix(c,                       vec3(1.4,  0.12,  0.0), smoothstep(0.25, 0.55, t));
    c      = mix(c,                       vec3(2.6,  0.9,   0.1), smoothstep(0.55, 0.80, t));
    c      = mix(c,                       vec3(4.0,  2.6,   1.2), smoothstep(0.80, 1.00, t));
    return c;
}

void main()
{
    vec3 N = normalize(v_WorldNormal);
    vec2 uv = v_TexCoord;

    // Downstream-scrolled temperature field: two octave sets moving at
    // different rates so the flow shimmers instead of sliding rigidly.
    vec2  fuv1 = vec2(uv.x * u_NoiseScale.x - u_Time * u_FlowSpeed,        uv.y * u_NoiseScale.y);
    vec2  fuv2 = vec2(uv.x * u_NoiseScale.x * 2.3 - u_Time * u_FlowSpeed * 1.6,
                      uv.y * u_NoiseScale.y * 2.1 + 7.3);
    float field = Fbm(fuv1) * 0.65 + Fbm(fuv2) * 0.35;

    // Cooling: V edges (flow margins) and downstream distance.
    float edge = 1.0 - u_EdgeCool * (1.0 - smoothstep(0.0, 0.35, uv.y)
                                         * smoothstep(1.0, 0.65, uv.y));
    float downstream = exp(-uv.x * max(u_CoolAlongLength, 0.0));
    float temp = clamp(u_Heat * edge * downstream * (0.45 + 0.9 * field), 0.0, 1.0);

    // Crust vs. melt: below the crust threshold the surface is solid rock;
    // glowing cracks (ridged noise) let the heat through.
    float crustiness = 1.0 - smoothstep(0.35, 0.6, temp);   // 1 = solid crust
    float crackN     = abs(FNoise(uv * u_CrackScale + vec2(0.0, u_Time * u_FlowSpeed * 0.3)) * 2.0 - 1.0);
    float cracks     = 1.0 - smoothstep(0.05, 0.22, crackN); // thin bright lines
    float glowVis    = mix(1.0, cracks * 0.9 + 0.05, crustiness);

    // Lit crust (Lambert sun + ambient — the melt outshines it anyway).
    vec3  L      = normalize(-u_SunDirection_Ambient.xyz);
    float ndl    = max(dot(N, L), 0.0);
    vec3  crustLit = u_CrustColor * (u_SunColor_Intensity.rgb * u_SunColor_Intensity.w * ndl
                                     + vec3(u_SunDirection_Ambient.w));

    vec3 emissive = TempRamp(temp) * u_EmissiveIntensity * glowVis;

    color      = vec4(crustLit * crustiness + emissive, 1.0);
    o_EntityID = u_EntityID;
}
