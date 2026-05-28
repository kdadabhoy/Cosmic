#type vertex
#version 450 core

// Location 0: Shared base unit-quad geometry [-0.5, 0.5],
// stepped once per vertex (divisor = 0)
layout(location = 0) in vec2  a_LocalPosition;

// Locations 1–4: Per-instance data streams,
// stepped once per instance (divisor = 1)
layout(location = 1) in vec3  a_InstanceWorldPosition;
layout(location = 2) in vec2  a_InstanceScale;
layout(location = 3) in vec4  a_InstanceColor;
layout(location = 4) in vec2  a_InstanceTexCoordOffset;  // atlas tile UV origin
layout(location = 5) in vec2  a_InstanceTexCoordScale;   // atlas tile UV extent
layout(location = 6) in float a_InstanceTexIndex;        // sampler2D u_Textures slot
layout(location = 7) in float a_InstanceTilingFactor;    // UV tiling multiplier

uniform mat4 u_ViewProjection;

out vec4  v_Color;
out vec2  v_TexCoord;
out float v_TexIndex;
out float v_TilingFactor;

void main()
{
    // Map local quad corners [-0.5, 0.5] to UV space [0.0, 1.0]
    vec2 baseUV = a_LocalPosition + vec2(0.5);

    // Apply atlas tile offset and scale so the quad samples the correct
    // sub-region of a texture sheet (for solid colors, offset = 0, scale = 1)
    v_TexCoord    = a_InstanceTexCoordOffset + baseUV * a_InstanceTexCoordScale;
    v_Color       = a_InstanceColor;
    v_TexIndex    = a_InstanceTexIndex;
    v_TilingFactor = a_InstanceTilingFactor;

    // Expand the shared unit quad geometry around the instance world position
    vec3 worldPos = a_InstanceWorldPosition
                  + vec3(a_LocalPosition * a_InstanceScale, 0.0);

    gl_Position = u_ViewProjection * vec4(worldPos, 1.0);
}


#type fragment
#version 450 core

layout(location = 0) out vec4 color;

in vec4  v_Color;
in vec2  v_TexCoord;
in float v_TexIndex;
in float v_TilingFactor;

// Batch texture array — same contract as the standard Texture.glsl
uniform sampler2D u_Textures[32];

void main()
{
    vec4 texColor = v_Color;
    int  index    = int(v_TexIndex);

    if (index >= 0 && index < 32)
    {
        texColor *= texture(u_Textures[index], v_TexCoord * v_TilingFactor);
    }

    // Alpha discard keeps sprite edges clean
    if (texColor.a < 0.01)
        discard;

    color = texColor;
}