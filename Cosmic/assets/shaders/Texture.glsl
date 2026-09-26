// Texture.glsl - the Renderer2D batch quad shader: 32 texture slots (slot 0 = the
// 1x1 white texture), colour * texture(slot, uv * tiling).
//
// KI-82: GLSL 1.30-3.30 allow a sampler array to be indexed only by a constant
// expression, and GLSL 4.00+ by a dynamically uniform one; a batch mixes slots
// per quad, so neither holds for u_Textures[int(v_TexIndex)] (Mesa rejects it;
// other drivers leave it undefined). The slot therefore travels as a flat int and
// the fetch is a switch over literal indices. Per fragment the maths is exactly
// the old one: same sampler, same UV * tiling, same colour multiply. Keep the
// 32-slot contract in step with Renderer2D's MaxTextureSlots;
// tests/check_gl_conformance.ps1 rejects any non-literal sampler-array index.
#type vertex
#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in float a_TexIndex;
layout(location = 4) in float a_TilingFactor;

uniform mat4 u_ViewProjection;

out vec4 v_Color;
out vec2 v_TexCoord;
flat out int v_TexIndex;
out float v_TilingFactor;

void main()
{
	v_Color = a_Color;
	v_TexCoord = a_TexCoord;
	v_TexIndex = int(a_TexIndex);
	v_TilingFactor = a_TilingFactor;
	gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;

in vec4 v_Color;
in vec2 v_TexCoord;
flat in int v_TexIndex;
in float v_TilingFactor;

uniform sampler2D u_Textures[32];

vec4 SampleSlot(int slot, vec2 uv)
{
	switch (slot)
	{
		case  0: return texture(u_Textures[ 0], uv);
		case  1: return texture(u_Textures[ 1], uv);
		case  2: return texture(u_Textures[ 2], uv);
		case  3: return texture(u_Textures[ 3], uv);
		case  4: return texture(u_Textures[ 4], uv);
		case  5: return texture(u_Textures[ 5], uv);
		case  6: return texture(u_Textures[ 6], uv);
		case  7: return texture(u_Textures[ 7], uv);
		case  8: return texture(u_Textures[ 8], uv);
		case  9: return texture(u_Textures[ 9], uv);
		case 10: return texture(u_Textures[10], uv);
		case 11: return texture(u_Textures[11], uv);
		case 12: return texture(u_Textures[12], uv);
		case 13: return texture(u_Textures[13], uv);
		case 14: return texture(u_Textures[14], uv);
		case 15: return texture(u_Textures[15], uv);
		case 16: return texture(u_Textures[16], uv);
		case 17: return texture(u_Textures[17], uv);
		case 18: return texture(u_Textures[18], uv);
		case 19: return texture(u_Textures[19], uv);
		case 20: return texture(u_Textures[20], uv);
		case 21: return texture(u_Textures[21], uv);
		case 22: return texture(u_Textures[22], uv);
		case 23: return texture(u_Textures[23], uv);
		case 24: return texture(u_Textures[24], uv);
		case 25: return texture(u_Textures[25], uv);
		case 26: return texture(u_Textures[26], uv);
		case 27: return texture(u_Textures[27], uv);
		case 28: return texture(u_Textures[28], uv);
		case 29: return texture(u_Textures[29], uv);
		case 30: return texture(u_Textures[30], uv);
		case 31: return texture(u_Textures[31], uv);
	}
	return vec4(1.0); // out of range (Renderer2D never emits one): colour only
}

void main()
{
	color = SampleSlot(v_TexIndex, v_TexCoord * v_TilingFactor) * v_Color;
}
