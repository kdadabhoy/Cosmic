// StarforgeAppPlatform.cpp — AP-03 (App Platform): the editor-UX half of the
// authoring workflow, kept OUT of StarforgeApp.cpp (that TU sits at the /bigobj
// limit; it only carries hook lines). Everything here is a StarforgeApp member:
//
//   * project open: the startup scene follows the manifest (flow start state ->
//     startup_scene -> scenes/Main.cscene), the project kind seeds AutoBuild;
//   * the Screens + DataBus panels (hosts/callbacks), the preview bus;
//   * hosted panels drawn in editor Play (contract §4) with the WO-07 stack oracle
//     bookkeeping (V05 editor half) and the DrawnThisFrame write;
//   * the Entity > UI widget entries (AP-02's seven + Hosted Panel);
//   * the viewport right-click "Open logic source" (§7 resolution order);
//   * the template picker + samples-on-disk buttons + the projects.toml cache (E06);
//   * the live loop (§6): 500 ms debounce, stop-build-resume, status chip (E07).

#include "StarforgeApp.h"

#include "commands/EditorCommands.h"
#include "editors/FlowEditor.h"
#include "layers/WorkspaceLayer.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/ui/UiComponents.h"
#include "scene/ui/UiSystem.h"
#include "utils/FileSystem.h"
#include "ui/IconsLucide.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace Starforge
{
    namespace
    {
        std::string FirstLine(const fs::path& readme)
        {
            std::ifstream in(readme);
            std::string line;
            while (std::getline(in, line))
            {
                // skip markdown heading markers / blank lines
                size_t i = 0;
                while (i < line.size() && (line[i] == '#' || line[i] == ' ')) ++i;
                if (i < line.size()) return line.substr(i);
            }
            return {};
        }

        std::string DefaultProjectsDirP()
        {
#pragma warning(push)
#pragma warning(disable: 4996)
            // Test seam (AP-03 self-test): redirect the samples' scaffold folder.
            if (const char* over = std::getenv("COSMIC_STARFORGE_PROJECTS_DIR"); over && *over)
                return over;
            const char* home = std::getenv("USERPROFILE");
#pragma warning(pop)
            fs::path base = home ? fs::path(home) / "Documents" : fs::path(".");
            return (base / "Starforge Projects").generic_string();
        }

        template<typename T>
        T* TryGet(Cosmic::Entity e) { return e.HasComponent<T>() ? &e.GetComponent<T>() : nullptr; }

        struct Depths { int color = 0, styleVar = 0, font = 0, popup = 0, window = 0; };
        Depths CaptureDepths()
        {
            Depths d;
            ImGuiContext* g = ImGui::GetCurrentContext();
            d.color = g->ColorStack.Size; d.styleVar = g->StyleVarStack.Size; d.font = g->FontStack.Size;
            d.popup = g->BeginPopupStack.Size; d.window = g->CurrentWindowStack.Size;
            return d;
        }
        bool SameDepths(const Depths& a, const Depths& b)
        {
            return a.color == b.color && a.styleVar == b.styleVar && a.font == b.font && a.popup == b.popup && a.window == b.window;
        }
    }

    // =========================================================================
    // Project open: the manifest-driven startup scene (AP-04 caveat fix)
    // =========================================================================
    void StarforgeApp::OpenStartupScene()
    {
        const ProjectManifest pm = ProjectManifest::Load("project://project.cproj");
        std::error_code ec;
        auto tryOpen = [&](const std::string& vfs) -> bool
        {
            if (vfs.empty()) return false;
            if (!fs::exists(Cosmic::FileSystem::Resolve(vfs), ec)) return false;
            OpenScene(vfs);
            return true;
        };
        // 1) the startup flow's start state (an app-kind project's first screen)
        if (!pm.StartupFlow.empty())
        {
            Cosmic::FlowAsset asset;
            if (Cosmic::FlowAsset::Load(asset, "project://" + pm.StartupFlow))
            {
                const Cosmic::FlowState* start = asset.Find(asset.Start);
                if (!start && !asset.States.empty()) start = &asset.States.front();
                if (start && tryOpen(start->Scene)) return;
            }
        }
        // 2) startup_scene, 3) scenes/Main.cscene, else a fresh scene
        if (tryOpen(pm.StartupScene.empty() ? std::string() : "project://" + pm.StartupScene)) return;
        if (tryOpen("project://scenes/Main.cscene")) return;
        NewScene();
    }

    bool StarforgeApp::NewProjectAt(const std::string& name, const std::string& location, const std::string& kind)
    {
        const std::string prev = m_NewProjectKind;
        m_NewProjectKind = kind.empty() ? "game" : kind;
        const bool ok = NewProjectAt(name, location);
        m_NewProjectKind = prev;
        return ok;
    }

    // =========================================================================
    // Screens + DataBus panels
    // =========================================================================
    ScreensPanel::Host StarforgeApp::ScreensHost()
    {
        ScreensPanel::Host h;
        h.ProjectRoot  = ProjectDir();
        h.ProjectName  = m_Ctx.ProjectName;
        h.ManifestFlow = m_ManifestFlow;
        h.OpenScene    = [this](const std::string& vfs) { if (!IsPlaying()) OpenScene(vfs); };
        h.SetManifestFlow = [this](const std::string& rel) { SetManifestFlow(rel); };
        h.OpenFlowDocument = [this](const std::string& vfs)
        {
            m_Editors.Open(vfs, [vfs]() { return std::make_unique<FlowEditor>(vfs); }, &m_ShowEditors);
        };
        return h;
    }

    void StarforgeApp::SetManifestFlow(const std::string& rel)
    {
        const std::string mpath = (fs::path(ProjectDir()) / "project.cproj").generic_string();
        ProjectManifest pm = ProjectManifest::Load(mpath);
        pm.StartupFlow = rel;
        if (pm.Name.empty()) pm.Name = m_Ctx.ProjectName;
        pm.Save(mpath);
        m_ManifestFlow = rel;
        m_Screens.Invalidate();
    }

    void StarforgeApp::DrawAppPlatformPanels()
    {
        if (m_ShowScreens) m_Screens.OnImGuiRender(m_Ctx, &m_ShowScreens, ScreensHost());
        if (m_ShowDataBus) m_DataBusPanel.OnImGuiRender(m_Ctx, &m_ShowDataBus, ProjectDir(), m_PlayBus, m_PreviewBus, IsPlaying());
        UpdateInspectorLinks();
    }

    void StarforgeApp::UpdateInspectorLinks()
    {
        InspectorPanel::SourceLinks l;
        l.ProjectRoot = ProjectDir();
        l.Bus         = &m_PlayBus;
        l.Panels      = IsPlaying() ? &m_PlayPanels : &m_LastPanels;
        l.Playing     = IsPlaying();
        m_Inspector.SetSourceLinks(l);
    }

    void StarforgeApp::CapturePanelSources()
    {
        // Before the services are destroyed (Panels.Clear()), copy each CS_PANEL's
        // recorded source into the shadow registry the edit-mode links read.
        m_LastPanels.Clear();
        for (const std::string& n : m_PlayPanels.Names())
        {
            const Cosmic::PanelRegistry::Source s = m_PlayPanels.SourceOf(n);
            m_LastPanels.Register(n, [](const Cosmic::UiRect&) {}, s.File.empty() ? nullptr : s.File.c_str(), s.Line);
        }
    }

    // =========================================================================
    // Hosted panels in editor Play (contract §4, V05 editor half)
    // =========================================================================
    void StarforgeApp::DrawHostedPanels()
    {
        m_HostedDraws.clear();
        if (!IsPlaying() || !m_Ctx.Scene || !m_Ctx.ProjectOpen) return;   // never in edit mode
        auto& app = Cosmic::Application::Get();
        const glm::vec2 vpPos  = app.GetViewportPos();
        const glm::vec2 vpSize = app.GetViewportSize();
        if (vpSize.x < 1.0f || vpSize.y < 1.0f) return;

        // The canvases lay out in the letterbox band (m_GameBandUv), so a panel's
        // canvas rect is already band-relative in viewport pixels: screen = vpPos + rect.
        const Cosmic::UiRect band{
            { m_GameBandUv.x * vpSize.x,                      m_GameBandUv.y * vpSize.y },
            { (m_GameBandUv.x + m_GameBandUv.z) * vpSize.x,   (m_GameBandUv.y + m_GameBandUv.w) * vpSize.y } };
        std::vector<Cosmic::UiHostedPanelDraw> panels;
        Cosmic::UiSystem::CollectHostedPanels(*m_Ctx.Scene, band, panels, &m_LastCamVP);
        if (panels.empty()) return;

        const Depths before = CaptureDepths();
        const ImGuiViewport* mv = ImGui::GetMainViewport();
        auto& reg = m_Ctx.Scene->GetRegistry();
        for (const auto& p : panels)   // back to front
        {
            const ImVec2 mn(vpPos.x + p.Rect.Min.x, vpPos.y + p.Rect.Min.y);
            const ImVec2 sz(std::max(1.0f, p.Rect.Width()), std::max(1.0f, p.Rect.Height()));
            ImGui::SetNextWindowPos(mn);
            ImGui::SetNextWindowSize(sz);
            ImGui::SetNextWindowViewport(mv->ID);
            const std::string id = "##hosted_" + p.Name + "_" + std::to_string(p.Handle);
            bool drawn = false;
            ImGui::Begin(id.c_str(), nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground);
            drawn = m_PlayPanels.Draw(p.Name, p.Rect);   // false => the canvas placeholder stays visible
            ImGui::End();

            const entt::entity e = static_cast<entt::entity>(p.Handle);
            if (auto* hp = reg.try_get<Cosmic::UiHostedPanelComponent>(e))
                hp->DrawnThisFrame = drawn;   // the §4 host write (AP-02 left it to the hosts)
            m_HostedDraws.push_back({ p.Name, p.Rect, { mn.x, mn.y }, drawn });
        }
        if (!SameDepths(before, CaptureDepths()))
            ++m_HostedImbalance;
    }

    // =========================================================================
    // Entity > UI: the seven AP-02 widgets + Hosted Panel
    // =========================================================================
    void StarforgeApp::DrawUiWidgetMenu()
    {
        auto uiChild = [&](const char* label, std::function<void(Cosmic::Entity)> build)
        {
            if (ImGui::MenuItem(label))
                Commands::Create(m_Ctx, label, m_Ctx.PrimaryEntity(), [build](Cosmic::Entity e)
                {
                    e.AddComponent<Cosmic::RectTransformComponent>();
                    build(e);
                });
        };
        ImGui::Separator();
        ImGui::TextDisabled("Bound widgets (DataBus)");
        uiChild("Value Text", [](Cosmic::Entity e)
        {
            e.AddComponent<Cosmic::UiTextComponent>();            // the sibling that supplies font/size/colour
            e.AddComponent<Cosmic::UiValueTextComponent>();
        });
        uiChild("Gauge",     [](Cosmic::Entity e) { e.AddComponent<Cosmic::UiGaugeComponent>(); });
        uiChild("Indicator", [](Cosmic::Entity e) { e.AddComponent<Cosmic::UiIndicatorComponent>(); });
        uiChild("Plot",      [](Cosmic::Entity e) { e.AddComponent<Cosmic::UiPlotComponent>(); });
        uiChild("Slider",    [](Cosmic::Entity e) { e.AddComponent<Cosmic::UiSliderComponent>(); });
        uiChild("Toggle",    [](Cosmic::Entity e)
        {
            e.AddComponent<Cosmic::UiImageComponent>().Tint = { 0.5f, 0.5f, 0.5f, 1.0f };   // the tinted sibling
            e.AddComponent<Cosmic::UiToggleComponent>();
        });
        uiChild("Hosted Panel", [](Cosmic::Entity e) { e.AddComponent<Cosmic::UiHostedPanelComponent>(); });
    }

    // =========================================================================
    // Viewport right-click: Open logic source (§7 resolution order)
    // =========================================================================
    SourceHit StarforgeApp::ResolveLogicSource(Cosmic::Entity e, std::string* what) const
    {
        SourceHit hit;
        if (!e) { hit.Reason = "no selection"; return hit; }
        const SourceLocator loc(ProjectDir());
        auto done = [&](const SourceHit& h, const char* kind) { if (what) *what = kind; return h; };

        // 1) hosted panel
        if (auto* hp = TryGet<Cosmic::UiHostedPanelComponent>(e))
        {
            SourceHit h = loc.ForPanel(hp->PanelName, IsPlaying() ? m_PlayPanels : m_LastPanels);
            if (h.Resolved()) return done(h, "panel");
            hit = h;
        }
        // 2) bound-widget channel producer
        std::string channel;
        if (auto* c = TryGet<Cosmic::UiValueTextComponent>(e)) channel = c->Channel;
        else if (auto* g = TryGet<Cosmic::UiGaugeComponent>(e)) channel = g->Channel;
        else if (auto* p = TryGet<Cosmic::UiPlotComponent>(e)) channel = p->Channel;
        else if (auto* i = TryGet<Cosmic::UiIndicatorComponent>(e)) channel = i->Channel;
        if (!channel.empty())
        {
            SourceHit h = loc.ForChannel(channel, m_PlayBus);
            if (h.Resolved()) return done(h, "channel");
            if (!hit.Resolved()) hit = h;
        }
        // 3) button signal (first handler file)
        if (auto* b = TryGet<Cosmic::UiButtonComponent>(e))
        {
            const auto hits = loc.ForSignal(b->Signal);
            if (!hits.empty()) return done(hits.front(), "signal");
            if (hit.Reason.empty()) hit.Reason = "no src/ file contains \"" + b->Signal + "\"";
        }
        // 4) the entity's script
        if (auto* ns = TryGet<Cosmic::NativeScriptComponent>(e))
        {
            SourceHit h = loc.ForScriptClass(ns->ClassName);
            if (h.Resolved()) return done(h, "script");
            if (hit.Reason.empty()) hit.Reason = h.Reason;
        }
        if (hit.Reason.empty()) hit.Reason = "the selected element has no logic (no hosted panel, channel, signal or script)";
        return hit;
    }

    void StarforgeApp::DrawViewportContextMenu()
    {
        auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
        if (!m_Ctx.ProjectOpen || !m_Ctx.Scene || !ws) return;
        Cosmic::Entity prim = m_Ctx.PrimaryEntity();
        if (ws->IsViewportHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && prim &&
            prim.HasComponent<Cosmic::RectTransformComponent>())
        {
            // Only when the right-click lands on the selected element.
            auto& app = Cosmic::Application::Get();
            const glm::vec2 vpPos = app.GetViewportPos(), vpSize = app.GetViewportSize();
            const Cosmic::UiRect band{
                { m_GameBandUv.x * vpSize.x,                      m_GameBandUv.y * vpSize.y },
                { (m_GameBandUv.x + m_GameBandUv.z) * vpSize.x,   (m_GameBandUv.y + m_GameBandUv.w) * vpSize.y } };
            const ImVec2 m = ImGui::GetIO().MousePos;
            uint32_t hit = 0;
            if (Cosmic::UiSystem::HitTest(*m_Ctx.Scene, band, { m.x - vpPos.x, m.y - vpPos.y }, hit) &&
                hit == static_cast<uint32_t>(static_cast<entt::entity>(prim)))
                ImGui::OpenPopup("##ap03_vpctx");
        }
        if (ImGui::BeginPopup("##ap03_vpctx"))
        {
            std::string what;
            const SourceHit h = ResolveLogicSource(m_Ctx.PrimaryEntity(), &what);
            ImGui::BeginDisabled(!h.Resolved());
            if (ImGui::MenuItem(ICON_LC_CODE " Open logic source")) SourceLocator::Open(h);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", h.Resolved() ? (what + ": " + h.Path + ":" + std::to_string(h.Line)).c_str() : h.Reason.c_str());
            ImGui::BeginDisabled(!h.Resolved());
            if (ImGui::MenuItem(ICON_LC_FOLDER_OPEN " Reveal in Explorer")) SourceLocator::Reveal(h);
            ImGui::EndDisabled();
            ImGui::EndPopup();
        }
    }

    // =========================================================================
    // Template picker + samples on disk + projects.toml cache (E06)
    // =========================================================================
    std::vector<StarforgeApp::TemplateInfo> StarforgeApp::ListTemplates() const
    {
        std::vector<TemplateInfo> out;
        const fs::path root = fs::path("assets") / "projects" / "Starforge" / "templates";
        std::error_code ec;
        struct Known { const char* Kind; const char* Display; const char* Desc; };
        static const Known kKnown[] = {
            { "app",   "App",   "A data-driven app: screens (Home / Dashboard / Settings), a service on the DataBus, bound widgets, live rebuild." },
            { "game",  "Game",  "A 2D game scaffold: sprite + ortho camera scene, example scripts, a game module to build." },
            { "blank", "Blank", "A canvas + camera and an empty module — start from nothing." },
        };
        for (const Known& k : kKnown)
        {
            if (!fs::exists(root / k.Kind / "project.cproj", ec)) continue;
            TemplateInfo t; t.Kind = k.Kind; t.Display = k.Display; t.Description = k.Desc;
            const std::string readme = FirstLine(root / k.Kind / "README.md");
            if (!readme.empty() && readme.size() < 140) t.Description = readme;
            out.push_back(t);
        }
        return out;
    }

    std::vector<std::string> StarforgeApp::ListSamples() const
    {
        std::vector<std::string> out;
        const fs::path root = fs::path("assets") / "projects" / "Starforge" / "templates" / "samples";
        std::error_code ec;
        if (!fs::exists(root, ec)) return out;
        for (const auto& e : fs::directory_iterator(root, ec))
            if (e.is_directory(ec) && fs::exists(e.path() / "project.cproj", ec))
                out.push_back(e.path().filename().string());
        std::sort(out.begin(), out.end());
        return out;
    }

    std::string StarforgeApp::SamplePath(const std::string& name) const
    {
        return (fs::path(DefaultProjectsDirP()) / name).generic_string();
    }

    bool StarforgeApp::SampleExists(const std::string& name) const
    {
        std::error_code ec;
        return fs::exists(fs::path(SamplePath(name)) / "project.cproj", ec);
    }

    bool StarforgeApp::OpenSample(const std::string& name)
    {
        const std::string dest = SamplePath(name);
        if (!SampleExists(name))
        {
            std::error_code ec;
            fs::create_directories(fs::path(dest).parent_path(), ec);
            if (!ScaffoldProjectTo(name, dest, "samples/" + name))
            {
                m_Ctx.Log("[Samples] Could not scaffold sample '" + name + "' — templates/samples/" + name + " missing.",
                          LogSeverity::Error);
                return false;
            }
            m_Ctx.Log("[Samples] Created '" + name + "' at " + dest + " from templates/samples/" + name + ".");
        }
        return OpenProjectPath(dest);
    }

    void StarforgeApp::DrawSampleButtons()
    {
        for (const std::string& s : ListSamples())
        {
            ImGui::SameLine();
            if (ImGui::Button((s + " Sample").c_str(), ImVec2(0, 34)))
                OpenSample(s);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("templates/samples/%s -> %s\n(scaffolded on first use, then reopened)", s.c_str(), SamplePath(s).c_str());
        }
    }

    void StarforgeApp::DrawTemplatePicker()
    {
        const auto templates = ListTemplates();
        ImGui::TextUnformatted("Template");
        if (templates.empty())
        {
            ImGui::TextDisabled("(no templates found under assets/projects/Starforge/templates)");
            return;
        }
        bool have = false;
        for (const auto& t : templates) if (t.Kind == m_NewProjectKind) have = true;
        if (!have) m_NewProjectKind = templates.front().Kind;
        for (const auto& t : templates)
        {
            const bool sel = (t.Kind == m_NewProjectKind);
            if (ImGui::RadioButton((t.Display + "##tpl_" + t.Kind).c_str(), sel)) m_NewProjectKind = t.Kind;
            ImGui::SameLine();
            ImGui::TextDisabled("- %s", t.Description.c_str());
        }
        ImGui::Checkbox("Pixel art (point-filtered textures)", &m_NewProjectPixelArt);
    }

    const std::vector<Prefs::ProjectEntry>& StarforgeApp::CachedProjects()
    {
        std::error_code ec;
        const std::string path = Prefs::RecentPath();
        fs::file_time_type now{};
        const bool exists = fs::exists(path, ec);
        if (exists) now = fs::last_write_time(path, ec);
        if (!m_ProjectsCacheValid || now != m_ProjectsCacheTime || exists != m_ProjectsCacheExists)
        {
            m_ProjectsCache = Prefs::LoadProjects();
            ++m_LoadProjectsCalls;
            m_ProjectsCacheValid  = true;
            m_ProjectsCacheTime   = now;
            m_ProjectsCacheExists = exists;
        }
        return m_ProjectsCache;
    }

    void StarforgeApp::InvalidateProjects() { m_ProjectsCacheValid = false; }

    // =========================================================================
    // Live loop (contract §6, E07)
    // =========================================================================
    void StarforgeApp::LiveLoopTick(float ts)
    {
        if (m_SrcWatchOn)
        {
            const auto changes = m_SrcWatcher.Poll();   // always drained
            if (!changes.empty()) m_Live.Debounce = 0.5f;   // coalesce: every event restarts the 500 ms window
        }
        if (m_Live.Debounce >= 0.0f)
        {
            m_Live.Debounce -= ts;
            if (m_Live.Debounce < 0.0f)
            {
                m_Live.Debounce = -1.0f;
                if (m_AutoBuild && !m_Builder.IsBuilding())
                    BuildScripts();   // stops Play first when needed (LiveBeforeBuild)
                else if (m_AutoBuild)
                    m_Live.Debounce = 0.5f;   // a build is running: try again after it
            }
        }
        if (m_Live.Reloading && !m_Builder.IsBuilding()) m_Live.Reloading = false;
    }

    void StarforgeApp::LiveBeforeBuild()
    {
        // Called by BuildScripts when Play is active: remember where the flow was so
        // a successful rebuild lands back on the same screen.
        m_Live.WasPlaying = true;
        m_Live.WasPaused  = (m_Play == PlayMode::Paused);
        m_Live.FlowState  = m_PlayFlowActive ? m_PlayFlow.CurrentState() : std::string();
        ++m_Live.Builds;
        m_Ctx.Log("[Live] Module rebuild while playing — stopping, then resuming on '" +
                  (m_Live.FlowState.empty() ? std::string("<scene>") : m_Live.FlowState) + "' after the reload.");
    }

    void StarforgeApp::LiveAfterBuild(bool ok)
    {
        if (m_BuildPurpose != BuildPurpose::HotReload) return;
        if (!ok)
        {
            m_Live.Failed = true;
            ++m_Live.Failures;
            return;   // stay stopped; WasPlaying survives so the next success resumes
        }
        m_Live.Failed = false;
        m_Live.Reloading = true;
        if (m_Live.WasPlaying && m_Settings.AutoResumePlay && m_Ctx.Scene)
        {
            m_PlayStartAt = m_Live.FlowState;   // consumed by PlayScene -> StartAt
            m_PlayKeepBus = true;               // consumed by PlayServicesStart: values + history survive (§6)
            PlayScene();
            m_PlayKeepBus = false;
            if (IsPlaying())
            {
                if (m_Live.WasPaused) m_Play = PlayMode::Paused;
                ++m_Live.Resumes;
                m_Ctx.Log("[Live] Resumed Play on '" + (m_PlayFlowActive ? m_PlayFlow.CurrentState() : std::string("<scene>")) + "'.");
            }
            m_PlayStartAt.clear();
        }
        m_Live.WasPlaying = false;
        m_Live.WasPaused  = false;
    }

    const char* StarforgeApp::LiveChipText(ImVec4& color) const
    {
        if (m_Builder.IsBuilding() && m_BuildPurpose == BuildPurpose::HotReload) { color = ImVec4(1.0f, 0.85f, 0.30f, 1.0f); return "Building…"; }
        if (m_Live.Failed)    { color = ImVec4(1.0f, 0.42f, 0.42f, 1.0f); return "Build failed"; }
        if (m_Live.Reloading) { color = ImVec4(0.55f, 0.80f, 1.0f, 1.0f); return "Reloading"; }
        if (m_AutoBuild)      { color = ImVec4(0.40f, 1.0f, 0.50f, 1.0f); return "Live"; }
        color = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
        return nullptr;   // auto-build off: no chip
    }

    void StarforgeApp::DrawLiveChip()
    {
        ImVec4 col;
        const char* txt = LiveChipText(col);
        if (!txt) return;
        ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
        ImGui::TextColored(col, ICON_LC_ZAP " %s", txt);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Live logic loop: a change under src/ rebuilds the module (500 ms debounce)%s.",
                              m_Settings.AutoResumePlay ? " and resumes Play on the same screen" : "");
    }
}
