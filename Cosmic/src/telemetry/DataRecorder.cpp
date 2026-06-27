// DataRecorder.cpp
// Last Modified: 5/29/2026

#include "telemetry/DataRecorder.h"
#include "utils/DataExport.h"
#include "core/Log.h"

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <ctime>

namespace Cosmic
{
    // =========================================================================
    // Registration
    // =========================================================================

    uint32_t DataRecorder::Register(const std::string& name,
                                    const std::string& tag,
                                    const std::vector<std::string>& channels)
    {
        std::lock_guard<std::mutex> lock(m_RegistryMutex);

        auto it = m_NameToId.find(name);
        if (it != m_NameToId.end())
            return it->second;

        uint32_t id = static_cast<uint32_t>(m_Records.size());

        auto rec = std::make_unique<EntityRecord>();
        rec->info.name     = name;
        rec->info.tag      = tag;
        rec->info.channels = channels;
        rec->columns.resize(channels.size());
        rec->currentFrame.timestamp = 0.0f;
        rec->currentFrame.values.assign(channels.size(), 0.0f);

        m_Records.push_back(std::move(rec));
        m_NameToId[name] = id;

        return id;
    }

    void DataRecorder::ReserveCapacity(size_t expectedFrames)
    {
        for (auto& rec : m_Records)
        {
            std::lock_guard<std::mutex> lock(rec->mutex);
            rec->timestamps.reserve(expectedFrames);
            for (auto& col : rec->columns)
                col.reserve(expectedFrames);
        }
    }

    // =========================================================================
    // Recording (thread-safe hot path — zero allocation after ReserveCapacity)
    // =========================================================================

    void DataRecorder::RecordImpl(uint32_t id, const float* values, size_t count)
    {
        if (id >= static_cast<uint32_t>(m_Records.size())) return;

        EntityRecord& rec = *m_Records[id];
        const size_t chCount = rec.info.channels.size();
        const float  t       = m_ElapsedTime.load(std::memory_order_relaxed);

        std::lock_guard<std::mutex> lock(rec.mutex);

        // Update live frame.
        rec.currentFrame.timestamp = t;
        const size_t nCopy = std::min(count, chCount);
        for (size_t i = 0; i < nCopy; ++i)
            rec.currentFrame.values[i] = values[i];
        for (size_t i = nCopy; i < chCount; ++i)
            rec.currentFrame.values[i] = 0.0f;

        // Append to columnar history.
        rec.timestamps.push_back(t);
        for (size_t ch = 0; ch < chCount; ++ch)
            rec.columns[ch].push_back(ch < nCopy ? values[ch] : 0.0f);
    }

    void DataRecorder::Record(uint32_t id, std::initializer_list<float> values)
    {
        RecordImpl(id, values.begin(), values.size());
    }

    void DataRecorder::Record(uint32_t id, const std::vector<float>& values)
    {
        RecordImpl(id, values.data(), values.size());
    }

    // =========================================================================
    // Queries
    // =========================================================================

    bool DataRecorder::GetCurrentFrame(const std::string& entityName, TelemetryFrame& out) const
    {
        // m_NameToId is written only in Register() (main thread, before jobs).
        // These query functions are also called on the main thread, so there is
        // no live data race. The registry lock is still acquired for correctness
        // if that contract ever changes.
        uint32_t id;
        {
            std::lock_guard<std::mutex> regLock(m_RegistryMutex);
            auto it = m_NameToId.find(entityName);
            if (it == m_NameToId.end()) return false;
            id = it->second;
        }

        const EntityRecord& rec = *m_Records[id];
        std::lock_guard<std::mutex> lock(rec.mutex);
        if (rec.timestamps.empty()) return false;

        out = rec.currentFrame;
        return true;
    }

    const EntityTelemetryInfo* DataRecorder::GetInfo(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(m_RegistryMutex);
        auto it = m_NameToId.find(name);
        if (it == m_NameToId.end()) return nullptr;
        return &m_Records[it->second]->info;
    }

    std::vector<std::string> DataRecorder::GetEntityNames() const
    {
        std::lock_guard<std::mutex> lock(m_RegistryMutex);
        std::vector<std::string> names;
        names.reserve(m_Records.size());
        for (auto& rec : m_Records)
            names.push_back(rec->info.name);
        return names;
    }

