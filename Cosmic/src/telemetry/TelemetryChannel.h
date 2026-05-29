#pragma once

// TelemetryChannel.h
// Last Modified: 5/29/2026

/**
 * Shared POD types used by every telemetry component.
 * No dependencies beyond <string> and <vector> — intentionally lightweight
 * so DataRecorder, DataPlayer, and TelemetryPanel can all include this
 * without pulling in any engine headers.
 */

#include <string>
#include <vector>

namespace Cosmic
{
    /**
     * @brief One recorded snapshot: a timestamp (seconds) plus one float per channel.
     *
     * Timestamps are set by DataRecorder based on m_ElapsedTime and by DataPlayer
     * based on the current playback position. TelemetryPanel stores these in its
     * circular buffers for ImPlot display.
     */
    struct TelemetryFrame
    {
        float              timestamp = 0.0f;
        std::vector<float> values;
    };

    /**
     * @brief Identity and channel metadata for one recorded entity.
     *
     * Stored inside DataRecorder::EntityRecord and DataPlayer::PlayerEntityData.
     * Also returned by GetInfo() for UI display in TelemetryPanel.
     */
    struct EntityTelemetryInfo
    {
        std::string              name;     // e.g. "Agent_042"
        std::string              tag;      // e.g. "Agent"
        std::vector<std::string> channels; // e.g. {"PosX","PosY","Speed","Heading","Power"}
    };

} // namespace Cosmic
