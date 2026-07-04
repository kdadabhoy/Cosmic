// TelemetryPanel.cpp — see TelemetryPanel.h.

#include "panels/TelemetryPanel.h"
#include "EditorContext.h"

#include "utils/FileSystem.h"

#include <imgui.h>
#include <implot.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <variant>

namespace fs = std::filesystem;

using Cosmic::Reflect::FieldDescriptor;
using Cosmic::Reflect::FieldKind;
using Cosmic::Reflect::FieldValue;
using Cosmic::Reflect::TypeDescriptor;

namespace Starforge
{
    namespace
    {
        constexpr const char* kTakeEntity = "take";   // single DataRecorder entity

        std::string Timestamp()
        {
            const auto  now = std::chrono::system_clock::now();
            const auto  tt  = std::chrono::system_clock::to_time_t(now);
            std::tm     tm{};
            localtime_s(&tm, &tt);
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
            return buf;
        }

        std::string EntityPrefix(Cosmic::Entity e)
        {
            if (e && e.HasComponent<Cosmic::TagComponent>())
            {
                const std::string& t = e.GetComponent<Cosmic::TagComponent>().Tag;
                if (!t.empty()) return t;
            }
            return "Entity";
        }

        // Collapse any boxed reflected value to one float channel. `comp` picks a
        // vector/quat sub-component (quats are boxed w,x,y,z — matching AxisSuffix).
        float FieldValueToFloat(const FieldValue& v, int comp)
        {
            const int i = std::clamp(comp < 0 ? 0 : comp, 0, 3);
            return std::visit([i](auto&& val) -> float
            {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, bool>)      return val ? 1.0f : 0.0f;
                else if constexpr (std::is_same_v<T, int32_t>)  return static_cast<float>(val);
                else if constexpr (std::is_same_v<T, uint32_t>) return static_cast<float>(val);
                else if constexpr (std::is_same_v<T, uint64_t>) return static_cast<float>(val);
                else if constexpr (std::is_same_v<T, float>)    return val;
                else if constexpr (std::is_same_v<T, glm::vec2>) return val[std::min(i, 1)];
                else if constexpr (std::is_same_v<T, glm::vec3>) return val[std::min(i, 2)];
                else if constexpr (std::is_same_v<T, glm::vec4>) return val[i];
                else if constexpr (std::is_same_v<T, glm::quat>) { const float a[4] = { val.w, val.x, val.y, val.z }; return a[i]; }
                else return 0.0f;   // std::string
            }, v);
        }

