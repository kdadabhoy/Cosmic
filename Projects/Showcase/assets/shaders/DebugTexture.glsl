#type vertex
#version 330 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in float a_TexIndex;
layout(location = 4) in float a_TilingFactor;

uniform mat4 u_ViewProjection;

out vec4 v_Color;
out vec2 v_TexCoord;
out float v_TexIndex;
out float v_TilingFactor;

void main()
{
    v_Color = a_Color;
    v_TexCoord = a_TexCoord;
    v_TexIndex = a_TexIndex;
    v_TilingFactor = a_TilingFactor;
    
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}

#type fragment
#version 330 core

layout(location = 0) out vec4 color;

in vec4 v_Color;
in vec2 v_TexCoord;
in float v_TexIndex;
in float v_TilingFactor;

// BATCH INTERFACE: Master texture array managed by Renderer2D::Flush
uniform sampler2D u_Textures[32];

// MATERIAL ANCHORS: Prevents driver optimization stripping and exposes named properties
uniform sampler2D u_Texture;
uniform vec4 u_Color;

void main()
{
    // Start with the vertex color (which holds our material tint)
    vec4 texColor = v_Color;

    int index = int(v_TexIndex);
    
    if (index >= 0 && index < 32)
    {
        // Sample from the batch array slot provided by Renderer2D
        texColor *= texture(u_Textures[index], v_TexCoord * v_TilingFactor);
    }

    // Alpha test discard to remove transparency artifacts around your Dino sprite
    if (texColor.a < 0.1)
        discard;

    color = texColor;
}