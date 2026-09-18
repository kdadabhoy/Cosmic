// wo08_tint_circle.glsl — WO-08 F-2D fixture: the batch SDF circle shader
// (Circle.glsl contract) with a uniform tint, so a custom-shader circle batch is
// visibly distinct from the engine default (R02 shader transitions, R03 state
// restoration). Not an engine shader; loaded by CosmicRenderTests only.
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
layout(location = 0) out vec4 color;

uniform vec4 u_Tint;

void main()
{
    float distance = 1.0 - length(Input.LocalPosition);
    float alpha = smoothstep(0.0, Input.Fade, distance);
    alpha *= smoothstep(Input.Thickness + Input.Fade, Input.Thickness, 1.0 - distance);
    if (alpha == 0.0)
        discard;
    color = Input.Color * u_Tint;
    color.a *= alpha;
}
