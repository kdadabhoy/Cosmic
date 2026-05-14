#pragma once

// Material.h
// Last Modified 5/14/2026

/**
 * General Description:
 * The Material class represents a specific instance of a Shader paired with its
 * own unique set of parameter values (uniforms). While a Shader defines the
 * rendering logic, the Material defines the visual properties—such as color,
 * shininess, and textures—for a specific object. It utilizes an internal caching
 * system to store uniform overrides and uploads them to the GPU during the
 * binding phase.
 *
 * Documentation Notes:
 * - Data Storage: Uses unordered_maps to store floats, vectors, and textures
 *   indexed by their uniform names as defined in the GLSL source.
 * - Uniform Upload: The Bind() method automates the process of calling
 *   Shader::SetUniform functions for all cached data.
 * - Memory Management: Utilizes Ref<T> smart pointers to ensure associated
 *   Shaders and Textures remain in memory for the lifetime of the Material.
 *
 * Public Function Prototypes (Pre and Post Conditions):
 *
 * 1. Material(const Ref<Shader>& shader, const std::string& name)
 *    Pre:  A valid reference-counted Shader is provided.
 *    Post: A Material instance is created and linked to the specified shader.
 *
 * 2. void Set(const std::string& name, [Type] value)
 *    Pre:  'name' matches a uniform name in the linked shader.
 *    Post: Stored value for 'name' is updated in the internal cache.
 *
 * 3. float GetFloat(const std::string& name) / glm::vec4 GetVector(...)
 *    Pre:  None.
 *    Post: Returns the cached value if it exists; otherwise returns a
 *          default (0.0f or 1.0f vector).
 *
 * 4. Ref<Texture> GetTexture(const std::string& name)
 *    Pre:  None.
 *    Post: Returns the cached Texture reference or nullptr if not found.
 *
 * 5. void Bind()
 *    Pre:  The internal Shader reference is valid.
 *    Post: The Shader is bound to the graphics pipeline and all cached
 *          uniforms are uploaded to the GPU.
 *
 * 6. static Ref<Material> Create(const Ref<Shader>& shader, const std::string& name)
 *    Pre:  None.
 *    Post: Returns a new reference-counted Material instance.
 *
 * 7. Ref<Shader> GetShader()
 *    Pre:  None.
 *    Post: Returns a reference to the underlying Shader logic.
 */

#include "core/Core.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <string>

namespace Cosmic
{
    class Material
    {
    public:
        ////////////////////////////////
        // Life Cycle & Creation
        ///////////////////////////////

        Material(const Ref<Shader>& shader, const std::string& name = "Untitled Material");
        ~Material() = default;

        static Ref<Material>        Create(const Ref<Shader>& shader, const std::string& name = "Untitled Material");

        ////////////////////////////////
        // Data Setters (Uniform Cache)
        ///////////////////////////////

        void            Set(const std::string& name, float value);
        void            Set(const std::string& name, const glm::vec3& value);
        void            Set(const std::string& name, const glm::vec4& value);
        void            Set(const std::string& name, const Ref<Texture>& texture);

        ////////////////////////////////
        // Data Getters
        ///////////////////////////////

        float               GetFloat(const std::string& name);
        glm::vec4           GetVector(const std::string& name);
        Ref<Texture>        GetTexture(const std::string& name);

        ////////////////////////////////
        // Binding Operations
        ///////////////////////////////

        void    Bind();

        ////////////////////////////////
        // Accessors
        ///////////////////////////////

        inline Ref<Shader>                  GetShader() const       { return m_Shader; }
        inline const std::string&           GetName() const         { return m_Name; }

    private:
        ////////////////////////////////
        // Internal Resource Links
        ///////////////////////////////

        Ref<Shader>     m_Shader;
        std::string     m_Name;

        ////////////////////////////////
        // Uniform Storage Maps
        ///////////////////////////////

        std::unordered_map<std::string, float>          m_Floats;
        std::unordered_map<std::string, glm::vec3>      m_Float3s;
        std::unordered_map<std::string, glm::vec4>      m_Float4s;
        std::unordered_map<std::string, Ref<Texture>>   m_Textures;
    };
}