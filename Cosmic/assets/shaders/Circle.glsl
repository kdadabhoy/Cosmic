#type vertex
#version 450 core

layout(location = 0) in vec3 a_WorldPosition;
layout(location = 1) in vec2 a_LocalPosition;
layout(location = 2) in vec4 a_Color;
layout(location = 3) in float a_Thickness;
layout(location = 4) in float a_Fade;

uniform mat4 u_ViewProjection;

struct VertexOutput
{
    vec2 LocalPosition;
    vec4 Color;
    float Thickness;
    float Fade;
};

layout(location = 0) out VertexOutput Output;

void main()
{
    Output.LocalPosition = a_LocalPosition;
    Output.Color = a_Color;
    Output.Thickness = a_Thickness;
    Output.Fade = a_Fade;

    gl_Position = u_ViewProjection * vec4(a_WorldPosition, 1.0);
}


#type fragment
#version 450 core

struct VertexOutput
{
    vec2 LocalPosition;
    vec4 Color;
    float Thickness;
    float Fade;
};

layout(location = 0) in VertexOutput Input;

// Renamed variable token to match engine preprocessor expectations
layout(location = 0) out vec4 color;

void main()
{
    // Compute distance from center of layout coordinate bounds (Radius is 1.0)
    float distance = 1.0 - length(Input.LocalPosition);

    // Evaluate edge anti-aliasing outer border smoothing
    float alpha = smoothstep(0.0, Input.Fade, distance);

    // Apply inner ring thickness clipping logic
    alpha *= smoothstep(Input.Thickness + Input.Fade, Input.Thickness, 1.0 - distance);

    // Clip completely transparent fragment overhead completely
    if (alpha == 0.0)
        discard;

    // Assign final pixel outputs safely using 'color'
    color = Input.Color;
    color.a *= alpha;
}