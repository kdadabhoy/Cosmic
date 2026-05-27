// JobSystem.cpp
// Last Modified: 5/27/26

#include "jobs/JobSystem.h"
#include "core/Log.h"

namespace Cosmic
{
    // =========================================================================
    // Singleton
    // =========================================================================

    JobSystem& JobSystem::Get()
    {
        // Guaranteed by the C++ standard to be initialized exactly once
        // (thread-safe as of C++11).
        static JobSystem s_Instance;
        return s_Instance;
    }

    // =========================================================================
    // Initialize
    // =========================================================================

    void JobSystem::Initialize()
    {
        if (m_Initialized)
        {
            CS_CORE_WARN("JobSystem::Initialize called more than once — ignoring.");
            return;
        }

        // -----------------------------------------------------------------------
        // WINDOWS CORE DISCOVERY
        // GetSystemInfo fills a SYSTEM_INFO struct. dwNumberOfProcessors is the
        // count of *logical* processors (hardware threads, not physical cores).
        // On a 6-core/12-thread i7 this returns 12, giving us 11 workers.
        //
        // Why logical rather than physical?
        // Each hardware thread can be scheduled by the OS independently. Using
        // logical count keeps all execution units busy during compute-heavy
        // phases like physics batch processing. If hyper-threading hurts your
        // specific workload (cache pressure, branch-heavy code), clamp this to
        // GetSystemInfo result divided by 2 instead.
        // -----------------------------------------------------------------------
        SYSTEM_INFO sysInfo = {};
        GetSystemInfo(&sysInfo);

        m_CoreCount = static_cast<uint32_t>(sysInfo.dwNumberOfProcessors);

        // Reserve one slot for the main thread — spawning coreCount workers
        // would produce coreCount+1 schedulable threads total, which forces the
        // OS scheduler to context-switch unnecessarily.
        m_WorkerCount = (m_CoreCount > 1) ? (m_CoreCount - 1) : 1;

        CS_CORE_INFO("JobSystem: Detected {0} logical cores. Spawning {1} worker thread(s).",
            m_CoreCount, m_WorkerCount);

        // -----------------------------------------------------------------------
        // Spawn persistent worker threads
        // Each thread blocks on m_WorkAvailable until a job arrives. After
        // completing its job it checks for more work and goes back to sleep if
        // the queue is empty — zero OS thread creation overhead per frame.
        // -----------------------------------------------------------------------
        m_Workers.reserve(m_WorkerCount);
        for (uint32_t i = 0; i < m_WorkerCount; ++i)
        {
            m_Workers.emplace_back(&JobSystem::WorkerThread, this);
        }

        m_Initialized = true;
        CS_CORE_INFO("JobSystem: Thread pool initialised successfully.");
    }

    // =========================================================================
    // Shutdown
    // =========================================================================

    void JobSystem::Shutdown()
    {
        if (!m_Initialized || m_Workers.empty()) return;

        // Signal all workers to stop after draining the queue.
        {
            std::lock_guard<std::mutex> lock(m_QueueMutex);
            m_Stopping.store(true, std::memory_order_release);
        }

        // Wake every sleeping worker so they can observe m_Stopping and exit.
        m_WorkAvailable.notify_all();

        // Join all worker threads. This blocks until the last job finishes.
        for (auto& worker : m_Workers)
        {
            if (worker.joinable())
                worker.join();
        }

        m_Workers.clear();
        m_Initialized = false;
        CS_CORE_INFO("JobSystem: Thread pool shut down cleanly.");
    }

    // =========================================================================
    // Submit
    // =========================================================================

    void JobSystem::Submit(Job job)
    {
        {
            std::lock_guard<std::mutex> lock(m_QueueMutex);
            m_JobQueue.push(std::move(job));
        }
        // Notify exactly one waiting worker. If all workers are busy the job
        // sits in the queue until a worker finishes its current task.
        m_WorkAvailable.notify_one();
    }

    // =========================================================================
    // WaitIdle
    // =========================================================================

    void JobSystem::WaitIdle()
    {
        std::unique_lock<std::mutex> lock(m_QueueMutex);

        // Wait condition: queue is empty AND no workers are executing a job.
        // m_ActiveJobs is decremented by the worker *before* it tries to pick
        // up another job, so the combination of both checks is required to
        // guarantee all work has truly completed.
        m_AllIdle.wait(lock, [this]()
        {
            return m_JobQueue.empty() && m_ActiveJobs.load(std::memory_order_acquire) == 0;
        });
    }

    // =========================================================================
    // WorkerThread — runs for the lifetime of the thread pool
    // =========================================================================

    void JobSystem::WorkerThread()
    {
        while (true)
        {
            Job job;

            // ---------------------------------------------------------------
            // Scope 1: Try to dequeue a job
            // ---------------------------------------------------------------
            {
                std::unique_lock<std::mutex> lock(m_QueueMutex);

                // Block until there is work OR we are asked to stop.
                m_WorkAvailable.wait(lock, [this]()
                {
                    return !m_JobQueue.empty() || m_Stopping.load(std::memory_order_acquire);
                });

                // If the pool is shutting down and the queue is drained, exit.
                if (m_Stopping.load(std::memory_order_acquire) && m_JobQueue.empty())
                    return;

                // Claim the front job.
                job = std::move(m_JobQueue.front());
                m_JobQueue.pop();

                // Increment *before* releasing the lock so WaitIdle() cannot
                // observe an empty queue with zero active jobs prematurely
                // (i.e. between the pop and the actual execution below).
                m_ActiveJobs.fetch_add(1, std::memory_order_acq_rel);
            }

            // ---------------------------------------------------------------
            // Scope 2: Execute the job outside the lock
            // This is critical — holding the lock during execution would
            // serialize all workers through the mutex, defeating parallelism.
            // ---------------------------------------------------------------
            job();

            // ---------------------------------------------------------------
            // Scope 3: Mark this job done and potentially wake WaitIdle()
            // ---------------------------------------------------------------
            {
                // Decrement the active-job counter.
                uint32_t remaining = m_ActiveJobs.fetch_sub(1, std::memory_order_acq_rel) - 1;

                // If the counter just hit zero and the queue is empty, notify
                // anyone blocked in WaitIdle().
                if (remaining == 0)
                {
                    // Re-acquire the lock briefly so the notify + condition check
                    // in WaitIdle() are atomic with respect to each other.
                    std::lock_guard<std::mutex> lock(m_QueueMutex);
                    m_AllIdle.notify_all();
                }
            }
        }
    }

} // namespace Cosmic
