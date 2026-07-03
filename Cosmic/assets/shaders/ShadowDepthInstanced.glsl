#type vertex
#version 450 core

// ShadowDepthInstanced — Phase 11 (S12.3-lite / doc 10 F5): the depth-only
// shadow-caster pass for instanced meshes (ShadowMap::DrawCasterInstanced).
// Reads the SAME std430 instance pool PBRInstanced.glsl renders from
// (binding 9 = Bindings::InstancesSsbo), so a forest shadows itself with one
// draw. Sibling of ShadowDepth.glsl.

struct InstanceData
{
    mat4 Model;
    vec4 Tint;    // unused here; layout must match PBRInstanced.glsl
};

layout(std430, binding = 9) readonly buffer InstancePool
{
    InstanceData instances[];
};

layout(location = 0) in vec3 a_Position;

uniform mat4 u_LightViewProj;

void main()
{
    gl_Position = u_LightViewProj * instances[gl_InstanceID].Model * vec4(a_Position, 1.0);
}

#type fragment
#version 450 core

// Depth-only FBO — the color write is discarded (same note as ShadowDepth.glsl).
layout(location = 0) out vec4 color;

void main()
{
    color = vec4(1.0);
}
