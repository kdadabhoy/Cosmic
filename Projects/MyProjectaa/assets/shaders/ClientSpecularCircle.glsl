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
    Output.Color         = a_Color;
    Output.Thickness     = a_Thickness;
    Output.Fade          = a_Fade;

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

// Targets location 0 out per engine preprocessor expectations
layout(location = 0) out vec4 color;

void main()
{
    // 1. Calculate standard mathematical circle boundary (Radius = 1.0)
    float distance = 1.0 - length(Input.LocalPosition);

    // Evaluate anti-aliasing outer edge smoothing
    float alpha = smoothstep(0.0, Input.Fade, distance);

    // Apply inner-ring thickness clipping logic 
    alpha *= smoothstep(Input.Thickness + Input.Fade, Input.Thickness, 1.0 - distance);

    // Completely drop fragment processing overhead if outside the shape
    if (alpha == 0.0)
        discard;

    // 2. THE HIGH-PERFORMANCE SPECULAR OVERLAY
    // Calculate how far this specific pixel is from an upper-right light source offset (0.35, 0.35)
    vec2 lightSourceOffset = vec2(0.35, 0.35);
    float specDist = length(Input.LocalPosition - lightSourceOffset);
    
    // Evaluate a smooth white dot overlay that falls off perfectly at a local radius of 0.38
    // multiplied by an intensity weight of 0.45 to blend naturally over the base color
    float specularHighlight = smoothstep(0.38, 0.0, specDist) * 0.45;

    // 3. Output assembly
    color = Input.Color;
    color.rgb += vec3(specularHighlight); // Inject white specular intensity directly into color channels
    color.a *= alpha;                     // Restrict the overlay perfectly to the circle's shape bounds
}