#type vertex
#version 450 core

// DemoChecker3D — acceptance shader for S4.2 material-driven meshes.
// Demonstrates the full material path: it declares the canonical mesh attribute
// layout (graphics/Mesh.h), consumes the engine convention uniforms uploaded by
// Renderer3D::DrawMesh(mesh, transform, material) — including the NEW per-draw
// u_NormalMatrix (mat3) so normals are not recomputed in-shader — and exposes
// material-owned uniforms (u_Color, u_Tiling, u_Texture) set via Material::Set.

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;

uniform mat4 u_ViewProjection;
uniform mat4 u_Model;
uniform mat3 u_NormalMatrix;   // engine convention (S4.2): transpose(inverse(mat3(model)))

out vec3 v_WorldNormal;
out vec2 v_TexCoord;

void main()
{
    vec4 world    = u_Model * vec4(a_Position, 1.0);
    v_WorldNormal = u_NormalMatrix * a_Normal;
    v_TexCoord    = a_TexCoord;

    gl_Position = u_ViewProjection * world;
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;
layout(location = 1) out int  o_EntityID;   // S4.6 entity-ID pick attachment

in vec3 v_WorldNormal;
in vec2 v_TexCoord;

uniform vec3      u_LightDir;   // engine convention: direction the light TRAVELS
uniform float     u_Ambient;    // engine convention: ambient floor in [0, 1]
uniform int       u_EntityID;   // S4.6: -1 when not picking

uniform vec4      u_Color;      // material-owned tint
uniform float     u_Tiling;     // material-owned checker/texture tiling
uniform sampler2D u_Texture;    // material-owned base texture

void main()
{
    // Procedural checker from the tiled UVs.
    vec2  uv      = v_TexCoord * u_Tiling;
    vec2  cell    = floor(uv);
    float checker = mod(cell.x + cell.y, 2.0);         // 0 or 1
    float shade   = mix(0.35, 1.0, checker);

    // Modulate by a texture sample (proves Material::BindFull's texture path).
    vec3 tex = texture(u_Texture, uv).rgb;

    vec3 base = u_Color.rgb * shade * tex;

    // Lambert with the engine's directional light + ambient floor.
    vec3  n   = normalize(v_WorldNormal);
    float ndl = max(dot(n, -u_LightDir), 0.0);
    float lit = u_Ambient + (1.0 - u_Ambient) * ndl;

    color      = vec4(base * lit, u_Color.a);
    o_EntityID = u_EntityID;
}
