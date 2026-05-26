// LauncherLayer.cpp
// Last Modified: 2026

#include "LauncherLayer.h"
#include "core/Application.h"
#include "renderer/RenderCommand.h"
#include "renderer/Renderer2D.h"
#include "camera/OrthographicCamera.h"
#include "graphics/Shader.h"
#include "layers/ImGuiLayer.h"
#include "core/Log.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <Windows.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;

namespace Cosmic
{

	// =============================================================================
	// Internal helpers — file generation
	// =============================================================================

	static std::string ReadAndProcessTemplate(
		const fs::path& templatePath,
		const std::string& projectName,
		const std::string& sdkPath)
	{
		if (!fs::exists(templatePath))
		{
			CS_CORE_ERROR("LauncherLayer: Template missing at '{0}'", templatePath.string());
			return "";
		}

		std::ifstream in(templatePath, std::ios::in | std::ios::binary);
		if (!in.is_open())
		{
			CS_CORE_ERROR("LauncherLayer: Cannot open template '{0}'", templatePath.string());
			return "";
		}

		std::stringstream buf;
		buf << in.rdbuf();
		std::string content = buf.str();

		// Token replacement
		auto replaceAll = [](std::string& str, const std::string& from, const std::string& to)
			{
				size_t pos = 0;
				while ((pos = str.find(from, pos)) != std::string::npos)
				{
					str.replace(pos, from.size(), to);
					pos += to.size();
				}
			};

		replaceAll(content, "TemplateProject", projectName);
		replaceAll(content, "ENGINE_SDK_PATH_TOKEN", sdkPath);

		// Normalise line endings to \r\n
		std::string out;
		out.reserve(static_cast<size_t>(content.size() * 1.05));
		for (size_t i = 0; i < content.size(); ++i)
		{
			if (content[i] == '\r')
			{
				out += "\r\n";
				if (i + 1 < content.size() && content[i + 1] == '\n') ++i;
			}
			else if (content[i] == '\n')
			{
				out += "\r\n";
			}
			else
			{
				out += content[i];
			}
		}
		return out;
	}

	// =============================================================================

	void LauncherLayer::WriteFileContents(const fs::path& filepath, const std::string& content)
	{
		if (content.empty())
		{
			CS_CORE_ERROR("LauncherLayer: Aborting write — empty content for '{0}'", filepath.string());
			return;
		}

		std::ofstream file(filepath, std::ios::out | std::ios::binary);
		if (file.is_open())
		{
			file.write(content.c_str(), static_cast<std::streamsize>(content.size()));
			CS_CORE_INFO("LauncherLayer: Wrote '{0}'", filepath.string());
		}
		else
		{
			CS_CORE_ERROR("LauncherLayer: Failed to write '{0}'", filepath.string());
		}
	}

	// =============================================================================
	// Construction & Lifecycle
	// =============================================================================

	LauncherLayer::LauncherLayer()
		: Layer("LauncherLayer")
		, m_Camera(-8.0f, 8.0f, -4.5f, 4.5f)
	{
	}

	void LauncherLayer::OnAttach()
	{
		ImGuiLayer::SetTheme(ImGuiTheme::CosmicEmerald);

		// Try to build an animated background material from the built-in launcher shader.
		// Falls back gracefully to a plain clear colour if the shader is absent.
		const std::string shaderPath = "assets/shaders/Launcher.glsl";
		if (fs::exists(shaderPath))
		{
			auto shader = Shader::Create(shaderPath);
			if (shader)
			{
				m_BgMaterial = Material::Create(shader, "LauncherBg");
				m_BgMaterial->Set("u_Color", glm::vec4(0.18f, 0.42f, 0.72f, 1.0f));
				CS_CORE_INFO("LauncherLayer: Background shader loaded.");
			}
		}
		else
		{
			CS_CORE_WARN("LauncherLayer: Launcher.glsl not found - falling back to solid clear colour.");
		}

		ScanForProjects();
	}

	void LauncherLayer::OnDetach()
	{
		m_BgMaterial.reset();
	}

	// =============================================================================
	// Per-frame update
	// =============================================================================

	void LauncherLayer::OnUpdate(float dt)
	{
		m_BgTime += dt;

		// Auto-rescan every 2 s
		m_ScanTimer += dt;
		if (m_ScanTimer >= 2.0f) { ScanForProjects(); m_ScanTimer = 0.0f; }

		RenderBackground(dt);
	}

