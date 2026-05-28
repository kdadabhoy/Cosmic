#pragma once

// SystemQuery.h
// Last Modified: 5/28/26

/**
 * ============================================================================
 * COSMIC ENGINE — SystemQuery<T>
 * ============================================================================
 *
 * OVERVIEW
 * --------
 * SystemQuery<T> is the primary data-access API for ParallelSystem subclasses.
 * Declare query objects as member variables, pass `this` to the constructor,
 * and the engine stages and commits them automatically around the parallel
 * execution passes. You only write the logic — not the plumbing.
 *
 * TWO QUERY TYPES
 * ---------------
 *   ReadWriteQuery<T>  — Stages a mutable snapshot of all T components.
 *                        Workers modify elements in-place inside ForEachAsync.
 *                        Results are written back to the registry after OnMerge.
 *
 *   ReadOnlyQuery<T>   — Stages an immutable snapshot of all T components.
 *                        Used alongside ReadWriteQuery when workers need to
 *                        read a stable copy of another component type without
 *                        race conditions (e.g. reading neighbour positions
 *                        during a collision pass). No writeback.
 *
 * TYPICAL USAGE
 * -----------------------------------------------------------------------
 *   class BallPhysicsSystem : public Cosmic::ParallelSystem
 *   {
 *       // Declare queries as members — pass `this` so they self-register.
 *       Cosmic::ReadWriteQuery<PhysicsBody> m_Bodies{ this };
 *
 *   public:
 *       float Gravity = -9.8f;
 *
 *       void OnFixedParallelExecute(Cosmic::Scene& scene, float dt) override
 *       {
 *           const float g = Gravity;
 *           m_Bodies.ForEachAsync([dt, g](PhysicsBody& body)
 *           {
 *               body.Velocity.y  += g * dt;
 *               body.Position    += body.Velocity * dt;
 *           });
 *       }
 *
 *       // OnFixedPrepare and OnFixedMerge are optional.
 *       // If you need to sync results to a second component (e.g. TransformComponent):
 *       void OnFixedMerge(Cosmic::Scene& scene, float dt) override
 *       {
 *           m_Bodies.ForEachWithEntity([&scene](PhysicsBody& body, entt::entity e)
 *           {
 *               auto& t = scene.GetRegistry().get<Cosmic::TransformComponent>(e);
 *               t.Position = { body.Position.x, body.Position.y, t.Position.z };
 *           });
 *       }
 *   };
 * -----------------------------------------------------------------------
 *
 * WHAT THE ENGINE HANDLES AUTOMATICALLY
 * --------------------------------------
 *   - Snapshotting component values from the registry each frame (Stage)
 *   - Writing computed results back to the registry (Commit)
 *   - Double-buffer management
 *   - Entity-to-index mapping
 *
 * WHAT YOU STILL DO
 * -----------------
 *   - Write the transform logic in OnParallelExecute / OnFixedParallelExecute
 *   - Override OnMerge if you need to sync results to a second component type
 *   - Override OnPrepare if you need per-frame setup (rare)
 *
 * CROSS-ENTITY PATTERNS (e.g. collision, flocking)
 * -------------------------------------------------
 * ForEachAsync gives each element a mutable reference. Workers operate on
 * disjoint index ranges, so two workers never touch the same element — no
 * data race within a single ReadWriteQuery. HOWEVER, a worker should not read
 * element [i+5] while another worker may be writing it. For algorithms that
 * need a stable read of the whole dataset alongside a separate write target,
 * pair a ReadOnlyQuery (stable snapshot) with a ReadWriteQuery (output):
 *
 *   Cosmic::ReadOnlyQuery<PhysicsBody>   m_ReadBodies{ this };
 *   Cosmic::ReadWriteQuery<PhysicsBody>  m_WriteBodies{ this };
 *
 * Note: both queries stage independently, so they start each frame as
 * identical snapshots. Workers read from ReadOnly, write to ReadWrite.
 *
 * ============================================================================
 */

#include "jobs/ParallelSystem.h"
#include "jobs/ParallelFor.h"
#include "scene/Scene.h"
#include <entt/entt.hpp>
#include <vector>
#include <cstddef>

namespace Cosmic
{
    // =========================================================================
    // ReadWriteQuery<T>
    // =========================================================================

