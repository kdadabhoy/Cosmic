#pragma once

// ComponentArray.h
// Last Modified: 5/27/26

/**
 * ============================================================================
 * COSMIC ENGINE — ComponentArray<T>
 * ============================================================================
 *
 * OVERVIEW
 * --------
 * ComponentArray<T> is a lightweight, non-owning view over a contiguous block
 * of component data stored in an entt::registry. It exposes raw pointers and
 * counts that ParallelFor and DoubleBuffer can consume directly, eliminating
 * per-entity virtual dispatch and pointer-chasing.
 *
 * WHY THIS EXISTS
 * ---------------
 * EnTT's default view API (registry.view<T>()) iterates using an internal
 * storage iterator that hops between component pools. This is efficient for
 * sparse, random access, but suboptimal for bulk parallel processing because:
 *
 * 1. The iterator abstraction prevents the compiler from auto-vectorising
 * inner loops (no contiguous memory guarantee is visible to the optimizer).
 * 2. Each iterator dereference involves a pool lookup — fine for one entity,
 * slow for 10,000.
 * 3. ParallelFor needs a raw pointer + count, not an iterator pair.
 *
 * EnTT's storage<T> *does* allocate components in contiguous pages. By
 * accessing the underlying storage directly via registry.storage<T>(), we
 * get a raw data pointer that is fully contiguous within each page. For dense
 * components (most simulation components), this is effectively one flat array.
 *
 * USAGE
 * -----------------------------------------------------------------------
 * // Inside a System::OnUpdate implementation:
 * auto view = ComponentArray<TransformComponent>::From(scene.GetRegistry());
 *
 * // Direct contiguous access — no per-entity overhead
 * ParallelForEach(view.Data(), view.Count(),
 * [dt](TransformComponent* begin, TransformComponent* end)
 * {
 * for (auto* c = begin; c != end; ++c)
 * c->Position.x += c->VelocityX * dt;
 * });
 * -----------------------------------------------------------------------
 *
 * IMPORTANT NOTES
 * - ComponentArray does NOT own the data. The entt::registry must remain
 * alive for the lifetime of any ComponentArray referencing it.
 *
 * - The pointer returned by Data() is valid only as long as no components
 * are added to or removed from the registry. Entity creation/destruction
 * can invalidate the pointer. Only use ComponentArray inside system Update
 * calls where no structural changes are expected mid-frame.
 *
 * - EnTT stores components in page-aligned chunks. For large counts, the
 * data is contiguous per page but not necessarily across page boundaries.
 * FlatComponentArray (below) copies all pages into one allocation when
 * cross-page contiguity is required.
 *
 * ============================================================================
 */

#include "core/Core.h"
#include <entt/entt.hpp>
#include <vector>
#include <cstddef>

namespace Cosmic
{
    // =========================================================================
    // ComponentArray<T> — non-owning view into EnTT's internal storage
    // =========================================================================

    /**
     * @brief Non-owning view of a component type's contiguous storage.
     *
     * Prefer this over FlatComponentArray when:
     * - Component count is small-to-medium (< ~50,000 elements)
     * - No structural changes happen between construction and use
     * - Zero-copy access is required for performance
     */
    template<typename T>
    class ComponentArray
    {
    public:
        ComponentArray() = default;

        static ComponentArray<T> From(entt::registry& registry)
        {
            ComponentArray<T> arr;

            // registry.storage<T>() returns a direct object reference instance
            auto& storage = registry.storage<T>();

            // Use object dot accessors (.) uniformly
            if (!storage.empty())
            {
                // EnTT's basic_storage exposes raw() for the first page.
                // This is the fast path — direct pointer, zero copies.
                arr.m_Data = storage.raw()[0];
                arr.m_Count = storage.size();

                // ComponentArray only maps page 0. If the pool has grown beyond one
                // EnTT storage page, m_Data covers only a subset of m_Count elements —
                // accessing indices past the first page is undefined behaviour.
                // Use FlatComponentArray<T> for pools that may exceed one page.
                CS_CORE_ASSERT(storage.raw().size() == 1,
                    "ComponentArray only covers page 0; use FlatComponentArray for large pools.");
            }
            return arr;
        }

        /** @brief Raw pointer to the first component element. May be nullptr if empty. */
        T* Data() { return m_Data; }
        const T* Data() const { return m_Data; }

        /** @brief Number of component instances in the pool. */
        size_t Count() const { return m_Count; }

