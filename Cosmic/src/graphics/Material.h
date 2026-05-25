#pragma once

#include "core/Core.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <string>

namespace Cosmic
{
    class COSMIC_API Material
    {
    public:
        ////////////////////////////////
        // Life Cycle & Creation
        ///////////////////////////////

        Material(const Ref<Shader>& shader, const std::string& name = "Untitled Material");
        ~Material() = default;

        static Ref<Material>        Create(const Ref<Shader>& shader, const std::string& name = "Untitled Material");

        /**
         * Clone
         * * Deep copies parameters and textures into an isolated instance.
         * Shares the immutable compiled base Ref<Shader> across copies.
         */
        static Ref<Material>        Clone(const Ref<Material>& source, const std::string& newName);

        ////////////////////////////////
        // Data Setters (Uniform Cache)
        ///////////////////////////////

        void            Set(const std::string& name, float value);
        void            Set(const std::string& name, const glm::vec2& value);
        void            Set(const std::string& name, const glm::vec3& value);
        void            Set(const std::string& name, const glm::vec4& value);
        void            Set(const std::string& name, const Ref<Texture>& texture);

        ////////////////////////////////
        // Data Getters
        ///////////////////////////////

        float               GetFloat(const std::string& name);
        glm::vec2           GetVector2(const std::string& name);
        glm::vec3           GetVector3(const std::string& name);
        glm::vec4           GetVector4(const std::string& name);
        inline glm::vec4 GetVector(const std::string& name)             { return GetVector4(name); } // Legacy (Refactor Renderer2D later to be able to remove this)

        Ref<Texture>        GetTexture(const std::string& name);

        ////////////////////////////////
        // Binding Operations
        ///////////////////////////////

        /**
         * Bind
         * * TEXTURE BINDING BATCH CONTRACT NOTE:
         * 1. When drawn through Renderer2D::DrawQuad, actual slot binding optimization
         * is evaluated dynamically on the fly per-quad by batch tracking queues.
         * 2. When executed outside the batch renderer (e.g., custom Renderer::Submit calls),
         * calling Bind() alone does NOT bind individual sample units to physical slots.
         * The caller must explicitly bind textures to slots and map uniform integers manually.
         */
        void    Bind();

        ////////////////////////////////
        // Accessors
        ///////////////////////////////

        inline Ref<Shader>                  GetShader() const { return m_Shader; }
        inline const std::string&           GetName() const { return m_Name; }

        bool                                HasFloat(const std::string& name) const;
        bool                                HasFloat2(const std::string& name) const;

    private:
        Ref<Shader>     m_Shader;
        std::string     m_Name;

        std::unordered_map<std::string, float>          m_Floats;
        std::unordered_map<std::string, glm::vec2>      m_Float2s;
        std::unordered_map<std::string, glm::vec3>      m_Float3s;
        std::unordered_map<std::string, glm::vec4>      m_Float4s;
        std::unordered_map<std::string, Ref<Texture>>   m_Textures;
    };
}