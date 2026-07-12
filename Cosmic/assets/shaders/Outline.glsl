#type vertex
#version 450 core

// Selection outline (Phase 22 / K12) — fullscreen composite over the LDR
// target. The mask is the ScenePicker's RED_INTEGER id attachment rendered
// with a selection filter: id >= 0 inside the selection, -1 outside.

out vec2 v_TexCoord;

void main()
{
    vec2 uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    v_TexCoord = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}

#type fragment
#version 450 core

layout(location = 0) out vec4 o_Color;

in vec2 v_TexCoord;

uniform isampler2D u_IdMask;     // selection-filtered id buffer (-1 = outside)
uniform vec2       u_TexelSize;  // 1 / mask size
uniform vec4       u_Color;      // outline color (straight alpha)
uniform float      u_WidthPx;    // outline half-width in pixels

bool Inside(vec2 uv)
{
    return texture(u_IdMask, uv).r >= 0;
}

void main()
{
    // 4-tap silhouette: a pixel is on the edge when its inside/outside state
    // differs from any of its 4 neighbors at the outline width. The outline
    // grows OUTWARD (edge fragments outside the mask only) so the selected
    // surface itself stays untinted.
    if (Inside(v_TexCoord))
        discard;

    vec2 o = u_TexelSize * u_WidthPx;
    bool edge = Inside(v_TexCoord + vec2( o.x, 0.0)) ||
                Inside(v_TexCoord + vec2(-o.x, 0.0)) ||
                Inside(v_TexCoord + vec2(0.0,  o.y)) ||
                Inside(v_TexCoord + vec2(0.0, -o.y));
    // Diagonal taps fill the corners so the ring reads as one stroke.
    edge = edge ||
           Inside(v_TexCoord + vec2( o.x,  o.y)) ||
           Inside(v_TexCoord + vec2(-o.x,  o.y)) ||
           Inside(v_TexCoord + vec2( o.x, -o.y)) ||
           Inside(v_TexCoord + vec2(-o.x, -o.y));

    if (!edge)
        discard;
    o_Color = u_Color;
}
