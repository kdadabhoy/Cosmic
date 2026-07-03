#type vertex
#version 450 core

// Tonemap — the S6.1 HDR resolve pass. A single fullscreen triangle generated
// from gl_VertexID (0,1,2) — no vertex buffer or attributes are bound; the
// engine issues it via RenderCommand::DrawArrays(Triangles, 0, 3) over its
// private empty VAO. The triangle over-covers the screen (NDC corners
// (-1,-1),(3,-1),(-1,3)); v_TexCoord runs 0..1 across the visible region.

out vec2 v_TexCoord;

void main()
{
    vec2 uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);   // (0,0),(2,0),(0,2)
    v_TexCoord = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}

#type fragment
#version 450 core

// Resolve the linear HDR scene (RGBA16F) to the LDR target: exposure scale,
// ACES filmic tonemap (Narkowicz approximation), then linear->sRGB gamma.
// This is where overbright (>1.0) values roll off on the ACES shoulder instead
// of hard-clipping to flat white. UI/2D composites AFTER this pass (contract 7).
//
// NOTE (documented, S12.6): the engine's authored colors are not yet converted
// sRGB->linear on input, so the final gamma here makes HDR-on look brighter/more
// filmic than HDR-off. That A/B difference is expected for the foundation; a full
// sRGB-correctness audit rides S12.6.
//
// PREPROCESSOR CONTRACT (OpenGLShader::PreProcess): the fragment output MUST be
// named `color` and the varying `v_TexCoord` — the engine preamble injector
// pattern-matches those exact strings and injects its own declarations when
// absent. A differently-named location-0 output used to collide with the
// injected one (duplicate location 0 = compile error). Every post shader in the
// S6 stack follows this convention.

layout(location = 0) out vec4 color;

in vec2 v_TexCoord;

uniform sampler2D u_Scene;      // HDR scene color (slot 0)
uniform float     u_Exposure;   // linear exposure multiplier (1.0 = neutral)

// Krzysztof Narkowicz's ACES filmic curve fit.
vec3 ACESFilmic(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec3 hdr    = texture(u_Scene, v_TexCoord).rgb;
    vec3 mapped = ACESFilmic(hdr * u_Exposure);
    mapped      = pow(mapped, vec3(1.0 / 2.2));   // linear -> sRGB
    color       = vec4(mapped, 1.0);
}
