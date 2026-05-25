#include "WorkspaceLayer.h"
#include <imgui.h>
#include <imgui_internal.h>

namespace Cosmic
{
	WorkspaceLayer::WorkspaceLayer()
		: Layer("WorkspaceLayer")
	{
	}

	void WorkspaceLayer::OnAttach()
	{
		// Pure generalized canvas: Starts completely clean.
		// DLL Plugin Loaders or dynamic project scripts can push their 
		// runtime layers directly to SetViewportLayer() when initialized.
	}

	void WorkspaceLayer::OnDetach()
	{
		ClearViewportLayer();
		// Do NOT call ImGui functions... or it will cause crashes
	}

	void WorkspaceLayer::SetViewportLayer(Cosmic::Layer* layer)
	{
		// 1. If there is an existing runtime layer mounted, detach it cleanly first
		if (m_ClientViewportLayer)
		{
			CS_CORE_WARN("WorkspaceLayer: Evicting previous client layer context: {0}", m_ClientViewportLayer->GetName());
			m_ClientViewportLayer->OnDetach();
		}

		m_ClientViewportLayer = layer;

		// 2. Crucial Engine Fix: Instantly link the engine assembly lifecycle cascade
		if (m_ClientViewportLayer)
		{
			CS_CORE_INFO("WorkspaceLayer: Coupling incoming dynamic plugin layer: {0}", m_ClientViewportLayer->GetName());
			m_ClientViewportLayer->OnAttach();
		}
	}

	void WorkspaceLayer::ClearViewportLayer()
	{
		if (m_ClientViewportLayer)
		{
			CS_CORE_WARN("WorkspaceLayer: Clearing viewport layer context: {0}", m_ClientViewportLayer->GetName());
			m_ClientViewportLayer->OnDetach();
		}
		m_ClientViewportLayer = nullptr;
	}

	void WorkspaceLayer::OnUpdate(float ts)
	{
		Ref<FrameBuffer> fb = Cosmic::Application::Get().GetFrameBuffer();

		if (m_ViewportSize.x > 0.0f && (fb->GetWidth() != m_ViewportSize.x || fb->GetHeight() != m_ViewportSize.y))
		{
			fb->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
		}

		fb->Bind();
		Cosmic::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
		Cosmic::RenderCommand::Clear();
		Cosmic::RenderCommand::SetViewport(0, 0, (uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);

		if (m_ClientViewportLayer)
		{
			// 'ts' arriving here is already scaled by Application::Run!
			// Simply propagate it downward safely.
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
			// Fix: Scale the fixed step by the client layer's explicit local time scale
			float scaledFixedDelta = deltaFixedTime * m_ClientViewportLayer->GetTimeScale();

			m_ClientViewportLayer->OnFixedUpdate(scaledFixedDelta);
		}
	}

	void WorkspaceLayer::OnImGuiRender()
	{
		// 0. Handle Reset/Cleanup first
		if (m_ShouldResetLayout)
		{
			ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
			ImGui::DockBuilderRemoveNode(dockspace_id);

			m_ShouldResetLayout = false;
			m_ReadyForDeletion = true;
			return; // Exit immediately; do not attempt to render layout or framebuffers
		}

		static bool dockspaceOpen = true;
		static bool firstTime = true;

		// 1. Setup the master full-screen workspace layout panel container
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(viewport->Size);
		ImGui::SetNextWindowViewport(viewport->ID);

		ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::Begin("MasterDockSpace", &dockspaceOpen, window_flags);
		ImGui::PopStyleVar(2);

		// 2. Instantiate and mount the root Workspace DockSpace layout identifier
		ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

		// 3. Automated Layout Generator (Fires once during initialization)
		if (firstTime)
		{
			firstTime = false;
			ImGui::DockBuilderRemoveNode(dockspace_id);
			ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
			ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

			ImGuiID dock_id_main = dockspace_id;
			ImGuiID dock_id_right = ImGui::DockBuilderSplitNode(dock_id_main, ImGuiDir_Right, 0.25f, nullptr, &dock_id_main);

			ImGui::DockBuilderDockWindow("Viewport", dock_id_main);
			ImGui::DockBuilderDockWindow("Project Inspector", dock_id_right);
			ImGui::DockBuilderFinish(dockspace_id);
		}

		// 4. Main Menu Bar
		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Return to Launcher"))
				{
					Cosmic::Application::Get().TransitionToLauncher();
				}

				if (ImGui::MenuItem("Exit"))
					Cosmic::Application::Get().Close();

				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}

		// 5. GRAPHICS VIEWPORT INTERACTION WINDOW
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
		ImGui::Begin("Viewport");

		m_ViewportFocused = ImGui::IsWindowFocused();
		m_ViewportHovered = ImGui::IsWindowHovered();

		// Handle event blocking cleanly based on interface user tracking matrix focus states
		Cosmic::Application::Get().GetImGuiLayer()->BlockEvents(!m_ViewportFocused && !m_ViewportHovered);

		ImVec2 panelSize = ImGui::GetContentRegionAvail();
		if (panelSize.x > 0 && panelSize.y > 0)
		{
			m_ViewportSize = { panelSize.x, panelSize.y };
		}

		// Bind texture element mapping identities directly to the layout view frame block
		uint32_t textureID = Cosmic::Application::Get().GetFrameBuffer()->GetColorAttachmentRendererID();
		ImGui::Image((void*)(uintptr_t)textureID, panelSize, { 0, 1 }, { 1, 0 });

		ImGui::End();
		ImGui::PopStyleVar();

		// 6. DETACHED INSPECTOR CONTEXT WINDOW
		ImGui::Begin("Project Inspector");
		if (m_ClientViewportLayer)
		{
			// Render the custom UI panels built straight into our workspace's active layer 
			m_ClientViewportLayer->OnImGuiRender();
		}
		else
		{
			ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No Active Viewport Layer Mounted.");
			ImGui::Text("Load a project assembly module or game plugin to spin up the UI canvas pipeline.");
		}
		ImGui::End();

		ImGui::End(); // End MasterDockSpace
	}

	void WorkspaceLayer::OnEvent(Cosmic::Event& e)
	{
		if (e.Handled)
		{
			return;
		}
	

		// Pass core inputs/window alterations downward to the client viewport layer context
		if (m_ClientViewportLayer)
		{
			m_ClientViewportLayer->OnEvent(e);
		}
	}
}