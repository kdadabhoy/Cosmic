#pragma once
// Components.h
// Last Modified: 5/20/2026

#include "core/Core.h"
#include "graphics/Material.h"
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

        SpriteRendererComponent() = default;
        SpriteRendererComponent(const SpriteRendererComponent&) = default;
        SpriteRendererComponent(const Ref<Material>& material) : ActiveMaterial(material) {}
        SpriteRendererComponent(const glm::vec4& color) : Color(color) {}
    };
}



// ============================================================================
// CRITICAL: Type Hash Safety Adapters for multi-binary/DLL boundaries
// ============================================================================
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