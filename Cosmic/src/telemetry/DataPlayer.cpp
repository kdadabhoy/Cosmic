// DataPlayer.cpp
// Last Modified: 5/29/2026

#include "telemetry/DataPlayer.h"
#include "core/Log.h"

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace Cosmic
{
    // =========================================================================
    // Loading
    // =========================================================================

    bool DataPlayer::Load(const std::string& folderOrFilePath)
    {
        namespace fs = std::filesystem;

        m_Entities.clear();
        m_NameToId.clear();
        m_Position = 0.0f;
        m_Duration = 0.0f;
        m_Playing  = false;
        m_Loaded   = false;

        fs::path path(folderOrFilePath);

        if (fs::is_directory(path))
        {
            // Prefer scene.bin (v2/v3 multi-entity). If it exists and loads
            // successfully, skip all individual .bin files to avoid duplicates.
            bool foundSceneBin = false;
            for (const auto& entry : fs::directory_iterator(path))
            {
                if (entry.is_regular_file() && entry.path().filename() == "scene.bin")
                {
                    if (LoadBinaryFile(entry.path().string()))
                        foundSceneBin = true;
                    break;
                }
            }

            // Fall back to individual v1 files only when no scene.bin was found.
            if (!foundSceneBin)
            {
                for (const auto& entry : fs::directory_iterator(path))
                {
                    if (entry.is_regular_file()
                        && entry.path().extension() == ".bin"
                        && entry.path().filename() != "scene.bin")
                    {
                        LoadBinaryFile(entry.path().string());
                    }
                }
            }
        }
        else if (fs::is_regular_file(path) && path.extension() == ".bin")
        {
            LoadBinaryFile(folderOrFilePath);
        }
        else
        {
            CS_CORE_ERROR("DataPlayer::Load: '{}' is not a .bin file or directory.",
                          folderOrFilePath);
            return false;
        }

        if (m_Entities.empty())
        {
            CS_CORE_WARN("DataPlayer::Load: No valid .bin entities found in '{}'.",
                         folderOrFilePath);
            return false;
        }

        // Duration from the entity with the most frames.
        for (const auto& entity : m_Entities)
        {
            if (entity.frames.size() > 1)
            {
                float dur = static_cast<float>(entity.frames.size() - 1) / entity.sampleRate;
                m_Duration = std::max(m_Duration, dur);
            }
        }

        m_Loaded = true;
        CS_CORE_INFO("DataPlayer: Loaded {} entities, duration = {:.2f}s from '{}'.",
                     m_Entities.size(), m_Duration, folderOrFilePath);
        return true;
    }

    bool DataPlayer::LoadBinaryFile(const std::string& filepath)
    {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open())
        {
            CS_CORE_ERROR("DataPlayer::LoadBinaryFile: Cannot open '{}'.", filepath);
            return false;
        }

        char magic[4] = {};
        file.read(magic, 4);
        if (magic[0] != 'C' || magic[1] != 'S' || magic[2] != 'M' || magic[3] != 'C')
        {
            CS_CORE_ERROR("DataPlayer::LoadBinaryFile: Bad magic in '{}'. Not a CSMC file.",
                          filepath);
            return false;
        }

        uint32_t version = 0;
        file.read(reinterpret_cast<char*>(&version), sizeof(version));

        // -----------------------------------------------------------------------
        // Version 1 — one entity per file (legacy)
        // -----------------------------------------------------------------------
        if (version == 1u)
        {
            PlayerEntityData data;

            char nameBuf[64] = {};
            file.read(nameBuf, 64);
            data.info.name = nameBuf;

            char tagBuf[64] = {};
            file.read(tagBuf, 64);
            data.info.tag = tagBuf;

            uint32_t channelCount = 0, sampleCount = 0;
            float    sampleRate   = 60.0f;
            file.read(reinterpret_cast<char*>(&channelCount), sizeof(channelCount));
            file.read(reinterpret_cast<char*>(&sampleCount),  sizeof(sampleCount));
            file.read(reinterpret_cast<char*>(&sampleRate),   sizeof(sampleRate));
            data.sampleRate = sampleRate;

            if (!file.good())
            {
                CS_CORE_ERROR("DataPlayer: Truncated v1 header in '{}'.", filepath);
                return false;
            }

            data.info.channels.reserve(channelCount);
            for (uint32_t c = 0; c < channelCount; ++c)
            {
                char chBuf[32] = {};
                file.read(chBuf, 32);
                data.info.channels.emplace_back(chBuf);
            }

            data.frames.resize(sampleCount);
            for (uint32_t s = 0; s < sampleCount; ++s)
            {
                data.frames[s].timestamp = static_cast<float>(s) / sampleRate;
                data.frames[s].values.resize(channelCount);
                file.read(reinterpret_cast<char*>(data.frames[s].values.data()),
                          channelCount * sizeof(float));
            }

            if (!file.good() && !file.eof())
            {
                CS_CORE_ERROR("DataPlayer: Read error in v1 file '{}'.", filepath);
                return false;
            }

            m_NameToId[data.info.name] = static_cast<uint32_t>(m_Entities.size());
            m_Entities.push_back(std::move(data));
            return true;
        }

        // -----------------------------------------------------------------------
        // Version 2 — all entities, global sample_count in header
        // -----------------------------------------------------------------------
        if (version == 2u)
        {
            uint32_t entityCount = 0, sampleCount = 0;
            float    sampleRate  = 60.0f;
            file.read(reinterpret_cast<char*>(&entityCount), sizeof(entityCount));
            file.read(reinterpret_cast<char*>(&sampleCount), sizeof(sampleCount));
            file.read(reinterpret_cast<char*>(&sampleRate),  sizeof(sampleRate));

            if (!file.good())
            {
                CS_CORE_ERROR("DataPlayer: Truncated v2 header in '{}'.", filepath);
                return false;
            }

            std::vector<PlayerEntityData> entities(entityCount);
            for (uint32_t e = 0; e < entityCount; ++e)
            {
                char nameBuf[64] = {}; file.read(nameBuf, 64);
                entities[e].info.name = nameBuf;
                char tagBuf[64] = {}; file.read(tagBuf, 64);
                entities[e].info.tag = tagBuf;

                uint32_t chCount = 0;
                file.read(reinterpret_cast<char*>(&chCount), sizeof(chCount));

                entities[e].info.channels.reserve(chCount);
                for (uint32_t c = 0; c < chCount; ++c)
                {
                    char chBuf[32] = {}; file.read(chBuf, 32);
                    entities[e].info.channels.emplace_back(chBuf);
                }
                entities[e].sampleRate = sampleRate;

                entities[e].frames.resize(sampleCount);
                for (uint32_t s = 0; s < sampleCount; ++s)
                {
                    entities[e].frames[s].timestamp = static_cast<float>(s) / sampleRate;
                    entities[e].frames[s].values.resize(chCount, 0.0f);
                }
            }

            for (uint32_t e = 0; e < entityCount; ++e)
            {
                const uint32_t chCount = static_cast<uint32_t>(entities[e].info.channels.size());
                for (uint32_t s = 0; s < sampleCount; ++s)
                {
                    file.read(reinterpret_cast<char*>(entities[e].frames[s].values.data()),
                              chCount * sizeof(float));
                }
            }

            if (!file.good() && !file.eof())
            {
                CS_CORE_ERROR("DataPlayer: Read error in v2 file '{}'.", filepath);
                return false;
            }

            for (auto& ent : entities)
            {
                m_NameToId[ent.info.name] = static_cast<uint32_t>(m_Entities.size());
                m_Entities.push_back(std::move(ent));
            }
            return true;
        }

        // -----------------------------------------------------------------------
        // Version 3 — all entities, per-entity sample_count in descriptor table
        // -----------------------------------------------------------------------
        if (version == 3u)
        {
            uint32_t entityCount = 0;
            float    sampleRate  = 60.0f;
            file.read(reinterpret_cast<char*>(&entityCount), sizeof(entityCount));
            file.read(reinterpret_cast<char*>(&sampleRate),  sizeof(sampleRate));

            if (!file.good())
            {
                CS_CORE_ERROR("DataPlayer: Truncated v3 header in '{}'.", filepath);
                return false;
            }

            std::vector<PlayerEntityData> entities(entityCount);
            std::vector<uint32_t>         sampleCounts(entityCount, 0u);

            for (uint32_t e = 0; e < entityCount; ++e)
            {
                char nameBuf[64] = {}; file.read(nameBuf, 64);
                entities[e].info.name = nameBuf;
                char tagBuf[64] = {}; file.read(tagBuf, 64);
                entities[e].info.tag = tagBuf;

                uint32_t chCount = 0;
                file.read(reinterpret_cast<char*>(&chCount),           sizeof(chCount));
                file.read(reinterpret_cast<char*>(&sampleCounts[e]),   sizeof(sampleCounts[e]));

                entities[e].info.channels.reserve(chCount);
                for (uint32_t c = 0; c < chCount; ++c)
                {
                    char chBuf[32] = {}; file.read(chBuf, 32);
                    entities[e].info.channels.emplace_back(chBuf);
                }
                entities[e].sampleRate = sampleRate;

                entities[e].frames.resize(sampleCounts[e]);
                for (uint32_t s = 0; s < sampleCounts[e]; ++s)
                {
                    entities[e].frames[s].timestamp = static_cast<float>(s) / sampleRate;
                    entities[e].frames[s].values.resize(chCount, 0.0f);
                }
            }

            // Data blocks — one per entity, each potentially different length
            for (uint32_t e = 0; e < entityCount; ++e)
            {
                const uint32_t chCount     = static_cast<uint32_t>(entities[e].info.channels.size());
                const uint32_t sampleCount = sampleCounts[e];
                for (uint32_t s = 0; s < sampleCount; ++s)
                {
                    file.read(reinterpret_cast<char*>(entities[e].frames[s].values.data()),
                              chCount * sizeof(float));
                }
            }

            if (!file.good() && !file.eof())
            {
                CS_CORE_ERROR("DataPlayer: Read error in v3 file '{}'.", filepath);
                return false;
            }

            for (auto& ent : entities)
            {
                m_NameToId[ent.info.name] = static_cast<uint32_t>(m_Entities.size());
                m_Entities.push_back(std::move(ent));
            }
            return true;
        }

        CS_CORE_ERROR("DataPlayer: Unknown binary version {} in '{}'.", version, filepath);
        return false;
    }

    void DataPlayer::Unload()
    {
        m_Entities.clear();
        m_NameToId.clear();
        m_Position = 0.0f;
        m_Duration = 0.0f;
        m_Playing  = false;
        m_Loaded   = false;
    }

    // =========================================================================
    // Playback controls
    // =========================================================================

    void DataPlayer::SetPosition(float seconds)
    {
        m_Position = std::clamp(seconds, 0.0f, m_Duration);
    }

    void DataPlayer::Tick(float dt)
    {
        if (!m_Playing || !m_Loaded) return;

        m_Position += dt * m_Speed;

        if (m_Position >= m_Duration)
        {
            m_Position = m_Duration;
            m_Playing  = false;
        }
        else if (m_Position <= 0.0f)
        {
            m_Position = 0.0f;
            if (m_Speed < 0.0f)
                m_Playing = false;
        }
    }

    // =========================================================================
    // Data access
    // =========================================================================

    bool DataPlayer::GetFrame(const std::string& entityName, TelemetryFrame& out) const
    {
        auto it = m_NameToId.find(entityName);
        if (it == m_NameToId.end()) return false;

        const PlayerEntityData& data = m_Entities[it->second];
        if (data.frames.empty()) return false;

        float t = m_Position * data.sampleRate;
        Interpolate(data, t, out);
        out.timestamp = m_Position;
        return true;
    }

    bool DataPlayer::SampleAt(const std::string& entityName, float seconds, TelemetryFrame& out) const
    {
        auto it = m_NameToId.find(entityName);
        if (it == m_NameToId.end()) return false;

        const PlayerEntityData& data = m_Entities[it->second];
        if (data.frames.empty()) return false;

        const float pos = std::clamp(seconds, 0.0f, m_Duration);
        float t = pos * data.sampleRate;
        Interpolate(data, t, out);
        out.timestamp = pos;
        return true;
    }

    void DataPlayer::Interpolate(const PlayerEntityData& data, float t, TelemetryFrame& out) const
    {
        const int sampleCount = static_cast<int>(data.frames.size());

        if (sampleCount == 1)
        {
            out = data.frames[0];
            return;
        }

        int   i    = std::clamp(static_cast<int>(t), 0, sampleCount - 2);
        float frac = std::clamp(t - static_cast<float>(i), 0.0f, 1.0f);

        const TelemetryFrame& f0 = data.frames[i];
        const TelemetryFrame& f1 = data.frames[i + 1];

        const size_t chCount = std::min(f0.values.size(), f1.values.size());
        out.values.resize(chCount);

        for (size_t ch = 0; ch < chCount; ++ch)
            out.values[ch] = f0.values[ch] * (1.0f - frac) + f1.values[ch] * frac;

        out.timestamp = f0.timestamp * (1.0f - frac) + f1.timestamp * frac;
    }

    std::vector<std::string> DataPlayer::GetEntityNames() const
    {
        std::vector<std::string> names;
        names.reserve(m_Entities.size());
        for (const auto& e : m_Entities)
            names.push_back(e.info.name);
        return names;
    }

    const EntityTelemetryInfo* DataPlayer::GetInfo(const std::string& name) const
    {
        auto it = m_NameToId.find(name);
        if (it == m_NameToId.end()) return nullptr;
        return &m_Entities[it->second].info;
    }

    float DataPlayer::GetSampleRate() const
    {
        return m_Entities.empty() ? 60.0f : m_Entities[0].sampleRate;
    }

} // namespace Cosmic
