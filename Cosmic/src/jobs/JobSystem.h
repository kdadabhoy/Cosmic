#pragma once

// JobSystem.h
// Last Modified: 5/27/26

/**
 * ============================================================================
 * COSMIC ENGINE — JOB SYSTEM (Windows, Data-Oriented Architecture)
 * ============================================================================
 *
 * OVERVIEW
 * --------
 * The JobSystem is the engine's multi-threaded execution backbone. It creates
 * a fixed-size pool of persistent worker threads sized to the host machine's
 * logical CPU count. Submitting a job enqueues a callable onto a lock-based
 * work queue; idle workers block on a condition variable and wake the moment
 * work is available.
 *
 * WHY A PERSISTENT POOL?
 * Thread creation on Windows costs ~50–100 µs. For 60 Hz simulation, that
 * budget evaporates after spawning just two threads per frame. Persistent
 * workers eliminate that overhead entirely — threads are created once at
 * engine init and join only at shutdown.
 *
 * WINDOWS CORE DISCOVERY
 * The pool queries GetSystemInfo() for dwNumberOfProcessors (logical cores).
 * One slot is reserved for the main thread, so a 12-core machine spawns
 * 11 workers. This prevents the OS scheduler from fighting itself when
 * more threads than cores are runnable simultaneously.
 *
 * THREAD SAFETY CONTRACT
 * - Submit()     : thread-safe, callable from any thread at any time.
 * - WaitIdle()   : blocks the caller until ALL enqueued jobs are complete.
 *                  Call this at the end of a parallel phase before reading
 *                  results back on the main thread.
 * - Shutdown()   : must only be called from the main thread during teardown.
 *
 * USAGE (from Scene or a System)
 * -----------------------------------------------------------------------
 *   JobSystem& js = JobSystem::Get();
 *
 *   // Enqueue independent work units
 *   for (auto& chunk : chunks)
 *       js.Submit([&chunk]{ ProcessChunk(chunk); });
 *
 *   // Wait for all workers to finish before reading results
 *   js.WaitIdle();
 * -----------------------------------------------------------------------
 *
 * ============================================================================
 */

#include "core/Core.h"

#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>
#include <vector>
#include <cstdint>

// WIN32_LEAN_AND_MEAN and NOMINMAX are defined globally via CMake.
#include <Windows.h>

namespace Cosmic
{
    // =========================================================================
    // Job type alias
    // A Job is any zero-argument callable that returns void. Lambdas, free
    // functions, and std::bind results all qualify. Keep jobs small and
    // independent — they should not capture raw pointers that may be freed
    // before the job runs, and they must never block waiting on another job
    // (that would cause a deadlock with a fixed-size pool).
    // =========================================================================
    using Job = std::function<void()>;

    // =========================================================================
    // JobSystem
    // =========================================================================
    class COSMIC_API JobSystem
    {
    public:
        // ---------------------------------------------------------------------
        // Singleton access
        // The engine owns exactly one JobSystem instance, created during
        // Application::Initialize and destroyed during Application::Shutdown.
        // ---------------------------------------------------------------------
        static JobSystem& Get();

        // ---------------------------------------------------------------------
        // Lifecycle
        // ---------------------------------------------------------------------

        /**
         * @brief Initialize the thread pool.
         *
         * Queries the OS for the host machine's logical core count and spawns
         * (coreCount - 1) persistent worker threads. Reserving one slot for
         * the main thread keeps the OS scheduler from thrashing.
         *
         * Must be called exactly once, from the main thread, before any
         * Submit() calls. Calling Initialize() a second time is a no-op.
         */
        void Initialize();

        /**
         * @brief Drain the queue and join all worker threads.
         *
         * Safe to call only from the main thread during engine shutdown.
         * Blocks until every enqueued job has completed execution.
         */
        void Shutdown();

        // ---------------------------------------------------------------------
        // Work submission
        // ---------------------------------------------------------------------

