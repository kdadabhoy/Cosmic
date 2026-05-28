#pragma once

// ParallelFor.h
// Last Modified: 5/27/26

/**
 * ============================================================================
 * COSMIC ENGINE — ParallelFor Utility
 * ============================================================================
 *
 * OVERVIEW
 * --------
 * ParallelFor is a zero-overhead, header-only utility that distributes a
 * contiguous index range (or span of elements) across all available worker
 * threads in the JobSystem. It is the primary tool for data-parallel component
 * processing — the equivalent of Unity DOTS' IJobParallelFor.
 *
 * DESIGN PRINCIPLES
 * -----------------
 * 1. Cache-friendly chunking: The range is divided into N contiguous chunks
 * (where N = worker count). Each worker processes a contiguous sub-range,
 * maximising L1/L2 cache hit rates on both reading and writing components.
 * Interleaved or strided access patterns would cause false-sharing and
 * cache-line bouncing between cores.
 *
 * 2. No heap allocation per call: Chunk boundaries are computed on the stack.
 * The lambda + index range is the only allocation, and modern compilers
 * inline trivial lambdas into the job closure directly.
 *
 * 3. Automatic synchronisation: ParallelFor always calls WaitIdle() before
 * returning to the caller. This means every call site can treat the
 * function as a synchronous operation despite the parallel execution.
 * Results are safe to read immediately after the call returns.
 *
 * 4. Serial fallback: If the JobSystem has only one worker (e.g. single-core
 * machine or unit-test environment), the entire range executes on the
 * calling thread with zero overhead.
 *
 * FUNCTION SIGNATURES
 * -----------------------------------------------------------------------
 * // Synchronous — submits jobs AND calls WaitIdle before returning.
 * // Use for standalone parallel work outside of a ParallelSystem.
 * ParallelFor(size_t totalCount, auto func);
 * ParallelForEach(T* data, size_t count, auto func);
 * ParallelForEachIndexed(T* data, size_t count, auto func);
 *
 * // Async — submits jobs, does NOT call WaitIdle.
 * // Use inside ParallelSystem::OnParallelExecute. The Scene calls
 * // JobSystem::WaitIdle() once after all systems have submitted.
 * ParallelForAsync(size_t totalCount, auto func);
 * ParallelForEachAsync(T* data, size_t count, auto func);
 * ParallelForEachIndexedAsync(T* data, size_t count, auto func);
 * -----------------------------------------------------------------------
 *
 * USAGE EXAMPLES
 * -----------------------------------------------------------------------
 * // 1. Move all transform positions in parallel
 * auto* positions = scene.GetComponentArray<PositionComponent>();
 * ParallelForEach(positions, count, [dt](PositionComponent* begin, PositionComponent* end)
 * {
 * for (auto* p = begin; p != end; ++p)
 * p->x += p->velocityX * dt;
 * });
 *
 * // 2. Index-based (useful when you need entity IDs alongside components)
 * ParallelFor(entityCount, [&](size_t begin, size_t end)
 * {
 * for (size_t i = begin; i < end; ++i)
 * ApplyGravity(transforms[i], rigidBodies[i], dt);
 * });
 * -----------------------------------------------------------------------
 *
 * IMPORTANT CONSTRAINTS
 * - Elements within a single chunk are processed serially on one thread.
 * Intra-chunk ordering is preserved; inter-chunk ordering is not.
 * - Do NOT use ParallelFor for code that writes to shared mutable state
 * without external synchronisation (e.g. appending to a std::vector).
 * Use a DoubleBuffer<T> or per-chunk accumulation + merge instead.
 * - Nested ParallelFor calls will NOT deadlock with a fixed-size pool,
 * but they will saturate the queue and may cause starvation. Prefer
 * flat, single-level parallelism.
 *
 * ============================================================================
 */

#include "jobs/JobSystem.h"
#include <cstddef>
#include <algorithm>

namespace Cosmic
{
    // =========================================================================
    // ParallelFor — index range variant
    // =========================================================================

    /**
     * @brief Distribute [0, totalCount) across all workers.
     *
     * @param totalCount  Number of elements / iterations to process.
     * @param func        Callable with signature void(size_t begin, size_t end).
     * Receives an exclusive sub-range [begin, end).
     * @param minChunkSize Minimum number of elements per chunk. Prevents
     * spawning jobs with trivially small workloads where the
     * scheduling overhead exceeds the computation cost.
     * Defaults to 64 — tune per system based on profiling.
     */
    template<typename Func>
    void ParallelFor(size_t totalCount, Func&& func, size_t minChunkSize = 64)
    {
        if (totalCount == 0) return;

        JobSystem& js = JobSystem::Get();

        // -----------------------------------------------------------------
        // Serial fast-path: single element, single worker, or count below
        // the minimum chunk threshold.
        // -----------------------------------------------------------------
        const uint32_t workerCount = js.GetWorkerCount();
        if (workerCount <= 1 || totalCount <= minChunkSize)
        {
            func(0, totalCount);
            return;
        }

        // -----------------------------------------------------------------
        // Chunk calculation
        // We want as many chunks as workers, but no chunk smaller than
        // minChunkSize. Integer ceiling division avoids an off-by-one where
        // the last chunk gets an extra element due to flooring.
        // -----------------------------------------------------------------
        const size_t maxChunks = static_cast<size_t>(workerCount);
        const size_t chunkSize = std::max(minChunkSize, (totalCount + maxChunks - 1) / maxChunks);
        const size_t chunkCount = (totalCount + chunkSize - 1) / chunkSize;

        // -----------------------------------------------------------------
        // Submit one job per chunk. The lambda captures func by reference —
        // the calling thread's stack frame must remain valid until WaitIdle()
        // returns. Because WaitIdle() is called below before this function
        // returns, that invariant is always satisfied.
        // -----------------------------------------------------------------
        for (size_t c = 0; c < chunkCount; ++c)
        {
            const size_t begin = c * chunkSize;
            const size_t end = std::min(begin + chunkSize, totalCount);

            // Capture by value to avoid dangling references to the loop variable c.
            js.Submit([&func, begin, end]()
                {
                    func(begin, end);
                });
        }

        // Always synchronise before returning — callers can read results immediately.
        js.WaitIdle();
    }

