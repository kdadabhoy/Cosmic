#include "WorkspaceLayer.h"
#include <imgui_internal.h>
#include <imgui.h>

/**
 * *****PROJECT REGISTRATION GUIDE*****
 * 
 * To add a new engineering simulation to the workspace:
 * 
 * 1. Include the header here:
 * #include "projects/MyNewProject/MyNewProject.h"
 * 
 * 2. Add a MenuItem in OnImGuiRender() inside the "Select Project" menu:
 * if (ImGui::MenuItem("My Structural Analysis")) LoadProject<MyNewProject>();
 */

#include "projects/DinoProject/DinoProject.h"

namespace Workspace
{
	WorkspaceLayer::WorkspaceLayer() : Layer("WorkspaceLayer") {}

	void WorkspaceLayer::OnAttach()
	{
		// Boot into the primary project suite immediately upon layer attachment.
		LoadProject<DinoProject>();
	}

	void WorkspaceLayer::OnDetach() {}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * OnUpdate
	 * * THE ORCHESTRATOR: This function manages the synchronization between the
	 * UI-driven Viewport and the Graphics Hardware.
	 * 1. RESIZING: Checks if the ImGui Viewport size has changed. If so, it resizes
	 * the GPU Framebuffer to prevent pixel stretching.
	 * 2. CAPTURE: Binds the Framebuffer, clears it, and forwards the update/render
	 * signals to the active project.
	 * 3. RELEASE: Unbinds the buffer so the UI can draw its own panels.
	 */
	void WorkspaceLayer::OnUpdate(float ts)
	{
		auto& fb = Cosmic::Application::Get().GetFrameBuffer();

		// 1. Sync Framebuffer size with the ImGui Viewport dimensions
		if (m_ViewportSize.x > 0.0f && (fb->GetWidth() != m_ViewportSize.x || fb->GetHeight() != m_ViewportSize.y))
		{
			fb->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
			if (m_ActiveSim) m_ActiveSim->SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
		}

		// --- START RENDERING (Capture Phase) ---
		fb->Bind();

		// Set the render target background (Dark Grey Canvas)
		Cosmic::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
		Cosmic::RenderCommand::Clear();

		// Ensure the hardware viewport matches our captured texture size
		Cosmic::RenderCommand::SetViewport(0, 0, (uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);

		if (m_ActiveSim)
		{
			m_ActiveSim->OnUpdate(ts);
			m_ActiveSim->OnRender();
		}

		fb->Unbind();
		// --- END RENDERING ---

		// Clear the main OS window background (behind the ImGui DockSpace)
		Cosmic::RenderCommand::Clear(0.0f, 0.0f, 0.0f);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * OnImGuiRender
	 * * THE USER INTERFACE: Implements the "Host" shell logic.
	 * 1. MASTER DOCKSPACE: Creates a full-screen invisible window to act as
	 * the parent for all panels.
	 * 2. INITIAL LAYOUT: Uses DockBuilder to force a "75/25" split between
	 * the Viewport (Left) and the Inspector (Right) on first launch.
	 * 3. VIEWPORT WINDOW: Retrieves the texture from the Framebuffer and
	 * displays it as an Image.
	 * 4. INSPECTOR: Calls the active project's UI logic to populate properties.
	 */
	void WorkspaceLayer::OnImGuiRender()
	{
		static bool dockspaceOpen = true;
		static bool firstTime = true;

		// 1. Setup the full-screen parent window
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

		// 3. HARD-CODED INITIAL LAYOUT (Runs once)
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
				if (ImGui::MenuItem("Dino Simulator Suite")) LoadProject<DinoProject>();
				//Example: if (ImGui::MenuItem("My Structural Analysis")) LoadProject<MyNewProject>();
				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}

		// 5. VIEWPORT WINDOW: The display window for the simulation
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
		ImGui::Begin("Viewport");

		m_ViewportFocused = ImGui::IsWindowFocused();
		m_ViewportHovered = ImGui::IsWindowHovered();

		// Logic: If we are interacting with UI, tell the engine to ignore mouse input
		Cosmic::Application::Get().GetImGuiLayer()->BlockEvents(!m_ViewportFocused && !m_ViewportHovered);

		ImVec2 panelSize = ImGui::GetContentRegionAvail();
		if (panelSize.x > 0 && panelSize.y > 0)
			m_ViewportSize = { panelSize.x, panelSize.y };

		// Display the Framebuffer's texture
		uint32_t textureID = Cosmic::Application::Get().GetFrameBuffer()->GetColorAttachmentRendererID();
		ImGui::Image((void*)(uintptr_t)textureID, panelSize, { 0, 1 }, { 1, 0 });

		ImGui::End();
		ImGui::PopStyleVar();

		// 6. PROJECT INSPECTOR WINDOW: The controls window for simulation parameters
		ImGui::Begin("Project Inspector");
		if (m_ActiveSim)
		{
			m_ActiveSim->OnImGuiRender();
		}
		else
		{
			ImGui::TextColored(ImVec4(1, 1, 0, 1), "System Ready.");
			ImGui::Text("Please select a project from the 'Select Project' menu to begin.");
		}
		ImGui::End();

		ImGui::End(); // End MasterDockSpace
	}

	/////////////////////////////////////////////////////////////////////////////////

	void WorkspaceLayer::OnEvent(Cosmic::Event& e)
	{
		// Optional: Handle engine events before passing them to the simulation.
	}


	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * OnFixedUpdate
	 * * THE STABLE HEARTBEAT: Forwards the constant-time update signal to the
	 * active simulation. This is where physics calculations (gravity,
	 * collision detection, stress analysis) should occur to ensure
	 * deterministic behavior regardless of the frame rate.
	 */
	void WorkspaceLayer::OnFixedUpdate(float deltaFixedTime)
	{
		if (m_ActiveSim)
		{
			m_ActiveSim->OnFixedUpdate(deltaFixedTime);
		}
	}

	/////////////////////////////////////////////////////////////////////////////////
}