#pragma once

// Shader.h
// Last Modified 5/14/2026

/**
 * General Description:
 * 
 * Shader.h defines the abstract interface for GPU programs. In the Cosmic Engine,
 * a Shader represents a linked pair of Vertex and Fragment programs that
 * dictate how geometry is transformed and how pixels are colored.
 * * The class provides a unified API for "Uniform" communication—allowing the
 * CPU to send data (integers, vectors, matrices) directly to the GPU's registers.
 * 
 * 
 * Architecture Components:
 * 
 * 1. Shader (Interface): The abstract blueprint. High-level engine systems
 * interact with this class to remain agnostic of the underlying graphics
 * backend (OpenGL/DirectX).
 * 
 * 2. Uniform System: A suite of 'Set' functions that handle the transfer of
 * mathematical types (via GLM) to the shader programs.
 * 
 * 3. Factory Pattern: The 'Create' method handles the instantiation of
 * platform-specific shader implementations based on the active RendererAPI.
 * 
 * 
 * Public Function Prototypes (Pre and Post Conditions):
 * 
 * 
 * 1. virtual void Bind()
 * Pre:  The shader has been successfully compiled and linked.
 * Post: This shader program is activated in the GPU pipeline; subsequent
 * draw calls will use this shader's logic.
 * 
 * 2. virtual void Unbind()
 * Pre:  None.
 * Post: Deactivates the current shader program.
 * 
 * 3. virtual void Set[Type](const std::string& name, [Type] value)
 * Pre:  The shader must be bound (active) for reliable uniform upload.
 * 'name' must match the uniform variable name in the GLSL source.
 * Post: The value is uploaded to the GPU register associated with 'name'.
 * 
 * 4. static Ref<Shader> Create(const std::string& filepath)
 * Pre:  A valid path to a shader source file (containing both Vertex
 * and Fragment logic) is provided.
 * Post: Returns a reference-counted, platform-specific Shader instance.
 */

#include "core/Core.h"
#include <string>
#include <glm/glm.hpp>

namespace Cosmic
{
	class Shader
	{
	public:
		////////////////////////////////
		// Destructor
		///////////////////////////////
		virtual ~Shader() = default;

		////////////////////////////////
		// Pipeline State
		///////////////////////////////
		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;

		////////////////////////////////
		// Uniform Upload (CPU to GPU)
		///////////////////////////////
		virtual void SetInt(const std::string& name, int value) = 0;
		virtual void SetIntArray(const std::string& name, int* values, uint32_t count) = 0;
		virtual void SetFloat3(const std::string& name, const glm::vec3& value) = 0;
		virtual void SetFloat4(const std::string& name, const glm::vec4& value) = 0;
		virtual void SetMat3(const std::string& name, const glm::mat3& value) = 0;
		virtual void SetMat4(const std::string& name, const glm::mat4& value) = 0;

		////////////////////////////////
		// Factory Pattern
		///////////////////////////////
		static Ref<Shader> Create(const std::string& filepath);
	};
}