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
        /** Returns the cached vec4 for `name`, or glm::vec4(1.0f) (opaque white) if the key is absent.
         *  The white default is intentional so a missing u_Color tints geometry at full brightness rather than erasing it. */
        glm::vec4           GetVector4(const std::string& name);

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
         * calling Bind() alone does NOT bind individual sample units to physical slots —
         * use BindFull() there instead.
         */
        void    Bind();

        /**
         * BindFull
         * * Bind() plus texture binding: every cached texture uniform is bound to a
         * physical slot (0, 1, 2, ... in iteration order) and its sampler uniform is
         * set to that slot. Use for manual / Renderer::Submit rendering where the
         * batch renderer's per-quad slot tracking is not in play. Inside
         * Renderer2D::DrawQuad paths, keep using Bind() — the batch renderer owns slots.
         */
        void    BindFull();

        ////////////////////////////////
        // Accessors
        ///////////////////////////////

        inline Ref<Shader>                  GetShader() const { return m_Shader; }
        inline const std::string&           GetName() const { return m_Name; }

        bool                                HasFloat(const std::string& name) const;
        bool                                HasFloat2(const std::string& name) const;
        bool                                HasFloat3(const std::string& name) const;
        bool                                HasFloat4(const std::string& name) const;
        bool                                HasTexture(const std::string& name) const;

    private:
        struct PrivateTag {};
    public:
        Material(PrivateTag, const Ref<Shader>& shader, const std::string& name = "Untitled Material");

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