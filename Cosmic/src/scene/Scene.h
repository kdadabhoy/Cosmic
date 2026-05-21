#pragma once
// Scene.h
// Last Modified: 5/20/2026

#include "core/Core.h"
#include <entt/entt.hpp>
#include <string>
#include <memory>

namespace Cosmic
{
    class Entity; // Forward declaration

    class COSMIC_API Scene
    {
    public:
        Scene();
        ~Scene() = default;

        /**
         * @brief Static factory helper to match unified engine smart pointer instantiation rules.
         */
        template<typename... Args>
        static Ref<Scene> Create(Args&&... args)
        {
            return std::make_shared<Scene>(std::forward<Args>(args)...);
        }

        /** * @brief Instantiates a blank Entity handle bound to this scene instance.         */
        Entity CreateEntity(const std::string& name = "GenericEntity");

        /** * @brief Destroys and cleans up internal registry component references.         */
        void DestroyEntity(Entity entity);

        /** * @brief Runs ongoing frame logic updates.         */
        void OnUpdate(float deltaTime);

        /** * @brief Extracts view parameters and pipes them to Renderer2D.         */
        void OnRender();

    private:
        entt::registry m_Registry;

        friend class Entity; // Gives access to the registry mapping internals securely
    };
}