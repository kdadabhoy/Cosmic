#pragma once

#include <string>
#include <glm/glm.hpp>
#include <memory>

namespace Cosmic
{

	class Shader
	{
	public:
		virtual ~Shader() = default;

		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;

		virtual void SetMat4(const std::string& name, const glm::mat4& value) = 0;
		virtual void SetFloat4(const std::string& name, const glm::vec4& value) = 0;

		static std::shared_ptr<Shader> Create(const std::string& filepath);
	};

}