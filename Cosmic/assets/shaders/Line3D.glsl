#type vertex
#version 450 core

// Line3D — Renderer3D's batched line shader.
// World-space positions transformed by the scene view-projection; flat
// per-vertex color. Solid lines (contrast with the 2D Line.glsl dash pattern).

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;

// Per-frame camera (S6.2, binding: Bindings::CameraUbo). Instance-named so the
// literal "u_ViewProjection" never appears — see CameraUniforms.h for why.
layout(std140, binding = 1) uniform CameraBlock
{
    mat4 ViewProjection;
    vec4 CameraPosition;   // xyz = camera world pos
} u_Camera;

out vec4 v_Color;

void main()
{
    v_Color = a_Color;
    gl_Position = u_Camera.ViewProjection * vec4(a_Position, 1.0);
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;
layout(location = 1) out int  o_EntityID;   // S4.6: lines are not pickable → -1

in vec4 v_Color;

void main()
{
    color      = v_Color;
    o_EntityID = -1;   // writing an output the bound FBO lacks is harmless
}
