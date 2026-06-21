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
		Cosmic::Renderer2D::SetViewportSize(
			static_cast<uint32_t>(m_ViewportSize.x),
			static_cast<uint32_t>(m_ViewportSize.y));

		if (m_ClientViewportLayer)
		{
			m_ClientViewportLayer->UpdateLayerTime(ts);
			m_ClientViewportLayer->OnUpdate(ts * m_ClientViewportLayer->GetTimeScale());
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
			if (m_ApplyCodedLayoutOnLoad)
				BuildDockspace(dockspace_id, viewport);
			// else (future): leave the layout restored from imgui.ini untouched so
			// the user's last arrangement persists. Not wired yet — default path
			// always re-applies the client-coded layout.
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

		// Record content-area origin so pickers can map mouse → viewport space.
		ImVec2 contentOrigin = ImGui::GetCursorScreenPos();
		m_ViewportPos = { contentOrigin.x, contentOrigin.y };

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

	// File-local: split `node` into `count` near-equal sub-nodes along `dir`
	// (ImGuiDir_Up for vertical columns -> sections stack top..bottom;
	//  ImGuiDir_Left for horizontal rows -> sections run left..right).
	// Returns the sub-node ids in visual order. count <= 1 returns {node}.
	static std::vector<ImGuiID> SplitIntoSections(ImGuiID node, int count, ImGuiDir dir)
	{
		std::vector<ImGuiID> out;
		if (count <= 1) { out.push_back(node); return out; }

		ImGuiID remaining = node;
		for (int i = 0; i < count - 1; ++i)
		{
			// 1st cut gives 1/count to the leading piece, then 1/(count-1), ...
			const float ratio = 1.0f / static_cast<float>(count - i);
			ImGuiID piece = ImGui::DockBuilderSplitNode(remaining, dir, ratio, nullptr, &remaining);
			out.push_back(piece);
		}
		out.push_back(remaining);
		return out;
	}

	void WorkspaceLayer::BuildDockspace(ImGuiID dockspaceId, const ImGuiViewport* viewport)
	{
		ImGui::DockBuilderRemoveNode(dockspaceId);
		ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->Size);

		// =====================================================================
		// LEGACY MODE — no DockWindow() bindings registered.
		// Reproduce the original fixed 3-tier left sidebar so existing projects
		// that simply Begin("Project Inspector Top/Mid/Bottom") look identical.
		// =====================================================================
		if (m_DockBindings.empty())
		{
			ImGuiID dock_main;
			ImGuiID dock_left = ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.22f, nullptr, &dock_main);

			ImGuiID dock_left_top_mid;
			ImGuiID dock_left_bottom = ImGui::DockBuilderSplitNode(dock_left, ImGuiDir_Down, 0.33f, nullptr, &dock_left_top_mid);
			ImGuiID dock_left_top;
			ImGuiID dock_left_mid = ImGui::DockBuilderSplitNode(dock_left_top_mid, ImGuiDir_Down, 0.50f, nullptr, &dock_left_top);

			ImGui::DockBuilderDockWindow("Project Inspector Top",    dock_left_top);
			ImGui::DockBuilderDockWindow("Project Inspector Mid",    dock_left_mid);
			ImGui::DockBuilderDockWindow("Project Inspector Bottom", dock_left_bottom);
			ImGui::DockBuilderDockWindow("Viewport", dock_main);

			ImGuiID dock_remaining = dock_main;
			for (const auto& req : m_PendingPanelRequests)
			{
				if (req.WindowName == "Project Inspector Top")    { ImGui::DockBuilderDockWindow(req.WindowName.c_str(), dock_left_top);    continue; }
				if (req.WindowName == "Project Inspector Mid")    { ImGui::DockBuilderDockWindow(req.WindowName.c_str(), dock_left_mid);    continue; }
				if (req.WindowName == "Project Inspector Bottom") { ImGui::DockBuilderDockWindow(req.WindowName.c_str(), dock_left_bottom); continue; }

				ImGuiID dock_new;
				dock_remaining = ImGui::DockBuilderSplitNode(dock_remaining, req.SplitDir, req.SplitRatio, &dock_new, &dock_remaining);
				ImGui::DockBuilderDockWindow(req.WindowName.c_str(), dock_new);
			}

			ImGui::DockBuilderFinish(dockspaceId);
			CS_CORE_INFO("WorkspaceLayer: Dockspace built (legacy 3-tier left sidebar).");
			return;
		}

		// =====================================================================
		// PORT MODE — build ONLY the edges/sections that have bound windows, so
		// unused ports take no space. Multiple windows per port become tabs.
		// =====================================================================
		std::vector<std::string> portWindows[static_cast<int>(DockPort::Center) + 1];
		for (const auto& b : m_DockBindings)
			portWindows[static_cast<int>(b.Port)].push_back(b.WindowName);

		auto used   = [&](DockPort p) { return !portWindows[static_cast<int>(p)].empty(); };
		auto anyOf  = [&](DockPort a, DockPort b, DockPort c) { return used(a) || used(b) || used(c); };
		auto dockAll = [&](const std::vector<std::string>& names, ImGuiID node)
		{
			for (const auto& n : names) ImGui::DockBuilderDockWindow(n.c_str(), node);
		};

		const bool useLeft   = anyOf(DockPort::LeftTop,    DockPort::LeftMiddle,   DockPort::LeftBottom);
		const bool useRight  = anyOf(DockPort::RightTop,   DockPort::RightMiddle,  DockPort::RightBottom);
		const bool useTop    = anyOf(DockPort::TopLeft,    DockPort::TopCenter,    DockPort::TopRight);
		const bool useBottom = anyOf(DockPort::BottomLeft, DockPort::BottomCenter, DockPort::BottomRight);

		// Full-height side columns first, then top/bottom rows span the central band.
		ImGuiID dock_main = dockspaceId;
		ImGuiID dock_left = 0, dock_right = 0, dock_top = 0, dock_bottom = 0;
		if (useLeft)   dock_left   = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left,  m_LeftRatio,   nullptr, &dock_main);
		if (useRight)  dock_right  = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, m_RightRatio,  nullptr, &dock_main);
		if (useTop)    dock_top    = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Up,    m_TopRatio,    nullptr, &dock_main);
		if (useBottom) dock_bottom = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down,  m_BottomRatio, nullptr, &dock_main);

		// Central viewport (+ any windows explicitly bound to Center -> tabbed with it).
		ImGui::DockBuilderDockWindow("Viewport", dock_main);
		dockAll(portWindows[static_cast<int>(DockPort::Center)], dock_main);

		// Carve an edge node into its used sections and dock each section's windows.
		auto buildEdge = [&](ImGuiID edgeNode, DockPort s0, DockPort s1, DockPort s2, ImGuiDir dir)
		{
			DockPort secs[3] = { s0, s1, s2 };
			std::vector<int> usedIdx;
			for (int i = 0; i < 3; ++i) if (used(secs[i])) usedIdx.push_back(i);

			std::vector<ImGuiID> nodes = SplitIntoSections(edgeNode, static_cast<int>(usedIdx.size()), dir);
			for (size_t k = 0; k < usedIdx.size(); ++k)
				dockAll(portWindows[static_cast<int>(secs[usedIdx[k]])], nodes[k]);
		};

		if (useLeft)   buildEdge(dock_left,   DockPort::LeftTop,    DockPort::LeftMiddle,   DockPort::LeftBottom,   ImGuiDir_Up);
		if (useRight)  buildEdge(dock_right,  DockPort::RightTop,   DockPort::RightMiddle,  DockPort::RightBottom,  ImGuiDir_Up);
		if (useTop)    buildEdge(dock_top,    DockPort::TopLeft,    DockPort::TopCenter,    DockPort::TopRight,     ImGuiDir_Left);
		if (useBottom) buildEdge(dock_bottom, DockPort::BottomLeft, DockPort::BottomCenter, DockPort::BottomRight,  ImGuiDir_Left);

		// Escape-hatch custom split requests (RequestExtraDockedPanel), off center.
		ImGuiID dock_remaining = dock_main;
		for (const auto& req : m_PendingPanelRequests)
		{
			ImGuiID dock_new;
			dock_remaining = ImGui::DockBuilderSplitNode(dock_remaining, req.SplitDir, req.SplitRatio, &dock_new, &dock_remaining);
			ImGui::DockBuilderDockWindow(req.WindowName.c_str(), dock_new);
		}

		ImGui::DockBuilderFinish(dockspaceId);
		CS_CORE_INFO("WorkspaceLayer: Dockspace built (port mode L={} R={} T={} B={}).",
			useLeft, useRight, useTop, useBottom);
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