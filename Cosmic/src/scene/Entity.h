#pragma once
// Entity.h
// Last Modified: 5/20/2026

#include "Scene.h"
#include "Components.h"
#include <entt/entt.hpp>

namespace Cosmic
{
    class COSMIC_API Entity
    {
    public:
        Entity() = default;
        Entity(entt::entity handle, Scene* scene)
            : m_EntityHandle(handle), m_Scene(scene)
        {
        }
        Entity(const Entity& other) = default;

        /**
         * @brief Variadic template wrapper to construct components in-place.
         */
        template<typename T, typename... Args>
        T& AddComponent(Args&&... args)
        {
            GLCORE_ASSERT(!HasComponent<T>(), "Entity already contains this component type!");
            return m_Scene->m_Registry.emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
        }

        /**
         * @brief Returns a reference to a component type held by this entity.
         */
        template<typename T>
        T& GetComponent()
        {
            GLCORE_ASSERT(HasComponent<T>(), "Entity does not possess this component type!");
            return m_Scene->m_Registry.get<T>(m_EntityHandle);
        }

        /**
         * @brief Checks if the entity contains the given component.
         */
        template<typename T>
        bool HasComponent()
        {
            return m_Scene->m_Registry.all_of<T>(m_EntityHandle);
        }

        /**
         * @brief Removes a component type safely from the underlying registry pool.
         */
        template<typename T>
        void RemoveComponent()
        {
            GLCORE_ASSERT(HasComponent<T>(), "Cannot remove a non-existent component type!");
            m_Scene->m_Registry.remove<T>(m_EntityHandle);
        }

        // Implicit type conversion operators so this handle behaves like a primitive index
        operator bool() const { return m_EntityHandle != entt::null && m_Scene != nullptr; }
        operator entt::entity() const { return m_EntityHandle; }
        operator uint32_t() const { return (uint32_t)m_EntityHandle; }

        bool operator==(const Entity& other) const { return m_EntityHandle == other.m_EntityHandle && m_Scene == other.m_Scene; }
        bool operator!=(const Entity& other) const { return !(*this == other); }

    private:
        entt::entity m_EntityHandle{ entt::null };
        Scene* m_Scene = nullptr;
    };
}