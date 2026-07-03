#type vertex
#version 450 core

// GodRays — S10.3 tier 1: raymarched sun shafts against the S6.4 shadow map.
// For every (half-res) pixel, march from the camera toward the scene point and
// accumulate the fraction of steps the sun can see, weighted by a forward
// scattering phase — light pouring through gaps casts visible beams.
// The froxel fog grid (clustered volumetrics) is the documented follow-up tier.

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

uniform sampler2D u_Depth;          // scene depth
uniform sampler2D u_ShadowMap;      // sun shadow map (S6.4)
uniform mat4      u_InvViewProj;
uniform vec3      u_CameraPos;
uniform mat4      u_LightViewProj;
uniform vec3      u_SunDir;         // direction the sun light TRAVELS
uniform vec3      u_SunColor;       // color * intensity
uniform float     u_Intensity;
uniform float     u_Density;        // participating-media density (1/m)

const int   kSteps       = 24;
const float kMaxDistance = 120.0;   // meters of media considered

float LitAt(vec3 worldPos)
{
    vec4 lp   = u_LightViewProj * vec4(worldPos, 1.0);
    vec3 proj = lp.xyz / lp.w * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 1.0;                                  // outside the map: lit
    return (proj.z - 0.002 > texture(u_ShadowMap, proj.xy).r) ? 0.0 : 1.0;
}

void main()
{
    float d = texture(u_Depth, v_TexCoord).r;
    vec4 clip  = vec4(v_TexCoord * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec4 world = u_InvViewProj * clip;
    world /= world.w;

    vec3  ray    = world.xyz - u_CameraPos;
    float rayLen = min(length(ray), kMaxDistance);
    vec3  rd     = normalize(ray);

    // Interleaved dither breaks the step banding into noise the half-res
    // upsample smooths out (stable per-pixel hash — no per-frame flicker).
    float dither = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);

    float stepLen = rayLen / float(kSteps);
    vec3  pos     = u_CameraPos + rd * stepLen * dither;
    float sunVis  = 0.0;
    for (int i = 0; i < kSteps; ++i)
    {
        sunVis += LitAt(pos);
        pos    += rd * stepLen;
    }
    sunVis /= float(kSteps);

    // Forward-scattering phase: shafts bloom looking toward the sun.
    float phase   = 0.15 + 0.85 * pow(max(dot(rd, -normalize(u_SunDir)), 0.0), 8.0);
    float media   = 1.0 - exp(-rayLen * u_Density);

    color = vec4(u_SunColor * (sunVis * phase * media * u_Intensity), 1.0);
}
