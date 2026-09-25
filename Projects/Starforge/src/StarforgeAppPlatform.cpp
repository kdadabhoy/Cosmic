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

        // ---- UX-03 samples (contract §3) ------------------------------------
        constexpr const char* kFeaturedSample = "PendulumLab";

        fs::path SampleTemplatesRoot()
        {
            return fs::path("assets") / "projects" / "Starforge" / "templates" / "samples";
        }

        bool IsTemplateSample(const std::string& sourcePath)
        {
            const std::string root = SampleTemplatesRoot().generic_string() + "/";
            return sourcePath.rfind(root, 0) == 0;
        }

        // The sample's homescreen thumbnail (Projects/Starforge/assets/editor/samples/<Name>.png,
        // synced next to the runtime); "" when the sample has none.
        std::string SampleThumbPath(const std::string& name)
        {
            std::error_code ec;
            const fs::path p = fs::path("assets") / "projects" / "Starforge" / "editor" / "samples" / (name + ".png");
            return fs::exists(p, ec) ? p.generic_string() : std::string();
        }

        // The manifest's RAW kind key: "" when absent (ProjectManifest::Load maps an
        // absent key to "game", which would file AnalysisSample under Game samples).
        std::string RawKind(const fs::path& cproj)
        {
            if (auto cfg = Cosmic::Config::Load(cproj.generic_string()))
                return cfg->GetString("kind", "");
            return {};
        }

        // 0 = App samples, 1 = Game samples, 2 = Other samples (no kind / any other kind).
        int SampleGroup(const std::string& kind)
        {
            if (kind == "app")  return 0;
            if (kind == "game") return 1;
            return 2;
        }

        // The first non-title README line (headings, blank lines and a templated
        // "@PROJECT_NAME@" line skipped); "" without a README.
        std::string FirstDescriptionLine(const fs::path& readme)
        {
            std::ifstream in(readme);
            std::string line;
            while (std::getline(in, line))
            {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                const size_t a = line.find_first_not_of(" \t");
                if (a == std::string::npos) continue;          // blank
                if (line[a] == '#') continue;                  // a title / heading
                if (line.find("@PROJECT_NAME@") != std::string::npos) continue;
                return line.substr(a);
            }
            return {};
        }

        // A plain recursive copy of an SDK sample into its user folder, skipping the
        // per-machine outputs (build/, dist/, logs/, .starforge/ at any depth). The
        // top-level project.cproj is written LAST, so a failed copy never leaves a
        // folder that SampleExists() would take for a finished one.
        bool CopySampleTree(const std::string& src, const std::string& dest, std::string& why)
        {
            std::error_code ec;
            const fs::path from = src, to = dest;
            fs::create_directories(to, ec);
            if (ec) { why = "cannot create " + dest + ": " + ec.message(); return false; }
            auto skipped = [](const fs::path& name)
            {
                return name == "build" || name == "dist" || name == "logs" || name == ".starforge";
            };
            bool manifest = false;
            for (auto it = fs::recursive_directory_iterator(from, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                const fs::path rel = fs::relative(it->path(), from, ec);
                if (ec) break;
                if (it->is_directory(ec))
                {
                    if (skipped(it->path().filename())) { it.disable_recursion_pending(); continue; }
                    fs::create_directories(to / rel, ec);
                    if (ec) break;
                    continue;
                }
                if (rel == "project.cproj") { manifest = true; continue; }
                fs::create_directories((to / rel).parent_path(), ec);
                fs::copy_file(it->path(), to / rel, fs::copy_options::overwrite_existing, ec);
                if (ec) { why = "copying " + rel.generic_string() + ": " + ec.message(); return false; }
            }
            if (ec) { why = ec.message(); return false; }
            if (!manifest) { why = "no project.cproj in " + src; return false; }
            fs::copy_file(from / "project.cproj", to / "project.cproj", fs::copy_options::overwrite_existing, ec);
            if (ec) { why = "copying project.cproj: " + ec.message(); return false; }
            return true;
        }

        // Seed <dest>/.starforge/thumb.png (what the project card shows, ThumbFor) from the
        // sample's thumbnail, when it has one.
        void SeedSampleThumb(const std::string& name, const std::string& dest)
        {
            const std::string thumb = SampleThumbPath(name);
            if (thumb.empty()) return;
            std::error_code ec;
            const fs::path dir = fs::path(dest) / ".starforge";
            fs::create_directories(dir, ec);
            fs::copy_file(thumb, dir / "thumb.png", fs::copy_options::overwrite_existing, ec);
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
            // The README's first line is the description unless it is the templated title
            // (the app template's README opens with "# @PROJECT_NAME@", which the picker
            // used to show verbatim).
            const std::string readme = FirstLine(root / k.Kind / "README.md");
            if (!readme.empty() && readme.size() < 140 && readme.find("@PROJECT_NAME@") == std::string::npos) t.Description = readme;
            out.push_back(t);
        }
        return out;
    }

    // UX-03 (contract §3): the samples come from two places — the editor's own
    // templates/samples/* (scaffolded with the @PROJECT_NAME@ token) and the SDK's
    // Projects/* that carry a project.cproj (copied verbatim; never Starforge).
    // Ordered by group — App samples, Game samples, Other samples (no/other kind) —
    // featured first inside a group, then by name.
    std::vector<StarforgeApp::SampleInfo> StarforgeApp::ListSamples() const
    {
        std::vector<SampleInfo> out;
        std::error_code ec;
        auto add = [&](const fs::path& dir)
        {
            const std::string name = dir.filename().string();
            for (const SampleInfo& s : out) if (s.Name == name) return;   // the first source wins a name clash
            SampleInfo s;
            s.Name        = name;
            s.Kind        = RawKind(dir / "project.cproj");
            s.Description = FirstDescriptionLine(dir / "README.md");
            s.SourcePath  = dir.generic_string();
            s.Featured    = (name == kFeaturedSample);
            out.push_back(std::move(s));
        };

        const fs::path templ = SampleTemplatesRoot();
        if (fs::exists(templ, ec))
            for (const auto& e : fs::directory_iterator(templ, ec))
                if (e.is_directory(ec) && fs::exists(e.path() / "project.cproj", ec))
                    add(e.path());

        const fs::path sdkProjects = fs::path(SdkDir()) / "Projects";
        if (fs::exists(sdkProjects, ec))
            for (const auto& e : fs::directory_iterator(sdkProjects, ec))
                if (e.is_directory(ec) && e.path().filename() != "Starforge" && fs::exists(e.path() / "project.cproj", ec))
                    add(e.path());

        std::sort(out.begin(), out.end(), [](const SampleInfo& a, const SampleInfo& b)
        {
            const int ga = SampleGroup(a.Kind), gb = SampleGroup(b.Kind);
            if (ga != gb) return ga < gb;
            if (a.Featured != b.Featured) return a.Featured;
            return a.Name < b.Name;
        });
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
        // First use copies the sample into SamplePath(name) (Documents/Starforge Projects,
        // or COSMIC_STARFORGE_PROJECTS_DIR); later uses reopen that copy. The source —
        // the editor's templates or the SDK's Projects/<name> — is never edited in place.
        const std::string dest = SamplePath(name);
        if (!SampleExists(name))
        {
            const std::vector<SampleInfo> samples = ListSamples();
            const auto it = std::find_if(samples.begin(), samples.end(),
                                         [&](const SampleInfo& s) { return s.Name == name; });
            if (it == samples.end())
            {
                m_Ctx.Log("[Samples] No sample named '" + name + "' (templates/samples/ or " + SdkDir() + "/Projects/).",
                          LogSeverity::Error);
                return false;
            }
            std::error_code ec;
            fs::create_directories(fs::path(dest).parent_path(), ec);
            const bool fromTemplate = IsTemplateSample(it->SourcePath);
            std::string why;
            const bool ok = fromTemplate ? ScaffoldProjectTo(name, dest, "samples/" + name)
                                         : CopySampleTree(it->SourcePath, dest, why);
            if (!ok)
            {
                m_Ctx.Log("[Samples] Could not copy sample '" + name + "' from " + it->SourcePath +
                          (why.empty() ? std::string() : " — " + why), LogSeverity::Error);
                return false;
            }
            SeedSampleThumb(name, dest);
            m_Ctx.Log("[Samples] Created '" + name + "' at " + dest + " from " + it->SourcePath + ".");
        }
        return OpenProjectPath(dest);
    }

    void StarforgeApp::DrawSampleButtons()
    {
        // The homescreen draws every frame: re-list (directory scans + manifest/README
        // reads) at most every 2 s, not per frame.
        const double now = ImGui::GetTime();
        if (m_SampleCacheTime < 0.0 || now - m_SampleCacheTime > 2.0 || now < m_SampleCacheTime)
        {
            m_SampleCache = ListSamples();
            m_SampleCacheTime = now;
        }
        if (m_SampleCache.empty())
            return;

        std::string open;   // deferred: OpenSample leaves the homescreen
        auto SampleThumbTexture = [&](const std::string& name) -> Cosmic::Ref<Cosmic::Texture2D>
        {
            const std::string path = SampleThumbPath(name);
            if (path.empty()) return nullptr;
            std::error_code ec;
            const std::string key = fs::absolute(path, ec).generic_string();
            auto it = m_ThumbCache.find(key);
            if (it != m_ThumbCache.end()) return it->second;
            Cosmic::Ref<Cosmic::Texture2D> tex = Cosmic::Texture2D::Create(key);
            m_ThumbCache[key] = tex;
            return tex;
        };
        auto tooltip = [&](const SampleInfo& s)
        {
            if (!ImGui::IsItemHovered()) return;
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
            if (!s.Description.empty()) ImGui::TextUnformatted(s.Description.c_str());
            ImGui::TextDisabled("%s -> %s", s.SourcePath.c_str(), SamplePath(s.Name).c_str());
            ImGui::TextDisabled(SampleExists(s.Name) ? "(reopens your copy)" : "(copied on first use, then reopened)");
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        };

        static const char* const kGroupTitles[] = { "App samples", "Game samples", "Other samples" };
        bool firstGroup = true;
        for (int g = 0; g < 3; ++g)
        {
            std::vector<const SampleInfo*> items;
            for (const SampleInfo& s : m_SampleCache)
                if (SampleGroup(s.Kind) == g) items.push_back(&s);
            if (items.empty()) continue;

            if (!firstGroup) ImGui::SameLine(0.0f, 36.0f);
            firstGroup = false;
            ImGui::BeginGroup();
            ImGui::AlignTextToFramePadding();   // same baseline in every group (SameLine after a group of buttons)
            ImGui::TextDisabled("%s", kGroupTitles[g]);
            for (const SampleInfo* s : items)
            {
                ImGui::PushID(s->Name.c_str());
                if (s->Featured)
                {
                    const float w = 240.0f;
                    if (Cosmic::Ref<Cosmic::Texture2D> tex = SampleThumbTexture(s->Name))
                    {
                        const float h = w * (float)tex->GetHeight() / (float)std::max(1u, tex->GetWidth());
                        // Engine textures load V-flipped for GL UVs: the flipped UV pair draws upright.
                        if (ImGui::ImageButton("##thumb", (ImTextureID)(intptr_t)tex->GetRendererID(),
                                               ImVec2(w, h), ImVec2(0, 1), ImVec2(1, 0)))
                            open = s->Name;
                        tooltip(*s);
                    }
                    const float cardW = w + 2.0f * ImGui::GetStyle().FramePadding.x;   // the image button's outer width
                    if (ImGui::Button((std::string(ICON_LC_STAR " ") + s->Name + "   (featured)").c_str(), ImVec2(cardW, 30.0f)))
                        open = s->Name;
                    tooltip(*s);
                }
                else
                {
                    if (ImGui::Button(s->Name.c_str(), ImVec2(180.0f, 30.0f)))
                        open = s->Name;
                    tooltip(*s);
                }
                ImGui::PopID();
            }
            ImGui::EndGroup();
        }
        if (!open.empty())
            OpenSample(open);
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
