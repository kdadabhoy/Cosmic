/**
 * @file WorkspaceLayer.cpp
 * @brief Implementation of the Host workspace.
 *
 * LOGIC: This file implements the "Main Window" of your engine. It creates
 * a central DockSpace so that you can snap windows (Viewport, Stats,
 * Project Browser) together. It binds the engine's Framebuffer to
 * ensure all rendering is captured and displayed within an ImGui window.
 */

#include "WorkspaceLayer.h"
#include <imgui_internal.h>

 // Projects are included here
#include "projects/ExampleProject/ExampleProject.h"
#include "projects/DinoProject/DinoProject.h"
#include "projects/TelemetryProject/TelemetryProject.h"

// After adding the include also add a if (ImGui::MenuItem("Dino Simulator Suite")) LoadProject<DinoProject>(); ... in OnImGuiRender



#include <imgui.h>

namespace Workspace
{

    WorkspaceLayer::WorkspaceLayer() : Layer("WorkspaceLayer") {}

    void WorkspaceLayer::OnAttach()
    {
        // Start with the Example Project
        LoadProject<ExampleProject>();
    }

    void WorkspaceLayer::OnDetach() {}

	void WorkspaceLayer::OnUpdate(float ts)
	{
		auto& fb = Cosmic::Application::Get().GetFrameBuffer();

		// 1. Sync Framebuffer size
		if (m_ViewportSize.x > 0.0f && (fb->GetWidth() != m_ViewportSize.x || fb->GetHeight() != m_ViewportSize.y))
		{
			fb->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
			if (m_ActiveSim) m_ActiveSim->SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
		}

		// --- START RENDERING ---
		fb->Bind();

		// NEW CLEANER WAY: Clear the Framebuffer here so projects don't have to!
		Cosmic::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
		Cosmic::RenderCommand::Clear();

		Cosmic::RenderCommand::SetViewport(0, 0, (uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);

		if (m_ActiveSim)
		{
			m_ActiveSim->OnUpdate(ts);
			m_ActiveSim->OnRender();
		}

		fb->Unbind();
		// --- END RENDERING ---

		// Clear the main window (the area behind the ImGui panels)
		Cosmic::RenderCommand::Clear(0.0f, 0.0f, 0.0f);
	}





	void WorkspaceLayer::OnImGuiRender()
	{
		static bool dockspaceOpen = true;
		static bool firstTime = true; // Flag to trigger our layout setup

		// 1. Setup the full-screen parent window for the DockSpace
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

		// 2. Initialize the DockSpace
		ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

		// 3. HARD-CODED INITIAL LAYOUT
		if (firstTime)
		{
			firstTime = false;

			// Clear existing layout to ensure a clean slate
			ImGui::DockBuilderRemoveNode(dockspace_id);
			ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
			ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

			// Split the main area: 25% to the right for the Inspector, 75% left for Viewport
			ImGuiID dock_id_main = dockspace_id;
			ImGuiID dock_id_right = ImGui::DockBuilderSplitNode(dock_id_main, ImGuiDir_Right, 0.25f, nullptr, &dock_id_main);

			// Assign our windows to those specific dock slots
			ImGui::DockBuilderDockWindow("Viewport", dock_id_main);
			ImGui::DockBuilderDockWindow("Project Inspector", dock_id_right);

			ImGui::DockBuilderFinish(dockspace_id);
		}

		// 4. MAIN MENU BAR
		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Exit")) Cosmic::Application::Get().Close();
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Select Project"))
			{
				if (ImGui::MenuItem("Example Simulation Suite")) LoadProject<ExampleProject>();
				if (ImGui::MenuItem("Dino Simulator Suite")) LoadProject<DinoProject>();
				if (ImGui::MenuItem("Telemetry Project")) LoadProject<TelemetryProject>();

				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}

		// 5. VIEWPORT WINDOW
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 }); // Edge-to-edge game render
		ImGui::Begin("Viewport");

		m_ViewportFocused = ImGui::IsWindowFocused();
		m_ViewportHovered = ImGui::IsWindowHovered();

		// Only allow game input if the mouse is actually inside the viewport
		Cosmic::Application::Get().GetImGuiLayer()->BlockEvents(!m_ViewportFocused && !m_ViewportHovered);

		ImVec2 panelSize = ImGui::GetContentRegionAvail();

		// Safety: only update size if the window isn't collapsed/zero
		if (panelSize.x > 0 && panelSize.y > 0)
			m_ViewportSize = { panelSize.x, panelSize.y };

		// Get the texture from the Framebuffer and draw it as an Image
		uint32_t textureID = Cosmic::Application::Get().GetFrameBuffer()->GetColorAttachmentRendererID();
		ImGui::Image((void*)(uintptr_t)textureID, panelSize, { 0, 1 }, { 1, 0 });

		ImGui::End();
		ImGui::PopStyleVar();

		// 6. PROJECT INSPECTOR WINDOW
		ImGui::Begin("Project Inspector");
		if (m_ActiveSim)
		{
			m_ActiveSim->OnImGuiRender();
		}
		else
		{
			ImGui::TextColored(ImVec4(1, 1, 0, 1), "System Ready.");
			ImGui::Text("Please select a project from the 'File' menu to begin.");
		}
		ImGui::End();

		ImGui::End(); // End MasterDockSpace
	}





    void WorkspaceLayer::OnEvent(Cosmic::Event& e)
    {
        // Events can be dispatched here or passed down to the active simulation
    }

}