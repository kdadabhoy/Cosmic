#type vertex
#version 450 core

// Light2D (X5 / gap §12.1) — one additive radial light quad for the 2D lighting
// pass. VBO-free: six gl_VertexID corners form a quad sized 2*u_Radius around the
// light's world XY; the fragment shades a radial falloff. Drawn additively into a
// half-res HDR buffer that Scene::OnRender2DLights then MULTIPLIES over the scene.

uniform mat4  u_ViewProjection;   // the 2D camera's view-projection
uniform vec2  u_Center;           // light world XY
uniform float u_Radius;           // world-unit half-size of the quad

out vec2 v_Local;                 // [-1,1]^2 across the quad

void main()
{
    vec2 corners[6] = vec2[6](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
                              vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
    vec2 c = corners[gl_VertexID];
    v_Local = c;
    vec2 world = u_Center + c * u_Radius;
    gl_Position = u_ViewProjection * vec4(world, 0.0, 1.0);
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;

in vec2 v_Local;

uniform vec3  u_Color;
uniform float u_Intensity;
uniform float u_Falloff;

void main()
{
    // Radial distance: 0 at the center, 1 at the inscribed circle's edge.
    float d = length(v_Local);
    float f = pow(clamp(1.0 - d, 0.0, 1.0), u_Falloff);
    color = vec4(u_Color * (u_Intensity * f), 1.0);   // additive HDR contribution
}
