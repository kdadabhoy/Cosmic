#pragma once
// core/UUID.h
//
// ============================================================================
// Cosmic UUID — a 64-bit stable entity identity (Phase 13 / E2).
// ============================================================================
//
// Every entity created through Scene carries an IDComponent{ UUID }. The UUID
// is the stable reference used by serialization (parent links, EntityRef script
// fields, prefab sources) so those references survive save/load and session
// restarts. 64 bits is collision-safe at scene scale (see the 1e6-draw test) and
// cheap to store/compare. UUID(0) is the reserved "null / invalid" value.
// ============================================================================

#include "core/Core.h"

#include <cstdint>
#include <functional>
#include <string>

namespace Cosmic
{
    class COSMIC_API UUID
    {
    public:
        UUID();                                   // random, never 0
        UUID(uint64_t value) : m_UUID(value) {}   // explicit value (e.g. from JSON)
        UUID(const UUID&) = default;

        uint64_t Value() const { return m_UUID; }
        bool     IsValid() const { return m_UUID != 0; }

        operator uint64_t() const { return m_UUID; }

        bool operator==(const UUID& o) const { return m_UUID == o.m_UUID; }
        bool operator!=(const UUID& o) const { return m_UUID != o.m_UUID; }

        // 16-char lowercase hex, and the inverse (0 on parse failure).
        std::string        ToString() const;
        static UUID        FromString(const std::string& hex);

    private:
        uint64_t m_UUID;
    };
}

namespace std
{
    template<>
    struct hash<Cosmic::UUID>
    {
        std::size_t operator()(const Cosmic::UUID& uuid) const noexcept
        {
            return std::hash<uint64_t>{}(uuid.Value());
        }
    };
}
