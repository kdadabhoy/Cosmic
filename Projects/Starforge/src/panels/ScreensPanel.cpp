// panels/ScreensPanel.cpp — see ScreensPanel.h (AP-03, contract §5, E02 / F01).

#include "ScreensPanel.h"
#include "../ProjectManifest.h"

#include "utils/FileSystem.h"
#include "ui/IconsLucide.h"

#include <imgui.h>
#include <imgui_internal.h>   // UX-02 — GetCurrentContext for the throttled scene listing

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

namespace Starforge
{
    namespace
    {
        std::string StemOf(const std::string& vfs)
        {
            return fs::path(vfs).stem().string();
        }
    }

    std::string ScreensPanel::FlowDiskPath(const Host& host) const
    {
        if (host.ManifestFlow.empty()) return {};
        return (fs::path(host.ProjectRoot) / host.ManifestFlow).generic_string();
    }

    bool ScreensPanel::Reload(const Host& host)
    {
        const std::string key = host.ProjectRoot + "|" + host.ManifestFlow;
        if (m_Loaded && key == m_LoadedFor) return m_HasFlow;
        m_Loaded = true; m_LoadedFor = key;
        m_Asset = Cosmic::FlowAsset{};
        m_HasFlow = false;
        const std::string path = FlowDiskPath(host);
        std::error_code ec;
        if (path.empty() || !fs::exists(path, ec)) return false;
        std::string err;
        m_HasFlow = Cosmic::FlowAsset::Load(m_Asset, path, &err);
        if (!m_HasFlow) { m_Status = "flow failed to load: " + err; m_StatusError = true; }
        return m_HasFlow;
    }

    bool ScreensPanel::SaveFlow(const Host& host, std::string* error)
    {
        const std::string path = FlowDiskPath(host);
        if (path.empty()) { if (error) *error = "no startup flow"; return false; }
        if (!m_Asset.Save(path)) { if (error) *error = "could not write " + path; return false; }
        return true;
    }

    // ---- operations ----------------------------------------------------------
    ScreensPanel::OpResult ScreensPanel::CreateFlow(EditorContext& ctx, const Host& host)
    {
        OpResult r;
        const std::string rel = host.ManifestFlow.empty() ? std::string("flows/Main.cflow") : host.ManifestFlow;
        const fs::path path = fs::path(host.ProjectRoot) / rel;
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        if (!fs::exists(path, ec))
        {
            Cosmic::FlowAsset a;
            if (!a.Save(path.generic_string())) { r.Message = "could not write " + path.generic_string(); return r; }
        }
        if (host.SetManifestFlow) host.SetManifestFlow(rel);
        m_Loaded = false;
        ctx.Log("[Screens] Created " + rel + " and set it as the project's startup flow.");
        r.Ok = true; r.Message = "created " + rel;
        return r;
    }

    ScreensPanel::OpResult ScreensPanel::NewScreen(EditorContext& ctx, const Host& host,
                                                   const std::string& name, bool createScript)
    {
        OpResult r;
        if (!Reload(host)) { r.Message = "the project has no startup flow — create one first"; return r; }
        std::string err;
        if (!ScreenScaffold::ValidName(name)) { r.Message = "'" + name + "' is not a valid screen name (identifier)"; return r; }
        if (m_Asset.Find(name)) { r.Message = "a screen named '" + name + "' already exists"; return r; }

        // Script FIRST when requested: a marker refusal must leave the project unchanged.
        if (createScript)
        {
            const ScreenScaffoldResult s = ScreenScaffold::CreateScript(host.ProjectRoot, name, host.ProjectName);
            if (!s.Ok) { r.Message = s.Message; ctx.Log("[Screens] " + s.Message, LogSeverity::Error); return r; }
        }
        if (!ScreenScaffold::WriteScreenScene(host.ProjectRoot, name, createScript, &err)) { r.Message = err; return r; }
        if (!ScreenScaffold::AddFlowState(m_Asset, name, &err)) { r.Message = err; return r; }
        if (!SaveFlow(host, &err)) { r.Message = err; return r; }
        m_Selected = name;
        ctx.Log("[Screens] New screen '" + name + "': scenes/" + name + ".cscene + flow state" +
                (createScript ? " + src/screens/" + name + "Screen.h" : std::string()));
        r.Ok = true; r.Message = "created screen " + name;
        return r;
    }

    ScreensPanel::OpResult ScreensPanel::SetAsStart(EditorContext& ctx, const Host& host, const std::string& name)
    {
        OpResult r;
        if (!Reload(host) || !m_Asset.Find(name)) { r.Message = "unknown screen '" + name + "'"; return r; }
        m_Asset.Start = name;
        std::string err;
        if (!SaveFlow(host, &err)) { r.Message = err; return r; }
        ctx.Log("[Screens] Start state -> '" + name + "'.");
        r.Ok = true; r.Message = "start = " + name;
        return r;
    }

