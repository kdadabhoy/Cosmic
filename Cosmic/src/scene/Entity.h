#pragma once
// Entity.h
// Last Modified: 5/24/2026

#include "Scene.h"
#include "Components.h"
#include "core/Log.h"
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
         *
         * DOUBLE-ADD PROTECTION: If the entity already owns this component type the
         * engine logs a warning and returns the existing component rather than
         * forwarding to EnTT, which would trigger a sparse-set assertion and abort
         * the process. This guard is always active (not gated on CS_ENABLE_ASSERTS).
         *
         * EMPTY/TAG TYPE HANDLING: EnTT's zero-page-size storage (used for any T
         * where std::is_empty_v<T>) returns void from both emplace() and get() —
         * there is no component data to address. For those types we emplace to mark
         * the entity's presence, then return a reference to a process-lifetime static
         * sentinel. All instances of an empty type are equivalent so sharing the
         * sentinel is correct.
         */
        template<typename T, typename... Args>
        T& AddComponent(Args&&... args)
        {
            if (HasComponent<T>())
            {
                CS_CORE_WARN("Entity::AddComponent: entity already owns this component type "
                             "— returning the existing component without re-emplacing. "
                             "Call GetComponent<T>() directly to suppress this warning.");
                return GetComponent<T>();
            }

            if constexpr (std::is_empty_v<T>)
            {
                m_Scene->m_Registry.emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
                static T s_EmptyInstance{};
                return s_EmptyInstance;
            }
            else
            {
                return m_Scene->m_Registry.emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
            }
        }

        /**
         * @brief Returns the component, adding a default-constructed one first
         * if absent. Unlike AddComponent this never warns on an existing
         * component. NOTE: like any entt emplace, adding a component may
         * reallocate that type's pool — do not hold references to OTHER
         * components of the SAME type across this call.
         */
        template<typename T, typename... Args>
        T& GetOrAddComponent(Args&&... args)
        {
            if (HasComponent<T>())
                return GetComponent<T>();
            return AddComponent<T>(std::forward<Args>(args)...);
        }

        /**
         * @brief Returns a mutable reference to a component held by this entity.
         *
         * For empty/tag types EnTT's get() returns void (no storage), so we return
         * a reference to the same process-lifetime static sentinel used by AddComponent.
         */
        template<typename T>
        T& GetComponent()
        {
            CS_ASSERT(HasComponent<T>(), "Entity does not possess this component type!");
            if constexpr (std::is_empty_v<T>)
            {
                static T s_EmptyInstance{};
                return s_EmptyInstance;
            }
            else
            {
                return m_Scene->m_Registry.get<T>(m_EntityHandle);
            }
        }

        /**
         * @brief Returns a read-only reference to a component held by a const entity.
         *
         * Same empty-type sentinel logic as the non-const overload.
         */
        template<typename T>
        const T& GetComponent() const
        {
            CS_ASSERT(HasComponent<T>(), "Entity does not possess this component type!");
            if constexpr (std::is_empty_v<T>)
            {
                static T s_EmptyInstance{};
                return s_EmptyInstance;
            }
            else
            {
                return m_Scene->m_Registry.get<T>(m_EntityHandle);
            }
        }

        /**
         * @brief Checks if the entity contains the given component.
         */
        template<typename T>
        bool HasComponent() const
        {
            return m_Scene->m_Registry.all_of<T>(m_EntityHandle);
        }

        /**
         * @brief Removes a component type safely from the underlying registry pool.
         */
        template<typename T>
        void RemoveComponent()
        {
            CS_ASSERT(HasComponent<T>(), "Cannot remove a non-existent component type!");
            m_Scene->m_Registry.remove<T>(m_EntityHandle);
        }

        // Implicit type conversion operators so this handle behaves like a primitive index
        // NOTE: Checks registry liveness so that copies of an Entity handle held after
        // Scene::DestroyEntity(e) evaluate to false rather than silently aliasing the
        // recycled slot. Always discard Entity handles after destroying the entity.
        operator bool() const
        {
            return m_Scene != nullptr &&
                   m_EntityHandle != entt::null &&
                   m_Scene->GetRegistry().valid(m_EntityHandle);
        }
        operator entt::entity() const { return m_EntityHandle; }
        operator uint32_t() const { return (uint32_t)m_EntityHandle; }

        bool operator==(const Entity& other) const { return m_EntityHandle == other.m_EntityHandle && m_Scene == other.m_Scene; }
        bool operator!=(const Entity& other) const { return !(*this == other); }

    private:
        entt::entity m_EntityHandle{ entt::null };
        Scene* m_Scene = nullptr;
    };
}