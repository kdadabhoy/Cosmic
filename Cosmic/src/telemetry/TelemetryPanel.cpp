// TelemetryPanel.cpp
// Last Modified: 5/29/2026

#include "telemetry/TelemetryPanel.h"
#include "core/Log.h"

#include <imgui.h>
#include <implot.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

// Win32 file-open dialog — .cpp only, never leaked into headers.
// Macros may already be defined on the command line; guard to suppress C4005.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")

namespace Cosmic
{
    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    TelemetryPanel::TelemetryPanel()
    {
        m_SubHandle = EntitySelection::OnChanged(
            [this](const std::string& name, const std::string& tag)
            {
                OnSelectionChanged(name, tag);
            });
    }

    TelemetryPanel::~TelemetryPanel()
    {
        EntitySelection::Unsubscribe(m_SubHandle);
    }

    // =========================================================================
    // Mode
    // =========================================================================

    void TelemetryPanel::SetMode(Mode mode)
    {
        if (m_Mode == mode) return;
        m_Mode = mode;

        // Clear ring buffers — data from the old source is stale.
        m_PlotBuffers.clear();
        m_PlotTimes.clear();
        m_PlotOffset    = 0;
        m_PlotCount     = 0;
        m_LastFrame     = TelemetryFrame{};
        m_LastReplayPos = -1.0f;
    }

    // =========================================================================
    // Data sources
    // =========================================================================

    void TelemetryPanel::SetRecorder(DataRecorder* recorder)
    {
        m_Recorder = recorder;
        if (recorder)
            SetMode(Mode::Live);
    }

    void TelemetryPanel::SetPlayer(DataPlayer* player)
    {
        m_Player = player;
        // Mode stays as-is until a file is successfully loaded.
    }

    // =========================================================================
    // Inspector callbacks
    // =========================================================================

    void TelemetryPanel::RegisterTagInspector(const std::string& tag, InspectorFn fn)
    {
        m_TagInspectors[tag] = std::move(fn);
    }

    void TelemetryPanel::RegisterEntityInspector(const std::string& entityName, InspectorFn fn)
    {
        m_EntityInspectors[entityName] = std::move(fn);
    }

    // =========================================================================
    // Selection changed callback
    // =========================================================================

    void TelemetryPanel::OnSelectionChanged(const std::string& name, const std::string& tag)
    {
        m_SelectedName  = name;
        m_SelectedTag   = tag;
        m_LastReplayPos = -1.0f; // force first-frame push for the new entity

        if (name.empty())
        {
            m_ChannelNames.clear();
            m_PlotBuffers.clear();
            m_PlotTimes.clear();
            m_PlotOffset = 0;
            m_PlotCount  = 0;
            m_LastFrame  = TelemetryFrame{};
            return;
        }

        const EntityTelemetryInfo* info = nullptr;
        if (m_Mode == Mode::Live && m_Recorder)
            info = m_Recorder->GetInfo(name);
        if (!info && m_Player)
            info = m_Player->GetInfo(name);

        if (info)
            RebuildBuffers(*info);
    }

    // =========================================================================
    // Circular buffer management
    // =========================================================================

    void TelemetryPanel::RebuildBuffers(const EntityTelemetryInfo& info)
    {
        m_ChannelNames = info.channels;

        const int n = static_cast<int>(m_ChannelNames.size());
        m_PlotBuffers.assign(n, std::vector<float>(k_PlotCapacity, 0.0f));
        m_PlotTimes.assign(k_PlotCapacity, 0.0f);
        m_PlotOffset = 0;
        m_PlotCount  = 0;
        m_LastFrame  = TelemetryFrame{};
    }

    void TelemetryPanel::PushFrame(const TelemetryFrame& frame)
    {
        if (m_ChannelNames.empty()) return;

        const int writeIdx = (m_PlotOffset + m_PlotCount) % k_PlotCapacity;
        m_PlotTimes[writeIdx] = frame.timestamp;

        const int nCh = static_cast<int>(m_ChannelNames.size());
        for (int ch = 0; ch < nCh; ++ch)
        {
            float val = (ch < static_cast<int>(frame.values.size()))
                        ? frame.values[ch] : 0.0f;
            m_PlotBuffers[ch][writeIdx] = val;
        }

        if (m_PlotCount < k_PlotCapacity)
            ++m_PlotCount;
        else
            m_PlotOffset = (m_PlotOffset + 1) % k_PlotCapacity;

        m_LastFrame = frame;
    }