        /** @brief True if at least one component exists. */
        bool   IsEmpty() const { return m_Count == 0; }

        /** @brief Iterator support for range-based for loops. */
        T* begin() { return m_Data; }
        const T* begin() const { return m_Data; }
        T* end() { return m_Data + m_Count; }
        const T* end()   const { return m_Data + m_Count; }

        /** @brief Index operators with const correctness tracking. */
        T& operator[](size_t index) { return m_Data[index]; }
        const T& operator[](size_t index) const { return m_Data[index]; }

    private:
        T* m_Data = nullptr;
        size_t m_Count = 0;
    };


    // =========================================================================
    // FlatComponentArray<T> — owning, fully-contiguous copy
    // =========================================================================

    /**
     * @brief Flattened, fully-contiguous copy of all T components.
     *
     * EnTT uses paged storage internally. When components span multiple
     * pages (very large counts, or heavy fragmentation from entity deletion),
     * ComponentArray<T>::Data() only points to the first page. FlatComponentArray
     * iterates all pages and copies them into one contiguous heap allocation.
     *
     * This costs a memcpy upfront but guarantees:
     * - One pointer + one count, always contiguous.
     * - The ParallelFor chunking math is always correct.
     * - SIMD auto-vectorisation works without page-boundary conditionals.
     *
     * Use FlatComponentArray when:
     * - Component count exceeds ~50,000 (paging becomes relevant)
     * - You need to write back results without a DoubleBuffer
     * - Cross-page iteration correctness is mandatory
     *
     * WRITE-BACK NOTE: Modifying elements in FlatComponentArray does NOT
     * modify the registry. After a parallel phase, call WriteBack() to copy
     * modified values back, or use DoubleBuffer<T> for a non-copying approach.
     */
    template<typename T>
    class FlatComponentArray
    {
    public:
        FlatComponentArray() = default;

        /**
         * @brief Standard extraction factory. Copies the entire raw component storage pool.
         */
        static FlatComponentArray<T> From(entt::registry& registry)
        {
            FlatComponentArray<T> arr;
            auto& storage = registry.storage<T>();

            arr.m_Data.reserve(storage.size());
            arr.m_Entities.reserve(storage.size());

            // EnTT basic_storage allows us to fetch the corresponding entity 
            // for every raw component at the exact same iteration index!
            for (auto it = storage.begin(); it != storage.end(); ++it)
            {
                auto entity = storage.handle(it);
                arr.m_Data.push_back(*it);
                arr.m_Entities.push_back(entity);
            }

            return arr;
        }

        /**
         * @brief Custom View Multi-Array Sync Utility
         * Populates the flat buffer with elements filtered strictly via a shared multi-component view layout.
         * Guarantees structural match alignments down to identical array indices across separate component pools.
         */
        template<typename ViewType>
        void PrepareFromView(entt::registry& registry, ViewType& view)
        {
            auto& storage = registry.storage<T>();
            m_Data.clear();
            m_Entities.clear();

            m_Data.reserve(view.size_hint());
            m_Entities.reserve(view.size_hint());

            for (auto entity : view)
            {
                if (storage.contains(entity))
                {
                    m_Data.push_back(storage.get(entity));
                    m_Entities.push_back(entity);
                }
            }
        }

        T* Data() { return m_Data.data(); }
        const T* Data() const { return m_Data.data(); }
        size_t Count()  const { return m_Data.size(); }
        bool   IsEmpty() const { return m_Data.empty(); }

        T* begin() { return m_Data.data(); }
        const T* begin() const { return m_Data.data(); }
        T* end() { return m_Data.data() + m_Data.size(); }
        const T* end()   const { return m_Data.data() + m_Data.size(); }

        T& operator[](size_t index) { return m_Data[index]; }
        const T& operator[](size_t index) const { return m_Data[index]; }

        /**
         * @brief Safe entity-mapped WriteBack implementation
         * Uses tracked internal entity references to overwrite explicit component memory addresses.
         */
        void WriteBack(entt::registry& registry)
        {
            auto& storage = registry.storage<T>();

            for (size_t i = 0; i < m_Data.size(); ++i)
            {
                entt::entity entity = m_Entities[i];
                if (storage.contains(entity))
                {
                    storage.get(entity) = m_Data[i];
                }
            }
        }

    private:
        std::vector<T>            m_Data;
        std::vector<entt::entity> m_Entities; // Parallel tracking array for safe merging across indices
    };

} // namespace Cosmic