	void LauncherLayer::RenderBackground(float /*dt*/)
	{
		// Dark base clear
		RenderCommand::SetClearColor({ 0.04f, 0.04f, 0.07f, 1.0f });
		RenderCommand::Clear();

		Renderer2D::BeginScene(m_Camera);

		if (m_BgMaterial)
		{
			// Full-screen animated background quad
			m_BgMaterial->Set("u_Time", m_BgTime);
			Renderer2D::DrawQuad({ 0.0f, 0.0f, -0.5f }, { 16.0f, 9.0f }, m_BgMaterial);
		}
		else
		{
			// Fallback: dark backdrop + subtle grid + a few decorative SDF circles
			const glm::vec4 gridCol = { 0.10f, 0.10f, 0.15f, 1.0f };
			for (float x = -8.0f; x <= 8.0f; x += 1.0f)
				Renderer2D::DrawLine({ x, -4.5f, -0.4f }, { x,  4.5f, -0.4f }, gridCol);
			for (float y = -4.5f; y <= 4.5f; y += 1.0f)
				Renderer2D::DrawLine({ -8.0f, y, -0.4f }, { 8.0f, y, -0.4f }, gridCol);
		}

		// Decorative pulsing rings — always drawn regardless of shader availability
		float p1 = 1.0f + std::sin(m_BgTime * 0.7f) * 0.06f;
		float p2 = 1.0f + std::sin(m_BgTime * 0.5f + 1.2f) * 0.08f;
		float p3 = 1.0f + std::sin(m_BgTime * 0.9f + 2.4f) * 0.05f;

		// Outer glow
		Renderer2D::DrawCircle({ 0.0f, -0.3f, -0.3f }, glm::vec2(12.0f * p1), { 0.15f, 0.35f, 0.65f, 0.10f }, 1.0f, 0.30f);
		// Mid ring
		Renderer2D::DrawCircle({ 0.0f, -0.3f, -0.29f }, glm::vec2(8.5f * p2), { 0.20f, 0.55f, 0.90f, 0.18f }, 0.02f, 0.005f);
		// Inner ring
		Renderer2D::DrawCircle({ 0.0f, -0.3f, -0.28f }, glm::vec2(5.8f * p3), { 0.25f, 0.65f, 1.00f, 0.22f }, 0.015f, 0.004f);
		// Core dot
		Renderer2D::DrawCircle({ 0.0f, -0.3f, -0.27f }, { 1.2f, 1.2f }, { 0.30f, 0.75f, 1.00f, 0.12f }, 1.0f, 0.20f);

		// Accent corner lines
		const glm::vec4 ac = { 0.25f, 0.60f, 0.95f, 0.40f };
		Renderer2D::DrawLine({ -7.5f, -4.0f, -0.2f }, { -5.5f, -4.0f, -0.2f }, ac);
		Renderer2D::DrawLine({ -7.5f, -4.0f, -0.2f }, { -7.5f, -2.5f, -0.2f }, ac);
		Renderer2D::DrawLine({ 7.5f,  4.0f, -0.2f }, { 5.5f,  4.0f, -0.2f }, ac);
		Renderer2D::DrawLine({ 7.5f,  4.0f, -0.2f }, { 7.5f,  2.5f, -0.2f }, ac);
		Renderer2D::DrawLine({ -7.5f,  4.0f, -0.2f }, { -5.5f,  4.0f, -0.2f }, ac);
		Renderer2D::DrawLine({ -7.5f,  4.0f, -0.2f }, { -7.5f,  2.5f, -0.2f }, ac);
		Renderer2D::DrawLine({ 7.5f, -4.0f, -0.2f }, { 5.5f, -4.0f, -0.2f }, ac);
		Renderer2D::DrawLine({ 7.5f, -4.0f, -0.2f }, { 7.5f, -2.5f, -0.2f }, ac);

		Renderer2D::EndScene();
	}

	// =============================================================================
	// ImGui UI
	// =============================================================================

