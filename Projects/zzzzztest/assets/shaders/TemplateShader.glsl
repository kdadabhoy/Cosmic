// TemplateShader.glsl
// A clean, documented example shader for TemplateProject.
// Demonstrates: u_Time animation, u_Color tint, v_TexCoord UV, Shadertoy-style math.
//
// Optimized via Fragment SDF: Specular highlights computed in-parallel 
// without relying on extra CPU draw submissions.
//
// The Cosmic preprocessor will inject any uniforms you omit — but we declare
// them all explicitly here to bypass injection and show the full contract.

#type vertex
#version 450 core

// --- Renderer2D batch vertex layout — do NOT reorder ---
layout(location = 0) in vec3  a_Position;
layout(location = 1) in vec4  a_Color;
layout(location = 2) in vec2  a_TexCoord;
layout(location = 3) in float a_TexIndex;
layout(location = 4) in float a_TilingFactor;

// Declared explicitly — preprocessor will not inject a duplicate
uniform mat4 u_ViewProjection;

out vec4 v_Color;
out vec2 v_TexCoord;

void main()
{
    v_Color     = a_Color;
    v_TexCoord  = a_TexCoord;
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}


#type fragment
#version 450 core

layout(location = 0) out vec4 color;

in vec4 v_Color;
in vec2 v_TexCoord;

// All engine uniforms declared — preprocessor skips injection for these
uniform float u_Time;
uniform vec4  u_Color;
uniform vec2  u_ViewportSize;

// -----------------------------------------------------------------------
// Signed distance function helpers
// -----------------------------------------------------------------------
float sdBox(vec2 p, vec2 b)
{
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

float sdCircle(vec2 p, float r)
{
    return length(p) - r;
}

// -----------------------------------------------------------------------
// Simple hash for noise
// -----------------------------------------------------------------------
float hash(vec2 p)
{
    p = fract(p * vec2(127.1, 311.7));
    p += dot(p, p + 19.19);
    return fract(p.x * p.y);
}

// -----------------------------------------------------------------------
// Fragment main — grid + animated SDF pattern + integrated Specular Dot
// -----------------------------------------------------------------------
void main()
{
    // Map UV from [0,1] to [-1,1] centered
    vec2 uv = v_TexCoord * 2.0 - 1.0;

    // --- Background grid ---
    vec2 gridUV  = v_TexCoord * 8.0;
    vec2 gridFrac = fract(gridUV);
    float lineX  = smoothstep(0.96, 1.0, gridFrac.x) + smoothstep(0.04, 0.0, gridFrac.x);
    float lineY  = smoothstep(0.96, 1.0, gridFrac.y) + smoothstep(0.04, 0.0, gridFrac.y);
    float grid   = clamp(lineX + lineY, 0.0, 1.0);
    vec3 bgColor = mix(vec3(0.06, 0.06, 0.09), u_Color.rgb * 0.35, grid * 0.5);

    // --- Animated inner pattern ---
    float t    = u_Time * 0.8;
    float rings = 0.0;

    for (int i = 0; i < 3; i++)
    {
        float fi    = float(i);
        float phase = t + fi * 2.094; // 2pi/3
        vec2  offset = vec2(cos(phase) * 0.25, sin(phase) * 0.25);
        float d      = sdCircle(uv - offset, 0.35 + fi * 0.08);
        rings += smoothstep(0.02, 0.0, abs(d) - 0.005);
    }

    // Central SDF box
    float angle  = t * 0.4;
    float c = cos(angle), s = sin(angle);
    vec2  rotUV  = vec2(c * uv.x - s * uv.y, s * uv.x + c * uv.y);
    float box    = sdBox(rotUV, vec2(0.28, 0.28));
    float boxFill = smoothstep(0.01,  0.0, box);
    float boxEdge = smoothstep(0.06,  0.0, abs(box) - 0.01);

    // --- 2x Optimization: Integrated Specular Dot Highlight ---
    // Instead of drawing an independent circle via the CPU Renderer API,
    // we define an offset coordinate mask on the object space UVs.
    vec2 specularOffset = vec2(-0.35, 0.35); // Upper-left faux light source
    float specRadius    = 0.12; 
    float specDist      = sdCircle(uv - specularOffset, specRadius);
    
    // Create a crisp, anti-aliased white dot masked inside the bounds of the box
    float specDot       = smoothstep(0.015, 0.0, specDist) * boxFill;

    // Compose layers
    vec3 ringColor = u_Color.rgb;
    vec3 boxColor  = mix(u_Color.rgb * 0.4, u_Color.rgb, boxFill);
    vec3 finalColor = bgColor;
    finalColor = mix(finalColor, ringColor, rings * 0.75);
    finalColor = mix(finalColor, boxColor,  boxEdge);
    finalColor += boxFill * u_Color.rgb * 0.15;
    
    // Blend the specular highlight layer on top of the composed artwork
    finalColor = mix(finalColor, vec3(1.0, 1.0, 1.0), specDot * 0.9);

    // Vignette
    float vign = 1.0 - dot(uv * 0.6, uv * 0.6);
    finalColor *= clamp(vign, 0.0, 1.0);

    color = vec4(finalColor, 1.0) * v_Color;
}