// wo08_tint_circle_instance.glsl — WO-08 F-2D fixture: the instanced SDF circle
// shader (CircleInstance.glsl contract) with a uniform tint — the "custom shader
// path" of DrawInstancedCircles (R03). Not an engine shader; CosmicRenderTests only.
#type vertex
#version 450 core

layout(location = 0) in vec2 a_LocalPosition;
layout(location = 1) in vec3 a_InstanceWorldPosition;
layout(location = 2) in vec2 a_InstanceScale;
layout(location = 3) in vec4 a_InstanceColor;
layout(location = 4) in float a_InstanceThickness;
layout(location = 5) in float a_InstanceFade;

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
    Output.LocalPosition = a_LocalPosition * 2.0;
    Output.Color = a_InstanceColor;
    Output.Thickness = a_InstanceThickness;
    Output.Fade = a_InstanceFade;
    vec3 worldPosition = a_InstanceWorldPosition + vec3(a_LocalPosition * a_InstanceScale, 0.0);
    gl_Position = u_ViewProjection * vec4(worldPosition, 1.0);
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
