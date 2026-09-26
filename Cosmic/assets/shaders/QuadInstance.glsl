#type vertex
#version 450 core

// Location 0: Shared base unit-quad geometry [-0.5, 0.5],
// stepped once per vertex (divisor = 0)
layout(location = 0) in vec2  a_LocalPosition;

// Locations 1–4: Per-instance data streams,
// stepped once per instance (divisor = 1)
layout(location = 1) in vec3  a_InstanceWorldPosition;
layout(location = 2) in vec2  a_InstanceScale;
layout(location = 3) in vec4  a_InstanceColor;
layout(location = 4) in vec2  a_InstanceTexCoordOffset;  // atlas tile UV origin
layout(location = 5) in vec2  a_InstanceTexCoordScale;   // atlas tile UV extent
layout(location = 6) in float a_InstanceTexIndex;        // sampler2D u_Textures slot
layout(location = 7) in float a_InstanceTilingFactor;    // UV tiling multiplier

uniform mat4 u_ViewProjection;

out vec4  v_Color;
out vec2  v_TexCoord;
flat out int v_TexIndex;   // slot, flat: sampler arrays take literal indices only (KI-82)
out float v_TilingFactor;

void main()
{
    // Map local quad corners [-0.5, 0.5] to UV space [0.0, 1.0]
    vec2 baseUV = a_LocalPosition + vec2(0.5);

    // Apply atlas tile offset and scale so the quad samples the correct
    // sub-region of a texture sheet (for solid colors, offset = 0, scale = 1)
    v_TexCoord    = a_InstanceTexCoordOffset + baseUV * a_InstanceTexCoordScale;
    v_Color       = a_InstanceColor;
    v_TexIndex    = int(a_InstanceTexIndex);
    v_TilingFactor = a_InstanceTilingFactor;

    // Expand the shared unit quad geometry around the instance world position
    vec3 worldPos = a_InstanceWorldPosition
                  + vec3(a_LocalPosition * a_InstanceScale, 0.0);

    gl_Position = u_ViewProjection * vec4(worldPos, 1.0);
}


#type fragment
#version 450 core

layout(location = 0) out vec4 color;

in vec4  v_Color;
in vec2  v_TexCoord;
flat in int v_TexIndex;
in float v_TilingFactor;

// Batch texture array — same contract as the standard Texture.glsl
uniform sampler2D u_Textures[32];

// KI-82: an instanced draw mixes slots per instance, so u_Textures[index] with a
// per-instance index is not dynamically uniform (undefined in GLSL 4.50; Mesa and
// other conformant compilers may mis-sample). The fetch is a switch over literal
// indices instead; the maths per fragment is unchanged. Same scheme as Texture.glsl.
vec4 SampleSlot(int slot, vec2 uv)
{
    switch (slot)
    {
        case  0: return texture(u_Textures[ 0], uv);
        case  1: return texture(u_Textures[ 1], uv);
        case  2: return texture(u_Textures[ 2], uv);
        case  3: return texture(u_Textures[ 3], uv);
        case  4: return texture(u_Textures[ 4], uv);
        case  5: return texture(u_Textures[ 5], uv);
        case  6: return texture(u_Textures[ 6], uv);
        case  7: return texture(u_Textures[ 7], uv);
        case  8: return texture(u_Textures[ 8], uv);
        case  9: return texture(u_Textures[ 9], uv);
        case 10: return texture(u_Textures[10], uv);
        case 11: return texture(u_Textures[11], uv);
        case 12: return texture(u_Textures[12], uv);
        case 13: return texture(u_Textures[13], uv);
        case 14: return texture(u_Textures[14], uv);
        case 15: return texture(u_Textures[15], uv);
        case 16: return texture(u_Textures[16], uv);
        case 17: return texture(u_Textures[17], uv);
        case 18: return texture(u_Textures[18], uv);
        case 19: return texture(u_Textures[19], uv);
        case 20: return texture(u_Textures[20], uv);
        case 21: return texture(u_Textures[21], uv);
        case 22: return texture(u_Textures[22], uv);
        case 23: return texture(u_Textures[23], uv);
        case 24: return texture(u_Textures[24], uv);
        case 25: return texture(u_Textures[25], uv);
        case 26: return texture(u_Textures[26], uv);
        case 27: return texture(u_Textures[27], uv);
        case 28: return texture(u_Textures[28], uv);
        case 29: return texture(u_Textures[29], uv);
        case 30: return texture(u_Textures[30], uv);
        case 31: return texture(u_Textures[31], uv);
    }
    return vec4(1.0);
}

void main()
{
    vec4 texColor = v_Color;
    int  index    = v_TexIndex;

    if (index >= 0 && index < 32)
    {
        texColor *= SampleSlot(index, v_TexCoord * v_TilingFactor);
    }

    // Alpha discard keeps sprite edges clean
    if (texColor.a < 0.01)
        discard;

    color = texColor;
}