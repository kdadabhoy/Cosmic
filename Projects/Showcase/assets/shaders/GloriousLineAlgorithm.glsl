#type vertex
#version 330 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in float a_TexIndex;
layout(location = 4) in float a_TilingFactor;

// Preprocessor Test 1: Implicit engine uniform detection
// The engine must scan this block and recognize that 'u_ViewProjection' is explicitly 
// declared here. It should NOT inject a duplicate uniform header declaration.
uniform mat4 u_ViewProjection;

out vec4 v_Color;
out vec2 v_TexCoord;

void main()
{
    v_Color = a_Color;
    v_TexCoord = a_TexCoord;
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}

#type fragment
#version 330 core

/*--------------------------------------------------------------------------------------
License CC0 - http://creativecommons.org/publicdomain/zero/1.0/
To the extent possible under law, the author(s) have dedicated all copyright and related and neighboring rights to this software to the public domain worldwide. This software is distributed without any warranty.
----------------------------------------------------------------------------------------
^ This means do ANYTHING YOU WANT with this code. Because we are programmers, not lawyers.
-Otavio Good

**************** Glorious Line Algorithm ****************
Optimized for multi-instance ECS layout grids. Bypasses raw screenspace hardware 
derivatives to prevent coordinate-space blowing out on sub-unit meshes.
*/

// Preprocessor Test 2: Substring isolation lookup logic safety validation
// The preprocessor must NOT match these names inside strings/comments and try to inject them.
// False positive flags to test: "u_Time", "u_ViewportSize", "u_ViewProjection"

#define saturate(a) clamp(a, 0.0, 1.0)

float repeat(float x) { return abs(fract(x*0.5+0.5)-0.5)*2.0; }

float LineDistField(vec2 uv, vec2 pA, vec2 pB, vec2 thick, float rounded, float dashOn) {
    rounded = min(thick.y, rounded);
    vec2 mid = (pB + pA) * 0.5;
    vec2 delta = pB - pA;
    float lenD = length(delta);
    vec2 unit = delta / lenD;
    if (lenD < 0.0001) unit = vec2(1.0, 0.0);
    vec2 perp = unit.yx * vec2(-1.0, 1.0);
    
    float dpx = dot(unit, uv - mid);
    float dpy = dot(perp, uv - mid);
    
    float disty = abs(dpy) - thick.y + rounded;
    float distx = abs(dpx) - lenD * 0.5 - thick.x + rounded;

    float dist = length(vec2(max(0.0, distx), max(0.0,disty))) - rounded;
    dist = min(dist, max(distx, disty));

    // Preprocessor Test 3: Macro token replacements
    // The engine's injection blocks should define 'iTime' mapping to 'u_Time'
    float dashScale = 2.0 * thick.y;
    float dash = (repeat(dpx / dashScale + iTime) - 0.5) * dashScale;
    dist = max(dist, dash - (1.0 - dashOn * 1.0) * 10000.0);

    return dist;
}

float FillLineFixed(vec2 uv, vec2 pA, vec2 pB, vec2 thick, float rounded) {
    float df = LineDistField(uv, pA, pB, thick, rounded, 0.0);
    return saturate(df / 0.08);
}

float FillLineDashFixed(vec2 uv, vec2 pA, vec2 pB, vec2 thick, float rounded) {
    float df = LineDistField(uv, pA, pB, thick, rounded, 1.0);
    return saturate(df / 0.08);
}

float DrawOutlineFixed(vec2 uv, vec2 pA, vec2 pB, vec2 thick, float rounded, float outlineThick) {
    float df = LineDistField(uv, pA, pB, thick, rounded, 0.0);
    return saturate((abs(df + outlineThick) - outlineThick) / 0.08);
}

void DrawPointFixed(vec2 uv, vec2 p, inout vec3 col) {
    col = mix(col, vec3(1.0, 0.25, 0.25), saturate(0.2 / distance(uv, p) - 2.0));
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    // Preprocessor Test 4: Implicit uniform extraction
    // Your engine preprocessor must parse 'u_ViewportSize', find no declaration, 
    // and inject: 'uniform vec2 u_ViewportSize;' in the auto-generated preamble.
    vec2 uv = fragCoord / u_ViewportSize;
    uv -= 0.5;
    uv *= 16.0;

    // Preprocessor Test 5: iTime token resolution usage validation
    vec2 rotA = vec2(cos(iTime*0.82), sin(iTime*0.82));
    vec2 rotB = vec2(sin(iTime*0.82), -cos(iTime*0.82));
    
    vec2 pA = vec2(-4.0, 0.0) - rotA;
    vec2 pB = vec2(4.0, 0.0) + rotA;
    vec2 pC = pA + vec2(0.0, 4.0);
    vec2 pD = pB + vec2(0.0, 4.0);

    vec3 finalColor = vec3(1.0);

    finalColor *= FillLineFixed(uv, pA, pB, vec2(0.1, 0.1), 0.0);
    finalColor *= DrawOutlineFixed(uv, pA, pB, vec2(2.0), 1.0, 0.08);
    finalColor *= DrawOutlineFixed(uv, pA, pB, vec2(4.0), 0.0, 0.08);
    finalColor *= DrawOutlineFixed(uv, pA, pB, vec2(6.0), 6.0, 0.4);
    
    finalColor *= FillLineDashFixed(uv, pC, pD, vec2(0.0, 0.3), 0.0);
    finalColor *= FillLineDashFixed(uv, pC + vec2(0.0, 2.0), pD + vec2(0.0, 2.0), vec2(0.125), 1.0);
    
    finalColor *= DrawOutlineFixed(uv, (pA + pB) * 0.5 + vec2(0.0, -4.5), (pA + pB) * 0.5 + vec2(0.0, -4.5), vec2(2.0, 2.0), 2.0, 0.4);
    finalColor *= FillLineFixed(uv, pA - vec2(4.0, 0.0), pC - vec2(4.0, 0.0) + rotA, vec2(0.125), 1.0);
    finalColor *= FillLineFixed(uv, pB + vec2(4.0, 0.0), pD + vec2(4.0, 0.0) - rotA, vec2(0.125), 1.0);

    DrawPointFixed(uv, pA, finalColor);
    DrawPointFixed(uv, pB, finalColor);
    DrawPointFixed(uv, pC, finalColor);
    DrawPointFixed(uv, pD, finalColor);

    finalColor -= vec3(1.0, 1.0, 0.2) * saturate(repeat(uv.x * 2.0) - 0.92) * 2.0;
    finalColor -= vec3(1.0, 1.0, 0.2) * saturate(repeat(uv.y * 2.0) - 0.92) * 2.0;

    // Preprocessor Test 6: Verify second macro resolution ('iResolution')
    // Ensuring vec3 translation layout rules evaluate successfully inside equations
    vec3 screenCheck = iResolution;
    if(screenCheck.x > 0.0) {
        fragColor = vec4(sqrt(saturate(finalColor)), 1.0);
    } else {
        fragColor = vec4(1.0, 0.0, 1.0, 1.0); // Fail safe magenta tint if iResolution drops out
    }
}