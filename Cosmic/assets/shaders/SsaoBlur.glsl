#type vertex
#version 450 core

// SsaoBlur — a 4x4 box blur over the raw AO (S6.5), smoothing the noise-tile
// pattern the hemisphere rotation leaves behind.

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

uniform sampler2D u_Ssao;
uniform vec2      u_TexelSize;   // 1 / AO-target size

void main()
{
    float sum = 0.0;
    for (int x = -2; x < 2; ++x)
        for (int y = -2; y < 2; ++y)
            sum += texture(u_Ssao, v_TexCoord + vec2(x, y) * u_TexelSize).r;

    float ao = sum / 16.0;
    color = vec4(vec3(ao), 1.0);
}
