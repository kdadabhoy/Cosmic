#pragma once

// DataRecorder.h
// Last Modified: 5/29/2026

/**
 * ============================================================================
 * DataRecorder — per-entity telemetry capture with parallel-safe Record()
 * ============================================================================
 *
 * OVERVIEW
 * --------
 * DataRecorder stores time-series float data for a set of named entities.
 * Each entity is registered once (Register), given a uint32_t ID, and then
 * one or more worker threads call Record(id, values) each fixed timestep.
 *
 * THREAD SAFETY MODEL
 * -------------------
 * m_Records is populated exclusively during Register() on the main thread and
 * NEVER resized after that, so m_Records[id] dereferences are lock-free.
 * Each EntityRecord has its own mutex held only during Record() (< 1 µs).
 * m_ElapsedTime is std::atomic<float>; Tick() and RecordImpl() use relaxed
 * ordering — on x86 this is a plain mov with no barrier overhead.
 *
 * INTERNAL STORAGE — columnar layout
 * ------------------------------------
 * Each EntityRecord stores channel data in separate per-channel vectors
 * (columns[ch][frame]), eliminating the per-frame heap allocation that
 * vector<TelemetryFrame> caused. RecordImpl() does only push_back on
 * pre-reserved float vectors — zero malloc in the hot path after
 * ReserveCapacity() is called.
 *
 * BINARY FILE FORMAT v1
 * ----------------------
 * Single supported format. Each data row prepends the recorded simulation
 * timestamp so the player reconstructs exact timing regardless of any time
 * scale active during recording.
 *
 *   [4]  char     magic[4]         = "CSMC"
 *   [4]  uint32   version          = 1
 *   [4]  uint32   entity_count
 *   [4]  float    sample_rate
 *
 *   -- entity descriptor table (entity_count entries) --
 *   For each entity:
 *     [64] char entity_name[64]
 *     [64] char entity_tag[64]
 *     [4]  uint32 channel_count
 *     [4]  uint32 sample_count      ← per-entity
 *     [channel_count × 32] char channel_name[32]
 *
 *   -- data table (entity_count contiguous blocks) --
 *   For each entity:
 *     [sample_count × (channel_count + 1) × 4] float32 row-major
 *     Each row: [timestamp, ch0, ch1, ..., ch(N-1)]
 *
 * ============================================================================
 */

#include "core/Core.h"
#include "telemetry/TelemetryChannel.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <initializer_list>

namespace Cosmic
{
    class COSMIC_API DataRecorder
    {
    public:
        DataRecorder()  = default;
        ~DataRecorder() { WaitForFlush(); }

        // -------------------------------------------------------------------------
        // Registration  (main-thread only, before any jobs run)
        // -------------------------------------------------------------------------

        /**
         * @brief Register an entity for recording and return its stable numeric ID.
         *
         * Must be called on the main thread before any parallel jobs run.
         * Calling Register() after the first Record() on a worker thread is
         * undefined behaviour (potential resize + iterator invalidation on m_Records).
         *
         * If an entity with the same name was already registered, returns its
         * existing ID without creating a duplicate record.
         */
        uint32_t Register(const std::string& name,
                          const std::string& tag,
                          const std::vector<std::string>& channels);

        /**
         * @brief Pre-allocate column and timestamp storage for all registered entities.
         *
         * Call once after all Register() calls, before recording begins. Eliminates
         * all realloc churn during capture. expectedFrames = duration_s * sample_rate.
         */
        void ReserveCapacity(size_t expectedFrames);

        // -------------------------------------------------------------------------
        // Recording  (thread-safe, hot path)
        // -------------------------------------------------------------------------

        /**
         * @brief Record one frame of channel data for the entity at the given ID.
         *
         * Thread-safe. Per-entity mutex held < 1 µs. After ReserveCapacity() this
         * path performs zero heap allocations.
         *
         * @param id      ID returned by Register().
         * @param values  One float per channel, matching the registration order.
         */
        void Record(uint32_t id, std::initializer_list<float> values);
        void Record(uint32_t id, const std::vector<float>& values);

