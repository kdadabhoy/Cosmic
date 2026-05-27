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
 */

#include "Cosmic.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Cosmic
{
    // ---------------------------------------------------------------------------
    // Describes one extra panel slot to be pre-docked in the DockBuilder pass.
    // Client layers fill this in OnAttach and push to WorkspaceLayer via
    // RequestExtraDockedPanel() before the first ImGui frame.
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
    };
}