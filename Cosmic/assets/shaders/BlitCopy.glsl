#type vertex
#version 450 core

// BlitCopy — a plain fullscreen copy pass (S9 refraction grab; any pass that
// needs "duplicate this attachment" without a blit verb). Fullscreen triangle
// from gl_VertexID, no vertex buffer (S6.1 idiom).

out vec2 v_TexCoord;

void main()
{
    vec2 uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    v_TexCoord = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;

in vec2 v_TexCoord;

uniform sampler2D u_Source;

void main()
{
    color = texture(u_Source, v_TexCoord);
}