    /**
     * @brief Staged mutable component access for parallel systems.
     *
     * Declare as a member of your ParallelSystem and pass `this`:
     * @code
     *   Cosmic::ReadWriteQuery<PhysicsBody> m_Bodies{ this };
     * @endcode
     *
     * The engine calls Stage() before OnPrepare and Commit() after OnMerge.
     * All staged data is available throughout OnPrepare, OnParallelExecute,
     * and OnMerge.
     */
    template<typename T>
    class ReadWriteQuery : public ISystemQuery
    {
    public:
        /**
         * @brief Construct and register with the owning ParallelSystem.
         * @param owner  Pass `this` from your ParallelSystem subclass.
         */
        explicit ReadWriteQuery(ParallelSystem* owner)
        {
            owner->RegisterQuery(this);
        }

        // =====================================================================
        // Engine lifecycle  (called by Scene — do not call from user code)
        // =====================================================================

        /**
         * @brief Snapshot all T components from the registry.
         * Automatically called by the engine before OnPrepare.
         */
        void Stage(Scene& scene) override
        {
            auto view = scene.GetRegistry().view<T>();

            m_Data.clear();
            m_Entities.clear();

            for (auto [entity, component] : view.each())
            {
                m_Data.push_back(component);
                m_Entities.push_back(entity);
            }
        }

        /**
         * @brief Write staged results back to the registry.
         * Automatically called by the engine after OnMerge.
         */
        void Commit(Scene& scene) override
        {
            auto& reg = scene.GetRegistry();
            for (size_t i = 0; i < m_Entities.size(); ++i)
            {
                const entt::entity entity = m_Entities[i];
                if (reg.valid(entity) && reg.all_of<T>(entity))
                    reg.get<T>(entity) = m_Data[i];
            }
        }

        // =====================================================================
        // Data access
        // =====================================================================

        /** @brief Raw pointer to the staged data array. Safe from any phase. */
        T*       Data()       { return m_Data.data(); }
        const T* Data() const { return m_Data.data(); }

        /** @brief Number of staged elements (equals entity count with component T). */
        size_t Count()   const { return m_Data.size(); }

        /** @brief True if no T components exist in the scene. */
        bool   IsEmpty() const { return m_Data.empty(); }

        /** @brief Indexed access to a staged element. */
        T&       operator[](size_t i)       { return m_Data[i]; }
        const T& operator[](size_t i) const { return m_Data[i]; }

        /** @brief Entity handle for the element at the given index. */
        entt::entity EntityAt(size_t i) const { return m_Entities[i]; }

        // =====================================================================
        // Async iteration  (use inside OnParallelExecute / OnFixedParallelExecute)
        // =====================================================================

        /**
         * @brief Submit parallel in-place per-element jobs.
         *
         * Each element is passed by mutable reference. Workers operate on
         * disjoint index ranges, so no two workers ever access the same element
         * simultaneously. Do NOT read elements outside your own sub-range —
         * use ReadOnlyQuery for cross-element patterns.
         *
         * @param func         void(T& item) — called once per element.
         * @param minChunkSize Minimum elements per worker chunk (default 64).
         */
        template<typename Func>
        void ForEachAsync(Func&& func, size_t minChunkSize = 64)
        {
            if (m_Data.empty()) return;

            T* data = m_Data.data();

            // Capture func by value — the async call returns before jobs execute,
            // so a reference capture would dangle once this scope unwinds.
            ParallelForAsync(m_Data.size(),
                [data, func](size_t begin, size_t end)
                {
                    for (size_t i = begin; i < end; ++i)
                        func(data[i]);
                }, minChunkSize);
        }

        /**
         * @brief Submit parallel range-based jobs.
         *
         * For patterns where the inner loop structure matters (e.g. SIMD
         * intrinsics, manual unrolling).
         *
         * @param func  void(T* begin, T* end) — receives a contiguous sub-array.
         */
        template<typename Func>
        void DispatchAsync(Func&& func, size_t minChunkSize = 64)
        {
            if (m_Data.empty()) return;

            T* data = m_Data.data();
            ParallelForAsync(m_Data.size(),
                [data, func](size_t begin, size_t end)
                {
                    func(data + begin, data + end);
                }, minChunkSize);
        }

        // =====================================================================
        // Sync iteration  (use in OnPrepare or OnMerge — main thread only)
        // =====================================================================

        /**
         * @brief Iterate all staged elements on the calling thread.
         * @param func  void(T& item)
         */
        template<typename Func>
        void ForEach(Func&& func)
        {
            for (T& item : m_Data)
                func(item);
        }

