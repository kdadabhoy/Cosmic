#type vertex
#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;

uniform mat4 u_ViewProjection;

out vec4 v_Color;
out vec3 v_LocalPosition;

void main()
{
    v_Color = a_Color;
    v_LocalPosition = a_Position;
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;

in vec4 v_Color;
in vec3 v_LocalPosition;

void main()
{
    // Adjust dashScale to change line density (larger number = more tightly packed dashes)
    float dashScale = 20.0; 
    
    // Calculate distance-independent local frequency patterns
    if (mod(v_LocalPosition.x * dashScale, 1.0) > 0.5 || mod(v_LocalPosition.y * dashScale, 1.0) > 0.5)
        discard;

    color = v_Color;
}