#type vertex
#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;
layout(location = 2) in vec2 a_TexCoord;

uniform mat4 u_ViewProjection;

struct VertexOutput
{
    vec4 Color;
    vec2 TexCoord;
};

layout(location = 0) out VertexOutput Output;

void main()
{
    Output.Color    = a_Color;
    Output.TexCoord = a_TexCoord;
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}


#type fragment
#version 450 core

struct VertexOutput
{
    vec4 Color;
    vec2 TexCoord;
};

layout(location = 0) in VertexOutput Input;

layout(location = 0) out vec4 color;

uniform sampler2D u_FontAtlas;

void main()
{
    // The atlas stores a signed distance field: ~0.5 on the glyph edge, higher
    // inside, lower outside. fwidth() gives a screen-space derivative so edges
    // anti-alias consistently at any zoom level.
    float dist  = texture(u_FontAtlas, Input.TexCoord).r;
    float width = fwidth(dist);
    float alpha = smoothstep(0.5 - width, 0.5 + width, dist);

    if (alpha <= 0.0)
        discard;

    color = vec4(Input.Color.rgb, Input.Color.a * alpha);
}
