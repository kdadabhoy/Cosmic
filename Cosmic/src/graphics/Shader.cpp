#include "graphics/Shader.h"
#include "core/Log.h"
#include "renderer/RendererAPI.h"
#include "platform/opengl/OpenGLShader.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>

namespace Cosmic
{
	namespace
	{
		// Per thread, like every GL call: the reason the last Create() failed.
		thread_local std::string t_LastCreateError;

		bool EqualsNoCase(const std::string& a, const std::string& b)
		{
			if (a.size() != b.size())
				return false;
			for (size_t i = 0; i < a.size(); ++i)
				if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
					return false;
			return true;
		}

		// COSMIC_SHADER_OVERRIDE="<file name>=<path>[;<file name>=<path>...]" (see
		// Shader.h). Returns the replacement path for `filepath`, or "" for none.
		std::string OverrideFor(const std::string& filepath)
		{
#pragma warning(push)
#pragma warning(disable: 4996)   // std::getenv is fine here; no CRT state is retained
			const char* env = std::getenv("COSMIC_SHADER_OVERRIDE");
#pragma warning(pop)
			if (!env || !*env)
				return {};

			const std::string wanted = std::filesystem::path(filepath).filename().string();
			const std::string spec = env;
			size_t start = 0;
			while (start <= spec.size())
			{
				size_t end = spec.find(';', start);
				if (end == std::string::npos)
					end = spec.size();
				const std::string entry = spec.substr(start, end - start);
				const size_t eq = entry.find('=');
				if (eq != std::string::npos && eq > 0 && EqualsNoCase(entry.substr(0, eq), wanted))
					return entry.substr(eq + 1);
				start = end + 1;
			}
			return {};
		}
	}

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
	 * * FAILURE (UX-V0 / KI-83): nullptr, with ONE error line that names the path,
	 * the driver and the compiler's first error — every caller handles nullptr.
	 */
	Ref<Shader> Shader::Create(const std::string& filepath)
	{
		t_LastCreateError.clear();

		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:    return nullptr;
		case RendererAPI::API::OpenGL:
		{
			std::string loadPath = filepath;
			const std::string overridden = OverrideFor(filepath);
			if (!overridden.empty())
			{
				CS_CORE_WARN("Shader::Create: COSMIC_SHADER_OVERRIDE loads '{0}' in place of '{1}'.", overridden, filepath);
				loadPath = overridden;
			}

			auto shader = std::make_shared<OpenGLShader>(loadPath);
			if (!shader->IsValid())
			{
				const std::string shown = (loadPath == filepath)
					? "'" + filepath + "'"
					: "'" + filepath + "' (overridden to '" + loadPath + "')";
				t_LastCreateError = "shader " + shown + " failed to build on " + OpenGLShader::DescribeContext()
					+ ": " + shader->GetFailureReason();
				CS_CORE_ERROR("Shader::Create: {0}. Returning nullptr.", t_LastCreateError);
				return nullptr;
			}
			return shader;
		}
		}

		return nullptr;
	}

	const std::string& Shader::GetLastCreateError()
	{
		return t_LastCreateError;
	}

	/////////////////////////////////////////////////////////////////////////////////
}
