#type compute
#version 450 core

// ParticleUpdate — S10.1 GPU particle simulation. One invocation per pool slot.
// Ring-buffer emission: slots inside [u_SpawnStart, u_SpawnStart + u_SpawnCount)
// (mod pool size) respawn; live slots integrate; dead slots idle.
//
// THIS SHADER MIRRORS ParticleEmitter::StepCpu — the CPU fallback path — keep
// the spawn/integration math identical when editing either. The PCG hash below
// is the same generator, so both paths draw the same distributions.
//
// Pool layout: 3 vec4 per particle (48 B, std430) on binding 8 =
// Bindings::ParticlesSsbo (renderer/BindingPoints.h).

layout(local_size_x = 256) in;

struct Particle
{
    vec4 PosAge;     // xyz position (world or local), w age; age >= life = dead
    vec4 VelLife;    // xyz velocity, w lifetime
    vec4 SeedSize;   // x rand01, y size jitter, zw reserved
};

layout(std430, binding = 8) buffer ParticlePool
{
    Particle particles[];
};

uniform float u_Dt;
uniform int   u_MaxParticles;
uniform int   u_SpawnStart;
uniform int   u_SpawnCount;
uniform int   u_FrameSeed;

uniform int   u_Shape;           // 0 Point, 1 Sphere, 2 Cone, 3 Box
uniform vec4  u_ShapeParams;     // x radius, y cone angle (radians)
uniform vec3  u_BoxExtents;
uniform vec2  u_SpeedRange;
uniform vec2  u_LifeRange;
uniform vec3  u_Gravity;
uniform vec3  u_Wind;
uniform float u_Drag;
uniform int   u_WorldSpace;      // 1 = spawn through the emitter transform
uniform mat4  u_EmitterTransform;

const float PI = 3.14159265358979;

uint PcgHash(uint v)
{
    v = v * 747796405u + 2891336453u;
    v = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
    return (v >> 22u) ^ v;
}

uint  g_State;
float Rand01() { g_State = PcgHash(g_State); return float(g_State) * (1.0 / 4294967296.0); }

vec3 RandomUnitSphere()
{
    float z   = Rand01() * 2.0 - 1.0;
    float phi = Rand01() * 2.0 * PI;
    float r   = sqrt(max(1.0 - z * z, 0.0));
    return vec3(r * cos(phi), z, r * sin(phi));
}

void main()
{
    uint slot = gl_GlobalInvocationID.x;
    if (slot >= uint(u_MaxParticles))
        return;

    uint n   = uint(u_MaxParticles);
    uint rel = (slot + n - (uint(u_SpawnStart) % n)) % n;

    if (rel < uint(u_SpawnCount))
    {
        // --- Respawn this slot (identical draws to StepCpu) ---
        g_State = PcgHash(uint(u_FrameSeed) * 9781u + slot * 6271u + 1u);

        vec3 localPos = vec3(0.0);
        vec3 localDir = vec3(0.0, 1.0, 0.0);

        if (u_Shape == 0)                       // Point
        {
            localDir = RandomUnitSphere();
        }
        else if (u_Shape == 1)                  // Sphere
        {
            localDir = RandomUnitSphere();
            localPos = localDir * (u_ShapeParams.x * pow(Rand01(), 1.0 / 3.0));
        }
        else if (u_Shape == 2)                  // Cone (axis +Y)
        {
            float cosMax = cos(u_ShapeParams.y);
            float cosT   = 1.0 + (cosMax - 1.0) * Rand01();
            float sinT   = sqrt(max(1.0 - cosT * cosT, 0.0));
            float phi    = Rand01() * 2.0 * PI;
            localDir = vec3(sinT * cos(phi), cosT, sinT * sin(phi));
            float discR   = u_ShapeParams.x * sqrt(Rand01());
            float discPhi = Rand01() * 2.0 * PI;
            localPos = vec3(discR * cos(discPhi), 0.0, discR * sin(discPhi));
        }
        else                                    // Box
        {
            localPos = (vec3(Rand01(), Rand01(), Rand01()) - 0.5) * u_BoxExtents;
        }

        vec3 pos = localPos;
        vec3 dir = localDir;
        if (u_WorldSpace == 1)
        {
            pos = (u_EmitterTransform * vec4(localPos, 1.0)).xyz;
            dir = normalize(mat3(u_EmitterTransform) * localDir);
        }

        float speed = mix(u_SpeedRange.x, u_SpeedRange.y, Rand01());
        float life  = max(mix(u_LifeRange.x, u_LifeRange.y, Rand01()), 1e-3);

        particles[slot].PosAge   = vec4(pos, 0.0);
        particles[slot].VelLife  = vec4(dir * speed, life);
        particles[slot].SeedSize = vec4(Rand01(), 0.75 + 0.5 * Rand01(), 0.0, 0.0);
    }
    else if (particles[slot].PosAge.w < particles[slot].VelLife.w)
    {
        // --- Integrate a live particle ---
        vec3 vel = particles[slot].VelLife.xyz;
        vel += (u_Gravity + u_Wind) * u_Dt;
        vel *= max(1.0 - u_Drag * u_Dt, 0.0);

        particles[slot].PosAge  = vec4(particles[slot].PosAge.xyz + vel * u_Dt,
                                       particles[slot].PosAge.w + u_Dt);
        particles[slot].VelLife = vec4(vel, particles[slot].VelLife.w);
    }
}
