#include "graphics/Material.h"

namespace Cosmic
{
	Ref<Material> Material::Create(const Ref<Shader>& shader, const std::string& name)
	{
		return std::make_shared<Material>(shader, name);
	}

	Material::Material(const Ref<Shader>& shader, const std::string& name)
		: m_Shader(shader), m_Name(name)
	{
	}

	void Material::Set(const std::string& name, float value) { m_Floats[name] = value; }
	void Material::Set(const std::string& name, const glm::vec3& value) { m_Float3s[name] = value; }
	void Material::Set(const std::string& name, const glm::vec4& value) { m_Float4s[name] = value; }
	void Material::Set(const std::string& name, const Ref<Texture>& texture) { m_Textures[name] = texture; }

	void Material::Bind()
	{
		m_Shader->Bind();

		for (auto const& [name, val] : m_Floats)
			m_Shader->SetInt(name, (int)val); // Assuming floats for now, adjust as needed

		for (auto const& [name, val] : m_Float3s)
			m_Shader->SetFloat3(name, val);

		for (auto const& [name, val] : m_Float4s)
			m_Shader->SetFloat4(name, val);

		// Note: Textures are usually bound by the Renderer2D's 
		// texture slot system, not directly here.
	}

	float Material::GetFloat(const std::string& name)
	{
		return m_Floats.count(name) ? m_Floats[name] : 0.0f;
	}

	glm::vec4 Material::GetVector(const std::string& name)
	{
		return m_Float4s.count(name) ? m_Float4s[name] : glm::vec4(1.0f);
	}

	Ref<Texture> Material::GetTexture(const std::string& name)
	{
		return m_Textures.count(name) ? m_Textures[name] : nullptr;
	}
}