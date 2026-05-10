#pragma once

#include "core/Core.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <string>

namespace Cosmic
{
    /**
     * @brief Represents a specific instance of a Shader with its own parameter values.
     */
    class Material
    {
    public:
        Material(const Ref<Shader>& shader, const std::string& name = "Untitled Material");
        ~Material() = default;

        // --- Data Setters ---
        void Set(const std::string& name, float value);
        void Set(const std::string& name, const glm::vec3& value);
        void Set(const std::string& name, const glm::vec4& value);
        void Set(const std::string& name, const Ref<Texture>& texture);

        // --- Data Getters ---
        float GetFloat(const std::string& name);
        glm::vec4 GetVector(const std::string& name);
        Ref<Texture> GetTexture(const std::string& name);

        /**
         * @brief Uploads all stored uniforms to the shader.
         * Note: In a Batch Renderer, calling this will likely require a batch flush.
         */
        void Bind();

        inline Ref<Shader> GetShader() const { return m_Shader; }
        inline const std::string& GetName() const { return m_Name; }

        static Ref<Material> Create(const Ref<Shader>& shader, const std::string& name = "Untitled Material");

    private:
        Ref<Shader> m_Shader;
        std::string m_Name;

        // Storage for uniform overrides
        std::unordered_map<std::string, float> m_Floats;
        std::unordered_map<std::string, glm::vec3> m_Float3s;
        std::unordered_map<std::string, glm::vec4> m_Float4s;
        std::unordered_map<std::string, Ref<Texture>> m_Textures;
    };
}