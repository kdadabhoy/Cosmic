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

        /**
         * BindFullTo
         * * BindFull(), but onto `shader` instead of the material's own shader:
         * binds `shader` and uploads every cached uniform + texture slot to it.
         * Used by Renderer3D's S12.3 auto-instancing to bind this material's
         * values onto its instancing twin (see SetInstancingShader) — the twin
         * declares the same uniform contract, so the cache maps 1:1. Undeclared
         * names no-op on location -1 (engine-wide silent-ignore rule).
         */
        void    BindFullTo(const Ref<Shader>& shader);

        ////////////////////////////////
        // Render-queue hints (S12.2 / S12.3)
        ///////////////////////////////

        /**
         * @brief Mark this material TRANSPARENT (default false = opaque).
         * Renderer3D's queue draws transparent-material meshes AFTER all opaques,
         * sorted back-to-front, with depth writes off (depth test stays on) under
         * the engine's default Alpha blend — the state juggling apps used to do
         * by hand around DrawMesh. Opaque materials sort for state grouping +
         * front-to-back instead.
         */
        void    SetTransparent(bool transparent)    { m_Transparent = transparent; }
        bool    IsTransparent() const               { return m_Transparent; }

        /**
         * @brief Register this material's INSTANCING TWIN — a shader with the
         * same uniform/texture contract that reads per-instance { mat4 Model;
         * vec4 Tint; } from the SSBO at Bindings::InstancesSsbo instead of a
         * per-draw u_Model (e.g. PBR.glsl -> PBRInstanced.glsl). When set,
         * Renderer3D's queue may collapse runs of identical (mesh, material)
         * opaque submissions with entityID == -1 into one instanced draw
         * (S12.3). Transforms should be rigid + uniform scale — the twin
         * derives normals from mat3(Model) (same documented limitation as
         * InstanceSet). Null (default) = never auto-instanced.
         */
        void                SetInstancingShader(const Ref<Shader>& shader) { m_InstancingShader = shader; }
        const Ref<Shader>&  GetInstancingShader() const                    { return m_InstancingShader; }

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

        // Render-queue hints (S12.2/S12.3). Clone() copies both.
        bool            m_Transparent = false;
        Ref<Shader>     m_InstancingShader;

        std::unordered_map<std::string, float>          m_Floats;
        std::unordered_map<std::string, glm::vec2>      m_Float2s;
        std::unordered_map<std::string, glm::vec3>      m_Float3s;
        std::unordered_map<std::string, glm::vec4>      m_Float4s;
        std::unordered_map<std::string, Ref<Texture>>   m_Textures;
    };
}