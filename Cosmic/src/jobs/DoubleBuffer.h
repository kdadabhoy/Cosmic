#pragma once

// DoubleBuffer.h
// Last Modified: 5/27/26

/**
 * ============================================================================
 * COSMIC ENGINE — DoubleBuffer<T>
 * ============================================================================
 *
 * OVERVIEW
 * --------
 * DoubleBuffer<T> is a thread-safe double-buffering container for component
 * arrays. It is the solution to the inter-entity data race problem that occurs
 * in parallel systems where entities read from AND write to the same logical
 * dataset — for example, ball-to-ball collision, proximity queries, flocking,
 * or any simulation where one entity's output depends on another entity's
 * current state.
 *
 * THE PROBLEM
 * -----------
 * Consider a collision system processing 1000 balls in parallel. Thread A
 * is computing ball[0]'s new velocity based on ball[1]'s current position.
 * Thread B is simultaneously updating ball[1]'s position. Thread A reads a
 * partially-written position — a data race with undefined behavior.
 *
 * THE SOLUTION: READ-WRITE DOUBLE BUFFERING
 * ------------------------------------------
 * At the start of a parallel phase, the "read buffer" holds the authoritative
 * world state from the start of this frame. Every parallel worker reads
 * exclusively from this snapshot. Workers write their results to the separate
 * "write buffer". Because workers never write to the read buffer and never
 * read from the write buffer, there are zero data races regardless of how
 * many threads are active.
 *
 * At the end of the parallel phase (after WaitIdle()), the main thread calls
 * Swap(), which atomically promotes the write buffer to the new read buffer.
 * The old read buffer becomes the new write buffer for the next frame.
 *
 * BUFFER LAYOUT
 * -------------
 *   Frame N:   m_Buffers[m_ReadIndex]  = { state from end of frame N-1 }
 *              m_Buffers[!m_ReadIndex] = { write target during frame N  }
 *
 *   After Swap:
 *              m_Buffers[m_ReadIndex]  = { state from end of frame N   }
 *              m_Buffers[!m_ReadIndex] = { stale, will be overwritten  }
 *
 * USAGE
 * -----------------------------------------------------------------------
 *   // Declare (e.g. inside a Scene or a System)
 *   DoubleBuffer<BallPhysicsState> m_PhysicsBuffer;
 *   m_PhysicsBuffer.Resize(entityCount);
 *
 *   // --- Main thread, start of parallel phase ---
 *   // Populate the read buffer with this frame's starting state
 *   auto* writeTarget = m_PhysicsBuffer.GetWriteBuffer();
 *   // ... copy current component values into writeTarget ...
 *   m_PhysicsBuffer.Swap(); // promote: write becomes new read
 *
 *   // --- Parallel workers ---
 *   const auto* readSnap = m_PhysicsBuffer.GetReadBuffer();
 *   auto*       writeDst = m_PhysicsBuffer.GetWriteBuffer();
 *   // workers: read from readSnap[i], write to writeDst[i]
 *
 *   js.WaitIdle(); // wait for all workers to finish
 *   m_PhysicsBuffer.Swap(); // promote results for next phase / render
 * -----------------------------------------------------------------------
 *
 * THREAD SAFETY CONTRACT
 * - GetReadBuffer()  : thread-safe for concurrent reads.
 * - GetWriteBuffer() : each index in the write buffer must be owned by
 *                      exactly one thread. Use ParallelFor to ensure
 *                      disjoint chunk ownership.
 * - Swap()           : NOT thread-safe — call only from the main thread
 *                      after WaitIdle() confirms all workers are idle.
 * - Resize()         : NOT thread-safe — call only before the parallel
 *                      phase begins.
 *
 * ============================================================================
 */

#include "core/Core.h"

#include <vector>
#include <cstdint>
#include <cstring> // memcpy

