#type vertex
#version 450 core

// BloomBlur — separable 9-tap Gaussian (S6.6). Ping-ponged horizontal↔vertical
// over the half-res bright buffer to spread the glow. Stable (no per-frame RNG),
// so it doesn't flicker while the camera orbits.

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

uniform sampler2D u_Image;
uniform vec2      u_TexelSize;   // 1 / image size
uniform float     u_Horizontal;  // >0.5 = horizontal pass

const float weight[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

void main()
{
    vec3 result = texture(u_Image, v_TexCoord).rgb * weight[0];
    vec2 dir = (u_Horizontal > 0.5) ? vec2(u_TexelSize.x, 0.0) : vec2(0.0, u_TexelSize.y);

    for (int i = 1; i < 5; ++i)
    {
        result += texture(u_Image, v_TexCoord + dir * float(i)).rgb * weight[i];
        result += texture(u_Image, v_TexCoord - dir * float(i)).rgb * weight[i];
    }

    color = vec4(result, 1.0);
}
