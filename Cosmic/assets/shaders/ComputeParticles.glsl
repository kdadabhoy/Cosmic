#type compute
#version 450 core

// ComputeParticles — S4.7 GPU compute demo. One invocation per particle updates
// its position in a std430 SSBO (binding 0). Dispatched as ceil(N/256) groups.

layout(local_size_x = 256) in;

layout(std430, binding = 0) buffer Particles
{
    vec4 pos[];   // xyz = world position, w unused (padding to vec4/std430)
};

uniform float u_Time;
uniform int   u_Count;

void main()
{
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= uint(u_Count))
        return;

    float fi = float(gid);

    // A swirling orbit: radius/height/speed derived from the index so the cloud
    // has structure without any CPU-side per-particle state.
    float radius = 2.0 + mod(fi, 500.0) * 0.02;
    float speed  = 0.4 + fract(fi * 0.013) * 1.4;
    float ang    = u_Time * speed + fi * 0.15;
    float height = sin(u_Time * 0.7 + fi * 0.008) * 3.0;

    pos[gid] = vec4(cos(ang) * radius, height, sin(ang) * radius, 1.0);
}
