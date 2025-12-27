#pragma once

#include "graphics/Shader.h"
#include <glad/glad.h>

namespace Cosmic
{
	class OpenGLShader : public Shader
	{
	public:
		OpenGLShader(const std::string& filepath);
		virtual ~OpenGLShader();


		void						Bind() const												override;
		void						Unbind() const												override;

		void						SetMat4(const std::string& name, const glm::mat4& value)	override;
		void						SetFloat4(const std::string& name, const glm::vec4& value)	override;

		// OpenGL-specific uniform unloaders
		void						UploadUniformMat4(const std::string& name, const glm::mat4& matrix);
		void						UploadUniformFloat4(const std::string& name, const glm::vec4& values);


	private:
		std::string										ReadFile(const std::string& filepath);
		std::unordered_map<GLenum, std::string>			PreProcess(const std::string& source);
		void											Compile(const std::unordered_map<GLenum, std::string>& shaderSources);


	private:
		uint32_t		m_RendererID;
	};
}