        std::vector<double> AsDouble(const std::vector<float>& v)
        {
            return std::vector<double>(v.begin(), v.end());
        }
    }

    // =========================================================================
    // Capture lifecycle
    // =========================================================================

    void TelemetryPanel::ResetTake()
    {
        m_Channels.clear();
        m_Groups.clear();
        m_GroupIndex.clear();
        m_Times.clear();
        m_ScriptKeys.clear();
        m_Scratch.clear();
        m_Rec.reset();
        m_Player.Unload();
        m_TakeId    = 0;
        m_StepIndex = 0;
        m_Armed     = false;
        m_WarnedLate = false;
    }

    int TelemetryPanel::GroupFor(const std::string& key, const std::string& title)
    {
        auto it = m_GroupIndex.find(key);
        if (it != m_GroupIndex.end()) return it->second;
        const int idx = static_cast<int>(m_Groups.size());
        m_Groups.push_back({ title });
        m_GroupIndex[key] = idx;
        return idx;
    }

    void TelemetryPanel::OnPlayStart(EditorContext& ctx, float fixedDt)
    {
        m_FixedDt = (fixedDt > 0.0f) ? fixedDt : (1.0f / 60.0f);
        ResetTake();
        m_Mode     = Mode::Recording;
        m_TakeName = (ctx.SceneName.empty() ? std::string("take") : ctx.SceneName) + "_" + Timestamp();

        if (!ctx.Scene) return;

        // Build reflected channels from the marks, resolved against the runtime scene.
        for (const Telemetry::RecordedChannel& mark : ctx.Recorded)
        {
            Cosmic::Entity e = ctx.Scene->FindByUUID(Cosmic::UUID(mark.Entity));
            if (!e) continue;

            const TypeDescriptor* d = Cosmic::Reflect::GetRegistry().Find(mark.TypeId);
            if (!d) continue;
            const FieldDescriptor* f = d->FindField(mark.Field);
            if (!f) continue;

            Channel ch;
            ch.isScript = false;
            ch.uuid     = mark.Entity;
            ch.typeId   = mark.TypeId;
            ch.field    = mark.Field;
            ch.comp     = mark.Comp;
            ch.handle   = (entt::entity)e;

            const std::string prefix = EntityPrefix(e);
            const std::string base   = prefix + "." + mark.Field;
            const char*       axis   = Telemetry::AxisSuffix(f->Kind, mark.Comp);
            ch.label = (axis[0] == '\0') ? base : (base + "." + axis);
            ch.group = GroupFor("R:" + std::to_string(mark.Entity) + ":" +
                                std::to_string((uint64_t)mark.TypeId) + ":" + mark.Field, base);
            m_Channels.push_back(std::move(ch));
        }
    }

    void TelemetryPanel::AddScriptChannel(EditorContext& ctx, uint32_t raw, const std::string& name)
    {
        const std::string key = std::to_string(raw) + ":" + name;
        if (m_ScriptKeys.count(key)) return;

        Cosmic::Entity e((entt::entity)raw, ctx.Scene.get());
        const std::string prefix = EntityPrefix(e);
        const std::string base   = prefix + "." + name;

        Channel ch;
        ch.isScript      = true;
        ch.scriptRaw     = raw;
        ch.scriptChannel = name;
        ch.uuid          = (e && e.HasComponent<Cosmic::IDComponent>())
                         ? (uint64_t)e.GetComponent<Cosmic::IDComponent>().ID : 0;
        ch.label = base;
        ch.group = GroupFor("S:" + key, base);

        m_ScriptKeys.insert(key);
        m_Channels.push_back(std::move(ch));
    }

    void TelemetryPanel::DedupLabels()
    {
        std::unordered_set<std::string> used;
        for (Channel& ch : m_Channels)
        {
            std::string base = ch.label.size() > 31 ? ch.label.substr(0, 31) : ch.label;
            std::string label = base;
            int n = 2;
            while (used.count(label))
            {
                const std::string suffix = "_" + std::to_string(n++);
                std::string head = base;
                if (head.size() + suffix.size() > 31)
                    head = head.substr(0, 31 - suffix.size());
                label = head + suffix;
            }
            used.insert(label);
            ch.label = label;
        }
    }

    void TelemetryPanel::ArmRecorder(EditorContext& ctx)
    {
        DedupLabels();

        std::vector<std::string> labels;
        labels.reserve(m_Channels.size());
        for (const Channel& ch : m_Channels) labels.push_back(ch.label);

        m_Rec    = std::make_unique<Cosmic::DataRecorder>();
        m_TakeId = m_Rec->Register(kTakeEntity, kTakeEntity, labels);
        m_Rec->ReserveCapacity(static_cast<size_t>(180.0f / m_FixedDt));   // 3 min headroom

        // Crash-failsafe: roll a snapshot into the take folder every few seconds.
        const std::string base = Cosmic::FileSystem::Resolve("user://starforge/takes");
        std::error_code ec; fs::create_directories(base, ec);
        m_Rec->SetAutosave(base, m_TakeName, 5.0f, 1.0f / m_FixedDt);

        m_Armed = true;
        ctx.Log("[Telemetry] Recording " + std::to_string(m_Channels.size()) +
                " channel(s) -> " + m_TakeName);
    }

    float TelemetryPanel::ReadReflected(EditorContext& ctx, const Channel& ch)
    {
        if (!ctx.Scene) return 0.0f;
        auto& reg = ctx.Scene->GetRegistry();

        entt::entity h = ch.handle;
        if (!reg.valid(h))
        {
            Cosmic::Entity e = ctx.Scene->FindByUUID(Cosmic::UUID(ch.uuid));
            if (!e) return 0.0f;
            h = (entt::entity)e;
        }

        const TypeDescriptor* d = Cosmic::Reflect::GetRegistry().Find(ch.typeId);
        if (!d || !d->Get) return 0.0f;
        void* comp = d->Get(reg, h);
        if (!comp) return 0.0f;
        const FieldDescriptor* f = d->FindField(ch.field);
        if (!f) return 0.0f;
        return FieldValueToFloat(f->Get(comp), ch.comp);
    }

    void TelemetryPanel::OnFixedStep(EditorContext& ctx)
    {
        if (m_Mode != Mode::Recording || !ctx.Scene)
        {
            m_Scratch.clear();
            return;
        }

        // First step: discover script channels, then arm the store.
        if (!m_Armed)
        {
            for (const auto& [raw, chans] : m_Scratch)
                for (const auto& [name, val] : chans)
                    AddScriptChannel(ctx, raw, name);

            if (m_Channels.empty())   // nothing to record yet — wait for data
            {
                m_Scratch.clear();
                return;
            }
            ArmRecorder(ctx);
        }
        else
        {
            // A channel that first appears after arming can't be added (the store's
            // schema is fixed). Warn once so the drop is visible, then ignore it.
            for (const auto& [raw, chans] : m_Scratch)
                for (const auto& [name, val] : chans)
                {
                    (void)val;
                    if (!m_ScriptKeys.count(std::to_string(raw) + ":" + name) && !m_WarnedLate)
                    {
                        ctx.Log("[Telemetry] Channel '" + name + "' appeared after recording "
                                "started and was ignored (push it from the first step).",
                                LogSeverity::Warn);
                        m_WarnedLate = true;
                    }
                }
        }

        // Build one row in channel order.
        std::vector<float> row(m_Channels.size(), 0.0f);
        for (size_t i = 0; i < m_Channels.size(); ++i)
        {
            Channel& ch = m_Channels[i];
            float v;
            if (ch.isScript)
            {
                v = ch.samples.empty() ? 0.0f : ch.samples.back();   // carry last if not pushed
                auto it = m_Scratch.find(ch.scriptRaw);
                if (it != m_Scratch.end())
                {
                    auto jt = it->second.find(ch.scriptChannel);
                    if (jt != it->second.end()) v = jt->second;
                }
            }
            else
            {
                v = ReadReflected(ctx, ch);
            }
            ch.samples.push_back(v);
            row[i] = v;
        }

        m_Rec->Tick(m_FixedDt);
        m_Rec->Record(m_TakeId, row);
        m_Times.push_back(m_Rec->GetRecordedDuration());
        ++m_StepIndex;
        m_Scratch.clear();

        if (m_Follow && !m_Times.empty())
            m_Scrub = m_Times.back();
    }

    void TelemetryPanel::OnPlayStop(EditorContext& ctx)
    {
        if (m_Mode != Mode::Recording)
            return;

        if (m_Armed && m_Rec)
        {
            m_Rec->DisableAutosave();
            m_Rec->WaitForFlush();   // drain any in-flight autosave so the final Flush isn't skipped
            const std::string base = Cosmic::FileSystem::Resolve("user://starforge/takes");
            m_Rec->Flush(base, m_TakeName, 1.0f / m_FixedDt);
            m_Rec->WaitForFlush();   // guarantee files exist so the take reloads

            std::error_code ec;
            const fs::path abs = fs::absolute(base + "/" + m_TakeName, ec);
            m_LastTakeDir = ec ? (base + "/" + m_TakeName) : abs.string();
            m_TakeListDirty = true;
            ctx.Log("[Telemetry] Take saved: " + m_LastTakeDir + "  (" +
                    std::to_string(m_Times.size()) + " samples).");
            m_Mode  = Mode::Stopped;
            m_Scrub = m_Times.empty() ? 0.0f : m_Times.back();
        }
        else
        {
            m_Mode = Mode::Empty;
        }
        m_Scratch.clear();
    }

    void TelemetryPanel::Push(entt::entity source, const char* channel, float value)
    {
        if (!channel) return;
        m_Scratch[(uint32_t)source][channel] = value;
    }

    // =========================================================================
    // Take reload / CSV export
    // =========================================================================

    void TelemetryPanel::RefreshTakeList()
    {
        m_TakeFolders.clear();
        m_TakeListDirty = false;

        std::error_code ec;
        const fs::path root = Cosmic::FileSystem::Resolve("user://starforge/takes");
        if (!fs::exists(root, ec)) return;
        for (const auto& e : fs::directory_iterator(root, ec))
            if (e.is_directory(ec))
                m_TakeFolders.push_back(e.path().filename().string());
        std::sort(m_TakeFolders.rbegin(), m_TakeFolders.rend());   // newest first
    }

    bool TelemetryPanel::LoadTake(EditorContext& ctx, const std::string& folder)
    {
        const std::string dir = Cosmic::FileSystem::Resolve("user://starforge/takes/" + folder);

        Cosmic::DataPlayer player;
        if (!player.Load(dir))
        {
            ctx.Log("[Telemetry] Failed to load take: " + folder, LogSeverity::Error);
            return false;
        }

        const std::vector<std::string> names = player.GetEntityNames();
        if (names.empty()) { ctx.Log("[Telemetry] Take is empty: " + folder, LogSeverity::Warn); return false; }
        const std::string      ent  = names.front();
        const Cosmic::EntityTelemetryInfo* info = player.GetInfo(ent);
        if (!info) return false;

        // Rebuild plot columns by sampling the player at its native rate.
        ResetTake();
        m_Player = std::move(player);
        m_Mode   = Mode::Loaded;
        m_TakeName = folder;

        for (const std::string& chName : info->channels)
        {
            Channel ch;
            ch.label = chName;
            ch.group = GroupFor("L:" + chName, chName);
            m_Channels.push_back(std::move(ch));
        }

        const float dur  = m_Player.GetDuration();
        const float rate = m_Player.GetSampleRate() > 0.0f ? m_Player.GetSampleRate() : 60.0f;
        const int   N    = std::max(1, (int)std::lround(dur * rate));
        for (int s = 0; s <= N; ++s)
        {
            const float t = (float)s / rate;
            Cosmic::TelemetryFrame fr;
            m_Player.SampleAt(ent, t, fr);
            m_Times.push_back(t);
            for (size_t i = 0; i < m_Channels.size(); ++i)
                m_Channels[i].samples.push_back(i < fr.values.size() ? fr.values[i] : 0.0f);
        }

        m_Scrub = 0.0f;
        ctx.Log("[Telemetry] Loaded take: " + folder + "  (" +
                std::to_string(m_Channels.size()) + " channels, " +
                std::to_string(m_Times.size()) + " samples).");
        return true;
    }

    void TelemetryPanel::ExportCsv(EditorContext& ctx)
    {
        if (m_Channels.empty() || m_Times.empty())
        {
            ctx.Log("[Telemetry] Nothing to export.", LogSeverity::Warn);
            return;
        }

        std::vector<std::string> headers;
        headers.reserve(m_Channels.size() + 1);
        headers.push_back("Time");
        for (const Channel& ch : m_Channels) headers.push_back(ch.label);

        std::vector<std::vector<double>> cols;
        cols.reserve(m_Channels.size() + 1);
        cols.push_back(AsDouble(m_Times));
        for (const Channel& ch : m_Channels) cols.push_back(AsDouble(ch.samples));

        const std::string base = Cosmic::FileSystem::Resolve("user://starforge/takes");
        std::error_code ec; fs::create_directories(base, ec);
        const std::string name = m_TakeName.empty() ? std::string("take") : m_TakeName;
        const std::string path = base + "/" + name + "_export.csv";

        if (Cosmic::DataExport::WriteCSV(path, headers, cols))
        {
            const fs::path abs = fs::absolute(path, ec);
            ctx.Log("[Telemetry] Exported CSV: " + (ec ? path : abs.string()));
        }
        else
        {
            ctx.Log("[Telemetry] CSV export failed: " + path, LogSeverity::Error);
        }
    }

    // =========================================================================
    // UI
    // =========================================================================

    void TelemetryPanel::DrawMarksManager(EditorContext& ctx)
    {
        if (!ImGui::CollapsingHeader("Recorded fields", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        ImGui::BeginDisabled(!ctx.HasSelection());
        if (ImGui::Button("Add from selection"))
            for (entt::entity h : ctx.Selection)
                Telemetry::RecordAllFields(ctx, Cosmic::Entity(h, ctx.Scene.get()));
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(ctx.Recorded.empty());
        if (ImGui::Button("Clear all")) ctx.Recorded.clear();
        ImGui::EndDisabled();

        if (ctx.Recorded.empty())
        {
            ImGui::TextDisabled("Right-click a numeric field in the Inspector to record it,");
            ImGui::TextDisabled("or select entities and press \"Add from selection\".");
            return;
        }

        // Collapse per-component entries to one row per (entity, component, field).
        struct Key { uint64_t uuid; entt::id_type type; std::string field; };
        std::vector<Key> seen;
        for (const Telemetry::RecordedChannel& c : ctx.Recorded)
        {
            const bool have = std::any_of(seen.begin(), seen.end(), [&](const Key& k)
                { return k.uuid == c.Entity && k.type == c.TypeId && k.field == c.Field; });
            if (!have) seen.push_back({ c.Entity, c.TypeId, c.Field });
        }

        for (const Key& k : seen)
        {
            std::string tag = "?";
            if (ctx.Scene)
                if (Cosmic::Entity e = ctx.Scene->FindByUUID(Cosmic::UUID(k.uuid)))
                    tag = EntityPrefix(e);

            ImGui::PushID((int)(k.uuid ^ (uint64_t)k.type));
            if (ImGui::SmallButton("x"))
            {
                // Remove all components of this field.
                auto& v = ctx.Recorded;
                v.erase(std::remove_if(v.begin(), v.end(), [&](const Telemetry::RecordedChannel& c)
                        { return c.Entity == k.uuid && c.TypeId == k.type && c.Field == k.field; }),
                        v.end());
                ImGui::PopID();
                continue;
            }
            ImGui::SameLine();
            const bool live = ctx.Scene && (bool)ctx.Scene->FindByUUID(Cosmic::UUID(k.uuid));
            if (live) ImGui::Text("%s.%s", tag.c_str(), k.field.c_str());
            else      ImGui::TextDisabled("%s.%s  (missing)", tag.c_str(), k.field.c_str());
            ImGui::PopID();
        }
    }

    void TelemetryPanel::DrawPlots(EditorContext& ctx)
    {
        (void)ctx;
        if (m_Times.empty())
        {
            ImGui::TextDisabled(m_Mode == Mode::Recording
                ? "Waiting for the first fixed step..."
                : "No samples. Mark fields and press Play.");
            return;
        }

        const bool  scrubbing = (m_Mode == Mode::Stopped || m_Mode == Mode::Loaded);
        const float tLast     = m_Times.back();

        for (int g = 0; g < (int)m_Groups.size(); ++g)
        {
            // Skip empty groups (all channels hidden).
            bool any = false;
            for (const Channel& ch : m_Channels) if (ch.group == g && ch.visible) { any = true; break; }
            if (!any) continue;

            const std::string title = m_Groups[g].title + "##plot" + std::to_string(g);

            const bool follow = (m_Follow && m_Mode == Mode::Recording);
            const ImPlotAxisFlags xflags = follow ? ImPlotAxisFlags_None : ImPlotAxisFlags_AutoFit;

            if (ImPlot::BeginPlot(title.c_str(), ImVec2(-1.0f, m_PlotHeight)))
            {
                ImPlot::SetupAxes("t (s)", nullptr, xflags, ImPlotAxisFlags_AutoFit);
                if (follow)
                    ImPlot::SetupAxisLimits(ImAxis_X1,
                        std::max(0.0f, tLast - m_Window), tLast, ImPlotCond_Always);
                ImPlot::SetupLegend(ImPlotLocation_NorthEast, ImPlotLegendFlags_Outside);

                for (Channel& ch : m_Channels)
                {
                    if (ch.group != g || !ch.visible) continue;
                    ImPlot::PlotLine(ch.label.c_str(), m_Times.data(), ch.samples.data(),
                                     (int)ch.samples.size());
                }

                if (scrubbing)
                {
                    double x = m_Scrub;
                    ImPlot::DragLineX(9000 + g, &x, ImVec4(1.0f, 0.85f, 0.2f, 1.0f), 1.5f);
                    m_Scrub = std::clamp((float)x, 0.0f, tLast);
                }
                ImPlot::EndPlot();
            }
        }
    }

    void TelemetryPanel::OnImGuiRender(EditorContext& ctx)
    {
        ImGui::Begin("Telemetry");

        // --- status + take actions -------------------------------------------
        switch (m_Mode)
        {
            case Mode::Recording:
                ImGui::TextColored(ImVec4(0.30f, 1.0f, 0.42f, 1.0f),
                    "RECORDING — %d samples, %d channel(s)", m_StepIndex, (int)m_Channels.size());
                break;
            case Mode::Stopped:
                ImGui::Text("Take: %s  (%d samples)", m_TakeName.c_str(), (int)m_Times.size());
                break;
            case Mode::Loaded:
                ImGui::Text("Loaded: %s  (%d samples)", m_TakeName.c_str(), (int)m_Times.size());
                break;
            default:
                ImGui::TextDisabled("No take. Mark fields, then press Play to record.");
                break;
        }

        ImGui::BeginDisabled(m_Channels.empty() || m_Times.empty());
        if (ImGui::Button("Export CSV")) ExportCsv(ctx);
        ImGui::EndDisabled();

        ImGui::Separator();
        DrawMarksManager(ctx);
        ImGui::Separator();

        // --- transport / scope controls --------------------------------------
        if (m_Mode == Mode::Recording)
        {
            ImGui::Checkbox("Follow", &m_Follow);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(160.0f);
            ImGui::SliderFloat("Window (s)", &m_Window, 1.0f, 60.0f, "%.0f");
        }
        else if (m_Mode == Mode::Stopped || m_Mode == Mode::Loaded)
        {
            const float dur = m_Times.empty() ? 0.0f : m_Times.back();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderFloat("##scrub", &m_Scrub, 0.0f, dur, "t = %.3f s");
        }

        DrawPlots(ctx);

        // --- value readout at the playhead -----------------------------------
        if ((m_Mode == Mode::Stopped || m_Mode == Mode::Loaded) && !m_Times.empty())
        {
            // Nearest sample index to the scrub time (uniform spacing).
            const int   last = (int)m_Times.size() - 1;
            const float dt   = (last > 0) ? m_Times.back() / (float)last : 1.0f;
            int idx = (dt > 1e-6f) ? (int)std::lround(m_Scrub / dt) : 0;
            idx = std::clamp(idx, 0, last);
            if (ImGui::CollapsingHeader("Values at playhead"))
                for (const Channel& ch : m_Channels)
                    ImGui::Text("%-24s % .5f", ch.label.c_str(),
                                idx < (int)ch.samples.size() ? ch.samples[idx] : 0.0f);
        }

        // --- saved takes ------------------------------------------------------
        if (ImGui::CollapsingHeader("Saved takes"))
        {
            if (m_TakeListDirty) RefreshTakeList();
            if (ImGui::SmallButton("Refresh")) m_TakeListDirty = true;

            if (m_TakeFolders.empty())
                ImGui::TextDisabled("None yet — record a take and press Stop.");
            for (const std::string& f : m_TakeFolders)
            {
                ImGui::PushID(f.c_str());
                if (ImGui::SmallButton("Load")) LoadTake(ctx, f);
                ImGui::SameLine();
                ImGui::TextUnformatted(f.c_str());
                ImGui::PopID();
            }
        }

        ImGui::End();
    }
}
