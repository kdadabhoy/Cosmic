#type vertex
#version 450 core

// ParticlePoints — draws the compute-updated particles as GL points. No vertex
// attributes: each vertex reads its position from the same std430 SSBO (binding 0)
// by gl_VertexID. Requires GL_PROGRAM_POINT_SIZE (enabled in OpenGLRendererAPI::Init).

layout(std430, binding = 0) buffer Particles
{
    vec4 pos[];
};

uniform mat4 u_ViewProjection;

void main()
{
    vec3 p = pos[gl_VertexID].xyz;
    gl_Position  = u_ViewProjection * vec4(p, 1.0);
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
