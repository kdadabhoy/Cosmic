#type vertex
#version 450 core

// ParticleBillboards — S10.1 billboard renderer. ATTRIBUTE-LESS: 6 vertices per
// particle from gl_VertexID over the engine's empty VAO; the pool SSBO
// (binding 8 = Bindings::ParticlesSsbo) is read directly in the vertex stage.
// Dead slots (age >= life) collapse to zero-area triangles — the fixed-count
// draw + GPU compaction/indirect-draw upgrade is the documented S10.1 deviation.

struct Particle
{
    vec4 PosAge;
    vec4 VelLife;
    vec4 SeedSize;
};

layout(std430, binding = 8) readonly buffer ParticlePool
{
    Particle particles[];
};

layout(std140, binding = 1) uniform CameraBlock
{
    mat4 ViewProjection;
    vec4 CameraPosition;
} u_Camera;

uniform vec3 u_CamRight;
uniform vec3 u_CamUp;
uniform vec2 u_SizeRange;          // x start, y end (over lifetime)
uniform vec4 u_ColorStart;
uniform vec4 u_ColorEnd;
uniform int  u_WorldSpace;         // 0 = positions are emitter-local
uniform mat4 u_EmitterTransform;

uniform vec2  u_FlipbookTiles;     // >= (1,1)
uniform float u_FlipbookFps;       // 0 = static random tile per particle

// Phase 11 (S11 / doc 10): stretch the billboard along its screen-space
// velocity — rain streaks, sparks. Length grows by speed * this factor
// (seconds); the quad's V axis follows the motion. GL default 0 = the shipped
// camera-facing quad, byte-identical.
uniform float u_StretchByVelocity;

out vec2  v_UV0;
out vec2  v_UV1;
out float v_FrameBlend;
out vec4  v_Color;
out vec3  v_WorldPos;
out vec4  v_ClipPos;
out vec2  v_FromCenter;            // corner in [-1, 1] (heat-haze direction)

vec2 TileUV(vec2 corner01, float frame, vec2 tiles)
{
    float total = tiles.x * tiles.y;
    float f     = mod(frame, total);
    vec2  tile  = vec2(mod(f, tiles.x), floor(f / tiles.x));
    return (corner01 + tile) / tiles;
}

void main()
{
    const vec2 corners[6] = vec2[6](
        vec2(-0.5, -0.5), vec2(0.5, -0.5), vec2(0.5, 0.5),
        vec2(-0.5, -0.5), vec2(0.5, 0.5),  vec2(-0.5, 0.5));

    int pid    = gl_VertexID / 6;
    int corner = gl_VertexID % 6;

    Particle p = particles[pid];
    float age  = p.PosAge.w;
    float life = max(p.VelLife.w, 1e-3);
    float t    = clamp(age / life, 0.0, 1.0);

    float alive = (age < life) ? 1.0 : 0.0;
    float size  = mix(u_SizeRange.x, u_SizeRange.y, t) * p.SeedSize.y * alive;

    vec3 center = p.PosAge.xyz;
    if (u_WorldSpace == 0)
        center = (u_EmitterTransform * vec4(center, 1.0)).xyz;

    vec2 ofs   = corners[corner];
    vec3 world = center + (u_CamRight * ofs.x + u_CamUp * ofs.y) * size;
    if (u_StretchByVelocity > 0.0)
    {
        vec3 vel = p.VelLife.xyz;
        if (u_WorldSpace == 0)
            vel = mat3(u_EmitterTransform) * vel;
        vec2  vS    = vec2(dot(vel, u_CamRight), dot(vel, u_CamUp));   // screen-plane velocity
        float speed = length(vS);
        if (speed > 1e-3)
        {
            vec2 dirS  = vS / speed;
            vec2 perpS = vec2(-dirS.y, dirS.x);
            vec3 axisAlong = u_CamRight * dirS.x  + u_CamUp * dirS.y;
            vec3 axisPerp  = u_CamRight * perpS.x + u_CamUp * perpS.y;
            float len = size + speed * u_StretchByVelocity;
            world = center + axisAlong * (ofs.y * len) + axisPerp * (ofs.x * size);
        }
    }

    // Flipbook frame selection (+ random per-particle offset so a shared sheet
    // doesn't strobe in sync).
    vec2  corner01 = ofs + 0.5;
    float total    = u_FlipbookTiles.x * u_FlipbookTiles.y;
    float frameF   = (u_FlipbookFps > 0.0)
        ? age * u_FlipbookFps + p.SeedSize.x * total
        : floor(p.SeedSize.x * total);
    v_UV0        = TileUV(corner01, floor(frameF), u_FlipbookTiles);
    v_UV1        = TileUV(corner01, floor(frameF) + 1.0, u_FlipbookTiles);
    v_FrameBlend = fract(frameF);

    v_Color      = mix(u_ColorStart, u_ColorEnd, t);
    v_WorldPos   = world;
    v_FromCenter = ofs * 2.0;
    v_ClipPos    = u_Camera.ViewProjection * vec4(world, 1.0);
    gl_Position  = v_ClipPos;
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;

in vec2  v_UV0;
in vec2  v_UV1;
in float v_FrameBlend;
in vec4  v_Color;
in vec3  v_WorldPos;
in vec4  v_ClipPos;
in vec2  v_FromCenter;

layout(std140, binding = 1) uniform CameraBlock
{
    mat4 ViewProjection;
    vec4 CameraPosition;
} u_Camera;

uniform sampler2D u_Texture;
uniform float     u_FlipbookBlend;   // 1 = crossfade v_UV0 -> v_UV1

// Soft particles (S10.1): fade where the billboard nears scene geometry.
uniform sampler2D u_SceneDepth;
uniform float     u_SoftFade;        // meters; 0 = off
uniform mat4      u_InvViewProj;

// Heat-haze mode (S10.5): instead of color, write a screen-space distortion
// vector (radial from the billboard center, scaled by coverage) into the
// PostProcessStack distortion target.
uniform float u_DistortionMode;

void main()
{
    vec4 tex = texture(u_Texture, v_UV0);
    if (u_FlipbookBlend > 0.5)
        tex = mix(tex, texture(u_Texture, v_UV1), v_FrameBlend);

    vec4 c = tex * v_Color;

    if (u_SoftFade > 0.0)
    {
        vec2  uv     = v_ClipPos.xy / v_ClipPos.w * 0.5 + 0.5;
        float sceneD = texture(u_SceneDepth, uv).r;
        if (sceneD < 1.0)
        {
            vec4 clip  = vec4(uv * 2.0 - 1.0, sceneD * 2.0 - 1.0, 1.0);
            vec4 world = u_InvViewProj * clip;
            world /= world.w;
            float dScene = length(world.xyz - u_Camera.CameraPosition.xyz);
            float dFrag  = length(v_WorldPos - u_Camera.CameraPosition.xyz);
            c.a *= clamp((dScene - dFrag) / u_SoftFade, 0.0, 1.0);
        }
    }

    if (u_DistortionMode > 0.5)
    {
        // Offset direction = radially outward from the puff center; strength =
        // coverage. The tonemap consumes this as a UV displacement field.
        color = vec4(v_FromCenter * c.a, 0.0, c.a);
        return;
    }

    color = c;
}
