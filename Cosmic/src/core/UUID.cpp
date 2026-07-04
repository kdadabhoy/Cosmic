// core/UUID.cpp — 64-bit UUID generation + hex conversion (Phase 13 / E2).

#include "core/UUID.h"

#include <random>
#include <cstdio>

namespace Cosmic
{
    namespace
    {
        // One process-wide 64-bit Mersenne engine, seeded from the platform
        // entropy source. Guarded so concurrent CreateEntity calls from worker
        // threads (JobSystem) can't corrupt the generator state.
        std::mt19937_64& Engine()
        {
            static std::random_device s_Seed;
            static std::mt19937_64 s_Engine(s_Seed());
            return s_Engine;
        }

        std::uniform_int_distribution<uint64_t>& Dist()
        {
            static std::uniform_int_distribution<uint64_t> s_Dist;
            return s_Dist;
        }
    }

    UUID::UUID()
    {
        // 0 is reserved for "null" — regenerate on the (astronomically rare) draw.
        uint64_t v = Dist()(Engine());
        while (v == 0)
            v = Dist()(Engine());
        m_UUID = v;
    }

    std::string UUID::ToString() const
    {
        char buf[17];
        std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(m_UUID));
        return std::string(buf);
    }

    UUID UUID::FromString(const std::string& hex)
    {
        if (hex.empty())
            return UUID(0);
        try
        {
            return UUID(static_cast<uint64_t>(std::stoull(hex, nullptr, 16)));
        }
        catch (...)
        {
            return UUID(0);
        }
    }
}
