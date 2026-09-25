// StarforgeAppPrefs.cpp — UX-02 (UX & Shipping, contract §2 "Preferences" + "what a button
// does" + "Scenes list"): the editor-shell half of those rules, kept OUT of StarforgeApp.cpp
// (that TU sits at the /bigobj limit; it only carries hook lines). Everything here is a
// StarforgeApp member:
//
//   * Edit ▸ Preferences… — a modal with autosave_enabled, the autosave interval (whole
//     minutes 1-60, the existing autosave_minutes key), prompt_unsaved, and the autosave
//     folder (user://starforge/autosave/) with a Reveal button; every change is written to
//     user://starforge/editor.toml at once (Prefs::SaveSettings);
//   * the unsaved-changes prompt: the USER commands (File ▸ New Scene / Open Scene / Recent
//     Projects / Close Project / Exit to Launcher, Ctrl+N, the Screens panel's scene opens)
//     go through GuardUnsaved, which raises Save / Discard / Cancel (the shape of
//     AssetEditorHost's close prompt) when the scene is dirty and prompt_unsaved is on.
//     The FUNCTIONS (NewScene, OpenScene, OpenProject, CloseProject) stay unguarded: the
//     editor self-tests call them directly;
//   * the autosave copy (shared by the timed Autosave and OnDetach's window-close path)
//     and the status bar's "autosaved HH:MM" chip;
//   * File ▸ Open Scene through SourceLocator::ProjectScenes (the Screens ▸ Scenes lister);
//   * "Open in flow editor" for a Flow hit: open the .cflow document the way the Screens
//     panel does and select the transition (FlowEditor::HarnessSelect).
//
// The window's own close (the OS / title-bar ✕) cannot prompt: Application handles
// WindowCloseEvent before any layer sees it and the chrome ✕ calls Application::Close, so
// OnDetach writes an autosave copy of a dirty scene instead (contract deviation, recorded).

#include "StarforgeApp.h"

#include "editors/FlowEditor.h"
#include "scene/SceneSerializer.h"
#include "utils/FileSystem.h"
#include "ui/IconsLucide.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>

namespace fs = std::filesystem;

namespace Starforge
{
    namespace
    {
        std::string NowHHMM()
        {
            std::time_t now = std::time(nullptr);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &now);
#else
            localtime_r(&now, &tm);
#endif
            char buf[8] = { 0 };
            std::snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
            return buf;
        }

