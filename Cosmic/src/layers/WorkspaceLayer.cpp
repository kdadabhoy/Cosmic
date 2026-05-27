#include "WorkspaceLayer.h"
#include <imgui.h>
#include <imgui_internal.h>

namespace Cosmic
{

	// =============================================================================
	// Construction / Destruction
	// =============================================================================

	WorkspaceLayer::WorkspaceLayer()
		: Layer("WorkspaceLayer")
	{
	}

	void WorkspaceLayer::OnAttach()
	{
		// Nothing to load — starts completely clean.
		// DLL plugin loaders push their layer via SetViewportLayer().
	}

	void WorkspaceLayer::OnDetach()
	{
		ClearViewportLayer();
		// Do NOT call ImGui functions here — context may already be torn down.
	}

	// =============================================================================
	// Viewport Layer Management
	// =============================================================================

	void WorkspaceLayer::SetViewportLayer(Cosmic::Layer* layer)
	{
		if (m_ClientViewportLayer)
		{
			CS_CORE_WARN("WorkspaceLayer: Evicting previous client layer: {0}",
				m_ClientViewportLayer->GetName());
			m_ClientViewportLayer->OnDetach();
		}

		m_ClientViewportLayer = layer;

		if (m_ClientViewportLayer)
		{
			CS_CORE_INFO("WorkspaceLayer: Mounting client layer: {0}",
				m_ClientViewportLayer->GetName());
			m_ClientViewportLayer->OnAttach();
		}
	}

	void WorkspaceLayer::ClearViewportLayer()
	{
		if (m_ClientViewportLayer)
		{
			CS_CORE_WARN("WorkspaceLayer: Clearing client layer: {0}",
				m_ClientViewportLayer->GetName());
			m_ClientViewportLayer->OnDetach();
		}
		m_ClientViewportLayer = nullptr;
	}

	// =============================================================================
	// Update
	// =============================================================================

	void WorkspaceLayer::OnUpdate(float ts)
	{
		Ref<FrameBuffer> fb = Cosmic::Application::Get().GetFrameBuffer();

		// Resize the framebuffer to match the ImGui viewport panel
		if (m_ViewportSize.x > 0.0f &&
			(fb->GetWidth() != static_cast<uint32_t>(m_ViewportSize.x) ||
				fb->GetHeight() != static_cast<uint32_t>(m_ViewportSize.y)))
		{
			fb->Resize(static_cast<uint32_t>(m_ViewportSize.x),
				static_cast<uint32_t>(m_ViewportSize.y));
		}

		fb->Bind();
		Cosmic::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
		Cosmic::RenderCommand::Clear();
		Cosmic::RenderCommand::SetViewport(0, 0,
			static_cast<uint32_t>(m_ViewportSize.x),
			static_cast<uint32_t>(m_ViewportSize.y));

		if (m_ClientViewportLayer)
		{
			m_ClientViewportLayer->UpdateLayerTime(ts);
			m_ClientViewportLayer->OnUpdate(ts);
		}

		fb->Unbind();
		Cosmic::RenderCommand::Clear(0.0f, 0.0f, 0.0f);
	}

	void WorkspaceLayer::OnFixedUpdate(float deltaFixedTime)
	{
		if (m_ClientViewportLayer)
		{
			float scaledDelta = deltaFixedTime * m_ClientViewportLayer->GetTimeScale();
			m_ClientViewportLayer->OnFixedUpdate(scaledDelta);
		}
	}

	// =============================================================================
	// ImGui — Main Entry
	// =============================================================================

