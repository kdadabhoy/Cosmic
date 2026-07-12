#pragma once
// WorkspaceLayer.h
// Last Modified: 5/27/2026

/**
 * WorkspaceLayer — The editor shell.
 *
 * Hosts an ImGui dockspace with a Viewport panel and a default "Project Inspector"
 * slot on the left. Client layers now own their own ImGui windows completely —
 * WorkspaceLayer calls OnImGuiRender() at the top level so clients can dock
 * panels wherever they want, or create entirely new named windows.
 *
 * Key changes from the old version:
 *  - firstTime/dockspaceOpen are member variables (not statics) so re-creation works correctly.
 *  - Inspector is on the LEFT (22% width), Viewport takes the rest.
 *  - Client ImGui is rendered at top-level, not trapped inside a WorkspaceLayer window.
 *  - Header bar shows active project name + Exit button.
 *  - View menu has a "Reset Layout" item that re-runs DockBuilder next frame.
 *  - SetProjectName() lets Application pass a human-readable name for the header.
 *  - Teardown flags renamed to m_PendingTeardown / m_TeardownComplete for clarity.
 *
 * Client Usage
 * ------------
 * To have your panel appear in the default left Inspector slot, name your window
 * "Project Inspector":
 *
 *   ImGui::Begin("Project Inspector");
 *   // your controls
 *   ImGui::End();
 *
 * To appear in a second docked panel, just use any window name — the user can
 * drag and dock it wherever they want. To *programmatically* request a second
 * pre-docked slot, call WorkspaceLayer::RequestExtraDockedPanel() from OnAttach
 * (see below).
 *
 * Dock Ports (preferred)
 * ----------------------
 * The engine exposes a fixed set of 12 docking ports (4 edges x 3 sections) via
 * the DockPort enum. Bind your windows to ports in OnAttach:
 *
 *   auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
 *   ws->DockWindow("Serial Link",      Cosmic::DockPort::LeftTop);
 *   ws->DockWindow("Dashboard",        Cosmic::DockPort::RightTop);
 *   ws->DockWindow("Model",            Cosmic::DockPort::RightTop);   // same port -> tab
 *   ws->DockWindow("Telemetry",        Cosmic::DockPort::BottomCenter);
 *
 * Only ports that receive a window are carved out (unused ports take no space);
 * multiple windows on one port become tabs. See DockWindow() / DockPort below.
 */

