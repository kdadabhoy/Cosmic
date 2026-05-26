#type vertex
#version 450 core

// Cosmic Engine Renderer2D batch layout contract
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in float a_TexIndex;
layout(location = 4) in float a_TilingFactor;

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
#version 450 core

layout(location = 0) out vec4 color;

in vec4 v_Color;
in vec2 v_TexCoord;

// Declaring engine uniforms explicitly to bypass automatic injection loops
uniform float u_Time;
uniform vec4  u_Color;
uniform vec4  u_MountainColor;

// Simple deterministic hash for procedural star placement
float Hash(vec2 p) 
{
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453123);
}

// 1D Noise for generating smooth, rolling mountain ridges
float LinearNoise(float x) 
{
    float i = floor(x);
    float f = fract(x);
    float u = f * f * (3.0 - 2.0 * f);
    return mix(Hash(vec2(i, 0.0)), Hash(vec2(i + 1.0, 0.0)), u);
}

void main()
{
    // 1. Normalize coordinates (-1.0 to 1.0) and fix 16:9 aspect ratio
    vec2 uv = v_TexCoord * 2.0 - 1.0;
    uv.x *= 1.7777777;

    // 2. Deep Midnight Sky Gradient Background
    vec3 skyTop = vec3(0.02, 0.03, 0.08);
    vec3 skyBottom = u_Color.rgb;
    vec3 finalColor = mix(skyBottom, skyTop, clamp(uv.y * 0.5 + 0.5, 0.0, 1.0));

    // 3. Procedural Twinkling Stars
    vec2 starUv = uv * 12.0; 
    vec2 ipos = floor(starUv);
    vec2 fpos = fract(starUv);
    
    float starHash = Hash(ipos);
    if (starHash > 0.94) 
    {
        float twinkle = 0.4 + 0.6 * sin(u_Time * 1.5 + starHash * 6.28);
        vec2 targetPos = vec2(Hash(ipos + 1.0), Hash(ipos + 2.0)) * 0.8 + 0.1;
        float starDist = length(fpos - targetPos);
        float starGlow = smoothstep(0.06, 0.0, starDist);
        
        float starMask = smoothstep(-0.4, -0.2, uv.y);
        finalColor += vec3(0.9, 0.95, 1.0) * starGlow * twinkle * starMask;
    }

    // 4. Luminous Crescent Moon
    vec2 moonCenter = uv - vec2(0.8, 0.45); 
    float moonRadius = 0.22;
    
    float baseCircle = length(moonCenter);
    float moonMask = smoothstep(moonRadius, moonRadius - 0.005, baseCircle);
    
    float shadowCircle = length(moonCenter - vec2(-0.06, 0.04));
    float shadowMask = smoothstep(moonRadius - 0.01, moonRadius, shadowCircle);
    
    float crescent = clamp(moonMask * shadowMask, 0.0, 1.0);
    
    float moonGlow = smoothstep(0.8, 0.0, baseCircle);
    finalColor += vec3(0.95, 0.92, 0.82) * moonGlow * 0.28; 
    finalColor = mix(finalColor, vec3(0.98, 0.96, 0.88), crescent); 

    // 5. Parallax Mountain Ridges (Layered Silhouettes)
    // Read the background color directly from the new uniform vector
    vec3 backMountainColor = u_MountainColor.rgb;
    
    // Automatically derive a deeper, darker foreground shade for depth contrast
    vec3 foreMountainColor = u_MountainColor.rgb * 0.5; 

    // Far mountains (Slower movement)
    float backHeight = -0.1 + LinearNoise(uv.x * 1.2 + u_Time * 0.02) * 0.35;
    float backMask = smoothstep(backHeight, backHeight - 0.004, uv.y);
    finalColor = mix(finalColor, backMountainColor, backMask);

    // Near mountains (Faster movement)
    float foreHeight = -0.4 + LinearNoise(uv.x * 0.7 - u_Time * 0.05) * 0.30;
    float foreMask = smoothstep(foreHeight, foreHeight - 0.004, uv.y);
    finalColor = mix(finalColor, foreMountainColor, foreMask);

    // 6. Vignette
    vec2 d = v_TexCoord * (1.0 - v_TexCoord);
    float vignette = clamp(pow(d.x * d.y * 16.0, 0.25), 0.0, 1.0);
    finalColor *= vignette;

    color = vec4(finalColor, 1.0);
}