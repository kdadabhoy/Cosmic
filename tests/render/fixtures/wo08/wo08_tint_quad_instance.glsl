// wo08_tint_quad_instance.glsl — WO-08 F-2D fixture: the instanced quad shader
// (QuadInstance.glsl contract) with a uniform tint — the "custom shader path" of
// DrawInstancedQuads (R03). Not an engine shader; CosmicRenderTests only.
#type vertex
#version 450 core

layout(location = 0) in vec2  a_LocalPosition;
layout(location = 1) in vec3  a_InstanceWorldPosition;
layout(location = 2) in vec2  a_InstanceScale;
layout(location = 3) in vec4  a_InstanceColor;
layout(location = 4) in vec2  a_InstanceTexCoordOffset;
layout(location = 5) in vec2  a_InstanceTexCoordScale;
layout(location = 6) in float a_InstanceTexIndex;
layout(location = 7) in float a_InstanceTilingFactor;

uniform mat4 u_ViewProjection;

out vec4  v_Color;
out vec2  v_TexCoord;
flat out int v_TexIndex;
out float v_TilingFactor;

void main()
{
    vec2 baseUV = a_LocalPosition + vec2(0.5);
    v_TexCoord    = a_InstanceTexCoordOffset + baseUV * a_InstanceTexCoordScale;
    v_Color       = a_InstanceColor;
    v_TexIndex    = int(a_InstanceTexIndex);
    v_TilingFactor = a_InstanceTilingFactor;
    vec3 worldPos = a_InstanceWorldPosition + vec3(a_LocalPosition * a_InstanceScale, 0.0);
    gl_Position = u_ViewProjection * vec4(worldPos, 1.0);
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;

in vec4  v_Color;
in vec2  v_TexCoord;
flat in int v_TexIndex;
in float v_TilingFactor;

uniform sampler2D u_Textures[32];
uniform vec4 u_Tint;

// Literal-index fetch, same scheme as QuadInstance.glsl (KI-82).
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
        texColor *= SampleSlot(index, v_TexCoord * v_TilingFactor);
    if (texColor.a < 0.01)
        discard;
    color = texColor * u_Tint;
}
