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

// The array of all texture slots bound by the Renderer2D
uniform sampler2D u_Textures[32];

void main()
{
    // Sample the texture from the correct slot using the index passed from C++
    // We multiply by v_TilingFactor in case you want to repeat the texture
    vec4 texColor = texture(u_Textures[int(v_TexIndex)], v_TexCoord * v_TilingFactor);
    
    // Multiply the texture pixel by the color passed from the Material/ImGui
    color = texColor * v_Color;
    
    // Discard pixels with low alpha to allow for clean transparency
    if (color.a < 0.1)
        discard;
}