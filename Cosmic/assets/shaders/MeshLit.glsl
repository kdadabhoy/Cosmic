#type vertex
#version 450 core

// MeshLit — lighting v1 (S4.5): Blinn-Phong forward shading with a directional
// sun + up to 16 point lights fed by the engine LightsBlock UBO (binding 0).
// Consumes the engine convention uniforms uploaded by the material DrawMesh path
// (u_Model, u_ViewProjection, u_NormalMatrix, u_CameraPos) — it is the first real
// consumer of the S4.2 u_NormalMatrix convention (no in-shader normal recompute).
// Material-owned uniforms: u_Color (vec4) and u_Shininess (float) — both must be
// Set on the Material (no GLSL defaults).

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;

// Per-frame camera (S6.2, binding: Bindings::CameraUbo). Instance-named so the
// literal "u_ViewProjection" never appears — see CameraUniforms.h for why.
layout(std140, binding = 1) uniform CameraBlock
{
    mat4 ViewProjection;
    vec4 CameraPosition;   // xyz = camera world pos
} u_Camera;

uniform mat4 u_Model;
uniform mat3 u_NormalMatrix;

out vec3 v_WorldPos;
out vec3 v_WorldNormal;
out vec2 v_TexCoord;

void main()
{
    vec4 world    = u_Model * vec4(a_Position, 1.0);
    v_WorldPos    = world.xyz;
    v_WorldNormal = u_NormalMatrix * a_Normal;
    v_TexCoord    = a_TexCoord;

    gl_Position = u_Camera.ViewProjection * world;
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;
layout(location = 1) out int  o_EntityID;   // S4.6 entity-ID pick attachment

in vec3 v_WorldPos;
in vec3 v_WorldNormal;
in vec2 v_TexCoord;

// Per-frame camera (S6.2, binding 1) — same block as the vertex stage; the
// fragment needs the camera position for the Blinn specular view vector.
layout(std140, binding = 1) uniform CameraBlock
{
    mat4 ViewProjection;
    vec4 CameraPosition;   // xyz = camera world pos
} u_Camera;

uniform int   u_EntityID;    // S4.6: -1 when not picking
uniform vec4  u_Color;       // material-owned
uniform float u_Shininess;   // material-owned (Blinn specular exponent)

// Engine-wide scene lights (binding 0 = Bindings::LightsUbo). vec4-only std140
// layout — see Renderer3D::GpuLightsBlock (identical packing). The literal 16s
// mirror Renderer3D::kMaxPointLights — change both together.
layout(std140, binding = 0) uniform LightsBlock
{
    vec4 u_SunDirection_Ambient;     // xyz = dir the sun light TRAVELS, w = ambient
    vec4 u_SunColor_Intensity;       // rgb, w = intensity
    vec4 u_PointCount;               // x = active point count (as float)
    vec4 u_PointPos_Radius[16];      // xyz world pos, w = radius
    vec4 u_PointColor_Intensity[16]; // rgb, w = intensity
};

// --- Directional shadows (S6.4): 3x3 PCF against the sun's shadow map ---
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
        {
            float d = texture(u_ShadowMap, proj.xy + vec2(x, y) * texel).r;
            shadow += (proj.z - bias > d) ? 1.0 : 0.0;
        }
    return shadow / 9.0;
}

void main()
{
    vec3  N      = normalize(v_WorldNormal);
    vec3  V      = normalize(u_Camera.CameraPosition.xyz - v_WorldPos);
    vec3  albedo = u_Color.rgb;

    // Ambient floor.
    vec3 result = albedo * u_SunDirection_Ambient.w;

    // Directional sun: the block stores the direction the light TRAVELS, so the
    // vector toward the light is its negation. Shadowed by the S6.4 shadow map.
    vec3  Lsun    = normalize(-u_SunDirection_Ambient.xyz);
    float ndlSun  = max(dot(N, Lsun), 0.0);
    vec3  sunCol  = u_SunColor_Intensity.rgb * u_SunColor_Intensity.w;
    float sunShadow = (u_HasShadow > 0.5) ? ShadowFactor(v_WorldPos, N, Lsun) : 0.0;
    float sunVis  = 1.0 - sunShadow;
    result += albedo * sunCol * ndlSun * sunVis;

    vec3  Hsun    = normalize(Lsun + V);
    float specSun = (ndlSun > 0.0) ? pow(max(dot(N, Hsun), 0.0), u_Shininess) : 0.0;
    result += sunCol * specSun * sunVis;

    // Point lights.
    int count = int(u_PointCount.x);
    for (int i = 0; i < count && i < 16; ++i)
    {
        vec3  lp     = u_PointPos_Radius[i].xyz;
        float radius = max(u_PointPos_Radius[i].w, 1e-3);
        vec3  toL    = lp - v_WorldPos;
        float dist   = length(toL);
        vec3  L      = toL / max(dist, 1e-4);

        float ndl = max(dot(N, L), 0.0);
        float att = pow(clamp(1.0 - pow(dist / radius, 4.0), 0.0, 1.0), 2.0) / (dist * dist + 1.0);
        vec3  pc  = u_PointColor_Intensity[i].rgb * u_PointColor_Intensity[i].w;

        result += albedo * pc * ndl * att;

        vec3  H    = normalize(L + V);
        float spec = (ndl > 0.0) ? pow(max(dot(N, H), 0.0), u_Shininess) : 0.0;
        result += pc * spec * att;
    }

    color      = vec4(result, u_Color.a);
    o_EntityID = u_EntityID;
}