        // -------------------------------------------------------------------------
        // Queries  (main-thread; safe to call between frames)
        // -------------------------------------------------------------------------

        /**
         * @brief Get the most recently recorded frame for live display.
         *
         * Returns false if the entity name is unknown or no frames have been
         * recorded yet. The returned frame is a copy — safe to inspect after
         * the lock is released.
         */
        bool GetCurrentFrame(const std::string& entityName, TelemetryFrame& out) const;

        /** @brief Return metadata for a registered entity, or nullptr if not found. */
        const EntityTelemetryInfo* GetInfo(const std::string& name) const;

        /** @brief Names of all registered entities, in registration order. */
        std::vector<std::string> GetEntityNames() const;

        // -------------------------------------------------------------------------
        // Time tracking
        // -------------------------------------------------------------------------

        /**
         * @brief Advance the recorder's internal elapsed-time counter.
         * Call once per fixed timestep while recording is active.
         */
        void  Tick(float dt);

        /** @brief Total simulated time recorded so far (seconds). */
        float GetRecordedDuration() const;

        // -------------------------------------------------------------------------
        // Export  (non-blocking; call from main thread after simulation ends)
        // -------------------------------------------------------------------------

        /**
         * @brief Snapshot all frame data and write files in a background thread.
         *
         * Non-blocking — returns immediately after snapshotting under per-entity locks.
         * Workers may continue calling Record() while the background thread writes.
         * Call WaitForFlush() to block until writes complete.
         *
         * Output layout:
         *   <baseFolder>/<sessionName>/scene.bin  — all entities, v3 binary format
         *   <baseFolder>/<sessionName>/<name>.csv — one CSV per entity
         *
         * @param baseFolder   Root output directory (e.g. "logs"). Created if absent.
         * @param sessionName  Subfolder name. If empty, an ISO-8601 timestamp is used.
         * @param sampleRate   Nominal sample rate written to the binary header (Hz).
         */
        void Flush(const std::string& baseFolder,
                   const std::string& sessionName = "",
                   float              sampleRate  = 60.0f);

        /** @brief Block until the background flush thread has finished. */
        void WaitForFlush();

        // -------------------------------------------------------------------------
        // Reset
        // -------------------------------------------------------------------------

        /**
         * @brief Drop all recorded frames but keep entity registrations and reserves.
         * Resets elapsed time to zero. Safe to call between simulation runs.
         */
        void Clear();

        /** @brief True if a background flush is currently in progress. */
        bool IsFlushing() const { return m_Flushing.load(); }

        /** @brief Total frame count of the first registered entity (proxy for recording length). */
        size_t GetTotalFrameCount() const;

    private:
        // -------------------------------------------------------------------------
        // Internal storage — columnar layout eliminates per-frame heap allocation
        // -------------------------------------------------------------------------

        struct EntityRecord
        {
            EntityTelemetryInfo         info;
            std::vector<float>          timestamps;           // [frame_index]
            std::vector<std::vector<float>> columns;          // [channel][frame_index]
            TelemetryFrame              currentFrame;         // most recent (live display)
            mutable std::mutex          mutex;                // held < 1 µs during Record()
        };

        // unique_ptr keeps mutex addresses stable if the outer vector ever reallocates
        // (though we never resize m_Records after registration is complete).
        std::vector<std::unique_ptr<EntityRecord>>  m_Records;
        std::unordered_map<std::string, uint32_t>   m_NameToId;

        mutable std::mutex      m_RegistryMutex;
        std::thread             m_FlushThread;
        std::atomic<bool>       m_Flushing{ false };
        std::atomic<float>      m_ElapsedTime{ 0.0f }; // written by Tick (main), read by RecordImpl (workers)

        void RecordImpl(uint32_t id, const float* values, size_t count);
    };

} // namespace Cosmic
