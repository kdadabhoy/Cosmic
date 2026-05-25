#pragma once

#include <entt/entt.hpp>

/**
 * @brief Stabilizes EnTT type IDs across dynamic executable/DLL boundaries
 * by forcing compile-time string hashing instead of sequential static counters.
 */
#define CS_REGISTER_COMPONENT(T) \
    template<> struct entt::type_hash<T> final { \
        [[nodiscard]] static consteval entt::id_type value() noexcept { \
            return entt::hashed_string::value(#T); \
        } \
    };