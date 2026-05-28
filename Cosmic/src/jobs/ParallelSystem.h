#pragma once

// ParallelSystem.h
// Last Modified: 5/27/26

/**
 * ============================================================================
 * COSMIC ENGINE — ParallelSystem Base Class
 * ============================================================================
 *
 * OVERVIEW
 * --------
 * ParallelSystem extends the existing Cosmic::System interface with two
 * additional lifecycle hooks specifically designed for parallel, data-oriented
 * execution. It sits between the base System (single-threaded sequential logic)
 * and the fully parallel execution model described in the DOD architecture.
 *
 * ARCHITECTURE RELATIONSHIP
 * -------------------------
 *
 *   Cosmic::System           (existing — sequential, pointer-based ECS logic)
 *         │
 *         └─── Cosmic::ParallelSystem   (new — data-parallel, DOD-style bulk)
 *
 * Both types are registered in the Scene's system registry. The Scene ticks
 * them through separate passes to guarantee ordering:
 *
 *   Frame N execution order:
 *   ┌──────────────────────────────────────────────────────────────────────┐
 *   │ PASS A — Sequential Systems (main thread)                           │
 *   │   for each System* s  → s->OnUpdate(scene, dt)                     │
 *   │   for each System* s  → s->OnFixedUpdate(scene, dt)                │
 *   │                                                                      │
 *   │ PASS B — Parallel Prepare (main thread, single-threaded)           │
 *   │   for each ParallelSystem* ps → ps->OnPrepare(scene, dt)           │
 *   │                                                                      │
 *   │ PASS C — Parallel Execute (all worker threads simultaneously)       │
 *   │   for each ParallelSystem* ps → ps->OnParallelExecute(scene, dt)   │
 *   │   JobSystem::WaitIdle()   ← main thread blocks here                 │
 *   │                                                                      │
 *   │ PASS D — Parallel Merge (main thread, single-threaded)             │
 *   │   for each ParallelSystem* ps → ps->OnMerge(scene, dt)             │
 *   └──────────────────────────────────────────────────────────────────────┘
 *
 * HOW TO IMPLEMENT A PARALLEL SYSTEM
 * ------------------------------------
 * Override OnPrepare, OnParallelExecute, and OnMerge. Leave OnUpdate and
 * OnFixedUpdate as empty stubs unless you have sequential logic that must
 * run before the parallel phase.
 *
 *   class MyParallelSystem : public Cosmic::ParallelSystem
 *   {
 *   public:
 *       void OnPrepare(Scene& scene, float dt) override
 *       {
 *           // Snapshot component data into a DoubleBuffer or local array.
 *           // Reserve output buffers. Do NOT submit jobs here.
 *       }
 *
 *       void OnParallelExecute(Scene& scene, float dt) override
 *       {
 *           // Submit jobs using the ASYNC variants so the Scene's single
 *           // WaitIdle barrier covers all systems together:
 *           //
 *           //   ParallelForAsync(count, [src, dst](size_t begin, size_t end) { ... });
 *           //   ParallelForEachAsync(data, count, [](T* begin, T* end) { ... });
 *           //
 *           // DO NOT use the synchronous ParallelFor here — it calls WaitIdle
 *           // internally and serialises systems against each other.
 *           // DO NOT call JobSystem::WaitIdle() yourself — the Scene does it.
 *       }
 *
 *       void OnMerge(Scene& scene, float dt) override
 *       {
 *           // All jobs from all systems are now complete (Scene called WaitIdle).
 *           // Swap double buffers, write back to the registry, or update derived
 *           // state. Single-threaded, safe to perform structural registry changes.
 *       }
 *   };
 *
 * IMPORTANT: OnParallelExecute for ALL registered parallel systems runs
 * before WaitIdle() is called. This means multiple parallel systems can
 * have their jobs in-flight at the same time — maximising utilisation.
 * Systems that depend on each other's output must use sequential Systems
 * or be ordered such that Merge A runs before Execute B in the schedule.
 *
 * ============================================================================
 */

#include "scene/System.h"
#include <vector>

namespace Cosmic
{
    class Scene; // forward declaration

    // =========================================================================
    // ISystemQuery
    // =========================================================================

    /**
     * @brief Abstract interface implemented by ReadWriteQuery<T> and ReadOnlyQuery<T>.
     *
     * ParallelSystem holds a list of these and the Scene calls Stage/Commit
     * automatically around the parallel passes. Users never interact with this
     * interface directly — it is an engine-internal lifecycle contract.
     */
    class COSMIC_API ISystemQuery
    {
    public:
        virtual ~ISystemQuery() = default;