    ScreensPanel::OpResult ScreensPanel::Rename(EditorContext& ctx, const Host& host,
                                                const std::string& oldName, const std::string& newName)
    {
        OpResult r;
        if (!Reload(host) || !m_Asset.Find(oldName)) { r.Message = "unknown screen '" + oldName + "'"; return r; }
        if (!ScreenScaffold::ValidName(newName)) { r.Message = "'" + newName + "' is not a valid screen name"; return r; }
        if (m_Asset.Find(newName)) { r.Message = "a screen named '" + newName + "' already exists"; return r; }
        for (auto& s : m_Asset.States)
        {
            if (s.Name == oldName) s.Name = newName;
            for (auto& t : s.Transitions) if (t.To == oldName) t.To = newName;
        }
        if (m_Asset.Start == oldName) m_Asset.Start = newName;
        std::string err;
        if (!SaveFlow(host, &err)) { r.Message = err; return r; }
        if (m_Selected == oldName) m_Selected = newName;
        ctx.Log("[Screens] Renamed state '" + oldName + "' -> '" + newName + "' (the scene and script files keep their names).");
        r.Ok = true; r.Message = "renamed";
        return r;
    }

    ScreensPanel::OpResult ScreensPanel::CreateOrRelinkScript(EditorContext& ctx, const Host& host, const std::string& name)
    {
        OpResult r;
        const ScreenScaffoldResult s = ScreenScaffold::CreateScript(host.ProjectRoot, name, host.ProjectName);
        if (!s.Ok) { r.Message = s.Message; ctx.Log("[Screens] " + s.Message, LogSeverity::Error); return r; }
        std::string err;
        if (!ScreenScaffold::LinkScriptInScene(host.ProjectRoot, name, &err))
        {
            r.Message = err; ctx.Log("[Screens] " + err, LogSeverity::Warn); return r;
        }
        ctx.Log("[Screens] " + s.Message + " (rebuild with Ctrl+B).");
        r.Ok = true; r.Message = s.Message;
        return r;
    }

    bool ScreensPanel::OpenScript(const Host& host, const std::string& name)
    {
        return SourceLocator::Open(SourceLocator(host.ProjectRoot).ForScreen(name));
    }

    // ---- UX-02: the Scenes section ---------------------------------------------
    void ScreensPanel::RefreshScenes(const Host& host)
    {
        const std::string key = host.ProjectRoot + "|" + host.ManifestFlow;
        const double now = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
        if (key == m_ScenesFor && m_ScenesAt >= 0.0 && now - m_ScenesAt < 1.0) return;
        m_ScenesFor = key;
        m_ScenesAt  = now;
        m_SceneList = SourceLocator::ProjectScenes(host.ProjectRoot);
        // The start scene: the flow's start state's scene, else project.cproj startup_scene.
        m_StartScene.clear();
        if (Reload(host))
        {
            const Cosmic::FlowState* st = m_Asset.Find(m_Asset.Start);
            if (!st && !m_Asset.States.empty()) st = &m_Asset.States.front();
            if (st) m_StartScene = st->Scene;
        }
        if (m_StartScene.empty())
        {
            std::error_code ec;
            const fs::path manifest = fs::path(host.ProjectRoot) / "project.cproj";
            if (!host.ProjectRoot.empty() && fs::exists(manifest, ec))
            {
                const ProjectManifest pm = ProjectManifest::Load(manifest.generic_string());
                if (!pm.StartupScene.empty())
                    m_StartScene = pm.StartupScene.rfind("project://", 0) == 0 ? pm.StartupScene
                                                                               : "project://" + pm.StartupScene;
            }
        }
    }

    bool ScreensPanel::RevealScene(const Host& host, const std::string& vfs)
    {
        if (vfs.rfind("project://", 0) != 0) return false;
        SourceHit h;
        h.Path = SourceLocator::Normalize((fs::path(host.ProjectRoot) / vfs.substr(10)).generic_string());
        h.Reason = "scene file";
        return SourceLocator::Reveal(h);
    }

