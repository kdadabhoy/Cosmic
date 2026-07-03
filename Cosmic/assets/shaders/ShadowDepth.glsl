#type vertex
#version 450 core

// ShadowDepth — the trivial depth-only pass for directional shadow mapping (S6.4).
// Renders shadow casters from the sun's point of view into the shadow map's depth
// attachment; the lit shaders (PBR / MeshLit) then compare against it. u_Model is
// per-caster; u_LightViewProj is the sun's ortho view-projection (ShadowMap).

layout(location = 0) in vec3 a_Position;

uniform mat4 u_LightViewProj;
uniform mat4 u_Model;

void main()
{
    gl_Position = u_LightViewProj * u_Model * vec4(a_Position, 1.0);
}

#type fragment
#version 450 core

// The shadow FBO is depth-only (no color attachment). We still declare a color
// output so the preprocessor doesn't inject a conflicting one; the write is
// discarded. Depth is written automatically.
layout(location = 0) out vec4 color;

void main()
{
    color = vec4(1.0);
}