        /** @brief Snapshot component state from the registry. Called before OnPrepare. */
        virtual void Stage(Scene& scene)  = 0;

        /** @brief Write staged results back to the registry. Called after OnMerge. */
        virtual void Commit(Scene& scene) = 0;
    };

    // =========================================================================
    // ParallelSystem
    // =========================================================================

    class COSMIC_API ParallelSystem : public System
    {
    public:
        virtual ~ParallelSystem() = default;

        // =====================================================================
        // PASS B — Prepare
        // =====================================================================

        /**
         * @brief Single-threaded setup called before any parallel jobs launch.
         *
         * Use this hook to:
         *   - Snapshot current component state into read buffers / DoubleBuffers
         *   - Resize output arrays to match entity count
         *   - Compute per-frame constants (gravity, dt-scaled values, etc.)
         *   - Pre-sort or pre-filter entity lists
         *
         * Runs on the main thread. No race conditions; safe to read/write
         * the registry freely. Do NOT submit jobs to the JobSystem here.
         *
         * @param scene  The scene owning this system.
         * @param dt     Scaled variable delta-time (seconds).
         */
        virtual void OnPrepare(Scene& scene, float dt) {}

        // =====================================================================
        // PASS C — Parallel Execute
        // =====================================================================

        /**
         * @brief Submit parallel work to the JobSystem.
         *
         * Use ParallelForAsync() or ParallelForEachAsync() to dispatch jobs.
         * This function runs on the main thread; the submitted jobs run
         * concurrently on worker threads.
         *
         * RULES:
         * - Use the ASYNC variants (ParallelForAsync / ParallelForEachAsync).
         *   The synchronous variants (ParallelFor / ParallelForEach) call
         *   WaitIdle internally, serialising systems against each other.
         * - Do NOT call JobSystem::WaitIdle() yourself — the Scene calls it
         *   once after ALL systems have submitted, maximising job overlap.
         * - Do NOT modify the EnTT registry from a worker thread. Read from
         *   ComponentArray / DoubleBuffer read buffers; write to write buffers.
         * - Do NOT submit jobs from inside a running job (nested dispatch).
         *
         * @param scene  The scene owning this system.
         * @param dt     Scaled variable delta-time (seconds).
         */
        virtual void OnParallelExecute(Scene& scene, float dt) {}

        // =====================================================================
        // PASS D — Merge
        // =====================================================================

        /**
         * @brief Single-threaded merge of parallel results back into scene state.
         *
         * Called after JobSystem::WaitIdle() guarantees all worker jobs are done.
         * Use this hook to:
         *   - Swap DoubleBuffers
         *   - Write computed values back to EnTT components via WriteBack()
         *   - Apply structural changes (create/destroy entities)
         *   - Broadcast events triggered by parallel computation
         *
         * @param scene  The scene owning this system.
         * @param dt     Scaled variable delta-time (seconds).
         */
        virtual void OnMerge(Scene& scene, float dt) {}

        // =====================================================================
        // Fixed-step variants (optional override)
        // =====================================================================

        /**
         * @brief Fixed-timestep equivalents of the three parallel hooks.
         *
         * Override these if your parallel system must run at a deterministic
         * 60 Hz fixed step (e.g. physics simulation) rather than at the
         * variable frame rate. The Scene ticks these through the same
         * Prepare→Execute→WaitIdle→Merge pipeline as the variable hooks.
         */
        virtual void OnFixedPrepare(Scene& scene, float fixedDt)          {}
        virtual void OnFixedParallelExecute(Scene& scene, float fixedDt)  {}
        virtual void OnFixedMerge(Scene& scene, float fixedDt)            {}

        // =====================================================================
        // Query management — ENGINE USE ONLY
        // These are called by the Scene. Do not call them from user code.
        // =====================================================================

        /**
         * @brief Register a SystemQuery with this system.
         *
         * Called automatically by ReadWriteQuery<T> and ReadOnlyQuery<T>
         * constructors when you pass `this` as the owner. You never call
         * this directly.
         */
        void RegisterQuery(ISystemQuery* query)
        {
            m_Queries.push_back(query);
        }

        /**
         * @brief Snapshot all registered queries from the registry.
         * Called by the Scene before OnPrepare each frame.
         */
        void StageQueries(Scene& scene)
        {
            for (auto* q : m_Queries)
                q->Stage(scene);
        }

        /**
         * @brief Write all registered query results back to the registry.
         * Called by the Scene after OnMerge each frame.
         */
        void CommitQueries(Scene& scene)
        {
            for (auto* q : m_Queries)
                q->Commit(scene);
        }

    private:
        std::vector<ISystemQuery*> m_Queries; // non-owning; registered by query constructors
    };

} // namespace Cosmic
