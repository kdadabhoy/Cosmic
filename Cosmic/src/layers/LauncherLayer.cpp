#include "LauncherLayer.h"
#include "core/Application.h"
#include "renderer/RenderCommand.h"
#include "renderer/Renderer2D.h"
#include "camera/OrthographicCamera.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <Windows.h>
#include "core/Log.h"

namespace Cosmic
{
    LauncherLayer::LauncherLayer()
        : Layer("LauncherLayer"), m_Camera(-8.0f, 8.0f, -4.5f, 4.5f)
    {
    }

    void LauncherLayer::OnAttach()
    {
        ScanForProjects();

        m_BackgroundTexture = Texture2D::Create("assets/textures/Galaxy.png");
        if (!m_BackgroundTexture)
        {
            CS_CORE_WARN("LauncherLayer: Failed to load splash background 'assets/textures/Galaxy.png'!");
        }
    }

    void LauncherLayer::OnDetach()
    {
    }

    void LauncherLayer::OnUpdate(float deltaTime)
    {
        RenderCommand::SetClearColor({ 0.02f, 0.02f, 0.04f, 1.0f });
        RenderCommand::Clear();

        if (m_BackgroundTexture)
        {
            Renderer2D::BeginScene(m_Camera);
            Renderer2D::DrawQuad({ 0.0f, 0.0f, -0.1f }, { 16.0f, 9.0f }, m_BackgroundTexture);
            Renderer2D::EndScene();
        }
    }

	void LauncherLayer::OnImGuiRender()
	{
		ImGuiViewport* viewport = ImGui::GetMainViewport();

		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);

		ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
			ImGuiWindowFlags_NoBackground;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(40.0f, 40.0f));

		ImGui::Begin("CosmicEngineLauncherWindow", nullptr, window_flags);
		ImGui::PopStyleVar(3);

		// Header
		ImGui::Spacing(); ImGui::Spacing();
		ImGui::Indent(40.0f);
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 1.0f, 1.0f), "C O S M I C   E N G I N E");
		ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Project Hub System Launcher");
		ImGui::Separator();
		ImGui::Spacing();

		// Columns layout
		if (ImGui::BeginTable("LauncherColumns", 2, ImGuiTableFlags_Resizable))
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);

			ImGui::Text("Select Active Project Workspace:");
			ImGui::Spacing();

			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.03f, 0.03f, 0.06f, 0.75f));

			ImGui::BeginChild("ProjectList", ImVec2(0, 350), true);

			if (m_DiscoveredProjects.empty())
			{
				ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), " No project directories found inside 'Projects/'");
			}
			else
			{
				for (const auto& projectName : m_DiscoveredProjects)
				{
					ImGui::PushID(projectName.c_str());

					if (ImGui::Button(projectName.c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 10, 50)))
					{
						m_SelectedProject = projectName;
						m_TransitionTriggered = true;
					}

					ImGui::PopID();
					ImGui::Spacing();
				}
			}
			ImGui::EndChild();
			ImGui::PopStyleColor();
			ImGui::PopStyleVar();

			// Right column panel
			ImGui::TableSetColumnIndex(1);
			ImGui::Indent(20.0f);

			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.03f, 0.03f, 0.06f, 0.6f));
			ImGui::BeginChild("ActionsPanel", ImVec2(0, 350), true);

			ImGui::Text("Quick Actions");
			ImGui::Separator(); ImGui::Spacing();

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.3f, 0.15f, 1.0f));
			if (ImGui::Button("+ Create New Project Wizard", ImVec2(220, 40)))
			{
				m_StatusMessage = "Project generator placeholder initialized.";
			}
			ImGui::PopStyleColor();

			ImGui::EndChild();
			ImGui::PopStyleColor();

			ImGui::EndTable();
		}

		// Footer
		ImGui::SetCursorPosY(viewport->Size.y - 45.0f);
		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.9f, 1.0f), "Engine Pipeline Status: %s", m_StatusMessage.c_str());

		ImGui::End();

		if (m_TransitionTriggered)
		{
			Application::Get().TransitionFromLauncherToWorkspace(m_SelectedProject + ".dll");
		}
	}

	void LauncherLayer::ScanForProjects()
	{
		m_DiscoveredProjects.clear();

		std::string searchPath = "*.dll";
		WIN32_FIND_DATAA findData;
		HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);

		if (hFind != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
				{
					std::string fileName = findData.cFileName;

					// --- ADDED FILTER LOGIC ---
					// Skip the engine binary itself and any other files you don't want to load
					if (fileName == "Cosmic.dll" || fileName == "Renderer.dll")
					{
						continue;
					}
					// --------------------------

					size_t lastDot = fileName.find_last_of(".");
					if (lastDot != std::string::npos)
					{
						std::string rawProjectName = fileName.substr(0, lastDot);
						m_DiscoveredProjects.push_back(rawProjectName);
					}
				}
			} while (FindNextFileA(hFind, &findData));
			FindClose(hFind);
		}
	}
}