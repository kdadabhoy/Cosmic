#type vertex
#version 450 core

// PrefilterEnv — bakes the prefiltered specular cubemap (S6.3): one mip level per
// roughness, GGX importance-sampled from the environment cube. PBR.glsl samples
// this with textureLod(prefilter, R, roughness * maxLod) for the specular ambient.

layout(location = 0) in vec3 a_Position;

uniform mat4 u_ViewProjection;

out vec3 v_LocalPos;

void main()
{
    v_LocalPos  = a_Position;
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;

in vec3 v_LocalPos;

uniform samplerCube u_EnvironmentMap;
uniform float       u_Roughness;
uniform float       u_Resolution;   // environment cube face size (for the mip-bias trick)

const float PI = 3.14159265359;

float RadicalInverseVdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 Hammersley(uint i, uint n) { return vec2(float(i) / float(n), RadicalInverseVdC(i)); }

vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float rough)
{
    float a = rough * rough;
    float phi      = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

    vec3 up      = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitan   = cross(N, tangent);
    return normalize(tangent * H.x + bitan * H.y + N * H.z);
}

float DistributionGGX(vec3 N, vec3 H, float rough)
{
    float a2  = rough * rough * rough * rough;
    float ndh = max(dot(N, H), 0.0);
    float d   = (ndh * ndh) * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-6);
}

void main()
{
    vec3 N = normalize(v_LocalPos);
    vec3 R = N;
    vec3 V = N;   // split-sum assumption: view == reflection == normal

    const uint SAMPLES = 256u;
    vec3  prefiltered = vec3(0.0);
    float totalWeight = 0.0;

    for (uint i = 0u; i < SAMPLES; ++i)
    {
        vec2 Xi = Hammersley(i, SAMPLES);
        vec3 H  = ImportanceSampleGGX(Xi, N, u_Roughness);
        vec3 L  = normalize(2.0 * dot(V, H) * H - V);

        float ndl = max(dot(N, L), 0.0);
        if (ndl <= 0.0)
            continue;

        // Sample a mip of the env cube proportional to the solid angle of the
        // sample (kills fireflies on rough mips).
        float ndh = max(dot(N, H), 0.0);
        float hdv = max(dot(H, V), 0.0);
        float D   = DistributionGGX(N, H, u_Roughness);
        float pdf = (D * ndh / (4.0 * hdv)) + 1e-4;

        float saTexel  = 4.0 * PI / (6.0 * u_Resolution * u_Resolution);
        float saSample = 1.0 / (float(SAMPLES) * pdf + 1e-4);
        float mipLevel = u_Roughness == 0.0 ? 0.0 : 0.5 * log2(saSample / saTexel);

        prefiltered += textureLod(u_EnvironmentMap, L, mipLevel).rgb * ndl;
        totalWeight += ndl;
    }

    prefiltered /= max(totalWeight, 1e-4);
    color = vec4(prefiltered, 1.0);
}
