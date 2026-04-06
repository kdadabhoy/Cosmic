#pragma once

#include "core/Core.h"
#include <string>
#include <glm/glm.hpp>

namespace Cosmic
{
	class Shader
	{
	public:
		virtual ~Shader() = default;

		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;

		// Setters for uniforms
		virtual void SetInt(const std::string& name, int value) = 0;
		virtual void SetMat4(const std::string& name, const glm::mat4& value) = 0;
		virtual void SetFloat4(const std::string& name, const glm::vec4& value) = 0;

		static Ref<Shader> Create(const std::string& filepath);
	};
}