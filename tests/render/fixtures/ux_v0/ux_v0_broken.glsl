// ux_v0_broken.glsl - UX-V0 VM02 fixture (KI-83): a DELIBERATELY broken shader.
// The vertex stage is valid; the fragment stage reads an undeclared identifier,
// so every GLSL compiler rejects it with an error that names ux_v0_undeclared.
// render_ux_v0_shader_failure.cpp feeds it through the production loader:
// Shader::Create directly, and via COSMIC_SHADER_OVERRIDE in place of the
// engine's batch, line and circle shaders (in-process and in CosmicApp.exe).
// Not an engine shader; never shipped.
#type vertex
#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;

uniform mat4 u_ViewProjection;

out vec4 v_Color;

void main()
{
	v_Color = a_Color;
	gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;

in vec4 v_Color;

void main()
{
	color = v_Color * ux_v0_undeclared;
}
