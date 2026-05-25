#pragma once
// Last Modified: 5/24/2026

#include "core/Core.h"
#include "graphics/Material.h"
#include "scene/ComponentRegistry.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>

namespace Cosmic
{
    /**
     * @brief Provides every entity with an internal debug name identity tag.
     */
    struct COSMIC_API TagComponent
    {
        std::string Tag;

        TagComponent() = default;
        TagComponent(const TagComponent&) = default;
        TagComponent(const std::string& tag) : Tag(tag) {}
    };

    /**
     * @brief Spatial placement properties of an entity within world-space.
     */
    struct COSMIC_API TransformComponent
    {
        glm::vec3 Position{ 0.0f, 0.0f, 0.0f };
        glm::vec3 Rotation{ 0.0f, 0.0f, 0.0f }; // Z represents 2D roll rotation
        glm::vec2 Scale{ 1.0f, 1.0f };

        TransformComponent() = default;
        TransformComponent(const TransformComponent&) = default;
        TransformComponent(const glm::vec3& position) : Position(position) {}

        /**
         * @brief Standard matrix transformation construction convenience helper.
         */
        glm::mat4 GetTransform() const
        {
            glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), glm::radians(Rotation.x), { 1, 0, 0 })
                * glm::rotate(glm::mat4(1.0f), glm::radians(Rotation.y), { 0, 1, 0 })
                * glm::rotate(glm::mat4(1.0f), glm::radians(Rotation.z), { 0, 0, 1 });

            return glm::translate(glm::mat4(1.0f), Position)
                * rotation
                * glm::scale(glm::mat4(1.0f), glm::vec3(Scale.x, Scale.y, 1.0f));
        }
    };


    /**
     * @brief Combines spatial layout data with your graphic engine's Material layers.
     */
    struct COSMIC_API SpriteRendererComponent
    {
        Ref<Material> ActiveMaterial;
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f }; // Solid default fallback color tint

        bool FlipX = false;
        bool FlipY = false;

        SpriteRendererComponent() = default;
        SpriteRendererComponent(const SpriteRendererComponent&) = default;
        SpriteRendererComponent(const Ref<Material>& material) : ActiveMaterial(material) {}
        SpriteRendererComponent(const glm::vec4& color) : Color(color) {}
    };
}

// ============================================================================
// CRITICAL ARCHITECTURAL NOTE ON CLIENT COMPONENT REGISTRATION:
// Built-in engine components below use manual specialization layouts. 
// If you are writing a custom simulation or game component inside an external 
// client module / DLL project, you MUST use the 'CS_REGISTER_COMPONENT(MyType)' 
// macro inside your component header file. 
// Failure to do so will cause the host binary and dynamic plugin binary to map 
// the same type name to divergent integer indices, breaking component storage.
// ============================================================================

// Register built-in engine types using the standardized registration macro
CS_REGISTER_COMPONENT(Cosmic::TagComponent)
CS_REGISTER_COMPONENT(Cosmic::TransformComponent)
CS_REGISTER_COMPONENT(Cosmic::SpriteRendererComponent)



// The block below just manually does this (shows what ComponentRegistry is doing)
/*
#include <entt/entt.hpp>
namespace entt
{
    template<>
    struct type_hash<Cosmic::TagComponent> final
    {
        [[nodiscard]] static consteval id_type value() noexcept
        {
            return hashed_string::value("TagComponent");
        }
    };

    template<>
    struct type_hash<Cosmic::TransformComponent> final
    {
        [[nodiscard]] static consteval id_type value() noexcept
        {
            return hashed_string::value("TransformComponent");
        }
    };

    template<>
    struct type_hash<Cosmic::SpriteRendererComponent> final
    {
        [[nodiscard]] static consteval id_type value() noexcept
        {
            return hashed_string::value("SpriteRendererComponent");
        }
    };
}
*/