#include "LauncherLayer.h"
#include "core/Application.h"
#include "renderer/RenderCommand.h"
#include "renderer/Renderer2D.h"
#include "camera/OrthographicCamera.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <Windows.h>
#include <shlobj.h> 
#include <filesystem>
#include <fstream>
#include "core/Log.h"
#include <sstream>
#include <algorithm>

namespace fs = std::filesystem;

namespace Cosmic
{
	// ===================================================================
	// HELPER FUNCTIONS FOR DECOUPLED FILE TEMPLATES
	// ===================================================================

	std::string ReadAndProcessTemplate(const fs::path& templateFilePath, const std::string& projectName, const std::string& engineSDKPath)
	{
		if (!fs::exists(templateFilePath))
		{
			CS_CORE_ERROR("Template reading failure: Source file does not exist at '{0}'", templateFilePath.string());
			return "";
		}

		std::ifstream in(templateFilePath, std::ios::in | std::ios::binary);
		if (!in.is_open())
		{
			CS_CORE_ERROR("Template reading failure: Failed to open stream for '{0}'", templateFilePath.string());
			return "";
		}

		std::stringstream buffer;
		buffer << in.rdbuf();
		std::string content = buffer.str();

		size_t pos;
		while ((pos = content.find("TemplateProject")) != std::string::npos)
		{
			content.replace(pos, 15, projectName);
		}

		while ((pos = content.find("ENGINE_SDK_PATH_TOKEN")) != std::string::npos)
		{
			content.replace(pos, 21, engineSDKPath);
		}

		return content;
	}

	void LauncherLayer::WriteFileContents(const std::filesystem::path& filepath, const std::string& content)
	{
		if (content.empty())
		{
			CS_CORE_ERROR("Write aborted: Intended payload for '{0}' is completely empty!", filepath.string());
			return;
		}

		std::ofstream file(filepath);
		if (file.is_open())
		{
			file << content;
			file.close();
			CS_CORE_INFO("Successfully synchronized file creation: {0}", filepath.string());
		}
		else
		{
			CS_CORE_ERROR("Write failure: Unable to open target location '{0}' for stream output.", filepath.string());
		}
	}

	// ===================================================================
	// 1. CONSTRUCTOR & CORE LIFECYCLE
	// ===================================================================
	LauncherLayer::LauncherLayer()
		: Layer("LauncherLayer"), m_Camera(-8.0f, 8.0f, -4.5f, 4.5f), m_ScanTimer(0.0f)
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
		m_ScanTimer += deltaTime;
		if (m_ScanTimer >= 2.0f)
		{
			ScanForProjects();
			m_ScanTimer = 0.0f;
		}

		RenderCommand::SetClearColor({ 0.02f, 0.02f, 0.04f, 1.0f });
		RenderCommand::Clear();