    size_t DataRecorder::GetTotalFrameCount() const
    {
        if (m_Records.empty()) return 0;
        std::lock_guard<std::mutex> lock(m_Records[0]->mutex);
        return m_Records[0]->timestamps.size();
    }

    // =========================================================================
    // Time tracking
    // =========================================================================

    void DataRecorder::Tick(float dt)
    {
        m_ElapsedTime.fetch_add(dt, std::memory_order_relaxed);
    }

    float DataRecorder::GetRecordedDuration() const
    {
        return m_ElapsedTime.load(std::memory_order_relaxed);
    }

    // =========================================================================
    // Export
    // =========================================================================

    static std::string AutoTimestamp()
    {
        auto now  = std::chrono::system_clock::now();
        auto tt   = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf{};
        localtime_s(&tm_buf, &tt);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", &tm_buf);
        return buf;
    }

    void DataRecorder::Flush(const std::string& baseFolder,
                             const std::string& sessionName,
                             float              sampleRate)
    {
        if (m_Flushing.load())
        {
            CS_CORE_WARN("DataRecorder::Flush: a flush is already in progress — ignoring.");
            return;
        }

        // -----------------------------------------------------------------------
        // Snapshot all entity data under per-entity locks.
        // Columnar copy: timestamps + per-channel float vectors.
        // -----------------------------------------------------------------------
        struct Snapshot
        {
            EntityTelemetryInfo             info;
            uint32_t                        sampleCount;
            std::vector<float>              timestamps;
            std::vector<std::vector<float>> columns;   // [channel][frame]
        };

        std::vector<Snapshot> snapshots;
        snapshots.reserve(m_Records.size());

        for (auto& rec : m_Records)
        {
            std::lock_guard<std::mutex> lock(rec->mutex);
            Snapshot snap;
            snap.info        = rec->info;
            snap.sampleCount = static_cast<uint32_t>(rec->timestamps.size());
            snap.timestamps  = rec->timestamps;
            snap.columns     = rec->columns;
            snapshots.push_back(std::move(snap));
        }

        const std::string session   = sessionName.empty() ? AutoTimestamp() : sessionName;
        const std::string outputDir = baseFolder + "/" + session;

        if (m_FlushThread.joinable())
            m_FlushThread.join();

        m_Flushing.store(true);
        CS_CORE_INFO("DataRecorder: Starting background flush of {} entities → '{}'.",
                     snapshots.size(), outputDir);

        m_FlushThread = std::thread(
            [snapshots = std::move(snapshots), outputDir, sampleRate, this]() mutable
            {
                namespace fs = std::filesystem;

                std::error_code ec;
                fs::create_directories(outputDir, ec);
                if (ec)
                {
                    CS_CORE_ERROR("DataRecorder: Failed to create '{}': {}", outputDir, ec.message());
                    m_Flushing.store(false);
                    return;
                }

                // ==============================================================
                // 1. Write scene.bin — v3 format with per-entity sample_count
                // ==============================================================
                const std::string binPath = outputDir + "/scene.bin";
                std::ofstream binFile(binPath, std::ios::binary);

                if (!binFile.is_open())
                {
                    CS_CORE_ERROR("DataRecorder: Failed to open '{}' for writing.", binPath);
                    m_Flushing.store(false);
                    return;
                }

                const uint32_t entityCount = static_cast<uint32_t>(snapshots.size());

                // Header
                const char magic[4] = { 'C', 'S', 'M', 'C' };
                binFile.write(magic, 4);
                const uint32_t version = 1u;
                binFile.write(reinterpret_cast<const char*>(&version),     sizeof(version));
                binFile.write(reinterpret_cast<const char*>(&entityCount), sizeof(entityCount));
                binFile.write(reinterpret_cast<const char*>(&sampleRate),  sizeof(sampleRate));

                // Descriptor table — one entry per entity, includes per-entity sample_count
                for (const auto& snap : snapshots)
                {
                    char nameBuf[64] = {};
                    strncpy_s(nameBuf, sizeof(nameBuf), snap.info.name.c_str(), _TRUNCATE);
                    binFile.write(nameBuf, 64);

                    char tagBuf[64] = {};
                    strncpy_s(tagBuf, sizeof(tagBuf), snap.info.tag.c_str(), _TRUNCATE);
                    binFile.write(tagBuf, 64);

                    const uint32_t chCount = static_cast<uint32_t>(snap.info.channels.size());
                    binFile.write(reinterpret_cast<const char*>(&chCount),          sizeof(chCount));
                    binFile.write(reinterpret_cast<const char*>(&snap.sampleCount), sizeof(snap.sampleCount));

                    for (const auto& ch : snap.info.channels)
                    {
                        char chBuf[32] = {};
                        strncpy_s(chBuf, sizeof(chBuf), ch.c_str(), _TRUNCATE);
                        binFile.write(chBuf, 32);
                    }
                }

                // Data blocks — one per entity, row-major (frame × (1 + channel_count))
                // Each row: [timestamp, ch0, ch1, ..., ch(N-1)]
                std::vector<float> rowBuf;
                for (const auto& snap : snapshots)
                {
                    const uint32_t chCount = static_cast<uint32_t>(snap.info.channels.size());
                    rowBuf.resize(chCount + 1);

                    for (uint32_t s = 0; s < snap.sampleCount; ++s)
                    {
                        rowBuf[0] = (s < snap.timestamps.size()) ? snap.timestamps[s] : 0.0f;
                        for (uint32_t ch = 0; ch < chCount; ++ch)
                            rowBuf[ch + 1] = (ch < snap.columns.size() && s < snap.columns[ch].size())
                                             ? snap.columns[ch][s] : 0.0f;
                        binFile.write(reinterpret_cast<const char*>(rowBuf.data()),
                                      (chCount + 1) * sizeof(float));
                    }
                }

                binFile.close();

                // ==============================================================
                // 2. Write one CSV per entity
                // ==============================================================
                for (const auto& snap : snapshots)
                {
                    const std::string csvPath  = outputDir + "/" + snap.info.name + ".csv";
                    const uint32_t    chCount  = static_cast<uint32_t>(snap.info.channels.size());

                    std::vector<std::string> headers;
                    headers.reserve(chCount + 1u);
                    headers.push_back("Time");
                    for (const auto& ch : snap.info.channels)
                        headers.push_back(ch);

                    std::vector<std::vector<double>> columns(headers.size());
                    for (auto& col : columns) col.reserve(snap.sampleCount);

                    for (uint32_t s = 0; s < snap.sampleCount; ++s)
                    {
                        columns[0].push_back(static_cast<double>(
                            s < snap.timestamps.size() ? snap.timestamps[s] : 0.0f));
                        for (uint32_t ch = 0; ch < chCount; ++ch)
                        {
                            double v = (ch < snap.columns.size() && s < snap.columns[ch].size())
                                       ? static_cast<double>(snap.columns[ch][s]) : 0.0;
                            columns[ch + 1].push_back(v);
                        }
                    }

                    DataExport::WriteCSV(csvPath, headers, columns);
                }

                // Log the absolute path so it's obvious on disk where the recording
                // landed (outputDir is relative to the working dir = the exe folder).
                std::error_code absEc;
                const fs::path absDir = fs::absolute(outputDir, absEc);
                CS_CORE_INFO("DataRecorder: Flush complete — {} entities → '{}'.",
                             snapshots.size(),
                             absEc ? outputDir : absDir.string());
                m_Flushing.store(false);
            });
    }

    void DataRecorder::WaitForFlush()
    {
        if (m_FlushThread.joinable())
            m_FlushThread.join();
    }

    // =========================================================================
    // Reset
    // =========================================================================

    void DataRecorder::Clear()
    {
        for (auto& rec : m_Records)
        {
            std::lock_guard<std::mutex> lock(rec->mutex);
            rec->timestamps.clear();
            for (auto& col : rec->columns)
                col.clear();
            rec->currentFrame.timestamp = 0.0f;
            std::fill(rec->currentFrame.values.begin(),
                      rec->currentFrame.values.end(), 0.0f);
        }
        m_ElapsedTime.store(0.0f, std::memory_order_relaxed);
    }

} // namespace Cosmic
