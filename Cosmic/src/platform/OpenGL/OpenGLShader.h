#pragma once

// OpenGLShader.h
// Last Modified 5/21/2026

/**
 * General Description:
 * 
 * OpenGLShader is the concrete implementation of the Shader interface for the
 * OpenGL graphics backend. It manages the full lifecycle of a GPU program, including
 * reading source files from disk, preprocessing multi-stage shaders from a single
 * file, and runtime compilation/linking.
 * 
 * This class provides a bridge for uploading uniform data from the CPU to the GPU
 * registers, supporting various data types through GLM. It utilizes a custom
 * "#type" directive in shader files to distinguish between vertex and fragment
 * source code within the same asset.
 * 
 * 
 * Public Function Prototypes (Pre and Post Conditions):
 * 
 * 1. OpenGLShader(const std::string& filepath)
 * Pre:  The filepath points to a valid .glsl file containing #type directives.
 * Post: The shader is parsed, compiled, and linked into a valid GPU program ID.
 * 
 * 2. virtual ~OpenGLShader()
 * Pre:  The OpenGLShader instance exists.
 * Post: The associated GPU program is deleted via glDeleteProgram.
 * 
 * 3. void Bind()
 * Pre:  A valid GPU program exists.
 * Post: The shader program is activated in the OpenGL state machine for drawing.
 * 
 * 4. void Unbind()
 * Pre:  None.
 * Post: The current OpenGL shader program is set to 0.
 * 
 * 5. void SetInt / SetIntArray / SetFloat3 / SetFloat4 / SetMat3 / SetMat4
 * Pre:  The shader must be bound (active) for reliable uniform uploads.
 * Post: Data is uploaded to the specified uniform location in the shader.
 * 
 * 6. UploadUniform[Type](...)
 * Pre:  The uniform name must exist within the compiled shader source.
 * Post: Directly interacts with glGetUniformLocation and glUniform to modify
 * GPU constants.
 */

#include <glad/glad.h>
#include "graphics/Shader.h"
#include <unordered_map>

namespace Cosmic
{
	class OpenGLShader : public Shader
	{
	public:
		////////////////////////////////
		// Life Cycle & Initialization
		///////////////////////////////

		OpenGLShader(const std::string& filepath);
		virtual ~OpenGLShader();

		////////////////////////////////
		// State Management
		///////////////////////////////

		virtual void		Bind() const override;
		virtual void		Unbind() const override;

		////////////////////////////////
		// Virtual Uniform API (Abstraction)
		///////////////////////////////

		virtual void		SetInt(const std::string& name, int value) override;
		virtual void		SetIntArray(const std::string& name, int* values, uint32_t count) override;

		virtual void		SetFloat(const std::string& name, float value) override;
		virtual void		SetFloat2(const std::string& name, const glm::vec2& value) override;
		virtual void		SetFloat3(const std::string& name, const glm::vec3& value) override;
		virtual void		SetFloat4(const std::string& name, const glm::vec4& value) override;

		virtual void		SetMat3(const std::string& name, const glm::mat3& value) override;
		virtual void		SetMat4(const std::string& name, const glm::mat4& value) override;


		////////////////////////////////
		// Native OpenGL Uniform Uploaders
		///////////////////////////////

		void				UploadUniformInt(const std::string& name, int value);
		void				UploadUniformIntArray(const std::string& name, int* values, uint32_t count);

		void				UploadUniformFloat(const std::string& name, float value);
		void				UploadUniformFloat2(const std::string& name, const glm::vec2& values);
		void				UploadUniformFloat3(const std::string& name, const glm::vec3& values);
		void				UploadUniformFloat4(const std::string& name, const glm::vec4& values);

		void				UploadUniformMat3(const std::string& name, const glm::mat3& matrix);
		void				UploadUniformMat4(const std::string& name, const glm::mat4& matrix);

	private:
		////////////////////////////////
		// Internal Build Pipeline
		///////////////////////////////

		std::string									ReadFile(const std::string& filepath);
		std::unordered_map<GLenum, std::string>		PreProcess(const std::string& source);
		void										Compile(const std::unordered_map<GLenum, std::string>& shaderSources);


	private:
		////////////////////////////////
		// GPU Resource Handles
		///////////////////////////////

		uint32_t			m_RendererID;
		std::string         m_Name; // Added to store shader identity safely for tracking and log systems

	private: 
		std::unordered_map<std::string, GLint>	m_UniformLocationCache;
		GLint GetUniformLocation(const std::string& name);



	};
}