#type vertex
#version 450 core

// Location 0: Shared quad geometry vertex attribute (stepped per vertex)
layout(location = 0) in vec2 a_LocalPosition;

// Locations 1 to 5: Dynamic instanced data streams (stepped per instance via attribute divisors)
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
    // Pass structural layout metrics directly down to the fragment shader.
    // Multiply a_LocalPosition by 2.0 to expand the bounding quad coordinates
    // from [-0.5, 0.5] to [-1.0, 1.0], matching your fragment shader's radius expectations.
    Output.LocalPosition = a_LocalPosition * 2.0;
    Output.Color = a_InstanceColor;
    Output.Thickness = a_InstanceThickness;
    Output.Fade = a_InstanceFade;

    // Expand the quad geometry outwards around the circle's center instance point in world space.
    // The quad size is determined on the fly by multiplying the local vertex offsets by the instanced scale width/height.
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