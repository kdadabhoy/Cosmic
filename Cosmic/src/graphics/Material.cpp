#include "graphics/Material.h"

namespace Cosmic
{
	Ref<Material> Material::Create(const Ref<Shader>& shader, const std::string& name)
	{
		return std::make_shared<Material>(PrivateTag{}, shader, name);
	}

	Material::Material(PrivateTag, const Ref<Shader>& shader, const std::string& name)
		: m_Shader(shader), m_Name(name)
	{
	}

	Ref<Material> Material::Clone(const Ref<Material>& source, const std::string& newName)
	{
		auto copy = std::make_shared<Material>(PrivateTag{}, source->m_Shader, newName);
		copy->m_Floats = source->m_Floats;
		copy->m_Float2s = source->m_Float2s;
		copy->m_Float3s = source->m_Float3s;
		copy->m_Float4s = source->m_Float4s;
		copy->m_Textures = source->m_Textures;
		copy->m_Transparent      = source->m_Transparent;        // S12.2 queue hint
		copy->m_InstancingShader = source->m_InstancingShader;   // S12.3 twin
		return copy;
	}

	void Material::Set(const std::string& name, float value) { m_Floats[name] = value; }
	void Material::Set(const std::string& name, const glm::vec2& value) { m_Float2s[name] = value; }
	void Material::Set(const std::string& name, const glm::vec3& value) { m_Float3s[name] = value; }
	void Material::Set(const std::string& name, const glm::vec4& value) { m_Float4s[name] = value; }
	void Material::Set(const std::string& name, const Ref<Texture>& texture) { m_Textures[name] = texture; }

	/**
	 * Bind
	 * Orchestrates dynamic uniform streaming from CPU to GPU registers.
	 * Streams scalar constants safely down to the pipeline program.
	 */
	void Material::Bind()
	{
		m_Shader->Bind();

		// Streams real fractional scalar floats down to the driver safely
		for (auto const& [name, val] : m_Floats)
			m_Shader->SetFloat(name, val);

		for (auto const& [name, val] : m_Float2s)
			m_Shader->SetFloat2(name, val);

		for (auto const& [name, val] : m_Float3s)
			m_Shader->SetFloat3(name, val);

		for (auto const& [name, val] : m_Float4s)
			m_Shader->SetFloat4(name, val);

		// NOTE: Manual binding loop removed here!
		// Custom texture arrays and material samplers are resolved dynamically
		// per-quad slot tracking inside Renderer2D::DrawQuad / DrawRotatedQuad.
		// For rendering OUTSIDE the batch renderer, use BindFull() below.
	}

	/**
	 * BindFull
	 * Bind() plus texture slot binding for use outside the batch renderer
	 * (e.g. Renderer::Submit with a textured material). Each cached texture is
	 * bound to slots 0, 1, 2, ... in iteration order and its sampler uniform is
	 * pointed at that slot.
	 */
	void Material::BindFull()
	{
		Bind();

		uint32_t slot = 0;
		for (auto const& [name, texture] : m_Textures)
		{
			if (!texture) continue;
			texture->Bind(slot);
			m_Shader->SetInt(name, static_cast<int>(slot));
			++slot;
		}
	}

	/**
	 * BindFullTo
	 * BindFull(), but onto a caller-supplied shader (the S12.3 instancing twin):
	 * binds `shader` and streams this material's cached uniforms + texture slots
	 * to it. The twin shares the material's uniform contract; names it does not
	 * declare no-op on location -1.
	 */
	void Material::BindFullTo(const Ref<Shader>& shader)
	{
		if (!shader)
			return;

		shader->Bind();

		for (auto const& [name, val] : m_Floats)
			shader->SetFloat(name, val);
		for (auto const& [name, val] : m_Float2s)
			shader->SetFloat2(name, val);
		for (auto const& [name, val] : m_Float3s)
			shader->SetFloat3(name, val);
		for (auto const& [name, val] : m_Float4s)
			shader->SetFloat4(name, val);

		uint32_t slot = 0;
		for (auto const& [name, texture] : m_Textures)
		{
			if (!texture) continue;
			texture->Bind(slot);
			shader->SetInt(name, static_cast<int>(slot));
			++slot;
		}
	}

	float Material::GetFloat(const std::string& name)
	{
		return m_Floats.count(name) ? m_Floats.at(name) : 0.0f;
	}

	glm::vec2 Material::GetVector2(const std::string& name)
	{
		return m_Float2s.count(name) ? m_Float2s.at(name) : glm::vec2(0.0f);
	}

	glm::vec3 Material::GetVector3(const std::string& name)
	{
		return m_Float3s.count(name) ? m_Float3s.at(name) : glm::vec3(0.0f);
	}

	glm::vec4 Material::GetVector4(const std::string& name)
	{
		return m_Float4s.count(name) ? m_Float4s.at(name) : glm::vec4(1.0f);
	}

	Ref<Texture> Material::GetTexture(const std::string& name)
	{
		return m_Textures.count(name) ? m_Textures.at(name) : nullptr;
	}

	bool Material::HasFloat(const std::string& name) const
	{
		return m_Floats.count(name) > 0;
	}

	bool Material::HasFloat2(const std::string& name) const
	{
		return m_Float2s.count(name) > 0;
	}

	bool Material::HasFloat3(const std::string& name) const
	{
		return m_Float3s.count(name) > 0;
	}

	bool Material::HasFloat4(const std::string& name) const
	{
		return m_Float4s.count(name) > 0;
	}

	bool Material::HasTexture(const std::string& name) const
	{
		return m_Textures.count(name) > 0;
	}
}