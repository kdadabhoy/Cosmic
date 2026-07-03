#type vertex
#version 450 core

// BrdfLut — bakes the split-sum BRDF integration LUT (S6.3), a 2D texture indexed
// by (N·V, roughness). PBR.glsl reads .rg as (scale, bias) for the IBL specular
// term. One fullscreen-triangle pass into an RGBA16F target (we only use RG).

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
    vec3  H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

    vec3 up      = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitan   = cross(N, tangent);
    return normalize(tangent * H.x + bitan * H.y + N * H.z);
}

float GeometrySchlickGGX(float ndv, float rough)
{
    float k = (rough * rough) / 2.0;   // IBL geometry k
    return ndv / (ndv * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float rough)
{
    return GeometrySchlickGGX(max(dot(N, V), 0.0), rough) *
           GeometrySchlickGGX(max(dot(N, L), 0.0), rough);
}

vec2 IntegrateBRDF(float ndv, float rough)
{
    vec3 V = vec3(sqrt(1.0 - ndv * ndv), 0.0, ndv);
    vec3 N = vec3(0.0, 0.0, 1.0);

    float A = 0.0, B = 0.0;
    const uint SAMPLES = 1024u;
    for (uint i = 0u; i < SAMPLES; ++i)
    {
        vec2 Xi = Hammersley(i, SAMPLES);
        vec3 H  = ImportanceSampleGGX(Xi, N, rough);
        vec3 L  = normalize(2.0 * dot(V, H) * H - V);

        float ndl = max(L.z, 0.0);
        float ndh = max(H.z, 0.0);
        float hdv = max(dot(H, V), 0.0);
        if (ndl <= 0.0)
            continue;

        float G     = GeometrySmith(N, V, L, rough);
        float gVis  = (G * hdv) / (ndh * ndv + 1e-4);
        float Fc    = pow(1.0 - hdv, 5.0);
        A += (1.0 - Fc) * gVis;
        B += Fc * gVis;
    }
    return vec2(A, B) / float(SAMPLES);
}

void main()
{
    vec2 integrated = IntegrateBRDF(max(v_TexCoord.x, 1e-3), v_TexCoord.y);
    color = vec4(integrated, 0.0, 1.0);
}
