#type vertex
#version 450 core

// Infinite editor grid (Phase 22 / K10) — one fullscreen triangle; the plane
// intersection happens per fragment (the standard ray-plane trick), so the
// grid has no extent and never ends at a mesh boundary.

out vec2 v_Ndc;

void main()
{
    vec2 uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    v_Ndc = uv * 2.0 - 1.0;
    gl_Position = vec4(v_Ndc, 0.0, 1.0);
}

#type fragment
#version 450 core

layout(location = 0) out vec4 o_Color;

in vec2 v_Ndc;

uniform mat4  u_ViewProj;
uniform mat4  u_InvViewProj;
uniform vec3  u_CameraPos;
uniform float u_Height;        // world Y of the grid plane
uniform vec4  u_MinorColor;
uniform vec4  u_MajorColor;
uniform vec4  u_AxisXColor;    // the X axis line (z == 0)
uniform vec4  u_AxisZColor;    // the Z axis line (x == 0)
uniform float u_FadeDistance;  // 0 = auto from camera height

// Anti-aliased line mask for a square grid of `cell` metres at point p (world
// XZ). fwidth-based so lines stay ~1.2 px at any zoom.
float GridMask(vec2 p, float cell)
{
    vec2 q    = p / cell;
    vec2 dist = abs(fract(q - 0.5) - 0.5) / fwidth(q);
    return 1.0 - min(min(dist.x, dist.y) / 1.2, 1.0);
}

void main()
{
    // Unproject the pixel to a world ray (near -> far), intersect y = height.
    vec4 pn = u_InvViewProj * vec4(v_Ndc, -1.0, 1.0); pn /= pn.w;
    vec4 pf = u_InvViewProj * vec4(v_Ndc,  1.0, 1.0); pf /= pf.w;
    float denom = pf.y - pn.y;
    if (abs(denom) < 1e-9)
        discard;                              // ray parallel to the plane
    float t = (u_Height - pn.y) / denom;
    if (t <= 0.0 || t >= 1.0)
        discard;                              // behind the camera / past far
    vec3 world = mix(pn.xyz, pf.xyz, t);

    // Depth of the plane point so scene geometry occludes the grid correctly
    // (depth WRITES are off — the grid never occludes anything itself).
    vec4 clip = u_ViewProj * vec4(world, 1.0);
    gl_FragDepth = clamp((clip.z / clip.w) * 0.5 + 0.5, 0.0, 1.0);

    // Decade LOD: cell size steps x10 with distance; the finer level fades out
    // exactly as the coarser one takes over, so zooming never pops or shimmers.
    float dist = length(world - u_CameraPos);
    float lod  = max(log(dist * 0.08) / log(10.0), -3.0);   // ~12 cells across the view
    float lodF = floor(lod);
    float f    = fract(lod);
    float cell = pow(10.0, lodF);

    float minor = GridMask(world.xz, cell)        * (1.0 - f);
    float major = GridMask(world.xz, cell * 10.0);

    vec4 grid = u_MinorColor;
    grid.a *= minor;
    vec4 mj = u_MajorColor;
    mj.a *= major;
    if (mj.a > grid.a)
        grid = mj;

    // Axis lines (X axis lies along z == 0; Z axis along x == 0).
    vec2 axisPx = abs(world.xz) / fwidth(world.xz);
    if (axisPx.y < 1.4) { grid.rgb = u_AxisXColor.rgb; grid.a = max(grid.a, u_AxisXColor.a * (1.0 - axisPx.y / 1.4)); }
    if (axisPx.x < 1.4) { grid.rgb = u_AxisZColor.rgb; grid.a = max(grid.a, u_AxisZColor.a * (1.0 - axisPx.x / 1.4)); }

    // Distance fade: auto radius grows with camera height so the grid reads
    // from a 0.1 m close-up to a multi-km overview without a hard horizon.
    float radius = u_FadeDistance > 0.0
        ? u_FadeDistance
        : max(abs(u_CameraPos.y - u_Height), 2.0) * 60.0;
    grid.a *= 1.0 - smoothstep(radius * 0.45, radius, dist);

    if (grid.a < 0.004)
        discard;
    o_Color = grid;
}