    // =========================================================================
    // Per-frame update
    // =========================================================================

    void TelemetryPanel::OnUpdate(float dt)
    {
        // Only advance player when time moves forward — negative dt (global time
        // scale < 0) would pin the position at 0 with m_Playing still true.
        if (dt > 0.0f && m_Mode == Mode::Replay && m_Player && m_Player->IsLoaded())
            m_Player->Tick(dt);

        if (m_SelectedName.empty()) return;

        TelemetryFrame frame;
        bool got = false;

        if (m_Mode == Mode::Live && m_Recorder)
        {
            got = m_Recorder->GetCurrentFrame(m_SelectedName, frame);
        }
        else if (m_Mode == Mode::Replay && m_Player && m_Player->IsLoaded())
        {
            // Only push when the playback position has changed — this both
            // freezes the plot while paused and avoids wasting ring-buffer
            // capacity with duplicate entries during normal scrubbing.
            const float pos = m_Player->GetPosition();
            if (std::abs(pos - m_LastReplayPos) > 1e-6f)
            {
                got = m_Player->GetFrame(m_SelectedName, frame);
                m_LastReplayPos = pos;
            }
        }

        if (got)
            PushFrame(frame);
    }

    // =========================================================================
    // ImGui / ImPlot rendering
    // =========================================================================

