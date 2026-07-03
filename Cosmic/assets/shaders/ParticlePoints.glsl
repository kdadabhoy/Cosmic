#type vertex
#version 450 core

// ParticlePoints — draws the compute-updated particles as GL points. No vertex
// attributes: each vertex reads its position from the same std430 SSBO (binding 0)
// by gl_VertexID. Requires GL_PROGRAM_POINT_SIZE (enabled in OpenGLRendererAPI::Init).

layout(std430, binding = 0) buffer Particles
{
    vec4 pos[];
};

// Per-frame camera (S6.2, binding: Bindings::CameraUbo). Uploaded by the last
// Renderer3D::BeginScene of the frame (the main pass), still current here since
// the compute-particle draw follows it — no per-draw setter needed.
layout(std140, binding = 1) uniform CameraBlock
{
    mat4 ViewProjection;
    vec4 CameraPosition;
} u_Camera;

void main()
{
    vec3 p = pos[gl_VertexID].xyz;
    gl_Position  = u_Camera.ViewProjection * vec4(p, 1.0);
    gl_PointSize = 2.0;
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;

uniform vec4 u_Color;

void main()
{
    color = u_Color;
}
