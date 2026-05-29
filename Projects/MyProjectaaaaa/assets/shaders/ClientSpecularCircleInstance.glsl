#type vertex
#version 450 core

// Location 0: Shared base unit-quad geometry (stepped per vertex)
layout(location = 0) in vec2 a_LocalPosition;

// Locations 1 to 5: Dynamic instanced property streams (stepped per instance via divisors)
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
    // Convert local positions from [-0.5, 0.5] to [-1.0, 1.0] 
    // to match what your custom specular fragment logic expects!
    Output.LocalPosition = a_LocalPosition * 2.0;
    Output.Color         = a_InstanceColor;
    Output.Thickness     = a_InstanceThickness;
    Output.Fade          = a_InstanceFade;

    // Expand the quad vertex positions around the ball's center position in world space
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