#include "Cosmic.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Cosmic
{
    // ---------------------------------------------------------------------------
    // DockPort — the fixed set of pre-defined docking slots the engine offers.
    // Four optional edges, each divided into three sections, plus the central
    // Viewport. A client binds a window to a port with WorkspaceLayer::DockWindow().
    //
    //   * Only edges/sections that actually receive a window are carved out, so
    //     unused ports take ZERO space (no empty panels).
    //   * Binding multiple windows to the SAME port makes them tabs.
    //   * The set is fixed, so clients use these from the engine header without
    //     ever recompiling the engine. For an arbitrary position not covered
    //     here, fall back to RequestExtraDockedPanel().
    // ---------------------------------------------------------------------------
    enum class DockPort
    {
        LeftTop,    LeftMiddle,    LeftBottom,     // left edge,  stacked top -> bottom
        RightTop,   RightMiddle,   RightBottom,    // right edge, stacked top -> bottom
        TopLeft,    TopCenter,     TopRight,       // top edge,   left -> right
        BottomLeft, BottomCenter,  BottomRight,    // bottom edge, left -> right
        Center                                     // tabbed with the central Viewport
    };

    // Optional per-window dock-node behavior (H5). NoTabBar strips the "▼ Name"
    // tab header from the node a window docks into — for chrome-less docks (a top
    // toolbar, a full-bleed viewport) where the tab row is wasted vertical space.
    enum class DockFlags : uint32_t { None = 0, NoTabBar = 1u << 0 };
    inline DockFlags operator|(DockFlags a, DockFlags b)
    { return static_cast<DockFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b)); }
    inline bool HasFlag(DockFlags v, DockFlags f)
    { return (static_cast<uint32_t>(v) & static_cast<uint32_t>(f)) != 0; }

    // One window-to-port binding registered by a client layer.
    struct DockBinding
    {
        std::string WindowName; // must match the client's ImGui::Begin("...")
        DockPort    Port  = DockPort::LeftTop;
        DockFlags   Flags = DockFlags::None;
    };

    // ---------------------------------------------------------------------------
    // Describes one extra panel slot to be pre-docked in the DockBuilder pass.
    // Client layers fill this in OnAttach and push to WorkspaceLayer via
    // RequestExtraDockedPanel() before the first ImGui frame. (Escape hatch for
    // arbitrary split positions not covered by the fixed DockPort set.)
    // ---------------------------------------------------------------------------
    struct DockedPanelRequest
    {
        std::string WindowName;     // Must match the ImGui::Begin("...") call in the client
        ImGuiDir    SplitDir;       // Direction to split from the main viewport area
        float       SplitRatio;     // Fraction of the viewport to give to this panel
    };

    class WorkspaceLayer : public Cosmic::Layer
    {
    public:
        WorkspaceLayer();
        virtual ~WorkspaceLayer() override = default;

        virtual void OnAttach() override;
        virtual void OnDetach() override;
        virtual void OnUpdate(float ts) override;
        virtual void OnFixedUpdate(float deltaFixedTime) override;
        virtual void OnImGuiRender() override;
        virtual void OnEvent(Cosmic::Event& e) override;

        // -----------------------------------------------------------------------
        // Viewport layer management
        // -----------------------------------------------------------------------
        void SetViewportLayer(Cosmic::Layer* layer);
        void ClearViewportLayer();
        inline bool HasViewportLayer() const { return m_ClientViewportLayer != nullptr; }

        // Show/hide the central Viewport panel. When hidden, the (often empty)
        // Viewport tab is not drawn and is not docked, leaving the central node
        // for whatever the client docks to DockPort::Center. Re-runs the builder.
        void SetViewportVisible(bool visible)
        {
            if (m_ShowViewport == visible) return;
            m_ShowViewport = visible;
            m_DockspaceInitialized = false;
        }
        bool IsViewportVisible() const { return m_ShowViewport; }

        // -----------------------------------------------------------------------
        // Project identification (called by Application after DLL load)
        // -----------------------------------------------------------------------
        void SetProjectName(const std::string& name) { m_ProjectName = name; }
        const std::string& GetProjectName() const { return m_ProjectName; }

        // -----------------------------------------------------------------------
        // Extra pre-docked panels
        //
        // Call from your root layer's OnAttach() BEFORE the first ImGui frame.
        // WorkspaceLayer picks these up during the DockBuilder initialisation pass.
        //
        // Example (in ShowcaseProject::OnAttach):
        //   if (auto* ws = dynamic_cast<WorkspaceLayer*>(
        //           Application::Get().GetWorkspaceLayer()))
        //   {
        //       ws->RequestExtraDockedPanel({"Timeline", ImGuiDir_Down, 0.25f});
        //   }
        // -----------------------------------------------------------------------
        void RequestExtraDockedPanel(const DockedPanelRequest& request)
        {
            m_PendingPanelRequests.push_back(request);
            m_DockspaceInitialized = false; // trigger a DockBuilder rebuild
        }

        // -----------------------------------------------------------------------
        // Dock a client window into one of the engine's pre-defined ports.
        //
        // Call from your root layer's OnAttach(), before the first ImGui frame:
        //
        //   auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
        //   ws->DockWindow("Serial Link",      Cosmic::DockPort::LeftTop);
        //   ws->DockWindow("Weapon Dashboard", Cosmic::DockPort::RightTop);
        //   ws->DockWindow("Weapon Model",     Cosmic::DockPort::RightTop); // -> tab
        //
        // The WindowName must match your ImGui::Begin("..."). Multiple windows on
        // the same port become tabs. Ports with no windows are never created.
        //
        // Inline on purpose: WorkspaceLayer is not COSMIC_API-exported, so this
        // compiles into the client DLL (same pattern as RequestExtraDockedPanel).
        // -----------------------------------------------------------------------
        void DockWindow(const std::string& windowName, DockPort port, DockFlags flags = DockFlags::None)
        {
            for (auto& b : m_DockBindings)
                if (b.WindowName == windowName) { b.Port = port; b.Flags = flags; m_DockspaceInitialized = false; return; }
            m_DockBindings.push_back({ windowName, port, flags });
            m_DockspaceInitialized = false; // trigger a DockBuilder rebuild
        }

        // Drop all port bindings (e.g. before re-registering for a new project).
        void ClearDockWindows()
        {
            m_DockBindings.clear();
            m_DockspaceInitialized = false;
        }

        // -----------------------------------------------------------------------
        // Engine-hosted theme selector.
        //
        // One call gives the client a ready-made, dockable theme picker — the
        // engine owns the window and renders Cosmic::UI::ThemeSelector() into it;
        // the client just chooses where it docks. Call from OnAttach:
        //
        //   auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
        //   ws->ShowThemeSelector(true, Cosmic::DockPort::RightTop);   // or any port
        //
        // Pass a custom windowName to control its tab label (and dock identity).
        // -----------------------------------------------------------------------
        // The theme selector is a FLOATING popout (not docked), off by default,
        // toggled from the View menu. (port is accepted for source compatibility
        // but no longer used.)
        void ShowThemeSelector(bool show, DockPort port = DockPort::RightTop,
                               const char* windowName = "Themes")
        {
            m_ThemeSelectorWindow = (windowName && *windowName) ? windowName : "Themes";
            m_ThemeSelectorPort   = port;
            m_ShowThemeSelector   = show;
        }
        bool IsThemeSelectorVisible() const { return m_ShowThemeSelector; }

        // Per-edge size as a fraction of the dockspace. Optional; sane defaults
        // are used otherwise. Re-runs the builder next frame.
        void SetEdgeRatios(float left, float right, float top, float bottom)
        {
            m_LeftRatio = left; m_RightRatio = right; m_TopRatio = top; m_BottomRatio = bottom;
            m_DockspaceInitialized = false;
        }

        // Per-edge MINIMUM size in DPI-independent pixels (H5). Each edge is carved
        // at max(ratio·dockspaceSize, minPx·dpiScale), so a docked menu+toolbar row
        // never clips under a small ratio on a large monitor. 0 = ratio only (default).
        // Re-runs the builder next frame.
        void SetEdgeMinPixels(float top, float bottom, float left, float right)
        {
            m_TopMinPx = top; m_BottomMinPx = bottom; m_LeftMinPx = left; m_RightMinPx = right;
            m_DockspaceInitialized = false;
        }

        // Reserve a horizontal band at the BOTTOM of the OS window, below the
        // dockspace (K5 — status bars). The dock host shrinks by `px` so docked
        // panels never underlap whatever the client draws in the freed band (the
        // client owns that drawing — position at viewport bottom). 0 (default)
        // = the historical full-height host, byte-identical for every app that
        // never calls this. Pass a font-derived height so DPI scaling follows
        // the UI scale for free.
        void  SetBottomInsetPixels(float px) { m_BottomInsetPx = px < 0.0f ? 0.0f : px; }
        float GetBottomInsetPixels() const   { return m_BottomInsetPx; }

        // Show/hide the engine "File / View" chrome menus in the host title bar (H5).
        // An app that supplies its OWN menu bar (Starforge) hides these to avoid a
        // duplicate "File" menu; the borderless min/max/close controls + the centered
        // project name + title-bar drag are UNAFFECTED. Restore true on project exit
        // (the Launcher relies on the chrome menus). Default true = every other app
        // is unchanged.
        void SetChromeMenusVisible(bool visible) { m_ShowChromeMenus = visible; }
        bool AreChromeMenusVisible() const       { return m_ShowChromeMenus; }

        // The central viewport's DISPLAYED title (H5). The dock-node identity stays
        // "Viewport" via the "Title###Viewport" idiom, so renaming the tab per scene
        // never resets the layout. Default "Viewport" = every shipped app unchanged.
        void SetViewportTitle(const std::string& title) { m_ViewportTitle = title.empty() ? "Viewport" : title; }
        const std::string& GetViewportTitle() const     { return m_ViewportTitle; }

        // Layout-persistence policy.
        //   true  (default): re-apply the client-coded layout on EVERY load.
        //   false (future) : restore the user's last arrangement from imgui.ini.
        // Only the coded-layout path is wired today; the flag is the hook for the
        // future "remember my layout" toggle.
        void SetApplyCodedLayoutOnLoad(bool enable) { m_ApplyCodedLayoutOnLoad = enable; }
        bool GetApplyCodedLayoutOnLoad() const { return m_ApplyCodedLayoutOnLoad; }

        // -----------------------------------------------------------------------
        // Viewport bounds — ImGui SCREEN pixels (OS virtual-desktop coordinates;
        // multi-viewport is enabled, so all ImGui rects live in this space).
        // ViewportPos is the top-left of the rendered image content (below the
        // tab bar). Compare against Input::GetMouseScreenPosition() — the
        // window-relative Input::GetMousePosition() only matches when the app
        // window sits at the desktop origin (e.g. borderless maximized).
        // -----------------------------------------------------------------------
        glm::vec2 GetViewportPos()  const { return m_ViewportPos; }
        glm::vec2 GetViewportSize() const { return m_ViewportSize; }

        // Hover/focus state of the central Viewport panel, refreshed each ImGui
        // frame (so worth one frame of lag when read in OnUpdate). Use these to
        // gate POLLED input — e.g. only let a camera controller orbit while the
        // viewport is actually under the cursor, not while the user drags a
        // slider in a side panel. (The EVENT path is already gated: ImGuiLayer
        // blocks mouse/key events to client layers when ImGui wants them.)
        bool IsViewportHovered() const { return m_ViewportHovered; }
        bool IsViewportFocused() const { return m_ViewportFocused; }

        // -----------------------------------------------------------------------
        // Viewport overlay — draw ImGui content ON TOP of the rendered viewport
        // image (transform gizmos, view cubes, HUD chips). ImGui supports
        // appending to an existing window by re-Begin'ing it within the frame;
        // these helpers wrap that so clients never hard-code the viewport
        // window's identity, and Cosmic::Gizmo gets the host window its hover
        // logic requires (see graphics/Gizmo.h FRAME PROTOCOL).
        //
        //   if (ws->BeginViewportOverlay())
        //   {
        //       // current window == the Viewport; draw list is clipped to it
        //       Cosmic::Gizmo::SetRect(...);  Cosmic::Gizmo::Manipulate(...);
        //       ImGui::SetCursorScreenPos(...); ImGui::Image(...);   // widgets OK
        //   }
        //   ws->EndViewportOverlay();   // ALWAYS pair with BeginViewportOverlay
        //
        // Returns false (and pushes no window) when the viewport is hidden via
        // SetViewportVisible(false). Call from OnImGuiRender only — the client
        // renders after the viewport window exists for the frame.
        //
        // Inline on purpose: WorkspaceLayer is not COSMIC_API-exported, so this
        // compiles into the client DLL (same pattern as DockWindow).
        // -----------------------------------------------------------------------
        bool BeginViewportOverlay()
        {
            if (!m_ShowViewport)
                return false;
            ImGui::Begin("Viewport");   // appends to this frame's existing window
            m_OverlayOpen = true;
            return true;
        }
        void EndViewportOverlay()
        {
            if (m_OverlayOpen)
            {
                ImGui::End();
                m_OverlayOpen = false;
            }
        }

        // -----------------------------------------------------------------------
        // Teardown / layout
        // -----------------------------------------------------------------------
        void RequestLayoutReset()
        {
            // Full teardown path (used when returning to Launcher)
            m_PendingTeardown = true;
            m_TeardownComplete = false;
        }
        bool IsReadyForDeletion() const { return m_TeardownComplete; }

        // Soft reset — just re-runs DockBuilder next frame, no layer destruction
        void ResetLayout() { m_DockspaceInitialized = false; }

    private:
        // -----------------------------------------------------------------------
        // Internal helpers
        // -----------------------------------------------------------------------
        void BuildDockspace(ImGuiID dockspaceId, const ImGuiViewport* viewport);
        void RenderMenuBar();
        void RenderHeaderChrome();  // project name + exit button in the menu bar

        Cosmic::Layer* m_ClientViewportLayer = nullptr;

        // UI state — member variables (NOT statics) so re-creation works correctly
        bool m_DockspaceInitialized = false;
        bool m_DockspaceOpen = true;

        // Viewport tracking
        glm::vec2 m_ViewportPos  = { 0.0f, 0.0f };
        glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
        bool      m_ViewportFocused = false;
        bool      m_ViewportHovered = false;
        bool      m_ShowViewport    = true;   // see SetViewportVisible()
        bool      m_OverlayOpen     = false;  // BeginViewportOverlay pairing guard

        // Teardown handshake
        bool m_PendingTeardown = false;
        bool m_TeardownComplete = false;

        // Project identity
        std::string m_ProjectName = "Untitled Project";

        // Extra panel requests accumulated before the first DockBuilder run
        std::vector<DockedPanelRequest> m_PendingPanelRequests;

        // Window -> port bindings registered via DockWindow() (new dock-port API)
        std::vector<DockBinding> m_DockBindings;

        // Engine-hosted theme selector window (see ShowThemeSelector()).
        bool        m_ShowThemeSelector   = false;
        std::string m_ThemeSelectorWindow = "Themes";
        DockPort    m_ThemeSelectorPort   = DockPort::RightTop;

        // Borderless chrome: whether the cursor is over the draggable part of the
        // custom title bar (the menu bar minus its menus/buttons). Read by the
        // window's hit-test callback (registered in OnAttach).
        bool m_TitlebarDrag = false;

        // Per-edge layout ratios (fraction of the dockspace) — tunable via SetEdgeRatios.
        float m_LeftRatio   = 0.20f;
        float m_RightRatio  = 0.20f;
        float m_TopRatio    = 0.18f;
        float m_BottomRatio = 0.22f;

        // Per-edge minimum pixels (H5, DPI-scaled) — tunable via SetEdgeMinPixels.
        float m_TopMinPx = 0.0f, m_BottomMinPx = 0.0f, m_LeftMinPx = 0.0f, m_RightMinPx = 0.0f;

        // Bottom band reserved below the dock host (K5) — see SetBottomInsetPixels.
        float m_BottomInsetPx = 0.0f;

        // Chrome menus + viewport title (H5).
        bool        m_ShowChromeMenus = true;
        std::string m_ViewportTitle   = "Viewport";

        // Layout-persistence policy (see SetApplyCodedLayoutOnLoad).
        bool m_ApplyCodedLayoutOnLoad = true;
    };
}