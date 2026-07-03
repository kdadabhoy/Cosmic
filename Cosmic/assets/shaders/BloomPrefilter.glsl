#type vertex
#version 450 core

// BloomPrefilter — extracts the bright (HDR > threshold) part of the scene with a
// soft knee (S6.6), the input to the blur chain. Emissive PBR materials and blown
// highlights bloom; everything below threshold is dropped smoothly (no hard edge).

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

uniform sampler2D u_Scene;
uniform float     u_Threshold;
uniform float     u_Knee;       // soft-knee width

void main()
{
    vec3  c  = texture(u_Scene, v_TexCoord).rgb;
    float br = max(c.r, max(c.g, c.b));

    // Soft-knee curve (Unity/Sledgehammer style).
    float knee = max(u_Knee, 1e-4);
    float soft = clamp(br - u_Threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee);
    float contrib = max(soft, br - u_Threshold) / max(br, 1e-4);

    color = vec4(c * contrib, 1.0);
}
