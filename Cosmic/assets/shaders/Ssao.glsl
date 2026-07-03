#type vertex
#version 450 core

// SSAO — screen-space ambient occlusion, reconstruct-from-depth (S6.5). Runs as a
// fullscreen post pass over the HDR scene's depth attachment (no G-buffer): it
// rebuilds view-space position from depth, derives a faceted normal from screen
// derivatives, and estimates occlusion with a hemisphere kernel + rotation noise.
// Output is a single AO factor (1 = open, 0 = fully occluded) in every channel.

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

uniform sampler2D u_Depth;         // HDR scene depth attachment
uniform sampler2D u_Noise;         // 4x4 tiling rotation noise (RGB, [0,1])
uniform mat4      u_Projection;
uniform mat4      u_InvProjection;
uniform vec2      u_NoiseScale;    // screenSize / noiseTexSize
uniform float     u_Radius;
uniform float     u_Bias;

const int MAX_KERNEL = 64;
uniform vec3 u_Kernel[MAX_KERNEL];
uniform int  u_KernelSize;

vec3 ViewPosFromDepth(vec2 uv)
{
    float d = texture(u_Depth, uv).r;
    vec4 clip = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec4 view = u_InvProjection * clip;
    return view.xyz / view.w;
}

void main()
{
    float rawDepth = texture(u_Depth, v_TexCoord).r;
    if (rawDepth >= 0.9999)   // background / far plane — fully open
    {
        color = vec4(1.0);
        return;
    }

    vec3 fragPos = ViewPosFromDepth(v_TexCoord);
    vec3 normal  = normalize(cross(dFdx(fragPos), dFdy(fragPos)));

    vec3 randomVec = normalize(texture(u_Noise, v_TexCoord * u_NoiseScale).xyz * 2.0 - 1.0);
    vec3 tangent   = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN       = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    int   count     = min(u_KernelSize, MAX_KERNEL);
    for (int i = 0; i < count; ++i)
    {
        vec3 samplePos = fragPos + TBN * u_Kernel[i] * u_Radius;

        vec4 offset = u_Projection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xyz  = offset.xyz * 0.5 + 0.5;
        if (offset.x < 0.0 || offset.x > 1.0 || offset.y < 0.0 || offset.y > 1.0)
            continue;

        float sampleZ    = ViewPosFromDepth(offset.xy).z;
        float rangeCheck = smoothstep(0.0, 1.0, u_Radius / max(abs(fragPos.z - sampleZ), 1e-4));
        occlusion += (sampleZ >= samplePos.z + u_Bias ? 1.0 : 0.0) * rangeCheck;
    }

    float ao = 1.0 - occlusion / float(max(count, 1));
    color = vec4(vec3(ao), 1.0);
}
