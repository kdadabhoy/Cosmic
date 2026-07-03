#type vertex
#version 450 core

// Ribbon — S10.2 camera-facing trail strip. The CPU builds the extruded
// vertices each frame (RibbonEmitter::Render); this just projects and shades.

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;

layout(std140, binding = 1) uniform CameraBlock
{
    mat4 ViewProjection;
    vec4 CameraPosition;
} u_Camera;

out vec4 v_Color;

void main()
{
    v_Color     = a_Color;
    gl_Position = u_Camera.ViewProjection * vec4(a_Position, 1.0);
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;

in vec4 v_Color;

void main()
{
    color = v_Color;
}
