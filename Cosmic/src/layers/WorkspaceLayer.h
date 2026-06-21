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

    // One window-to-port binding registered by a client layer.
    struct DockBinding
    {
        std::string WindowName; // must match the client's ImGui::Begin("...")
        DockPort    Port = DockPort::LeftTop;
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
        void DockWindow(const std::string& windowName, DockPort port)
        {
            for (auto& b : m_DockBindings)
                if (b.WindowName == windowName) { b.Port = port; m_DockspaceInitialized = false; return; }
            m_DockBindings.push_back({ windowName, port });
            m_DockspaceInitialized = false; // trigger a DockBuilder rebuild
        }

        // Drop all port bindings (e.g. before re-registering for a new project).
        void ClearDockWindows()
        {
            m_DockBindings.clear();
            m_DockspaceInitialized = false;
        }

        // Per-edge size as a fraction of the dockspace. Optional; sane defaults
        // are used otherwise. Re-runs the builder next frame.
        void SetEdgeRatios(float left, float right, float top, float bottom)
        {
            m_LeftRatio = left; m_RightRatio = right; m_TopRatio = top; m_BottomRatio = bottom;
            m_DockspaceInitialized = false;
        }

        // Layout-persistence policy.
        //   true  (default): re-apply the client-coded layout on EVERY load.
        //   false (future) : restore the user's last arrangement from imgui.ini.
        // Only the coded-layout path is wired today; the flag is the hook for the
        // future "remember my layout" toggle.
        void SetApplyCodedLayoutOnLoad(bool enable) { m_ApplyCodedLayoutOnLoad = enable; }
        bool GetApplyCodedLayoutOnLoad() const { return m_ApplyCodedLayoutOnLoad; }

        // -----------------------------------------------------------------------
        // Viewport bounds — pixel coords matching glfwGetCursorPos space.
        // ViewportPos is the top-left of the rendered image content (below title bar).
        // -----------------------------------------------------------------------
        glm::vec2 GetViewportPos()  const { return m_ViewportPos; }
        glm::vec2 GetViewportSize() const { return m_ViewportSize; }

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

        // Teardown handshake
        bool m_PendingTeardown = false;
        bool m_TeardownComplete = false;

        // Project identity
        std::string m_ProjectName = "Untitled Project";

        // Extra panel requests accumulated before the first DockBuilder run
        std::vector<DockedPanelRequest> m_PendingPanelRequests;

        // Window -> port bindings registered via DockWindow() (new dock-port API)
        std::vector<DockBinding> m_DockBindings;

        // Per-edge layout ratios (fraction of the dockspace) — tunable via SetEdgeRatios.
        float m_LeftRatio   = 0.20f;
        float m_RightRatio  = 0.20f;
        float m_TopRatio    = 0.18f;
        float m_BottomRatio = 0.22f;

        // Layout-persistence policy (see SetApplyCodedLayoutOnLoad).
        bool m_ApplyCodedLayoutOnLoad = true;
    };
}