	void WorkspaceLayer::OnImGuiRender()
	{
		// ------------------------------------------------------------------
		// STEP 0 — Handle teardown handshake (used when returning to Launcher)
		// ------------------------------------------------------------------
		if (m_PendingTeardown)
		{
			ImGuiID dockspace_id = ImGui::GetID("CosmicDockSpace");
			ImGui::DockBuilderRemoveNode(dockspace_id);
			m_PendingTeardown = false;
			m_TeardownComplete = true;
			return; // Skip all other rendering this frame
		}

		// ------------------------------------------------------------------
		// STEP 1 — Full-screen transparent host window
		// ------------------------------------------------------------------
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(viewport->Size);
		ImGui::SetNextWindowViewport(viewport->ID);

		ImGuiWindowFlags hostFlags =
			ImGuiWindowFlags_MenuBar |
			ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoBringToFrontOnFocus |
			ImGuiWindowFlags_NoNavFocus;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("##CosmicWorkspace", &m_DockspaceOpen, hostFlags);
		ImGui::PopStyleVar(3);

		// ------------------------------------------------------------------
		// STEP 2 — Menu bar (File, View, project name, exit button)
		// ------------------------------------------------------------------
		RenderMenuBar();

		// ------------------------------------------------------------------
		// STEP 3 — Dockspace
		// ------------------------------------------------------------------
		ImGuiID dockspace_id = ImGui::GetID("CosmicDockSpace");
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

		if (!m_DockspaceInitialized)
		{
			m_DockspaceInitialized = true;
			BuildDockspace(dockspace_id, viewport);
		}

		ImGui::End(); // ##CosmicWorkspace

		// ------------------------------------------------------------------
		// STEP 4 — Viewport panel (owned by WorkspaceLayer)
		// ------------------------------------------------------------------
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("Viewport");

		m_ViewportFocused = ImGui::IsWindowFocused();
		m_ViewportHovered = ImGui::IsWindowHovered();

		Cosmic::Application::Get().GetImGuiLayer()->BlockEvents(
			!m_ViewportFocused && !m_ViewportHovered);

		ImVec2 panelSize = ImGui::GetContentRegionAvail();
		if (panelSize.x > 0.0f && panelSize.y > 0.0f)
			m_ViewportSize = { panelSize.x, panelSize.y };

		uint32_t textureID =
			Cosmic::Application::Get().GetFrameBuffer()->GetColorAttachmentRendererID();
		ImGui::Image(reinterpret_cast<void*>(static_cast<uintptr_t>(textureID)),
			panelSize, { 0, 1 }, { 1, 0 });

		ImGui::End();
		ImGui::PopStyleVar();

		// ------------------------------------------------------------------
		// STEP 5 — Client renders its OWN ImGui windows at top level.
		//
		// Clients that want to appear in the default "Project Inspector" slot
		// simply name their window "Project Inspector".  Clients that want
		// additional panels can name them anything — users (or RequestExtraDockedPanel)
		// control where they dock.
		// ------------------------------------------------------------------
		if (m_ClientViewportLayer)
		{
			m_ClientViewportLayer->OnImGuiRender();
		}
		else
		{
			// Fallback placeholder when no project is loaded
			ImGui::Begin("Project Inspector");
			ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
				"No Active Project Loaded.");
			ImGui::TextWrapped("Load a project from the Launcher to begin.");
			ImGui::End();
		}
	}

	// =============================================================================
	// Menu Bar
	// =============================================================================

	void WorkspaceLayer::RenderMenuBar()
	{
		if (!ImGui::BeginMenuBar()) return;

		// ---- File menu ----
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Return to Launcher", "Alt+F4"))
				Cosmic::Application::Get().TransitionToLauncher();

			ImGui::Separator();

			if (ImGui::MenuItem("Exit"))
				Cosmic::Application::Get().Close();

			ImGui::EndMenu();
		}

		// ---- View menu ----
		if (ImGui::BeginMenu("View"))
		{
			if (ImGui::MenuItem("Reset Layout"))
				m_DockspaceInitialized = false; // Re-runs DockBuilder next frame

			ImGui::EndMenu();
		}

		// ---- Centered project name ----
		{
			std::string displayName = "  [ " + m_ProjectName + " ]  ";
			float       textWidth = ImGui::CalcTextSize(displayName.c_str()).x;
			float       menuBarW = ImGui::GetWindowWidth();

			// Only center if there's room — otherwise just append after menus
			float cursorAfterMenus = ImGui::GetCursorPosX();
			float centeredX = (menuBarW - textWidth) * 0.5f;

			if (centeredX > cursorAfterMenus + 4.0f)
				ImGui::SetCursorPosX(centeredX);

			ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f),
				"%s", displayName.c_str());
		}

		// ---- Exit button — right-aligned ----
		{
			const char* label = "  Exit  ";
			float       btnWidth = ImGui::CalcTextSize(label).x + 16.0f;
			float       menuBarW = ImGui::GetWindowWidth();
			float       rightEdge = menuBarW - btnWidth - 4.0f;

			if (ImGui::GetCursorPosX() < rightEdge)
				ImGui::SetCursorPosX(rightEdge);

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.12f, 0.12f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.18f, 0.18f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.40f, 0.08f, 0.08f, 1.0f));

			if (ImGui::Button(label))
				Cosmic::Application::Get().TransitionToLauncher();

			ImGui::PopStyleColor(3);
		}

		ImGui::EndMenuBar();
	}

	// =============================================================================
	// DockBuilder — called once per layout initialization
	// =============================================================================

	void WorkspaceLayer::BuildDockspace(ImGuiID dockspaceId, const ImGuiViewport* viewport)
	{
		ImGui::DockBuilderRemoveNode(dockspaceId);
		ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->Size);

		// Split: LEFT panel (Project Inspector) | MAIN (Viewport)
		ImGuiID dock_main;
		ImGuiID dock_left = ImGui::DockBuilderSplitNode(
			dockspaceId, ImGuiDir_Left, 0.22f, nullptr, &dock_main);

		ImGui::DockBuilderDockWindow("Project Inspector", dock_left);
		ImGui::DockBuilderDockWindow("Viewport", dock_main);

		// Process any extra panel requests the client submitted before this frame
		ImGuiID dock_remaining = dock_main;
		for (const auto& req : m_PendingPanelRequests)
		{
			ImGuiID dock_new;
			dock_remaining = ImGui::DockBuilderSplitNode(
				dock_remaining, req.SplitDir, req.SplitRatio, &dock_new, &dock_remaining);
			ImGui::DockBuilderDockWindow(req.WindowName.c_str(), dock_new);

			CS_CORE_INFO("WorkspaceLayer: Pre-docked panel '{}' ({} split, ratio {:.2f})",
				req.WindowName,
				req.SplitDir == ImGuiDir_Left ? "Left" :
				req.SplitDir == ImGuiDir_Right ? "Right" :
				req.SplitDir == ImGuiDir_Up ? "Up" : "Down",
				req.SplitRatio);
		}
		// Don't clear m_PendingPanelRequests — keep them so a layout reset re-applies them

		ImGui::DockBuilderFinish(dockspaceId);

		CS_CORE_INFO("WorkspaceLayer: Dockspace layout built. Inspector left (22%), Viewport main.");
	}

	// =============================================================================
	// Events
	// =============================================================================

	void WorkspaceLayer::OnEvent(Cosmic::Event& e)
	{
		if (e.Handled) return;

		if (m_ClientViewportLayer)
			m_ClientViewportLayer->OnEvent(e);
	}

} // namespace Cosmic