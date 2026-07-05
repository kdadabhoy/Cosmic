#type vertex
#version 450 core

// EquirectToCube — projects a floating-point equirectangular (lat-long) HDR image
// onto the six faces of the environment cubemap (H4). Same cube-render shape as
// EnvSky.glsl: u_ViewProjection is the per-face capture view-projection; the
// fragment turns the interpolated local cube position into a world direction and
// samples the equirect source. The result feeds the SAME irradiance/prefilter
// chain, so HDRI lighting + the skybox agree exactly like the procedural sky.

layout(location = 0) in vec3 a_Position;

uniform mat4 u_ViewProjection;

out vec3 v_LocalPos;

void main()
{
    v_LocalPos  = a_Position;
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}

#type fragment
#version 450 core

layout(location = 0) out vec4 color;

in vec3 v_LocalPos;

uniform sampler2D u_Equirect;     // linear RGBA16F equirect source
uniform float     u_SkyIntensity; // overall HDR multiplier (matches EnvSky)

// Direction -> lat-long UV (the standard spherical map).
const vec2 kInvAtan = vec2(0.1591, 0.3183);   // (1/2π, 1/π)
vec2 SampleSphericalMap(vec3 v)
{
    vec2 uv = vec2(atan(v.z, v.x), asin(clamp(v.y, -1.0, 1.0)));
    uv *= kInvAtan;
    uv += 0.5;
    return uv;
}

void main()
{
    vec2 uv  = SampleSphericalMap(normalize(v_LocalPos));
    vec3 hdr = texture(u_Equirect, uv).rgb;
    color    = vec4(hdr * u_SkyIntensity, 1.0);
}