        /**
         * @brief Iterate all staged elements with their entity handles.
         *
         * The primary use case is OnMerge cross-component sync — e.g. writing
         * computed physics positions back to a TransformComponent:
         * @code
         *   m_Bodies.ForEachWithEntity([&scene](PhysicsBody& body, entt::entity e)
         *   {
         *       auto& t = scene.GetRegistry().get<TransformComponent>(e);
         *       t.Position = { body.Position.x, body.Position.y, t.Position.z };
         *   });
         * @endcode
         *
         * @param func  void(T& item, entt::entity entity)
         */
        template<typename Func>
        void ForEachWithEntity(Func&& func)
        {
            for (size_t i = 0; i < m_Data.size(); ++i)
                func(m_Data[i], m_Entities[i]);
        }

    private:
        std::vector<T>            m_Data;     // staged component values; workers modify in-place
        std::vector<entt::entity> m_Entities; // parallel entity map for Commit and ForEachWithEntity
    };


    // =========================================================================
    // ReadOnlyQuery<T>
    // =========================================================================

    /**
     * @brief Staged immutable component access for parallel systems.
     *
     * Provides workers with a stable snapshot of component T that cannot be
     * written to. Use alongside ReadWriteQuery when your algorithm needs to
     * read the whole dataset (including elements being computed by other workers)
     * without race conditions.
     *
     * No writeback occurs — the registry values for T are not modified.
     *
     * @code
     *   Cosmic::ReadOnlyQuery<PhysicsBody>  m_ReadBodies{ this };
     *   Cosmic::ReadWriteQuery<PhysicsBody> m_WriteBodies{ this };
     *
     *   void OnFixedParallelExecute(Scene& scene, float dt) override
     *   {
     *       const PhysicsBody* stable = m_ReadBodies.Data();
     *       m_WriteBodies.DispatchAsync([stable, dt](PhysicsBody* begin, PhysicsBody* end)
     *       {
     *           // Safe: reads from stable snapshot, writes to separate output
     *       });
     *   }
     * @endcode
     */
    template<typename T>
    class ReadOnlyQuery : public ISystemQuery
    {
    public:
        /**
         * @brief Construct and register with the owning ParallelSystem.
         * @param owner  Pass `this` from your ParallelSystem subclass.
         */
        explicit ReadOnlyQuery(ParallelSystem* owner)
        {
            owner->RegisterQuery(this);
        }

        // =====================================================================
        // Engine lifecycle
        // =====================================================================

        void Stage(Scene& scene) override
        {
            auto view = scene.GetRegistry().view<T>();

            m_Data.clear();
            m_Entities.clear();

            for (auto [entity, component] : view.each())
            {
                m_Data.push_back(component);
                m_Entities.push_back(entity);
            }
        }

        /** @brief No-op — read-only queries never write back to the registry. */
        void Commit(Scene& scene) override {}

        // =====================================================================
        // Data access
        // =====================================================================

        const T* Data()  const { return m_Data.data(); }
        size_t   Count() const { return m_Data.size(); }
        bool     IsEmpty() const { return m_Data.empty(); }

        const T& operator[](size_t i) const { return m_Data[i]; }
        entt::entity EntityAt(size_t i) const { return m_Entities[i]; }

        // =====================================================================
        // Async iteration  (use inside OnParallelExecute / OnFixedParallelExecute)
        // =====================================================================

        /**
         * @brief Submit parallel read-only per-element jobs.
         * @param func  void(const T& item)
         */
        template<typename Func>
        void ForEachAsync(Func&& func, size_t minChunkSize = 64)
        {
            if (m_Data.empty()) return;

            const T* data = m_Data.data();
            ParallelForAsync(m_Data.size(),
                [data, func](size_t begin, size_t end)
                {
                    for (size_t i = begin; i < end; ++i)
                        func(data[i]);
                }, minChunkSize);
        }

        /**
         * @brief Submit parallel read-only range-based jobs.
         * @param func  void(const T* begin, const T* end)
         */
        template<typename Func>
        void DispatchAsync(Func&& func, size_t minChunkSize = 64)
        {
            if (m_Data.empty()) return;

            const T* data = m_Data.data();
            ParallelForAsync(m_Data.size(),
                [data, func](size_t begin, size_t end)
                {
                    func(data + begin, data + end);
                }, minChunkSize);
        }

        // =====================================================================
        // Sync iteration  (use in OnPrepare or OnMerge — main thread only)
        // =====================================================================

        /** @param func  void(const T& item) */
        template<typename Func>
        void ForEach(Func&& func) const
        {
            for (const T& item : m_Data)
                func(item);
        }

    private:
        std::vector<T>            m_Data;
        std::vector<entt::entity> m_Entities;
    };

} // namespace Cosmic
