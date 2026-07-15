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

// Curl-noise turbulence (X3) — mirror of ParticleEmitter::CurlNoise.
uniform int   u_NoiseEnabled;    // 1 = add the curl force
uniform float u_NoiseStrength;
uniform float u_NoiseFrequency;
uniform int   u_NoiseOctaves;    // already clamped 1..4 on upload

// Local-space bounds (X4) — half-extents about the emitter origin; <=0 axis =
// unbounded; all <=0 = off (byte-identical). Kill (0) or wrap (1) past the box.
uniform vec3  u_BoundsExtents;
uniform int   u_BoundsWrap;

const float PI = 3.14159265358979;

uint PcgHash(uint v)
{
    v = v * 747796405u + 2891336453u;
    v = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
    return (v >> 22u) ^ v;
}

// --- Curl-noise turbulence (X3). EXACT mirror of the CPU CurlNoise in
//     ParticleSystem.cpp: PcgHash-based value noise, the SAME four magic
//     constants + epsilon, so the GPU sim and the CPU preview agree. ---
float ValueLattice(int xi, int yi, int zi, uint seed)
{
    uint h = uint(xi) * 0x8DA6B343u
           ^ uint(yi) * 0xD8163841u
           ^ uint(zi) * 0xCB1AB31Fu
           ^ seed     * 0x165667B1u;
    return float(PcgHash(h)) * (1.0 / 4294967296.0);   // [0,1)
}

float ValueNoise3(vec3 p, uint seed)
{
    vec3  fp = floor(p);
    ivec3 ic = ivec3(fp);
    vec3  t  = p - fp;
    vec3  w  = t * t * (3.0 - 2.0 * t);

    float c000 = ValueLattice(ic.x,     ic.y,     ic.z,     seed);
    float c100 = ValueLattice(ic.x + 1, ic.y,     ic.z,     seed);
    float c010 = ValueLattice(ic.x,     ic.y + 1, ic.z,     seed);
    float c110 = ValueLattice(ic.x + 1, ic.y + 1, ic.z,     seed);
    float c001 = ValueLattice(ic.x,     ic.y,     ic.z + 1, seed);
    float c101 = ValueLattice(ic.x + 1, ic.y,     ic.z + 1, seed);
    float c011 = ValueLattice(ic.x,     ic.y + 1, ic.z + 1, seed);
    float c111 = ValueLattice(ic.x + 1, ic.y + 1, ic.z + 1, seed);

    float x00 = c000 + (c100 - c000) * w.x;
    float x10 = c010 + (c110 - c010) * w.x;
    float x01 = c001 + (c101 - c001) * w.x;
    float x11 = c011 + (c111 - c011) * w.x;
    float y0  = x00 + (x10 - x00) * w.y;
    float y1  = x01 + (x11 - x01) * w.y;
    return y0 + (y1 - y0) * w.z;
}

vec3 CurlPotential(vec3 q, int octaves)
{
    vec3  sum  = vec3(0.0);
    float amp  = 1.0;
    float freq = 1.0;
    for (int o = 0; o < octaves; ++o)
    {
        vec3 qo = q * freq;
        sum += amp * vec3(ValueNoise3(qo, 0u), ValueNoise3(qo, 1u), ValueNoise3(qo, 2u));
        amp  *= 0.5;
        freq *= 2.0;
    }
    return sum;
}

vec3 CurlNoise(vec3 pos, float frequency, int octaves)
{
    vec3  q = pos * frequency;
    float e = 0.1;   // == kCurlEpsilon in ParticleSystem.cpp
    vec3 dx = CurlPotential(q + vec3(e, 0.0, 0.0), octaves) - CurlPotential(q - vec3(e, 0.0, 0.0), octaves);
    vec3 dy = CurlPotential(q + vec3(0.0, e, 0.0), octaves) - CurlPotential(q - vec3(0.0, e, 0.0), octaves);
    vec3 dz = CurlPotential(q + vec3(0.0, 0.0, e), octaves) - CurlPotential(q - vec3(0.0, 0.0, e), octaves);
    float inv = 1.0 / (2.0 * e);
    return vec3(dy.z - dz.y, dz.x - dx.z, dx.y - dy.x) * inv;
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
        // Curl-noise turbulence (X3) — identical term in StepCpu. Disabled ⇒
        // skipped, so the shipped integration stays byte-identical.
        if (u_NoiseEnabled == 1)
            vel += CurlNoise(particles[slot].PosAge.xyz, u_NoiseFrequency, u_NoiseOctaves)
                 * u_NoiseStrength * u_Dt;
        vel *= max(1.0 - u_Drag * u_Dt, 0.0);

        vec3  newPos = particles[slot].PosAge.xyz + vel * u_Dt;
        float newAge = particles[slot].PosAge.w + u_Dt;

        // X4 — optional local-space bounds (kill or wrap). All-zero extents skip
        // this block, so the shipped integration stays byte-identical.
        vec3 ext = u_BoundsExtents;
        if (ext.x > 0.0 || ext.y > 0.0 || ext.z > 0.0)
        {
            vec3 org = (u_WorldSpace == 1) ? u_EmitterTransform[3].xyz : vec3(0.0);
            vec3 rel = newPos - org;
            if (u_BoundsWrap == 1)
            {
                if (ext.x > 0.0) rel.x -= 2.0 * ext.x * floor((rel.x + ext.x) / (2.0 * ext.x));
                if (ext.y > 0.0) rel.y -= 2.0 * ext.y * floor((rel.y + ext.y) / (2.0 * ext.y));
                if (ext.z > 0.0) rel.z -= 2.0 * ext.z * floor((rel.z + ext.z) / (2.0 * ext.z));
                newPos = org + rel;
            }
            else if ((ext.x > 0.0 && abs(rel.x) > ext.x) ||
                     (ext.y > 0.0 && abs(rel.y) > ext.y) ||
                     (ext.z > 0.0 && abs(rel.z) > ext.z))
            {
                newAge = particles[slot].VelLife.w;   // age >= life ⇒ dead
            }
        }

        particles[slot].PosAge  = vec4(newPos, newAge);
        particles[slot].VelLife = vec4(vel, particles[slot].VelLife.w);
    }
}
