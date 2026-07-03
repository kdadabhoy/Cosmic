#type vertex
#version 450 core

// IrradianceConvolve — bakes the diffuse irradiance cubemap from the environment
// cube (S6.3): for each output direction, integrate incoming radiance over the
// cosine-weighted hemisphere. Small (32^3) target — this is low-frequency data.

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

const float PI = 3.14159265359;

void main()
{
    vec3 N = normalize(v_LocalPos);

    // Tangent frame around N.
    vec3 up    = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up         = normalize(cross(N, right));

    vec3  irradiance = vec3(0.0);
    float sampleCount = 0.0;
    const float dPhi   = 0.025;
    const float dTheta = 0.025;

    for (float phi = 0.0; phi < 2.0 * PI; phi += dPhi)
    {
        for (float theta = 0.0; theta < 0.5 * PI; theta += dTheta)
        {
            // Spherical (tangent space) → world.
            vec3 tangentSample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            vec3 worldSample   = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;

            irradiance += texture(u_EnvironmentMap, worldSample).rgb * cos(theta) * sin(theta);
            sampleCount += 1.0;
        }
    }

    irradiance = PI * irradiance / max(sampleCount, 1.0);
    color = vec4(irradiance, 1.0);
}
