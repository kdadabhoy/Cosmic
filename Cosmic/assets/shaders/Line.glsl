// #type vertex
#version 330 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;
layout(location = 2) in float a_Distance; // New attribute for dash calculation

uniform mat4 u_ViewProjection;

out vec4 v_Color;
out float v_Distance;

void main() {
    v_Color = a_Color;
    v_Distance = a_Distance;
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}

// #type fragment
#version 330 core
layout(location = 0) out vec4 color;

in vec4 v_Color;
in float v_Distance;

void main() {
    // Dash logic: 0.2 is the total period (dash + gap)
    // If the remainder is greater than 0.1, we discard the pixel (the gap)
    if (mod(v_Distance, 0.2) > 0.1)
        discard;

    color = v_Color;
}