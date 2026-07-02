#type vertex
#version 450 core

// Mesh3D — the engine's FIRST mesh shader (doc 05 S2), and the template every
// future mesh/material shader extends (S4). It declares the CANONICAL MESH
// ATTRIBUTE LAYOUT (graphics/Mesh.h) and the engine-wide uniform conventions:
//   u_Model / u_ViewProjection / u_CameraPos  (per-draw transform + camera)
// UVs are consumed as a varying from day one even though this shader doesn't
// sample a texture yet — the layout contract, not the feature set, is binding.

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;

uniform mat4 u_ViewProjection;
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

    gl_Position = u_ViewProjection * world;
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;

in vec3 v_WorldNormal;
in vec3 v_WorldPos;
in vec2 v_TexCoord;

uniform vec4  u_Color;      // per-draw flat color (Renderer3D::DrawMesh)
uniform vec3  u_LightDir;   // direction the light TRAVELS (normalized)
uniform vec3  u_CameraPos;  // reserved for specular in the S4/S5 tiers
uniform float u_Ambient;    // ambient floor in [0, 1]

void main()
{
    // Lambert: N·L against the direction TO the light, with an ambient floor
    // so unlit faces stay readable (engineering clarity over realism).
    vec3  n   = normalize(v_WorldNormal);
    float ndl = max(dot(n, -u_LightDir), 0.0);
    float lit = u_Ambient + (1.0 - u_Ambient) * ndl;

    color = vec4(u_Color.rgb * lit, u_Color.a);
}
