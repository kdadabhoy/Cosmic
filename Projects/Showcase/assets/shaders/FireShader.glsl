#type vertex
#version 330 core

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
#version 330 core

layout(location = 0) out vec4 color;

in vec4 v_Color;
in vec2 v_TexCoord;

uniform float u_Time;

// --- Procedural 3D Raymarched Flame by XT95/2013 ---
float noise(vec3 p)
{
    vec3 i = floor(p);
    vec4 a = dot(i, vec3(1., 57., 21.)) + vec4(0., 57., 21., 78.);
    vec3 f = cos((p-i)*acos(-1.))*(-.5)+.5;
    a = mix(sin(cos(a)*a), sin(cos(1.+a)*(1.+a)), f.x);
    a.xy = mix(a.xz, a.yw, f.y);
    return mix(a.x, a.y, f.z);
}

float sphere(vec3 p, vec4 spr)
{
    return length(spr.xyz-p) - spr.w;
}

float flame(vec3 p)
{
    float d = sphere(p*vec3(1.,.5,1.), vec4(.0,-1.,.0,1.));
    return d + (noise(p+vec3(.0, u_Time*2.,.0)) + noise(p*3.)*.5)*.25*(p.y) ;
}

float scene(vec3 p)
{
    return min(100.-length(p) , abs(flame(p)) );
}

vec4 raymarch(vec3 org, vec3 dir)
{
    float d = 0.0, glow = 0.0, eps = 0.02;
    vec3  p = org;
    bool glowed = false;
    
    for(int i=0; i<64; i++)
    {
        d = scene(p) + eps;
        p += d * dir;
        if( d>eps )
        {
            if(flame(p) < .0)
                glowed=true;
            if(glowed)
                   glow = float(i)/64.;
        }
    }
    return vec4(p,glow);
}

void main()
{
    // Convert 2D Quad texture space [0, 1] to centered coordinates [-1, 1]
    vec2 uv = v_TexCoord * 2.0 - 1.0;
    
    vec3 org = vec3(0., -2., 4.);
    
    // FIX: Removed the hardcoded 1.6 screen multiplier. 
    // Since the layout sprite entities are square quads, a 1.0 baseline ensures 
    // the raymarched camera projection treats local X and Y dimensions uniformly.
    vec3 dir = normalize(vec3(uv.x * 1.0, -uv.y, -1.5));
    
    vec4 p = raymarch(org, dir);
    float glow = p.w;
    
    vec4 col = mix(vec4(1.,.5,.1,1.), vec4(0.1,.5,1.,1.), p.y*.02+.4);
    vec4 finalColor = mix(vec4(0.), col, pow(glow*2.,4.));
    
    // Discard low alpha pixels to keep clean boundaries around the quads
    if (finalColor.a < 0.05)
        discard;
        
    color = finalColor * v_Color;
}