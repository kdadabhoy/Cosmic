#include "graphics/Shader.h"
#include "core/Log.h"
#include "renderer/RendererAPI.h"
#include "platform/opengl/OpenGLShader.h"

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Shader::Create
	 * * Static factory method to instantiate a platform-specific Shader.
	 * * ROLE IN PIPELINE: Shaders are the most critical programmable stage of the
	 * graphics pipeline. This method ensures that the engine can load a single
	 * ".glsl" file and internally handle the platform-specific compilation and
	 * linking logic.
	 * * API ABSTRACTION: By checking RendererAPI::GetAPI(), the engine determines
	 * whether to return an OpenGLShader or another backend implementation.
	 * This allows the Renderer to load assets without knowing the underlying
	 * hardware driver details.
	 */
	Ref<Shader> Shader::Create(const std::string& filepath)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:    return nullptr;
		case RendererAPI::API::OpenGL:
		{
			auto shader = std::make_shared<OpenGLShader>(filepath);
			if (!shader->IsValid())
			{
				CS_CORE_ERROR("Shader::Create: compilation or link failure for '{0}'. Returning nullptr.", filepath);
				return nullptr;
			}
			return shader;
		}
		}

		return nullptr;
	}

	/////////////////////////////////////////////////////////////////////////////////
}