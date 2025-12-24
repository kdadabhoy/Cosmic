#version 330 core
layout(location = 0) in vec3 a_Position;

uniform mat4 u_ViewProjection; // Uploaded by Renderer::BeginScene
uniform mat4 u_Transform;      // Uploaded by Renderer::Submit

void main() {
    gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);
}