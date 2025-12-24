#include "graphics/Shader.h"
#include "graphics/RendererAPI.h"
#include "platform/opengl/OpenGLShader.h"
#include <memory>


namespace Cosmic
{
	std::shared_ptr<Shader> Shader::Create(const std::string& filepath)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:    return nullptr;
		case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLShader>(filepath);
		case RendererAPI::API::DirectX: return nullptr; // Return DirectXShader(filepath) here later
		}

		return nullptr;
	}
}