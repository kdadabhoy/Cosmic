#pragma once

// DataPlayer.h
// Last Modified: 5/29/2026

/**
 * ============================================================================
 * DataPlayer — binary telemetry playback with linear interpolation
 * ============================================================================
 *
 * OVERVIEW
 * --------
 * DataPlayer reads .bin files produced by DataRecorder and provides a
 * time-indexed playback interface. It supports variable speed, reverse
 * playback, seek-anywhere scrubbing, and linear interpolation so that
 * callers can query any position regardless of the recorded sample rate.
 *
 * LOADING
 * -------
 * Load() accepts either a directory (loads scene.bin first; falls back to
 * individual v1 .bin files if scene.bin is absent) or a single .bin path.
 * This deduplication prevents duplicate entities when a session folder
 * contains both scene.bin and legacy per-entity files.
 *
 * FORMAT SUPPORT
 * --------------
 * v1 — one entity per file (legacy)
 * v2 — all entities in one file, global sample_count
 * v3 — all entities in one file, per-entity sample_count
 *
 * INTERPOLATION
 * -------------
 * Given position P seconds and sample rate R Hz:
 *   t    = P * R
 *   i    = clamp((int)t, 0, sample_count - 2)
 *   frac = t - (float)i
 *   out.values[ch] = frames[i].values[ch] * (1 - frac)
 *                  + frames[i+1].values[ch] * frac
 *
 * ============================================================================
 */

#include "core/Core.h"
#include "telemetry/TelemetryChannel.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace Cosmic
{
    class COSMIC_API DataPlayer
    {
    public:
        DataPlayer()  = default;
        ~DataPlayer() = default;

        // -------------------------------------------------------------------------
        // Loading
        // -------------------------------------------------------------------------

        /**
         * @brief Load one entity (file) or many entities (folder).
         *
         * For a directory: loads scene.bin (v2/v3) if present; otherwise loads
         * all individual .bin files (v1). This prevents duplicate entities when
         * both scene.bin and legacy per-entity files coexist.
         *
         * On success all previously loaded data is replaced.
         *
         * @return True if at least one entity was loaded successfully.
         */
        bool Load(const std::string& folderOrFilePath);

        bool IsLoaded() const { return m_Loaded; }

        /** @brief Unload all data and reset playback state. */
        void Unload();

        // -------------------------------------------------------------------------
        // Playback controls
        // -------------------------------------------------------------------------

        void  SetSpeed(float speed)  { m_Speed = speed; }
        float GetSpeed()  const      { return m_Speed; }

        void  Play()                 { m_Playing = true; }
        void  Pause()                { m_Playing = false; }
        bool  IsPlaying() const      { return m_Playing; }

        void  SetPosition(float seconds);
        float GetPosition() const    { return m_Position; }
        float GetDuration() const    { return m_Duration; }

        /**
         * @brief Advance playback by dt * speed seconds. Auto-stops at endpoints.
         * Has no effect if paused or not loaded.
         */
        void Tick(float dt);

        // -------------------------------------------------------------------------
        // Data access
        // -------------------------------------------------------------------------

        /**
         * @brief Interpolate a frame for the named entity at the current position.
         *
         * @param entityName  Registered entity name (e.g. "Agent_00").
         * @param out         Receives the interpolated frame (timestamp = m_Position).
         * @return False if entity not found or no frames loaded.
         */
        bool GetFrame(const std::string& entityName, TelemetryFrame& out) const;

        /**
         * @brief Interpolate a frame at an arbitrary position without moving m_Position.
         *
         * Useful for reconstructing trails, seeking without side effects, or
         * querying historical positions during scrubbing.
         *
         * @param entityName  Registered entity name.
         * @param seconds     Query position in [0, GetDuration()]. Clamped automatically.
         * @param out         Receives the interpolated frame (timestamp = seconds).
         * @return False if entity not found or no frames loaded.
         */
        bool SampleAt(const std::string& entityName, float seconds, TelemetryFrame& out) const;

        std::vector<std::string>   GetEntityNames() const;
        const EntityTelemetryInfo* GetInfo(const std::string& name) const;

        /** @brief Sample rate of the loaded data (from file header). Returns 60 if not loaded. */
        float GetSampleRate() const;

    private:
        struct PlayerEntityData
        {
            EntityTelemetryInfo         info;
            std::vector<TelemetryFrame> frames;
            float sampleRate = 60.0f;
        };

        bool LoadBinaryFile(const std::string& filepath);
        void Interpolate(const PlayerEntityData& data, float t, TelemetryFrame& out) const;

        std::vector<PlayerEntityData>             m_Entities;
        std::unordered_map<std::string, uint32_t> m_NameToId;

        float m_Position = 0.0f;
        float m_Duration = 0.0f;
        float m_Speed    = 1.0f;
        bool  m_Playing  = false;
        bool  m_Loaded   = false;
    };

} // namespace Cosmic
