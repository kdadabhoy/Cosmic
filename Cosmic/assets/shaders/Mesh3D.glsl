#type vertex
#version 450 core

// Mesh3D — the engine's FIRST mesh shader (doc 05 S2), and the template every
// future mesh/material shader extends (S4). It declares the CANONICAL MESH
// ATTRIBUTE LAYOUT (graphics/Mesh.h) and the engine-wide uniform conventions:
//   u_Model / u_ViewProjection / u_CameraPos  (per-draw transform + camera)
// UVs are consumed as a varying from day one even though this shader doesn't
// sample a texture yet — the layout contract, not the feature set, is binding.
//
// This is the Lambert COLOR-path shader (Renderer3D::DrawMesh with a vec4).
// S4.2 added a MATERIAL path (DrawMesh with a Ref<Material>) that also uploads
// u_NormalMatrix (mat3 = transpose(inverse(mat3(u_Model)))). This shader keeps
// computing its own normal matrix in-vertex (unchanged) — material shaders that
// want the CPU-side one just declare u_NormalMatrix (see DemoChecker3D.glsl).

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

out vec3 v_WorldNormal;
out vec3 v_WorldPos;
out vec2 v_TexCoord;

void main()
{
    vec4 world = u_Model * vec4(a_Position, 1.0);
    v_WorldPos = world.xyz;

    // Inverse-transpose handles non-uniform scale correctly. Computed per
    // vertex — fine at sim scale (tens of meshes); move to a CPU-side uniform
    // when instancing/perf ever demands it.
    v_WorldNormal = mat3(transpose(inverse(u_Model))) * a_Normal;
    v_TexCoord = a_TexCoord;

    gl_Position = u_Camera.ViewProjection * world;
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;
layout(location = 1) out int  o_EntityID;   // S4.6 entity-ID pick attachment

in vec3 v_WorldNormal;
in vec3 v_WorldPos;
in vec2 v_TexCoord;

uniform vec4  u_Color;      // per-draw flat color (Renderer3D::DrawMesh)
uniform int   u_EntityID;   // S4.6: -1 when not picking

// Engine-wide scene lights (binding 0 = Bindings::LightsUbo) — SAME std140 block as
// MeshLit.glsl (H3: the cheap Lambert color path now reads scene lights too, so a
// DirectionalLight drives it and PointLights actually light default-material meshes).
// The 16s mirror Renderer3D::kMaxPointLights — change both together.
layout(std140, binding = 0) uniform LightsBlock
{
    vec4 u_SunDirection_Ambient;     // xyz = dir the sun light TRAVELS, w = ambient
    vec4 u_SunColor_Intensity;       // rgb, w = intensity
    vec4 u_PointCount;               // x = active point count (as float)
    vec4 u_PointPos_Radius[16];      // xyz world pos, w = radius
    vec4 u_PointColor_Intensity[16]; // rgb, w = intensity
};

// --- Directional shadows (S6.4): the flat Lambert path receives them too, so a
//     shadow lands on the plain-colored ground pad / meshes, not just PBR/MeshLit. ---
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
    // Lambert: N·L against the direction TO the light, with an ambient floor
    // so unlit faces stay readable (engineering clarity over realism). Sun dir +
    // color + ambient now come from the LightsBlock UBO. With the default white sun
    // (intensity 1) and no point lights this is BYTE-IDENTICAL to the pre-H3 formula
    // `ambient + (1-ambient)*ndl*(1-shadow)` (sunCol == vec3(1)); a colored/dimmed
    // DirectionalLight tints it, and point lights add within the same headroom.
    vec3  n       = normalize(v_WorldNormal);
    float ambient = u_SunDirection_Ambient.w;
    vec3  Lsun    = normalize(-u_SunDirection_Ambient.xyz);
    float ndlSun  = max(dot(n, Lsun), 0.0);

    float shadow  = (u_HasShadow > 0.5) ? ShadowFactor(v_WorldPos, n, Lsun) : 0.0;
    vec3  sunCol  = u_SunColor_Intensity.rgb * u_SunColor_Intensity.w;

    vec3  lit = vec3(ambient) + (1.0 - ambient) * ndlSun * (1.0 - shadow) * sunCol;

    // Point lights (cheap diffuse only — no specular on the flat path). Same
    // windowed inverse-square attenuation as MeshLit, folded into the (1-ambient)
    // headroom so a dropped-in light reads as extra fill without blowing out.
    int count = int(u_PointCount.x);
    for (int i = 0; i < count && i < 16; ++i)
    {
        vec3  lp     = u_PointPos_Radius[i].xyz;
        float radius = max(u_PointPos_Radius[i].w, 1e-3);
        vec3  toL    = lp - v_WorldPos;
        float dist   = length(toL);
        vec3  L      = toL / max(dist, 1e-4);

        float ndl = max(dot(n, L), 0.0);
        float att = pow(clamp(1.0 - pow(dist / radius, 4.0), 0.0, 1.0), 2.0) / (dist * dist + 1.0);
        vec3  pc  = u_PointColor_Intensity[i].rgb * u_PointColor_Intensity[i].w;

        lit += (1.0 - ambient) * ndl * att * pc;
    }

    color      = vec4(u_Color.rgb * lit, u_Color.a);
    o_EntityID = u_EntityID;
}
