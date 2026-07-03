#type vertex
#version 450 core

// SnowAccum — Phase 11 (S11.1 accumulation mask v1 / doc 10 F8): the coverage
// UPDATE pass of the generic CoverageCapture system. A fullscreen triangle over
// the coverage mask target (RG16F, ping-pong pair): R accumulates coverage
// [0,1] at a drivable rate, G re-encodes the TOP-SURFACE height sampled from a
// fresh top-down orthographic depth capture (rendered with ShadowDepth /
// TerrainDepth / ShadowDepthInstanced — no new shaders). Material shaders
// (PBR / PBRInstanced / Terrain) sample the mask in world XZ and reject
// receivers below the encoded top surface, so covered floors stay bare.
//
// Standard post-shader contract: fragment output `color`, varying `v_TexCoord`.

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

uniform sampler2D u_PrevMask;      // previous RG coverage mask (ping-pong read)
uniform sampler2D u_TopDepth;      // fresh top-down ortho depth of the scene
uniform float     u_AccumRate;     // coverage/second while precipitating (>0)
uniform float     u_MeltRate;      // coverage/second while melting (>0)
uniform float     u_DeltaTime;     // seconds since the last update
uniform vec2      u_DepthToWorldY; // worldY = depth * x + y (from the ortho volume)
uniform vec2      u_WorldYEncode;  // encodedG = (worldY - y) * x  (x = 1/range, y = min)
uniform float     u_FirstFrame;    // 1 = ignore u_PrevMask (bootstrap clear)

void main()
{
    vec2 prev = (u_FirstFrame > 0.5) ? vec2(0.0, 0.0)
                                     : texture(u_PrevMask, v_TexCoord).rg;

    // Decode the current top surface under this texel. The ortho camera looks
    // straight DOWN: depth 0 = the volume top. A far-plane depth means no
    // geometry in the column — keep coverage but flag the column empty by
    // encoding the volume floor.
    float d      = texture(u_TopDepth, v_TexCoord).r;
    float worldY = d * u_DepthToWorldY.x + u_DepthToWorldY.y;

    float coverage = clamp(prev.x + (u_AccumRate - u_MeltRate) * u_DeltaTime, 0.0, 1.0);

    color = vec4(coverage, clamp((worldY - u_WorldYEncode.y) * u_WorldYEncode.x, 0.0, 1.0),
                 0.0, 1.0);
}
