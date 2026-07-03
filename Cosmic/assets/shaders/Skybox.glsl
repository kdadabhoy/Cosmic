#type vertex
#version 450 core

// Skybox — draws the baked environment cube behind the scene (S6.3). A single
// fullscreen triangle placed at the far plane (z = 1); the fragment reconstructs
// a world-space view ray from the inverse view-projection and samples the cube.
// Rendered INTO the HDR scene target after opaque geometry with depth test LEQUAL
// and depth writes off, so it only fills unwritten (far) pixels.

out vec2 v_TexCoord;

void main()
{
    vec2 uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    v_TexCoord = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 1.0, 1.0);   // z = 1 → far plane
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;
layout(location = 1) out int  o_EntityID;   // MRT-safe: skybox is never pickable

in vec2 v_TexCoord;

layout(std140, binding = 1) uniform CameraBlock
{
    mat4 ViewProjection;
    vec4 CameraPosition;
} u_Camera;

uniform mat4        u_InvViewProj;   // inverse(ViewProjection); set per-frame
uniform samplerCube u_EnvironmentMap;

void main()
{
    vec4 clip  = vec4(v_TexCoord * 2.0 - 1.0, 1.0, 1.0);
    vec4 world = u_InvViewProj * clip;
    world /= world.w;

    vec3 dir = normalize(world.xyz - u_Camera.CameraPosition.xyz);
    color      = vec4(texture(u_EnvironmentMap, dir).rgb, 1.0);
    o_EntityID = -1;
}
