#include "graphics/Material.h"

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Create
	 *
	 * Static factory method to instantiate a Material. Returns a Ref (shared pointer)
	 * to ensure the material's lifetime is managed safely across different engine
	 * layers and render commands.
	 */
	Ref<Material> Material::Create(const Ref<Shader>& shader, const std::string& name)
	{
		return std::make_shared<Material>(shader, name);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Constructor
	 *
	 * Links the Material to a specific Shader logic. The name parameter is primarily
	 * for debugging and editor identification.
	 */
	Material::Material(const Ref<Shader>& shader, const std::string& name)
		: m_Shader(shader), m_Name(name)
	{
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Set (Overloaded)
	 *
	 * These functions populate the internal uniform cache.
	 *
	 * Note: These do NOT upload data to the GPU immediately. Instead, they store
	 * the values in unordered_maps so that the state can be modified at any time
	 * without requiring an active shader binding.
	 */
	void Material::Set(const std::string& name, float value) { m_Floats[name] = value; }
	void Material::Set(const std::string& name, const glm::vec3& value) { m_Float3s[name] = value; }
	void Material::Set(const std::string& name, const glm::vec4& value) { m_Float4s[name] = value; }
	void Material::Set(const std::string& name, const Ref<Texture>& texture) { m_Textures[name] = texture; }

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Bind
	 *
	 * Orchestrates the data transfer from CPU to GPU.
	 *
	 * 1. Binds the underlying Shader logic.
	 * 2. Iterates through all cached floats and vectors, uploading them as Uniforms.
	 *
	 * DESIGN NOTE: In a Batch Renderer environment, calling Bind() frequently causes
	 * "State Changes" which can trigger a batch flush. For high-performance 2D
	 * rendering, these are often optimized by the Renderer2D.
	 */
	void Material::Bind()
	{
		m_Shader->Bind();

		for (auto const& [name, val] : m_Floats)
			m_Shader->SetInt(name, (int)val);

		for (auto const& [name, val] : m_Float3s)
			m_Shader->SetFloat3(name, val);

		for (auto const& [name, val] : m_Float4s)
			m_Shader->SetFloat4(name, val);

		// Note: Textures are usually bound by the Renderer2D's 
		// texture slot system rather than directly in the material bind.
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Data Getters
	 *
	 * Safe retrieval of cached parameters. If a requested uniform name does not
	 * exist in the local cache, these return standard defaults (0.0f, null, or
	 * white color/identity vector) to prevent crashes.
	 */
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

	/////////////////////////////////////////////////////////////////////////////////
}