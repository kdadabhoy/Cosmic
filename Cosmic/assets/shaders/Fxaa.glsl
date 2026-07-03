#type vertex
#version 450 core

// FXAA — fast approximate anti-aliasing (S6.7), the final post pass. Runs on the
// LDR/gamma tonemap output (FXAA wants perceptual luma), reading a full-res LDR
// intermediate and resolving into the viewport. The classic console-version edge
// blend — cheap, no motion vectors, no history.

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

float Luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main()
{
    vec2 uv = v_TexCoord;
    vec2 t  = u_TexelSize;

    vec3 rgbM  = texture(u_Image, uv).rgb;
    float lM   = Luma(rgbM);
    float lTL  = Luma(texture(u_Image, uv + vec2(-t.x, -t.y)).rgb);
    float lTR  = Luma(texture(u_Image, uv + vec2( t.x, -t.y)).rgb);
    float lBL  = Luma(texture(u_Image, uv + vec2(-t.x,  t.y)).rgb);
    float lBR  = Luma(texture(u_Image, uv + vec2( t.x,  t.y)).rgb);

    float lMin = min(lM, min(min(lTL, lTR), min(lBL, lBR)));
    float lMax = max(lM, max(max(lTL, lTR), max(lBL, lBR)));

    // No contrast → nothing to smooth.
    if (lMax - lMin < 0.05 * lMax)
    {
        color = vec4(rgbM, 1.0);
        return;
    }

    vec2 dir;
    dir.x = -((lTL + lTR) - (lBL + lBR));
    dir.y =  ((lTL + lBL) - (lTR + lBR));

    const float REDUCE_MUL = 1.0 / 8.0;
    const float REDUCE_MIN = 1.0 / 128.0;
    const float SPAN_MAX   = 8.0;

    float reduce = max((lTL + lTR + lBL + lBR) * 0.25 * REDUCE_MUL, REDUCE_MIN);
    float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcpMin, vec2(-SPAN_MAX), vec2(SPAN_MAX)) * t;

    vec3 rgbA = 0.5 * (texture(u_Image, uv + dir * (1.0 / 3.0 - 0.5)).rgb +
                       texture(u_Image, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(u_Image, uv + dir * -0.5).rgb +
                                     texture(u_Image, uv + dir *  0.5).rgb);

    float lB = Luma(rgbB);
    color = vec4((lB < lMin || lB > lMax) ? rgbA : rgbB, 1.0);
}