namespace Cosmic
{
    template<typename T>
    class DoubleBuffer
    {
    public:
        // =====================================================================
        // Construction
        // =====================================================================

        DoubleBuffer() = default;

        /**
         * @brief Construct with a pre-allocated capacity.
         * @param capacity  Number of T elements to allocate in each buffer.
         */
        explicit DoubleBuffer(size_t capacity)
        {
            Resize(capacity);
        }

        // =====================================================================
        // Sizing
        // =====================================================================

        /**
         * @brief Resize both internal buffers to hold exactly `count` elements.
         *
         * This reallocates both buffers. Any existing data is destroyed.
         * Call only from the main thread before the parallel phase begins.
         *
         * @param count  Number of T elements per buffer.
         */
        void Resize(size_t count)
        {
            m_Buffers[0].assign(count, T{});
            m_Buffers[1].assign(count, T{});
            m_ReadIndex = 0;
        }

        /** @brief Current element count of each buffer. */
        size_t Count() const { return m_Buffers[0].size(); }

        /** @brief True if both buffers have been allocated. */
        bool   IsReady() const { return !m_Buffers[0].empty(); }

        // =====================================================================
        // Buffer access
        // =====================================================================

        /**
         * @brief Read-only pointer to the current authoritative state buffer.
         *
         * Thread-safe for concurrent reads from multiple worker threads.
         * Never write to this pointer.
         */
        const T* GetReadBuffer() const
        {
            return m_Buffers[m_ReadIndex].data();
        }

        /**
         * @brief Read-write pointer to the current write target.
         *
         * Workers should write their per-element results here. Each element
         * index must be owned by exactly one thread — use ParallelFor to
         * guarantee disjoint ownership.
         */
        T* GetWriteBuffer()
        {
            return m_Buffers[m_WriteIndex()].data();
        }

        /**
         * @brief Index-based element access on the read buffer.
         * Provided for convenience in single-threaded code (e.g. Scene render).
         */
        const T& ReadAt(size_t index) const
        {
            return m_Buffers[m_ReadIndex][index];
        }

        /**
         * @brief Index-based element access on the write buffer.
         */
        T& WriteAt(size_t index)
        {
            return m_Buffers[m_WriteIndex()][index];
        }

        // =====================================================================
        // Swap
        // =====================================================================

        /**
         * @brief Promote the write buffer to the authoritative read buffer.
         *
         * This is an O(1) pointer swap — no data is copied. Call only from
         * the main thread after WaitIdle() confirms all workers have finished.
         *
         * The old read buffer becomes the write target for the next frame.
         * Workers will overwrite its stale values during the next parallel phase.
         */
        void Swap()
        {
            m_ReadIndex ^= 1u; // toggle between 0 and 1
        }

        // =====================================================================
        // Convenience helpers
        // =====================================================================

        /**
         * @brief Copy all values from the read buffer into the write buffer.
         *
         * Useful as a "carry-forward" step at the start of a frame when most
         * elements will not be modified — workers only need to write the delta.
         * Call from the main thread before submitting jobs.
         */
        void CopyReadToWrite()
        {
            const auto& src = m_Buffers[m_ReadIndex];
            auto&       dst = m_Buffers[m_WriteIndex()];
            std::memcpy(dst.data(), src.data(), src.size() * sizeof(T));
        }

    private:
        // =====================================================================
        // Internal helpers
        // =====================================================================

        /** @brief Index of the write buffer (always the opposite of m_ReadIndex). */
        uint32_t m_WriteIndex() const { return m_ReadIndex ^ 1u; }

        // =====================================================================
        // Storage
        // =====================================================================

        // Two buffers allocated contiguously for cache locality.
        // uint32_t index keeps Swap() to a single XOR — no branch, no pointer
        // swap, no atomic needed (Swap() is main-thread-only by contract).
        std::vector<T> m_Buffers[2];
        uint32_t       m_ReadIndex = 0;
    };

} // namespace Cosmic
