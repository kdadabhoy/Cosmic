#type vertex
#version 450 core

// TerrainDepth — Phase 11 (doc 10 F4): the depth-only pass that makes TERRAIN
// a shadow caster (mountains shadow valleys at low sun — S8 shipped
// receive-only). The vertex displacement is a copy of Terrain.glsl's (same
// packed height texture, same per-node uniforms, same skirt drop) with the
// sun's ortho matrix instead of the camera block; Terrain::RenderDepth walks
// the SAME quadtree cut as the main draw so caster and receiver agree.
// KEEP THE DISPLACEMENT MATH IN SYNC WITH Terrain.glsl.

layout(location = 0) in vec3 a_Position;

uniform mat4 u_LightViewProj;

uniform sampler2D u_HeightMap;

uniform vec2  u_NodeOrigin;        // world XZ of the node's (0,0) corner
uniform float u_NodeSize;          // world size of the node
uniform vec2  u_NodeTexelOrigin;   // heightfield texel of the node's corner
uniform float u_NodeTexels;        // texels spanned by the node (power of two)

uniform float u_HeightScale;
uniform float u_BaseHeight;
uniform float u_SkirtDepth;

void main()
{
    ivec2 texel = ivec2(u_NodeTexelOrigin + a_Position.xz * u_NodeTexels + vec2(0.5));
    vec4  s     = texelFetch(u_HeightMap, texel, 0);

    float h01 = (s.r * 65280.0 + s.g * 255.0) / 65535.0;   // R = hi byte, G = lo byte

    vec2  xz = u_NodeOrigin + a_Position.xz * u_NodeSize;
    float y  = u_BaseHeight + h01 * u_HeightScale - a_Position.y * u_SkirtDepth;

    gl_Position = u_LightViewProj * vec4(xz.x, y, xz.y, 1.0);
}

#type fragment
#version 450 core

// Depth-only FBO — the color write is discarded (same note as ShadowDepth.glsl).
layout(location = 0) out vec4 color;

void main()
{
    color = vec4(1.0);
}
