#include "SandboxLayer.h"
#include "LayerOne.h"
#include "LayerTwo.h"
#include <imgui.h>

namespace Cosmic
{
	SandboxLayer::SandboxLayer() : Layer("EditorHost") {}

	void SandboxLayer::OnAttach() { SelectScene(0); }

	void SandboxLayer::SelectScene(int index)
	{
		if (index == 0) m_ActiveScene = std::make_unique<LayerOne>();
		else m_ActiveScene = std::make_unique<LayerTwo>();
		m_CurrentMode = index;

		// Push current size to new scene immediately
		if (m_ViewportSize.x > 0) m_ActiveScene->SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
	}

	void SandboxLayer::OnUpdate(float ts)
	{
		auto& fb = Application::Get().GetFrameBuffer();

		// 1. Check for UI Resize
		if (m_ViewportSize.x > 0.0f && (fb->GetWidth() != m_ViewportSize.x || fb->GetHeight() != m_ViewportSize.y))
		{
			fb->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
			m_ActiveScene->SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
		}

		// 2. Render Scene to Framebuffer
		fb->Bind();
		m_ActiveScene->OnUpdate(ts);
		m_ActiveScene->OnRender();
		fb->Unbind();

		// 3. Clear the main background (prevents smearing)
		RenderCommand::Clear(0.1f, 0.1f, 0.1f);
	}

	void SandboxLayer::OnImGuiRender()
	{
		// Fullscreen Dockspace
		static bool dockspaceOpen = true;
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(viewport->Size);
		ImGui::Begin("MasterWindow", &dockspaceOpen, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

		ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

		// Game Viewport
		ImGui::Begin("Viewport");
		ImVec2 panelSize = ImGui::GetContentRegionAvail();
		m_ViewportSize = { panelSize.x, panelSize.y };

		uint32_t textureID = Application::Get().GetFrameBuffer()->GetColorAttachmentRendererID();
		ImGui::Image((void*)(uintptr_t)textureID, panelSize, { 0, 1 }, { 1, 0 });
		ImGui::End();

		// Scene Selector
		ImGui::Begin("Scene Manager");
		const char* list[] = { "Red Scene", "Blue Scene" };
		if (ImGui::Combo("Active Layer", &m_CurrentMode, list, 2)) SelectScene(m_CurrentMode);
		m_ActiveScene->OnImGuiRender();
		ImGui::End();

		ImGui::End(); // End MasterWindow
	}
}