        void ItemCentre(float& x, float& y)
        {
            const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
            x = (mn.x + mx.x) * 0.5f;
            y = (mn.y + mx.y) * 0.5f;
        }
    }

    // =========================================================================
    // The unsaved-changes prompt around the user commands
    // =========================================================================
    bool StarforgeApp::UnsavedWork() const
    {
        // The open scene's edits (UX-01's dirty flow / story documents join this test at rebase).
        return m_Ctx.ProjectOpen && m_Ctx.Scene && m_Ctx.Dirty;
    }

    void StarforgeApp::GuardUnsaved(const std::string& what, std::function<void()> action)
    {
        if (!action) return;
        if (m_PendingGuarded) return;    // a prompt is already up: the second command is ignored
        if (IsPlaying()) StopScene();    // the prompt is about the EDIT scene (Play's is throwaway)
        if (!m_Settings.PromptUnsaved || !UnsavedWork())
        {
            action();
            return;
        }
        m_PendingGuarded     = std::move(action);
        m_PendingGuardedWhat = what;
        m_OpenUnsavedPrompt  = true;
    }

    void StarforgeApp::RequestNewScene()
    {
        GuardUnsaved("create a new scene", [this] { NewScene(); });
    }

    void StarforgeApp::RequestOpenScene(const std::string& vfsPath)
    {
        GuardUnsaved("open '" + fs::path(vfsPath).filename().generic_string() + "'",
                     [this, vfsPath] { OpenScene(vfsPath); });
    }

    void StarforgeApp::RequestOpenProject(const Prefs::ProjectEntry& e)
    {
        GuardUnsaved("open the project '" + e.Name + "'", [this, e] { OpenProject(e); });
    }

    void StarforgeApp::RequestCloseProject()
    {
        GuardUnsaved("close the project", [this] { CloseProject(); });
    }

    void StarforgeApp::RequestExitToLauncher()
    {
        GuardUnsaved("exit to the Launcher", [] { Cosmic::Application::Get().TransitionToLauncher(); });
    }

    void StarforgeApp::DrawUnsavedPrompt()
    {
        static const char* kPopup = "Unsaved Changes##ux02";
        if (m_OpenUnsavedPrompt)
        {
            ImGui::OpenPopup(kPopup);
            m_OpenUnsavedPrompt = false;
        }
        m_UnsavedProbe.Drawn = false;
        std::function<void()> run;
        if (ImGui::BeginPopupModal(kPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (!m_PendingGuarded)
            {
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                return;
            }
            const std::string scene = m_Ctx.SceneName.empty() ? std::string("Untitled") : m_Ctx.SceneName;
            ImGui::Text("Save changes to \"%s\" before you %s?", scene.c_str(), m_PendingGuardedWhat.c_str());
            ImGui::TextDisabled("Edit > Preferences... can turn this question off.");
            ImGui::Spacing();

            const bool untitled = m_Ctx.SceneVfsPath.empty();
            ImGui::BeginDisabled(untitled);
            const bool save = ImGui::Button("Save", ImVec2(110, 0));
            ImGui::EndDisabled();
            ItemCentre(m_UnsavedProbe.SaveX, m_UnsavedProbe.SaveY);
            if (untitled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("This scene has no file yet: use File > Save As... first.");
            ImGui::SameLine();
            const bool discard = ImGui::Button("Discard", ImVec2(110, 0));
            ItemCentre(m_UnsavedProbe.DiscardX, m_UnsavedProbe.DiscardY);
            ImGui::SameLine();
            const bool cancel = ImGui::Button("Cancel", ImVec2(110, 0));
            ItemCentre(m_UnsavedProbe.CancelX, m_UnsavedProbe.CancelY);
            m_UnsavedProbe.Drawn = true;

            bool close = false;
            if (save)
            {
                if (SaveScene() && !m_Ctx.Dirty) { run = std::move(m_PendingGuarded); close = true; }
                else m_Ctx.Log("[Scene] Save failed — the command was not run.", LogSeverity::Error);
            }
            else if (discard)
            {
                m_Ctx.ClearDirty();   // the edits are dropped on purpose
                run = std::move(m_PendingGuarded);
                close = true;
            }
            else if (cancel)
            {
                m_Ctx.Log("[Scene] Cancelled: " + m_PendingGuardedWhat + " (unsaved changes kept).");
                close = true;
            }
            if (close)
            {
                m_PendingGuarded = nullptr;
                m_PendingGuardedWhat.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        // Run the command after the popup is closed: it may replace the scene or the project.
        if (run) run();
    }

    // =========================================================================
    // Edit ▸ Preferences…
    // =========================================================================
    void StarforgeApp::DrawPreferencesPopup()
    {
        static const char* kPopup = "Preferences##ux02";
        if (m_OpenPreferences)
        {
            ImGui::OpenPopup(kPopup);
            m_OpenPreferences = false;
        }
        m_PrefsProbe.Drawn = false;
        if (!ImGui::BeginPopupModal(kPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        bool changed = false;
        ImGui::SeparatorText("Autosave");
        changed |= ImGui::Checkbox("Autosave the open scene", &m_Settings.AutosaveEnabled);
        ItemCentre(m_PrefsProbe.AutosaveX, m_PrefsProbe.AutosaveY);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("While the scene has unsaved changes, a copy is written to the autosave folder\n"
                              "every interval. Entering Play ALWAYS writes a safety copy of the edit scene\n"
                              "there too, whatever this is set to.");
        ImGui::BeginDisabled(!m_Settings.AutosaveEnabled);
        int minutes = Prefs::ClampAutosaveMinutes(m_Settings.AutosaveMinutes);
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::SliderInt("Interval", &minutes, 1, 60, minutes == 1 ? "%d minute" : "%d minutes",
                             ImGuiSliderFlags_AlwaysClamp))
        {
            m_Settings.AutosaveMinutes = minutes;
            changed = true;
        }
        ImGui::EndDisabled();
        {
            std::error_code ec;
            const std::string dir = Cosmic::FileSystem::Resolve("user://starforge/autosave/");
            const std::string abs = fs::absolute(dir, ec).lexically_normal().generic_string();
            ImGui::TextDisabled("Folder: %s", abs.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_LC_FOLDER_OPEN " Reveal"))
            {
                fs::create_directories(dir, ec);
                SourceHit h; h.Path = abs; h.Reason = "autosave folder";
                SourceLocator::Reveal(h);
            }
        }

        ImGui::SeparatorText("Unsaved changes");
        changed |= ImGui::Checkbox("Ask to save before New / Open / Close / Exit", &m_Settings.PromptUnsaved);
        ItemCentre(m_PrefsProbe.PromptX, m_PrefsProbe.PromptY);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Save / Discard / Cancel before New Scene, Open Scene, Open Project, Close Project\n"
                              "and Exit to Launcher when the scene has unsaved changes.\n"
                              "Closing the window itself cannot ask (the app ends first): the editor writes an\n"
                              "autosave copy of the scene then.");

        if (changed)
            Prefs::SaveSettings(m_Settings);

        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ItemCentre(m_PrefsProbe.CloseX, m_PrefsProbe.CloseY);
        m_PrefsProbe.Drawn = true;
        ImGui::EndPopup();
    }

    // =========================================================================
    // Autosave copy + status chip
    // =========================================================================
    bool StarforgeApp::WriteAutosaveCopy(const char* why)
    {
        if (!m_Ctx.Scene || IsPlaying()) return false;   // never the throwaway runtime scene
        std::error_code ec;
        const std::string dir = Cosmic::FileSystem::Resolve("user://starforge/autosave/" + m_Ctx.ProjectName);
        fs::create_directories(dir, ec);
        const std::string scene = m_Ctx.SceneName.empty() ? std::string("Untitled") : m_Ctx.SceneName;
        const std::string path = dir + "/" + scene + ".cscene";
        if (!Cosmic::SceneSerializer::Save(*m_Ctx.Scene, path))
        {
            m_Ctx.Log("[Autosave] FAILED: " + path, LogSeverity::Error);
            return false;
        }
        m_Ctx.Log(std::string(why ? why : "[Autosave] ") + path);
        m_LastAutosaveHHMM    = NowHHMM();
        m_LastAutosavePath    = path;
        m_LastAutosaveProject = m_Ctx.ProjectName;
        return true;
    }

    void StarforgeApp::DrawAutosaveChip()
    {
        m_AutosaveChipShown.clear();
        if (m_LastAutosaveHHMM.empty() || m_LastAutosaveProject != m_Ctx.ProjectName) return;
        m_AutosaveChipShown = "autosaved " + m_LastAutosaveHHMM;
        ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
        ImGui::TextDisabled(ICON_LC_SAVE " %s", m_AutosaveChipShown.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\n(Edit > Preferences... sets the interval or turns autosave off)", m_LastAutosavePath.c_str());
    }

    // =========================================================================
    // File ▸ Open Scene — the Screens ▸ Scenes lister
    // =========================================================================
    void StarforgeApp::DrawOpenSceneMenu()
    {
        const bool open = ImGui::BeginMenu("Open Scene", m_Ctx.ProjectOpen);
        ItemCentre(m_SceneMenuProbe.OpenSceneX, m_SceneMenuProbe.OpenSceneY);   // self-test probe
        if (!open) return;
        const std::vector<std::string> scenes = SceneMenuEntries();
        m_SceneMenuProbe.Drawn.clear();
        m_SceneMenuProbe.ItemX.clear();
        m_SceneMenuProbe.ItemY.clear();
        m_SceneMenuProbe.Frame = ImGui::GetFrameCount();
        if (scenes.empty()) ImGui::TextDisabled("(no scenes)");
        for (const std::string& vfs : scenes)
        {
            static const std::string kPrefix = "project://scenes/";
            const std::string rel = vfs.rfind(kPrefix, 0) == 0 ? vfs.substr(kPrefix.size()) : vfs;
            const bool clicked = ImGui::MenuItem(rel.c_str(), nullptr, vfs == m_Ctx.SceneVfsPath);
            float x = 0.0f, y = 0.0f; ItemCentre(x, y);
            m_SceneMenuProbe.Drawn.push_back(vfs);
            m_SceneMenuProbe.ItemX.push_back(x);
            m_SceneMenuProbe.ItemY.push_back(y);
            if (clicked) RequestOpenScene(vfs);
        }
        ImGui::EndMenu();
    }

    // =========================================================================
    // "Open in flow editor" (a Flow hit from SourceLocator::ForSignal)
    // =========================================================================
    void StarforgeApp::OpenFlowHit(const SourceHit& h)
    {
        if (!h.IsFlow() || h.FlowVfs.empty()) return;
        const std::string vfs = h.FlowVfs;
        // The same document path as Screens ▸ Flow graph (ScreensHost().OpenFlowDocument).
        IAssetEditor* doc = m_Editors.Open(vfs, [vfs]() { return std::make_unique<FlowEditor>(vfs); }, &m_ShowEditors);
        auto* fe = dynamic_cast<FlowEditor*>(doc);
        if (fe)
            fe->HarnessSelect(h.StateIndex, h.TransitionIndex);
        m_LastFlowOpen = { doc ? doc->Path() : std::string(), h.StateIndex, h.TransitionIndex, fe != nullptr };
        m_Ctx.Log("[Flow] " + h.FlowLine() + "  ->  " + vfs);
    }
}
