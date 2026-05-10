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

void main() {
    v_Color = a_Color;
    v_TexCoord = a_TexCoord;
    v_TexIndex = a_TexIndex;
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}

#type fragment
#version 330 core

layout(location = 0) out vec4 color;

in vec4 v_Color;
in vec2 v_TexCoord;
in float v_TexIndex;

uniform sampler2D u_Textures[32];

void main()
{
    // --- Visual Debugger ---
    // Index 0.0 is the WhiteTexture.
    // Index 1.0+ is your loaded textures (Dino).
    
    vec4 texColor = texture(u_Textures[int(v_TexIndex)], v_TexCoord);
    
    if (v_TexIndex < 0.5) {
        // If it's the white texture, tint it GREEN so we know it's slot 0
        color = texColor * v_Color * vec4(0.0, 1.0, 0.0, 1.0);
    } else {
        // If it's the Dino (or any other), tint it RED
        // If you see a RED shape but no dino, the index is right but the texture bind failed.
        color = texColor * v_Color * vec4(1.0, 0.0, 0.0, 1.0);
    }

    if (color.a < 0.1)
        discard;
}