    void TelemetryPanel::OnImGuiRender()
    {
        // -----------------------------------------------------------------------
        // 1. Replay loader (always shown when a DataPlayer is attached)
        // -----------------------------------------------------------------------
        if (m_Player)
        {
            DrawReplayLoader();
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }

        // -----------------------------------------------------------------------
        // 2. Entity selector combo
        // -----------------------------------------------------------------------
        std::vector<std::string> names;
        if (m_Mode == Mode::Live && m_Recorder)
            names = m_Recorder->GetEntityNames();
        else if (m_Mode == Mode::Replay && m_Player && m_Player->IsLoaded())
            names = m_Player->GetEntityNames();

        if (!names.empty())
        {
            const char* previewLabel = m_SelectedName.empty()
                                       ? "-- Select Entity --"
                                       : m_SelectedName.c_str();

            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##EntitySelector", previewLabel))
            {
                for (const auto& n : names)
                {
                    bool selected = (n == m_SelectedName);
                    if (ImGui::Selectable(n.c_str(), selected))
                    {
                        std::string tag;
                        const EntityTelemetryInfo* info = nullptr;
                        if (m_Recorder) info = m_Recorder->GetInfo(n);
                        if (!info && m_Player) info = m_Player->GetInfo(n);
                        if (info) tag = info->tag;

                        EntitySelection::SetByName(n, tag);
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        ImGui::Spacing();

        // -----------------------------------------------------------------------
        // 3. ImPlot charts
        // -----------------------------------------------------------------------
        if (!m_SelectedName.empty() && m_PlotCount > 0)
        {
            DrawPlots();
            ImGui::Spacing();
        }

        // -----------------------------------------------------------------------
        // 4. Inspector
        // -----------------------------------------------------------------------
        if (!m_SelectedName.empty() && !m_LastFrame.values.empty())
        {
            ImGui::Separator();
            ImGui::Spacing();
            DrawInspector(m_LastFrame);
        }
    }

    // =========================================================================
    // DrawReplayLoader
    // =========================================================================

    void TelemetryPanel::DrawReplayLoader()
    {
        ImGui::SeparatorText("Replay");

        // Path input + Browse
        {
            char pathBuf[512] = {};
            strncpy_s(pathBuf, sizeof(pathBuf), m_ReplayPath.c_str(), _TRUNCATE);
            ImGui::SetNextItemWidth(-160.0f);
            if (ImGui::InputText("##tp_rpath", pathBuf, sizeof(pathBuf)))
                m_ReplayPath = pathBuf;

            ImGui::SameLine();
            if (ImGui::Button("Browse...##tp_browse"))
            {
                char fileBuf[MAX_PATH] = {};
                static const char k_Filter[] =
                    "Scene recordings (*.bin)\0*.bin\0All files\0*.*\0";
                OPENFILENAMEA ofn   = {};
                ofn.lStructSize     = sizeof(ofn);
                ofn.lpstrFilter     = k_Filter;
                ofn.lpstrFile       = fileBuf;
                ofn.nMaxFile        = MAX_PATH;
                ofn.lpstrTitle      = "Open Replay File";
                ofn.lpstrDefExt     = "bin";
                ofn.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
                if (GetOpenFileNameA(&ofn))
                    m_ReplayPath = fileBuf;
            }
        }

        // Load + Unload buttons
        ImGui::Spacing();
        if (ImGui::Button("  Load  ##tp_load"))
        {
            m_Player->Unload();
            if (m_Player->Load(m_ReplayPath))
            {
                m_ReplayLoadOk = true;
                SetMode(Mode::Replay);

                // Auto-select first entity so charts light up immediately.
                auto entityNames = m_Player->GetEntityNames();
                if (!entityNames.empty())
                {
                    const auto* info = m_Player->GetInfo(entityNames[0]);
                    EntitySelection::SetByName(entityNames[0], info ? info->tag : "");
                }

                int nEntities = static_cast<int>(entityNames.size());
                int duration  = static_cast<int>(std::round(m_Player->GetDuration()));
                int hz        = static_cast<int>(m_Player->GetSampleRate());
                char buf[128];
                snprintf(buf, sizeof(buf), "Loaded — %d entities | %ds | %d Hz",
                         nEntities, duration, hz);
                m_ReplayStatus = buf;
            }
            else
            {
                m_ReplayLoadOk = false;
                m_ReplayStatus = "Load failed — file not found or invalid format.";
            }
        }

        ImGui::SameLine();
        const bool hasReplay = m_Player->IsLoaded();
        if (!hasReplay) ImGui::BeginDisabled();
        if (ImGui::Button("  Unload  ##tp_unload"))
        {
            m_Player->Unload();
            m_ReplayLoadOk = false;
            m_ReplayStatus = "Unloaded.";
            if (m_Recorder)
                SetMode(Mode::Live);
            else
                SetMode(Mode::None);
            EntitySelection::Clear();
        }
        if (!hasReplay) ImGui::EndDisabled();

        // Status line
        ImGui::Spacing();
        if (m_ReplayLoadOk)
            ImGui::TextColored({ 0.2f, 1.0f, 0.35f, 1.0f }, "%s", m_ReplayStatus.c_str());
        else
            ImGui::TextColored({ 1.0f, 0.35f, 0.35f, 1.0f }, "%s", m_ReplayStatus.c_str());
    }

    // =========================================================================
    // DrawPlots
    // =========================================================================

    void TelemetryPanel::DrawPlots()
    {
        const int nCh = static_cast<int>(m_ChannelNames.size());

        float tOldest = m_PlotTimes[m_PlotOffset % k_PlotCapacity];
        float tNewest = m_PlotTimes[(m_PlotOffset + m_PlotCount - 1) % k_PlotCapacity];
        if (tNewest <= tOldest) tNewest = tOldest + 1.0f;

        for (int ch = 0; ch < nCh; ++ch)
        {
            const char* chName = m_ChannelNames[ch].c_str();

            // Compute Y range from the valid portion of the ring buffer only.
            // SetNextAxisToFit scans all 512 slots including zero-initialised ones
            // not yet written, which keeps pinning the range to include 0 until
            // the buffer is full. Scanning m_PlotCount entries through the offset
            // gives the exact range of the data actually on screen.
            float yMin = FLT_MAX, yMax = -FLT_MAX;
            for (int s = 0; s < m_PlotCount; ++s)
            {
                float v = m_PlotBuffers[ch][(m_PlotOffset + s) % k_PlotCapacity];
                if (v < yMin) yMin = v;
                if (v > yMax) yMax = v;
            }
            if (yMin > yMax)       { yMin = 0.0f; yMax = 1.0f; }
            else if (yMin == yMax) { yMin -= 0.5f; yMax += 0.5f; }
            const float pad = (yMax - yMin) * 0.1f;

            ImPlot::SetNextAxisLimits(ImAxis_X1, tOldest, tNewest, ImPlotCond_Always);
            ImPlot::SetNextAxisLimits(ImAxis_Y1, yMin - pad, yMax + pad, ImPlotCond_Always);

            if (ImPlot::BeginPlot(chName, ImVec2(-1.0f, 110.0f)))
            {
                ImPlotSpec spec;
                spec.Offset = m_PlotOffset;
                ImPlot::PlotLine(
                    chName,
                    m_PlotTimes.data(),
                    m_PlotBuffers[ch].data(),
                    m_PlotCount,
                    spec
                );
                ImPlot::EndPlot();
            }
        }
    }

    // =========================================================================
    // DrawTransportControls  (public — embed in any ImGui window)
    // =========================================================================

    void TelemetryPanel::DrawTransportControls()
    {
        if (m_Mode != Mode::Replay || !m_Player || !m_Player->IsLoaded())
            return;

        ImGui::SeparatorText("Playback");

        const float duration = m_Player->GetDuration();
        float       pos      = m_Player->GetPosition();
        float       speed    = m_Player->GetSpeed();
        const bool  playing  = m_Player->IsPlaying();
        const bool  reverse  = speed < 0.0f;

        // Transport row: |<  <<  Play/Pause  >>  >|
        if (ImGui::Button("|<##tp_start"))
        {
            m_Player->Pause();
            m_Player->SetPosition(0.0f);
        }
        ImGui::SameLine();

        if (playing && reverse)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.55f, 1.0f, 1.0f));
        if (ImGui::Button("<<##tp_rev"))
        {
            m_Player->SetSpeed(-std::abs(speed));
            // If already at the start, jump to end so rewind has somewhere to go.
            if (m_Player->GetPosition() <= 0.0f)
                m_Player->SetPosition(m_Player->GetDuration());
            m_Player->Play();
        }
        if (playing && reverse) ImGui::PopStyleColor();
        ImGui::SameLine();

        if (playing)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
            if (ImGui::Button(" || ##tp_pause"))
                m_Player->Pause();
            ImGui::PopStyleColor();
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.15f, 1.0f));
            if (ImGui::Button(" |> ##tp_play"))
            {
                if (speed > 0.0f && pos >= duration) m_Player->SetPosition(0.0f);
                else if (speed < 0.0f && pos <= 0.0f) m_Player->SetPosition(duration);
                m_Player->Play();
            }
            ImGui::PopStyleColor();
        }
        ImGui::SameLine();