	void LauncherLayer::OnImGuiRender()
	{
		ImGuiViewport* vp = ImGui::GetMainViewport();

		// Full-screen transparent host window
		ImGui::SetNextWindowPos(vp->WorkPos);
		ImGui::SetNextWindowSize(vp->WorkSize);
		ImGui::SetNextWindowViewport(vp->ID);

		ImGuiWindowFlags hostFlags =
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
			ImGuiWindowFlags_NoBackground;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("##CosmicLauncher", nullptr, hostFlags);
		ImGui::PopStyleVar(3);

		// -----------------------------------------------------------------------
		// Centre panel — fixed size, centred on screen
		// -----------------------------------------------------------------------
		const float panelW = std::min(vp->WorkSize.x * 0.72f, 860.0f);
		const float panelH = std::min(vp->WorkSize.y * 0.78f, 580.0f);
		const float panelX = vp->WorkPos.x + (vp->WorkSize.x - panelW) * 0.5f;
		const float panelY = vp->WorkPos.y + (vp->WorkSize.y - panelH) * 0.5f;

		ImGui::SetNextWindowPos({ panelX, panelY }, ImGuiCond_Always);
		ImGui::SetNextWindowSize({ panelW, panelH }, ImGuiCond_Always);

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 22.0f));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.06f, 0.10f, 0.92f));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.22f, 0.48f, 0.80f, 0.60f));

		ImGui::Begin("##LauncherPanel", nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoScrollbar);

		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(3);

		// Header
		ImGui::Spacing();
		ImGui::TextColored({ 0.30f, 0.72f, 1.00f, 1.0f }, "COSMIC ENGINE");
		ImGui::SameLine(0.0f, 12.0f);
		ImGui::TextDisabled("  Project Launcher");
		ImGui::Separator();
		ImGui::Spacing();

		// Two-column layout
		const float leftW = panelW * 0.56f - 40.0f;
		const float rightW = panelW - leftW - 80.0f;

		ImGui::Columns(2, "LauncherCols", false);
		ImGui::SetColumnWidth(0, leftW);

		// ---- Left: project list ----
		ImGui::TextDisabled("Discovered Projects");
		ImGui::Spacing();

		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.03f, 0.04f, 0.07f, 0.85f));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.18f, 0.35f, 0.60f, 0.50f));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);

		const float listH = panelH - 180.0f;
		ImGui::BeginChild("##ProjectList", { leftW - 4.0f, listH }, true);

		if (m_DiscoveredProjects.empty())
		{
			ImGui::Spacing();
			ImGui::TextDisabled("  No project assemblies found.");
			ImGui::TextDisabled("  Build a project and place the .dll");
			ImGui::TextDisabled("  next to CosmicApp.exe.");
		}
		else
		{
			for (const auto& name : m_DiscoveredProjects)
			{
				ImGui::PushID(name.c_str());

				// Highlight selected
				bool sel = (m_SelectedProject == name);
				if (sel)
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.44f, 0.80f, 1.0f));
				else
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.12f, 0.20f, 1.0f));

				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.50f, 0.88f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.36f, 0.70f, 1.0f));
				ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);

				if (ImGui::Button(name.c_str(), { ImGui::GetContentRegionAvail().x, 42.0f }))
				{
					m_SelectedProject = name;
					m_TransitionTriggered = true;
				}

				ImGui::PopStyleVar();
				ImGui::PopStyleColor(3);
				ImGui::Spacing();
				ImGui::PopID();
			}
		}

		ImGui::EndChild();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar();

		// Rescan button beneath the list
		ImGui::Spacing();
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.18f, 0.30f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.28f, 0.46f, 1.0f));
		if (ImGui::Button("Rescan Directory", { leftW - 4.0f, 28.0f }))
		{
			ScanForProjects();
			m_StatusMessage = "Workspace rescanned.";
		}
		ImGui::PopStyleColor(2);

		// ---- Right: actions ----
		ImGui::NextColumn();
		ImGui::SetColumnWidth(1, rightW);
		ImGui::Spacing();
		ImGui::TextDisabled("Actions");
		ImGui::Spacing();

		// Create new project
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.38f, 0.18f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.12f, 0.52f, 0.26f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.06f, 0.28f, 0.13f, 1.0f));
		if (ImGui::Button("+ New Project", { rightW - 4.0f, 40.0f }))
		{
			// Default target = SDK_ROOT/Projects/
			fs::path sdkDir = fs::current_path();
			// Walk up if inside build/Runtime/Debug
			for (const char* name : { "Debug", "Release", "Runtime", "build" })
			{
				if (sdkDir.filename() == name) sdkDir = sdkDir.parent_path();
			}
			fs::path projDir = sdkDir / "Projects";
			if (!fs::exists(projDir)) fs::create_directories(projDir);
			m_TargetGenerationPath = projDir.string();
			ImGui::OpenPopup("##ProjectWizard");
		}
		ImGui::PopStyleColor(3);

		ImGui::Spacing();

		// Exit
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.09f, 0.09f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.48f, 0.14f, 0.14f, 1.0f));
		if (ImGui::Button("Exit", { rightW - 4.0f, 32.0f }))
			Application::Get().Close();
		ImGui::PopStyleColor(2);

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Status message
		ImGui::TextWrapped("%s", m_StatusMessage.c_str());

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// FPS
		float fps = ImGui::GetIO().Framerate;
		ImGui::TextColored({ 0.4f, 0.4f, 0.5f, 1.0f }, "%.0f fps", fps);

		ImGui::Columns(1);

		// -----------------------------------------------------------------------
		// New Project Wizard popup
		// -----------------------------------------------------------------------
		ImGui::SetNextWindowSize({ 420.0f, 0.0f }, ImGuiCond_Always);
		if (ImGui::BeginPopupModal("##ProjectWizard", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			static char nameBuf[128] = "MyProject";
			static std::string errMsg = "";

			ImGui::Text("New Project Wizard");
			ImGui::Separator();
			ImGui::Spacing();

			ImGui::Text("Target directory:");
			ImGui::TextColored({ 0.6f, 0.8f, 1.0f, 1.0f }, "%s", m_TargetGenerationPath.c_str());
			ImGui::SameLine();
			if (ImGui::SmallButton("Browse"))
			{
				std::string sel = BrowseFolder();
				if (!sel.empty()) m_TargetGenerationPath = sel;
			}

			ImGui::Spacing();
			ImGui::InputText("Project Name", nameBuf, IM_ARRAYSIZE(nameBuf));

			if (!errMsg.empty())
			{
				ImGui::Spacing();
				ImGui::TextColored({ 1.0f, 0.35f, 0.35f, 1.0f }, "%s", errMsg.c_str());
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			if (ImGui::Button("Generate", { 180.0f, 32.0f }))
			{
				std::string finalName = nameBuf;
				if (finalName.empty())
				{
					errMsg = "Name cannot be empty.";
				}
				else if (std::any_of(m_DiscoveredProjects.begin(), m_DiscoveredProjects.end(),
					[&](const std::string& p) { return p == finalName; }))
				{
					errMsg = "A project named '" + finalName + "' is already loaded.";
				}
				else if (fs::exists(fs::path(m_TargetGenerationPath) / finalName))
				{
					errMsg = "Folder already exists at target path.";
				}
				else
				{
					errMsg = "";
					GenerateProjectTemplate(m_TargetGenerationPath, finalName);
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", { 180.0f, 32.0f }))
			{
				errMsg = "";
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		ImGui::End(); // ##LauncherPanel
		ImGui::End(); // ##CosmicLauncher

		// Trigger engine transition (deferred, safe to call from ImGui)
		if (m_TransitionTriggered)
		{
			Application::Get().TransitionFromLauncherToWorkspace(m_SelectedProject + ".dll");
		}
	}

	// =============================================================================
	// File scanning
	// =============================================================================

	void LauncherLayer::ScanForProjects()
	{
		m_DiscoveredProjects.clear();

		WIN32_FIND_DATAA fd;
		HANDLE hFind = FindFirstFileA("*.dll", &fd);
		if (hFind == INVALID_HANDLE_VALUE) return;

		do
		{
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

			std::string file = fd.cFileName;
			// Skip known engine DLLs
			if (file == "Cosmic.dll" || file == "Renderer.dll") continue;

			size_t dot = file.find_last_of('.');
			if (dot != std::string::npos)
				m_DiscoveredProjects.push_back(file.substr(0, dot));

		} while (FindNextFileA(hFind, &fd));

		FindClose(hFind);

		std::sort(m_DiscoveredProjects.begin(), m_DiscoveredProjects.end());
	}

	// =============================================================================
	// Folder browser (Win32)
	// =============================================================================

	std::string LauncherLayer::BrowseFolder()
	{
		BROWSEINFOA bi = {};
		bi.lpszTitle = "Select the parent directory for the new project:";
		bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

		LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
		if (!pidl) return "";

		char path[MAX_PATH] = {};
		SHGetPathFromIDListA(pidl, path);
		CoTaskMemFree(pidl);
		return path;
	}

	// =============================================================================
	// Template generation
	// =============================================================================

	void LauncherLayer::GenerateProjectTemplate(const std::string& baseDir, const std::string& projName)
	{
		fs::path rootPath = fs::path(baseDir) / projName;

		// Safety: don't overwrite existing project
		if (fs::exists(rootPath))
		{
			m_StatusMessage = "[ERROR] Directory already exists: " + rootPath.string();
			CS_CORE_ERROR("LauncherLayer: GenerateProjectTemplate aborted — directory exists.");
			return;
		}

		// Resolve SDK root from CWD (handles Debug/Release/Runtime sub-directories)
		fs::path sdkDir = fs::current_path();
		for (const char* name : { "Debug", "Release", "Runtime", "build" })
		{
			if (sdkDir.filename() == name) sdkDir = sdkDir.parent_path();
		}

		std::string sdkPathStr = sdkDir.string();
		std::replace(sdkPathStr.begin(), sdkPathStr.end(), '\\', '/');

		fs::path templateRoot = sdkDir / "Cosmic" / "templates" / "ExampleProject";
		if (!fs::exists(templateRoot))
		{
			m_StatusMessage = "[ERROR] Template source missing — expected at Cosmic/templates/ExampleProject";
			CS_CORE_ERROR("LauncherLayer: Template root not found at '{0}'", templateRoot.string());
			return;
		}

		bool anyFailed = false;

		// -----------------------------------------------------------------------
		// Dynamic Recursive Generation
		// -----------------------------------------------------------------------
		try
		{
			for (const auto& entry : fs::recursive_directory_iterator(templateRoot))
			{
				const auto& srcPath = entry.path();

				// Compute the relative path from the template root
				fs::path relative = fs::relative(srcPath, templateRoot);
				std::string relativeStr = relative.string();

				// If the filename contains 'TemplateProject', swap it out for the actual project name
				size_t fileTokenPos = relativeStr.find("TemplateProject");
				if (fileTokenPos != std::string::npos)
				{
					relativeStr.replace(fileTokenPos, std::string("TemplateProject").length(), projName);
				}

				fs::path dstPath = rootPath / relativeStr;

				if (fs::is_directory(srcPath))
				{
					fs::create_directories(dstPath);
				}
				else if (fs::is_regular_file(srcPath))
				{
					// Ensure parent directory exists
					fs::create_directories(dstPath.parent_path());

					// Process text files that might require token replacements
					std::string ext = srcPath.extension().string();
					if (ext == ".cpp" || ext == ".h" || ext == ".txt" || ext == ".bat")
					{
						std::string content = ReadAndProcessTemplate(srcPath, projName, sdkPathStr);
						if (!content.empty())
						{
							WriteFileContents(dstPath, content);
						}
						else
						{
							anyFailed = true;
						}
					}
					else
					{
						// Binary or raw configuration files (like assets/shaders) get cleanly copied
						fs::copy_file(srcPath, dstPath, fs::copy_options::overwrite_existing);
						CS_CORE_INFO("LauncherLayer: Copied asset '{0}'", dstPath.string());
					}
				}
			}
		}
		catch (const fs::filesystem_error& e)
		{
			CS_CORE_ERROR("LauncherLayer: Filesystem exception during generation: {0}", e.what());
			anyFailed = true;
		}

		if (anyFailed)
		{
			m_StatusMessage = "[WARNING] Some template files failed — check the engine log.";
			CS_CORE_WARN("LauncherLayer: One or more template files failed processing.");
			return;
		}

		m_StatusMessage = "Generated '" + projName + "'. Run build.bat inside the new folder.";
		CS_CORE_INFO("LauncherLayer: Project '{}' generated at '{}'", projName, rootPath.string());

		// Launch build.bat in a new console window
		fs::path batchPath = rootPath / "build.bat";
		std::string cmd = "cmd.exe /c \"" + batchPath.string() + "\"";

		STARTUPINFOA si = {};
		si.cb = sizeof(si);
		PROCESS_INFORMATION pi = {};

		if (CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE,
			CREATE_NEW_CONSOLE, nullptr, rootPath.string().c_str(), &si, &pi))
		{
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);
		}
		else
		{
			m_StatusMessage += " (build.bat failed to launch — run it manually)";
			CS_CORE_WARN("LauncherLayer: Could not launch build.bat for '{}'", projName);
		}
	}

} // namespace Cosmic