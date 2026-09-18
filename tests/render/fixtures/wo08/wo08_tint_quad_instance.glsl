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
out float v_TexIndex;
out float v_TilingFactor;

void main()
{
    vec2 baseUV = a_LocalPosition + vec2(0.5);
    v_TexCoord    = a_InstanceTexCoordOffset + baseUV * a_InstanceTexCoordScale;
    v_Color       = a_InstanceColor;
    v_TexIndex    = a_InstanceTexIndex;
    v_TilingFactor = a_InstanceTilingFactor;
    vec3 worldPos = a_InstanceWorldPosition + vec3(a_LocalPosition * a_InstanceScale, 0.0);
    gl_Position = u_ViewProjection * vec4(worldPos, 1.0);
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;

in vec4  v_Color;
in vec2  v_TexCoord;
in float v_TexIndex;
in float v_TilingFactor;

uniform sampler2D u_Textures[32];
uniform vec4 u_Tint;

void main()
{
    vec4 texColor = v_Color;
    int  index    = int(v_TexIndex);
    if (index >= 0 && index < 32)
        texColor *= texture(u_Textures[index], v_TexCoord * v_TilingFactor);
    if (texColor.a < 0.01)
        discard;
    color = texColor * u_Tint;
}