    // =========================================================================
    // ParallelForAsync — index range variant, no WaitIdle
    // =========================================================================

    /**
     * @brief Like ParallelFor, but does NOT call WaitIdle before returning.
     *
     * Use this inside ParallelSystem::OnParallelExecute so that all parallel
     * systems can submit their jobs before the Scene issues a single WaitIdle
     * barrier. Calling the synchronous ParallelFor inside OnParallelExecute
     * would stall after each system, serialising the pipeline.
     *
     * The caller is responsible for ensuring WaitIdle (or the Scene's barrier)
     * is called before reading any results.
     */
    template<typename Func>
    void ParallelForAsync(size_t totalCount, Func&& func, size_t minChunkSize = 64)
    {
        if (totalCount == 0) return;

        JobSystem& js = JobSystem::Get();

        const uint32_t workerCount = js.GetWorkerCount();
        if (workerCount <= 1 || totalCount <= minChunkSize)
        {
            func(0, totalCount);
            return;
        }

        const size_t maxChunks = static_cast<size_t>(workerCount);
        const size_t chunkSize = std::max(minChunkSize, (totalCount + maxChunks - 1) / maxChunks);
        const size_t chunkCount = (totalCount + chunkSize - 1) / chunkSize;

        for (size_t c = 0; c < chunkCount; ++c)
        {
            const size_t begin = c * chunkSize;
            const size_t end = std::min(begin + chunkSize, totalCount);

            // Capture func BY VALUE. Unlike the synchronous ParallelFor (which
            // calls WaitIdle before returning, keeping func alive), the async
            // variant returns immediately. Capturing by reference would leave
            // workers holding a dangling pointer once the caller's stack unwinds.
            js.Submit([func, begin, end]()
                {
                    func(begin, end);
                });
        }
        // No WaitIdle — caller's Scene barrier covers this.
    }

    // =========================================================================
    // ParallelForEach — typed pointer span variant
    // =========================================================================

    /**
     * @brief Distribute a contiguous array of T across all workers.
     *
     * @param data        Pointer to the first element in the array.
     * @param count       Number of elements in the array.
     * @param func        Callable with signature void(T* begin, T* end).
     * Receives a pointer sub-range [begin, end).
     * @param minChunkSize Minimum elements per chunk (default 64).
     *
     * This overload is preferred over the index variant when you only need
     * to access a single component array, because the pointer arithmetic is
     * encapsulated and the call site reads more clearly.
     */
    template<typename T, typename Func>
    void ParallelForEach(T* data, size_t count, Func&& func, size_t minChunkSize = 64)
    {
        if (!data || count == 0) return;

        ParallelFor(count, [data, &func](size_t begin, size_t end)
            {
                func(data + begin, data + end);
            }, minChunkSize);
    }

    /** @brief Async variant of ParallelForEach — no WaitIdle. Use in OnParallelExecute. */
    template<typename T, typename Func>
    void ParallelForEachAsync(T* data, size_t count, Func&& func, size_t minChunkSize = 64)
    {
        if (!data || count == 0) return;

        // Capture func BY VALUE — see ParallelForAsync for the reasoning.
        ParallelForAsync(count, [data, func](size_t begin, size_t end)
            {
                func(data + begin, data + end);
            }, minChunkSize);
    }

    // =========================================================================
    // ParallelForEachIndexed — element + global index variant
    // =========================================================================

    /**
     * @brief Like ParallelForEach, but also provides the global element index.
     *
     * @param data        Pointer to the first element.
     * @param count       Number of elements.
     * @param func        Callable with signature void(T& element, size_t index).
     * Called once per element with its global array index.
     * @param minChunkSize Minimum elements per chunk (default 64).
     *
     * Use this variant when the index is needed alongside the element value
     * (e.g. to look up a corresponding entry in a parallel array, or to
     * map back to an entity ID).
     */
    template<typename T, typename Func>
    void ParallelForEachIndexed(T* data, size_t count, Func&& func, size_t minChunkSize = 64)
    {
        if (!data || count == 0) return;

        ParallelFor(count, [data, &func](size_t begin, size_t end)
            {
                for (size_t i = begin; i < end; ++i)
                    func(data[i], i);
            }, minChunkSize);
    }

    /** @brief Async variant of ParallelForEachIndexed — no WaitIdle. Use in OnParallelExecute. */
    template<typename T, typename Func>
    void ParallelForEachIndexedAsync(T* data, size_t count, Func&& func, size_t minChunkSize = 64)
    {
        if (!data || count == 0) return;

        // Capture func BY VALUE — see ParallelForAsync for the reasoning.
        ParallelForAsync(count, [data, func](size_t begin, size_t end)
            {
                for (size_t i = begin; i < end; ++i)
                    func(data[i], i);
            }, minChunkSize);
    }

} // namespace Cosmic