        if (playing && !reverse)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.55f, 1.0f, 1.0f));
        if (ImGui::Button(">>##tp_fwd"))
        {
            m_Player->SetSpeed(std::abs(speed));
            m_Player->Play();
        }
        if (playing && !reverse) ImGui::PopStyleColor();
        ImGui::SameLine();

        if (ImGui::Button(">|##tp_end"))
        {
            m_Player->Pause();
            m_Player->SetPosition(duration);
        }

        // Speed slider
        float absSpeed = std::abs(speed);
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::SliderFloat("##tp_speedslider", &absSpeed, 0.1f, 10.0f, "%.1fx"))
            m_Player->SetSpeed(reverse ? -absSpeed : absSpeed);
        ImGui::SameLine();
        ImGui::TextDisabled(reverse ? "Speed (Rev)" : "Speed (Fwd)");

        // Scrub bar
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat("##tp_posslider", &pos,
                               0.0f, duration > 0.0f ? duration : 1.0f, "%.2fs"))
            m_Player->SetPosition(pos);

        ImGui::Text("%.2f / %.2f s", m_Player->GetPosition(), duration);
    }

    // =========================================================================
    // DrawInspector
    // =========================================================================

    void TelemetryPanel::DrawInspector(const TelemetryFrame& frame)
    {
        ImGui::SeparatorText("Inspector");

        {
            auto it = m_EntityInspectors.find(m_SelectedName);
            if (it != m_EntityInspectors.end())
            {
                it->second(m_SelectedName, frame);
                return;
            }
        }

        {
            auto it = m_TagInspectors.find(m_SelectedTag);
            if (it != m_TagInspectors.end())
            {
                it->second(m_SelectedName, frame);
                return;
            }
        }

        ImGui::Text("Entity: %s  [%s]", m_SelectedName.c_str(), m_SelectedTag.c_str());
        ImGui::Spacing();
        for (int ch = 0; ch < static_cast<int>(m_ChannelNames.size()); ++ch)
        {
            float val = (ch < static_cast<int>(frame.values.size())) ? frame.values[ch] : 0.0f;
            ImGui::Text("  %-16s  %.4f", m_ChannelNames[ch].c_str(), val);
        }
    }

} // namespace Cosmic
