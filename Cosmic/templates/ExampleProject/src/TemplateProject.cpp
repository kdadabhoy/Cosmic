#include "TemplateProject.h"
#include <imgui.h>
#include <filesystem>

namespace Workspace
{
	TemplateProject::TemplateProject()
		: Layer("TemplateProject"), m_CameraController(1280.0f / 720.0f, true) // Pass standard aspect ratio context
	{
		// Establish Virtual File System context space
		Cosmic::FileSystem::SetActiveProject("TemplateProject");

		// Resolve local fire shader path 
		std::string resolvedShader = Cosmic::FileSystem::Resolve("project://shaders/FireShader.glsl");

		if (std::filesystem::exists(resolvedShader))
		{
			auto fireShader = Cosmic::Shader::Create(resolvedShader);
			m_FireMaterial = Cosmic::Material::Create(fireShader, "FireMaterial");
			if (m_FireMaterial)
			{
				m_FireMaterial->Set("u_Color", glm::vec4(1.0f, 0.5f, 0.2f, 1.0f));
			}
		}

		// Allocate simple entity context
		m_Scene = Cosmic::Scene::Create();
		m_FireQuadEntity = m_Scene->CreateEntity("Procedural Flame Quad");
	}

	void TemplateProject::OnDetach()
	{
		m_FireMaterial.reset();
		m_Scene.reset();
	}

	void TemplateProject::OnUpdate(float ts)
	{
		m_TimeAccumulator += (ts > 0.0f ? ts : 0.001f);

		// Synchronize your camera system updates (inputs, window dimension adjustments)
		m_CameraController.OnUpdate(ts);

		// Route time ticks straight into your core renderer backend matrices
		Cosmic::Renderer2D::UpdateTimeline(ts, 1280, 720);

		if (m_FireMaterial)
		{
			m_FireMaterial->Set("u_Time", m_TimeAccumulator);
		}

		// FIXED: Pass the active camera view matrix context into your render pass sequence
		Cosmic::Renderer2D::BeginScene(m_CameraController.GetCamera());

		if (m_FireMaterial)
		{
			auto& trans = m_FireQuadEntity.GetComponent<Cosmic::TransformComponent>();
			Cosmic::Renderer2D::DrawQuad(trans.Position, trans.Scale, m_FireMaterial);
		}

		Cosmic::Renderer2D::EndScene();
	}

	void TemplateProject::OnImGuiRender()
	{
		ImGui::Begin("Project Controller");
		ImGui::Text("Application Runtime Module: TemplateProject");

		if (m_FireMaterial && ImGui::CollapsingHeader("Flame Uniform Controls", ImGuiTreeNodeFlags_DefaultOpen))
		{
			glm::vec4 color = m_FireMaterial->GetVector("u_Color");
			if (ImGui::ColorEdit4("Flame Color Tint", &color.x))
			{
				m_FireMaterial->Set("u_Color", color);
			}
		}
		ImGui::End();
	}
}

extern "C"
{
	__declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context)
	{
		ImGui::SetCurrentContext(context.ImGuiCtx);
	}

	__declspec(dllexport) Cosmic::Layer* CreatePluginLayer()
	{
		return new Workspace::TemplateProject();
	}
}