        /**
         * @brief Enqueue a job for execution on the next available worker.
         *
         * Thread-safe. The job is appended to the shared queue and one
         * sleeping worker is notified. The caller retains no ownership of
         * the job after this call returns.
         *
         * @param job  Any callable matching void(). Prefer small, cache-
         *             friendly lambdas that capture by value or reference to
         *             stack-allocated data that outlives the job's execution.
         */
        void Submit(Job job);

        // ---------------------------------------------------------------------
        // Synchronisation
        // ---------------------------------------------------------------------

        /**
         * @brief Block the calling thread until the job queue is empty AND
         *        all currently executing jobs have returned.
         *
         * Call this from the main thread at the end of a parallel phase
         * (e.g. after submitting all chunk jobs for a ParallelFor) before
         * reading results back into scene state.
         *
         * It is safe to call WaitIdle() even if no jobs were submitted —
         * it returns immediately in that case.
         */
        void WaitIdle();

        // ---------------------------------------------------------------------
        // Hardware introspection
        // ---------------------------------------------------------------------

        /** @brief Total logical CPU cores detected on this machine. */
        uint32_t GetCoreCount()   const { return m_CoreCount; }

        /** @brief Number of worker threads in the pool (coreCount - 1). */
        uint32_t GetWorkerCount() const { return m_WorkerCount; }

        /** @brief True once Initialize() has been called successfully. */
        bool     IsInitialized()  const { return m_Initialized; }

        // ---- Read-only live stats (T18 — the Jobs panel) --------------------
        /** @brief Jobs waiting in the queue right now (brief lock). */
        uint32_t GetQueuedCount() const;
        /** @brief Jobs currently executing on workers. */
        uint32_t GetActiveCount() const { return m_ActiveJobs.load(std::memory_order_acquire); }
        /** @brief Total jobs completed since init (monotonic). */
        uint64_t GetCompletedCount() const { return m_CompletedJobs.load(std::memory_order_acquire); }

    private:
        // Not publicly constructible — access via Get()
        JobSystem()  = default;

        // NOT defaulted on purpose. A defaulted destructor would simply destroy
        // m_Workers, and destroying a *joinable* std::thread calls std::terminate().
        // That makes correctness depend on an external Shutdown() call having run
        // first — which is not guaranteed on every exit path (e.g. a teardown that
        // bypasses ~Application via exit()). The destructor therefore joins the
        // pool itself by calling Shutdown(), which is idempotent and safe to call
        // even after an explicit Shutdown(). Defined in JobSystem.cpp.
        ~JobSystem();

        // Non-copyable, non-movable singleton
        JobSystem(const JobSystem&)            = delete;
        JobSystem& operator=(const JobSystem&) = delete;
        JobSystem(JobSystem&&)                 = delete;
        JobSystem& operator=(JobSystem&&)      = delete;

        // The function each worker thread runs for its entire lifetime.
        void WorkerThread();

        // ----------------------------------------------------------------
        // State
        // ----------------------------------------------------------------

        std::vector<std::thread>    m_Workers;         // persistent worker pool
        std::queue<Job>             m_JobQueue;         // pending work
        mutable std::mutex          m_QueueMutex;       // guards m_JobQueue (mutable: read-only stats)
        std::condition_variable     m_WorkAvailable;    // wakes idle workers
        std::condition_variable     m_AllIdle;          // wakes WaitIdle() caller
        std::atomic<bool>           m_Stopping{ false }; // shutdown signal
        std::atomic<uint32_t>       m_ActiveJobs{ 0 };  // jobs currently executing
        std::atomic<uint64_t>       m_CompletedJobs{ 0 }; // T18 — monotonic completed count

        uint32_t                    m_CoreCount   = 1;
        uint32_t                    m_WorkerCount = 0;
        bool                        m_Initialized = false;
    };

} // namespace Cosmic
