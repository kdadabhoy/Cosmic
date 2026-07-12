#type vertex
#version 450 core

// ShadowDepthSkinned — Phase 20 (A2): the depth-only shadow-caster twin for
// skinned meshes, so an animated character's SHADOW deforms with it. Reads the
// same binding-10 skinning palette as PBRSkinned.glsl (Bindings::SkinningSsbo);
// ShadowMap::DrawCasterSkinned uploads the caster's palette and sets u_SkinBase.

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;    // present in the canonical layout; unused
layout(location = 2) in vec2 a_TexCoord;  // unused
layout(location = 3) in vec4 a_Tangent;   // unused
layout(location = 4) in vec4 a_Joints;    // 4 palette indices (as floats)
layout(location = 5) in vec4 a_Weights;   // matching blend weights

layout(std430, binding = 10) readonly buffer SkinPalette
{
    mat4 u_Palette[];
};

uniform int  u_SkinBase;
uniform mat4 u_LightViewProj;
uniform mat4 u_Model;

void main()
{
    float wsum = a_Weights.x + a_Weights.y + a_Weights.z + a_Weights.w;
    vec4  w    = (wsum > 1e-6) ? a_Weights / wsum : vec4(1.0, 0.0, 0.0, 0.0);
    mat4  skin = w.x * u_Palette[u_SkinBase + int(a_Joints.x + 0.5)]
               + w.y * u_Palette[u_SkinBase + int(a_Joints.y + 0.5)]
               + w.z * u_Palette[u_SkinBase + int(a_Joints.z + 0.5)]
               + w.w * u_Palette[u_SkinBase + int(a_Joints.w + 0.5)];

    gl_Position = u_LightViewProj * u_Model * (skin * vec4(a_Position, 1.0));
}

#type fragment
#version 450 core

// Depth-only target — the declared color write is discarded (ShadowDepth.glsl
// convention).
layout(location = 0) out vec4 color;

void main()
{
    color = vec4(1.0);
}