    void ScreensPanel::DrawScenes(EditorContext& ctx, const Host& host)
    {
        RefreshScenes(host);
        m_SceneRows.clear();
        ImGui::SeparatorText("Scenes");
        if (m_SceneList.empty())
        {
            ImGui::TextDisabled("no scenes under scenes/ yet (File > Save As... writes one)");
            return;
        }
        // A list sized to its rows (at most 10 visible, then it scrolls): the panel itself
        // scrolls when it is shorter, instead of squeezing the list to nothing.
        const float listH = ImGui::GetTextLineHeightWithSpacing() * (float)std::min<size_t>(m_SceneList.size(), 10) +
                            ImGui::GetStyle().WindowPadding.y * 2.0f + 2.0f;
        if (ImGui::BeginChild("##ux02scenes", ImVec2(0.0f, listH), ImGuiChildFlags_Borders))
        {
            const ImVec4 startCol(0.45f, 0.95f, 0.55f, 1.0f);
            for (const std::string& vfs : m_SceneList)
            {
                static const std::string kPrefix = "project://scenes/";
                const std::string rel = vfs.rfind(kPrefix, 0) == 0 ? vfs.substr(kPrefix.size()) : vfs;
                const bool isOpen  = (ctx.SceneVfsPath == vfs);
                const bool isStart = (vfs == m_StartScene);
                ImGui::PushID(vfs.c_str());
                if (isStart) ImGui::PushStyleColor(ImGuiCol_Text, startCol);
                const std::string label = isStart ? (rel + "   " ICON_LC_PLAY " start") : rel;
                if (ImGui::Selectable(label.c_str(), isOpen, ImGuiSelectableFlags_AllowDoubleClick))
                {
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && host.OpenScene)
                        host.OpenScene(vfs);
                }
                if (isStart) ImGui::PopStyleColor();
                {
                    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
                    m_SceneRows.push_back({ vfs, (mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f,
                                            ImGui::IsItemVisible(), isStart, isOpen });
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s%s%s\nDouble-click to open; right-click to reveal.", vfs.c_str(),
                                      isStart ? "\nthe project's start scene" : "", isOpen ? "\n(open now)" : "");
                if (ImGui::BeginPopupContextItem("##scenectx"))
                {
                    if (ImGui::MenuItem(ICON_LC_FOLDER_OPEN " Reveal in Explorer")) RevealScene(host, vfs);
                    if (ImGui::MenuItem("Open", nullptr, false, !isOpen && (bool)host.OpenScene)) host.OpenScene(vfs);
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }

    bool ScreensPanel::RevealScript(const Host& host, const std::string& name)
    {
        return SourceLocator::Reveal(SourceLocator(host.ProjectRoot).ForScreen(name));
    }

    // ---- draw ----------------------------------------------------------------
    void ScreensPanel::OnImGuiRender(EditorContext& ctx, bool* pOpen, const Host& host)
    {
        if (!ImGui::Begin("Screens", pOpen)) { ImGui::End(); return; }

        if (!ctx.ProjectOpen)
        {
            ImGui::TextDisabled("Open a project to manage its screens.");
            ImGui::End();
            return;
        }
        const bool hasFlow = Reload(host);
        auto report = [&](const OpResult& r) { m_Status = r.Message; m_StatusError = !r.Ok; };

        if (!hasFlow)
        {
            ImGui::TextDisabled("no flow");
            ImGui::TextWrapped("This project has no startup flow, so it has no screens. A flow is the list of "
                               "screens and how they navigate (project.cproj: startup_flow).");
            if (ImGui::Button(ICON_LC_PLUS " Create flow"))
                report(CreateFlow(ctx, host));
            if (!m_Status.empty()) { ImGui::Separator(); ImGui::TextDisabled("%s", m_Status.c_str()); }
            DrawScenes(ctx, host);   // UX-02 — a project without a flow still lists its scenes
            ImGui::End();
            return;
        }

        const SourceLocator loc(host.ProjectRoot);

        // Toolbar: New Screen popup + flow document.
        if (ImGui::Button(ICON_LC_PLUS " New Screen"))
            ImGui::OpenPopup("New Screen");
        ImGui::SameLine();
        if (ImGui::Button(ICON_LC_WORKFLOW " Flow graph") && host.OpenFlowDocument)
            host.OpenFlowDocument("project://" + host.ManifestFlow);
        ImGui::SameLine();
        ImGui::TextDisabled("%s  (%zu screens, start: %s)", host.ManifestFlow.c_str(),
                            m_Asset.States.size(), m_Asset.Start.empty() ? "-" : m_Asset.Start.c_str());

        if (ImGui::BeginPopupModal("New Screen", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Name (identifier; scenes/<Name>.cscene + flow state)");
            ImGui::SetNextItemWidth(260.0f);
            ImGui::InputText("##nsname", m_NewName, sizeof(m_NewName));
            ImGui::Checkbox("Create script (src/screens/<Name>Screen.h + CS_SCRIPT in Module.cpp)", &m_NewWithScript);
            ImGui::Separator();
            const bool valid = ScreenScaffold::ValidName(m_NewName) && !m_Asset.Find(m_NewName);
            ImGui::BeginDisabled(!valid);
            if (ImGui::Button("Create", ImVec2(120, 0)))
            {
                report(NewScreen(ctx, host, m_NewName, m_NewWithScript));
                if (!m_StatusError) ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
            if (!valid && m_NewName[0]) ImGui::TextDisabled("name must be a unique identifier");
            ImGui::EndPopup();
        }

        ImGui::Separator();

        // The table of screens. UX-02: sized to its rows (at most 8 visible, then it
        // scrolls) so the Scenes section below keeps the rest of the panel.
        const float rowH = ImGui::GetTextLineHeightWithSpacing() + 4.0f;
        const float tableH = rowH * (float)(std::min<size_t>(m_Asset.States.size(), 8) + 1) + 6.0f;
        if (ImGui::BeginTable("##screens", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                              ImGuiTableFlags_ScrollY, ImVec2(0, tableH)))
        {
            ImGui::TableSetupColumn("Screen", ImGuiTableColumnFlags_WidthStretch, 0.35f);
            ImGui::TableSetupColumn("Scene",  ImGuiTableColumnFlags_WidthStretch, 0.30f);
            ImGui::TableSetupColumn("Script", ImGuiTableColumnFlags_WidthStretch, 0.25f);
            ImGui::TableSetupColumn("Start",  ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableHeadersRow();
            for (const Cosmic::FlowState& s : m_Asset.States)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const bool sel = (m_Selected == s.Name);
                if (ImGui::Selectable((s.Name + "##row").c_str(), sel,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
                {
                    m_Selected = s.Name;
                    std::snprintf(m_RenameBuf, sizeof(m_RenameBuf), "%s", s.Name.c_str());
                    if (ImGui::IsMouseDoubleClicked(0) && !s.Scene.empty() && host.OpenScene)
                        host.OpenScene(s.Scene);
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(s.Scene.empty() ? "(none)" : StemOf(s.Scene).c_str());
                ImGui::TableSetColumnIndex(2);
                {
                    const SourceHit h = loc.ForScreen(s.Name);
                    if (h.Resolved()) ImGui::TextUnformatted((s.Name + "Screen").c_str());
                    else              ImGui::TextDisabled("(none)");
                }
                ImGui::TableSetColumnIndex(3);
                if (m_Asset.Start == s.Name) ImGui::TextUnformatted(ICON_LC_PLAY);
            }
            ImGui::EndTable();
        }

        // Actions on the selected screen.
        const Cosmic::FlowState* cur = m_Selected.empty() ? nullptr : m_Asset.Find(m_Selected);
        ImGui::BeginDisabled(cur == nullptr);
        if (ImGui::Button("Set as start") && cur)              report(SetAsStart(ctx, host, cur->Name));
        ImGui::SameLine();
        if (ImGui::Button("Open scene") && cur && host.OpenScene && !cur->Scene.empty()) host.OpenScene(cur->Scene);
        ImGui::SameLine();
        {
            const SourceHit h = cur ? loc.ForScreen(cur->Name) : SourceHit{};
            ImGui::BeginDisabled(!h.Resolved());
            if (ImGui::Button("Open script") && cur) OpenScript(host, cur->Name);
            if (!h.Resolved() && cur && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", h.Reason.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Reveal") && cur) RevealScript(host, cur->Name);
            if (!h.Resolved() && cur && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", h.Reason.c_str());
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button(h.Resolved() ? "Relink script" : "Create script") && cur)
                report(CreateOrRelinkScript(ctx, host, cur->Name));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(h.Resolved() ? "Re-add the CS_SCRIPT block / the scene's NativeScript for this screen"
                                               : "Write src/screens/<Name>Screen.h from the stub and register it in Module.cpp");
        }
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::InputText("##rename", m_RenameBuf, sizeof(m_RenameBuf), ImGuiInputTextFlags_EnterReturnsTrue) && cur)
            report(Rename(ctx, host, cur->Name, m_RenameBuf));
        ImGui::SameLine();
        if (ImGui::Button("Rename") && cur) report(Rename(ctx, host, cur->Name, m_RenameBuf));
        ImGui::EndDisabled();

        if (!m_Status.empty())
        {
            if (m_StatusError) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", m_Status.c_str());
            else               ImGui::TextDisabled("%s", m_Status.c_str());
        }
        DrawScenes(ctx, host);   // UX-02 — every scene file, under the flow states
        ImGui::End();
    }
}
