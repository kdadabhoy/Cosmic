#version 330 core

layout(location = 0) in vec4 position;

uniform mat4 u_MVP; // Name must match exactly (case-sensitive)

void main() {
    // If you don't multiply by u_MVP here, the compiler deletes the uniform!
    gl_Position = u_MVP * position; 
}