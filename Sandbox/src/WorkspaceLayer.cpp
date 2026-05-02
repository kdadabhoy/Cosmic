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

 // Projects are included here
#include "projects/ExampleProject/ExampleProject.h"
#include "projects/DinoProject/DinoProject.h"

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
		// Fullscreen Dockspace Container Setup
		static bool dockspaceOpen = true;
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(viewport->Size);

		ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::Begin("MasterDockSpace", &dockspaceOpen, window_flags);
		ImGui::PopStyleVar(2);

		ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

		// --- FIXED MENU BAR ---
		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Close Engine")) Cosmic::Application::Get().Close();
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Select Project"))
			{
				if (ImGui::MenuItem("Example Simulation Suite")) LoadProject<ExampleProject>();

				// MOVED DINO PROJECT HERE
				if (ImGui::MenuItem("Dino Simulator Suite")) LoadProject<DinoProject>();

				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}

		// --- VIEWPORT WINDOW ---
		ImGui::Begin("Viewport");
		m_ViewportFocused = ImGui::IsWindowFocused();
		m_ViewportHovered = ImGui::IsWindowHovered();
		Cosmic::Application::Get().GetImGuiLayer()->BlockEvents(!m_ViewportFocused || !m_ViewportHovered);

		ImVec2 panelSize = ImGui::GetContentRegionAvail();
		m_ViewportSize = { panelSize.x, panelSize.y };

		uint32_t textureID = Cosmic::Application::Get().GetFrameBuffer()->GetColorAttachmentRendererID();
		ImGui::Image((void*)(uintptr_t)textureID, panelSize, { 0, 1 }, { 1, 0 });
		ImGui::End();

		// --- PROJECT INSPECTOR ---
		ImGui::Begin("Project Inspector");
		if (m_ActiveSim) m_ActiveSim->OnImGuiRender();
		else ImGui::Text("No active project loaded.");
		ImGui::End();

		ImGui::End(); // End MasterDockSpace
	}





    void WorkspaceLayer::OnEvent(Cosmic::Event& e)
    {
        // Events can be dispatched here or passed down to the active simulation
    }

}