		if (m_BackgroundTexture)
		{
			Renderer2D::BeginScene(m_Camera);
			Renderer2D::DrawQuad({ 0.0f, 0.0f, -0.1f }, { 16.0f, 9.0f }, m_BackgroundTexture);
			Renderer2D::EndScene();
		}
	}

	// ===================================================================
	// 2. FILE SCANNING ENGINE
	// ===================================================================
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

					if (fileName == "Cosmic.dll" || fileName == "Renderer.dll")
					{
						continue;
					}

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

	// ===================================================================
	// 3. PROJECT GENERATOR UTILITIES
	// ===================================================================
	std::string LauncherLayer::BrowseFolder()
	{
		std::string resultPath = "";
		BROWSEINFOA bi = { 0 };
		bi.lpszTitle = "Select the directory where your new project folder will be created:";
		bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

		LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
		if (pidl != 0)
		{
			char path[MAX_PATH];
			if (SHGetPathFromIDListA(pidl, path))
			{
				resultPath = path;
			}
			CoTaskMemFree(pidl);
		}
		return resultPath;
	}

	void LauncherLayer::GenerateProjectTemplate(const std::string& baseDir, const std::string& projName)
	{
		fs::path rootPath = fs::path(baseDir) / projName;

		if (fs::exists(rootPath))
		{
			m_StatusMessage = "[ERROR] Generation aborted: Directory already exists!";
			CS_CORE_ERROR("Project generation failed: {0} already exists.", rootPath.string());
			return;
		}

		fs::path engineSDKDir = fs::current_path();
		if (engineSDKDir.filename() == "Debug" || engineSDKDir.filename() == "Release")
		{
			engineSDKDir = engineSDKDir.parent_path().parent_path().parent_path();
		}
		else if (engineSDKDir.filename() == "Runtime")
		{
			engineSDKDir = engineSDKDir.parent_path().parent_path();
		}

		std::string engineSDKPath = engineSDKDir.string();
		std::replace(engineSDKPath.begin(), engineSDKPath.end(), '\\', '/');

		fs::path templateRoot = engineSDKDir / "Cosmic" / "templates" / "ExampleProject";

		if (!fs::exists(templateRoot))
		{
			m_StatusMessage = "[ERROR] Template source directory not found inside SDK tree!";
			CS_CORE_ERROR("Failed to generate project: Template path missing at '{0}'", templateRoot.string());
			return;
		}

		fs::path srcPath = rootPath / "src";
		fs::path assetsPath = rootPath / "assets";

		fs::create_directories(srcPath);
		fs::create_directories(assetsPath);

		std::string cmakeContent = ReadAndProcessTemplate(templateRoot / "CMakeLists.txt", projName, engineSDKPath);
		std::string batchContent = ReadAndProcessTemplate(templateRoot / "build.bat", projName, engineSDKPath);
		std::string headerContent = ReadAndProcessTemplate(templateRoot / "src" / "TemplateProject.h", projName, engineSDKPath);
		std::string cppContent = ReadAndProcessTemplate(templateRoot / "src" / "TemplateProject.cpp", projName, engineSDKPath);

		if (cmakeContent.empty() || batchContent.empty() || headerContent.empty() || cppContent.empty())
		{
			m_StatusMessage = "[ERROR] Project files parsing failure. Check core engine log files.";
			CS_CORE_ERROR("Project structure parsing failed: One or more critical template components generated completely empty vectors.");
			return;
		}

		WriteFileContents(rootPath / "CMakeLists.txt", cmakeContent);
		WriteFileContents(rootPath / "build.bat", batchContent);
		WriteFileContents(srcPath / (projName + ".h"), headerContent);
		WriteFileContents(srcPath / (projName + ".cpp"), cppContent);

		fs::path templateShaderSrc = templateRoot / "assets" / "shaders" / "FireShader.glsl";
		if (fs::exists(templateShaderSrc))
		{
			fs::create_directories(assetsPath / "shaders");
			fs::copy_file(templateShaderSrc, assetsPath / "shaders" / "FireShader.glsl", fs::copy_options::overwrite_existing);
		}

		m_StatusMessage = "Successfully generated project: " + projName + ". Running compilation pipeline...";
		CS_CORE_INFO("Project workspace configuration complete. Initializing background compilation process.");

		fs::path batchPath = rootPath / "build.bat";
		std::string command = "cmd.exe /c \"" + batchPath.string() + "\"";

		STARTUPINFOA si;
		PROCESS_INFORMATION pi;
		ZeroMemory(&si, sizeof(si));
		si.cb = sizeof(si);
		ZeroMemory(&pi, sizeof(pi));

		if (CreateProcessA(NULL, command.data(), NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, rootPath.string().c_str(), &si, &pi))
		{
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);
		}
		else
		{
			m_StatusMessage = "[WARNING] Project created, but build.bat failed to initialize execution environment.";
			CS_CORE_WARN("Build System Warning: Win32 Environment Engine was unable to launch build process process handle.");
		}
	}

	// ===================================================================
	// 4. IMGUI UI LAYER RENDER LOOP
	// ===================================================================
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

		ImGui::Spacing(); ImGui::Spacing();
		ImGui::Indent(40.0f);
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 1.0f, 1.0f), "C O S M I C   E N G I N E");
		ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Project Hub System Launcher");
		ImGui::Separator();
		ImGui::Spacing();

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
				ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), " No project assemblies found in directory workspace root.");
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

			ImGui::TableSetColumnIndex(1);
			ImGui::Indent(20.0f);

			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.03f, 0.03f, 0.06f, 0.6f));
			ImGui::BeginChild("ActionsPanel", ImVec2(0, 350), true);

			ImGui::Text("Quick Actions");
			ImGui::Separator(); ImGui::Spacing();

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.3f, 0.15f, 1.0f));
			if (ImGui::Button("+ Create New Project Wizard", ImVec2(220, 40)))
			{
				// DEDUCE ENGINE SDK DIRECTORY ROOT 
				fs::path engineSDKDir = fs::current_path();
				if (engineSDKDir.filename() == "Debug" || engineSDKDir.filename() == "Release")
				{
					engineSDKDir = engineSDKDir.parent_path().parent_path().parent_path();
				}
				else if (engineSDKDir.filename() == "Runtime")
				{
					engineSDKDir = engineSDKDir.parent_path().parent_path();
				}

				// DETERMINE DEFAULT TARGET GENERATION SUB-FOLDER
				fs::path defaultProjectsFolder = engineSDKDir / "Projects";

				// Keep the runtime sturdy: construct directory if missing
				if (!fs::exists(defaultProjectsFolder))
				{
					fs::create_directories(defaultProjectsFolder);
				}

				m_TargetGenerationPath = defaultProjectsFolder.string();
				ImGui::OpenPopup("Project Creation Wizard");
			}
			ImGui::PopStyleColor();

			ImGui::Spacing();

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.3f, 1.0f));
			if (ImGui::Button("Sync Directories (Scan Workspace)", ImVec2(220, 30)))
			{
				ScanForProjects();
				m_StatusMessage = "Manual workspace assembly tree scan finished.";
			}
			ImGui::PopStyleColor();

			ImGui::Spacing();

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.12f, 0.12f, 1.0f));
			if (ImGui::Button("Exit Engine", ImVec2(220, 30)))
			{
				Application::Get().Close();
			}
			ImGui::PopStyleColor();

			if (ImGui::BeginPopupModal("Project Creation Wizard", NULL, ImGuiWindowFlags_AlwaysAutoResize))
			{
				static char nameBuffer[128] = "MyCosmicSimulation";
				static std::string modalError = "";

				ImGui::Text("Configure Blueprint Attributes");
				ImGui::Separator();
				ImGui::Spacing();

				// DISPLAY PRE-DEDUCED ROUTE ALONGSIDE THE MANUAL SEARCH OVERRIDE
				ImGui::Text("Target Root: %s", m_TargetGenerationPath.c_str());
				ImGui::SameLine();
				if (ImGui::Button("Browse..."))
				{
					std::string manualSelection = BrowseFolder();
					if (!manualSelection.empty())
					{
						m_TargetGenerationPath = manualSelection;
					}
				}

				ImGui::InputText("Project App Name", nameBuffer, IM_ARRAYSIZE(nameBuffer));

				if (!modalError.empty())
				{
					ImGui::Spacing();
					ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), modalError.c_str());
				}

				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();

				if (ImGui::Button("Generate Template", ImVec2(140, 30)))
				{
					std::string finalName = nameBuffer;
					if (!finalName.empty())
					{
						// 1. Check if the project name matches a project that is already compiled/synced
						auto it = std::find(m_DiscoveredProjects.begin(), m_DiscoveredProjects.end(), finalName);
						if (it != m_DiscoveredProjects.end())
						{
							modalError = "Error: A project named '" + finalName + "' is already synced!";
							CS_CORE_ERROR("Project Wizard Aborted: Validation checking failed. A project named '{0}' is already tracked in active layout.", finalName);
						}
						else
						{
							// 2. Fallback check: Does the physical folder already exist at the target path?
							fs::path checkPath = fs::path(m_TargetGenerationPath) / finalName;

							if (fs::exists(checkPath))
							{
								modalError = "Error: Folder '" + finalName + "' already exists at destination!";
								CS_CORE_ERROR("Project Wizard Aborted: Local validation failed. Destination subdirectory already exists on disk: '{0}'", checkPath.string());
							}
							else
							{
								// Clear errors and run generation pipeline safely
								modalError = "";
								GenerateProjectTemplate(m_TargetGenerationPath, finalName);
								ImGui::CloseCurrentPopup();
							}
						}
					}
					else
					{
						modalError = "Error: Project name cannot be empty!";
						CS_CORE_ERROR("Project Wizard Aborted: Target verification failed. Given configuration project string parameter was blank.");
					}
				}

				ImGui::SameLine();
				if (ImGui::Button("Cancel", ImVec2(140, 30)))
				{
					modalError = "";
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			ImGui::EndChild();
			ImGui::PopStyleColor();

			ImGui::EndTable();
		}

		ImGui::SetCursorPosY(viewport->Size.y - 45.0f);
		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.9f, 1.0f), "Engine Pipeline Status: %s", m_StatusMessage.c_str());

		ImGui::End();

		if (m_TransitionTriggered)
		{
			Application::Get().TransitionFromLauncherToWorkspace(m_SelectedProject + ".dll");
		}
	}
}