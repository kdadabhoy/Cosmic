#include "LauncherLayer.h"
#include "core/Application.h"
#include "renderer/RenderCommand.h"
#include "renderer/Renderer2D.h"
#include "camera/OrthographicCamera.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <Windows.h>
#include <shlobj.h> // Required for native folder selection dialog
#include <filesystem>
#include <fstream>
#include "core/Log.h"

namespace Cosmic
{
    // ===================================================================
    // 1. CONSTRUCTOR & CORE LIFECYCLE (Restored & Fixed)
    // ===================================================================
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

    // ===================================================================
    // 2. FILE SCANNING ENGINE (Restored & Fixed)
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

                    // Skip engine core binaries
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
    std::string BrowseFolder()
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

    void WriteFileContents(const std::filesystem::path& filepath, const std::string& content)
    {
        std::ofstream file(filepath);
        if (file.is_open())
        {
            file << content;
            file.close();
        }
    }

    void GenerateProjectTemplate(const std::string& baseDir, const std::string& projName)
    {
        std::filesystem::path rootPath = std::filesystem::path(baseDir) / projName;
        std::filesystem::path srcPath = rootPath / "src";
        std::filesystem::path assetsPath = rootPath / "assets";

        std::filesystem::create_directories(srcPath);
        std::filesystem::create_directories(assetsPath);

        std::string engineSDKPath = std::filesystem::current_path().string();
        std::replace(engineSDKPath.begin(), engineSDKPath.end(), '\\', '/');

        // CMakeLists.txt Template
        std::string cmakeContent =
            "cmake_minimum_required(VERSION 3.21)\n"
            "project(" + projName + " LANGUAGES C CXX)\n\n"
            "set(CMAKE_CXX_STANDARD 20)\n"
            "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
            "set(CMAKE_CXX_EXTENSIONS OFF)\n\n"
            "if(MSVC)\n"
            "    add_compile_options(/utf-8)\n"
            "else()\n"
            "    add_compile_options(-Wall -Wextra)\n"
            "endif()\n\n"
            "if(WIN32)\n"
            "    add_definitions(-DWIN32_LEAN_AND_MEAN)\n"
            "    add_definitions(-DNOMINMAX)\n"
            "endif()\n\n"
            "set(COSMIC_SDK_DIR \"" + engineSDKPath + "\" CACHE PATH \"Engine workspace target anchor\")\n\n"
            "file(GLOB_RECURSE PROJ_SOURCES \"src/*.cpp\" \"src/*.h\")\n\n"
            "if(NOT TARGET Cosmic)\n"
            "    list(APPEND PROJ_SOURCES\n"
            "        \"${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui.cpp\"\n"
            "        \"${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui_draw.cpp\"\n"
            "        \"${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui_widgets.cpp\"\n"
            "        \"${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui/imgui_tables.cpp\"\n"
            "        \"${COSMIC_SDK_DIR}/Cosmic/dependencies/implot/implot.cpp\"\n"
            "        \"${COSMIC_SDK_DIR}/Cosmic/dependencies/implot/implot_items.cpp\"\n"
            "    )\n"
            "endif()\n\n"
            "add_library(" + projName + " SHARED ${PROJ_SOURCES})\n\n"
            "target_include_directories(" + projName + " PRIVATE\n"
            "    src\n"
            "    ${COSMIC_SDK_DIR}/Cosmic/src\n"
            "    ${COSMIC_SDK_DIR}/Cosmic/dependencies/imgui\n"
            "    ${COSMIC_SDK_DIR}/Cosmic/dependencies/implot\n"
            "    ${COSMIC_SDK_DIR}/Cosmic/dependencies/glm\n"
            "    ${COSMIC_SDK_DIR}/Cosmic/dependencies/entt/src\n"
            "    ${COSMIC_SDK_DIR}/Cosmic/dependencies/spdlog/include\n"
            ")\n\n"
            "if(TARGET Cosmic)\n"
            "    target_link_libraries(" + projName + " PRIVATE Cosmic)\n"
            "else()\n"
            "    add_library(CosmicCore_Imported SHARED IMPORTED)\n"
            "    if(WIN32)\n"
            "        set_target_properties(CosmicCore_Imported PROPERTIES\n"
            "            IMPORTED_IMPLIB_DEBUG   \"${COSMIC_SDK_DIR}/build/Runtime/Debug/Cosmic.lib\"\n"
            "            IMPORTED_LOCATION_DEBUG \"${COSMIC_SDK_DIR}/build/Runtime/Debug/Cosmic.dll\"\n"
            "            IMPORTED_IMPLIB_RELEASE   \"${COSMIC_SDK_DIR}/build/Runtime/Release/Cosmic.lib\"\n"
            "            IMPORTED_LOCATION_RELEASE \"${COSMIC_SDK_DIR}/build/Runtime/Release/Cosmic.dll\"\n"
            "            MAP_IMPORTED_CONFIG_MINSIZEREL   \"Release\"\n"
            "            MAP_IMPORTED_CONFIG_RELWITHDEBINFO \"Release\"\n"
            "        )\n"
            "    else()\n"
            "        set_target_properties(CosmicCore_Imported PROPERTIES\n"
            "            IMPORTED_LOCATION \"${COSMIC_SDK_DIR}/build/Runtime/Debug/libCosmic${CMAKE_SHARED_LIBRARY_SUFFIX}\"\n"
            "        )\n"
            "    endif()\n"
            "    target_link_libraries(" + projName + " PRIVATE CosmicCore_Imported)\n"
            "endif()\n\n"
            "set_target_properties(" + projName + " PROPERTIES\n"
            "    RUNTIME_OUTPUT_DIRECTORY \"${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>\"\n"
            "    LIBRARY_OUTPUT_DIRECTORY \"${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>\"\n"
            ")\n\n"
            "if(EXISTS \"${CMAKE_CURRENT_SOURCE_DIR}/assets\")\n"
            "    add_custom_command(TARGET " + projName + " POST_BUILD\n"
            "        COMMAND ${CMAKE_COMMAND} -E copy_directory\n"
            "        \"${CMAKE_CURRENT_SOURCE_DIR}/assets\"\n"
            "        \"${COSMIC_SDK_DIR}/build/Runtime/$<CONFIG>/assets/projects/" + projName + "\"\n"
            "        COMMENT \"Syncing " + projName + " game assets to sandbox output directory...\"\n"
            "    )\n"
            "endif()\n";

        WriteFileContents(rootPath / "CMakeLists.txt", cmakeContent);

        // build.bat Template
        std::string batchContent =
            "@echo off\n"
            "SETLOCAL EnableDelayedExpansion\n"
            "CLS\n"
            "echo ======================================================\n"
            "echo             Cosmic Engine - Project Module: " + projName + "\n"
            "echo ======================================================\n\n"
            ":: 1. Smart MSVC Environment Detection\n"
            "set \"VS_PATH=\"\n"
            "if exist \"%ProgramFiles(x86)%\\Microsoft Visual Studio\\Installer\\vswhere.exe\" (\n"
            "    for /f \"usebackq tokens=*\" %%i in (`\"%ProgramFiles(x86)%\\Microsoft Visual Studio\\Installer\\vswhere.exe\" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (set \"VS_PATH=%%i\")\n"
            ")\n\n"
            "if defined VS_PATH (\n"
            "    if exist \"!VS_PATH!\\Common7\\Tools\\VsDevCmd.bat\" (\n"
            "        echo [STAGE 0] Initializing MSVC Environment...\n"
            "        call \"!VS_PATH!\\Common7\\Tools\\VsDevCmd.bat\" -arch=x64\n"
            "    )\n"
            ")\n\n"
            ":: 2. Smart SDK Detection Setup\n"
            "if \"%COSMIC_SDK%\"==\"\" (\n"
            "    set \"COSMIC_SDK=" + engineSDKPath + "\"\n"
            ")\n\n"
            "echo [INFO] Environment Context Pathing Resolved To: %COSMIC_SDK%\n\n"
            "if not exist build mkdir build\n"
            "cd build\n\n"
            "if not exist CMakeCache.txt (\n"
            "    echo [STAGE 1] Configuring CMake...\n"
            "    cmake .. -DCOSMIC_SDK_DIR=\"%COSMIC_SDK%\"\n"
            ")\n\n"
            "echo [STAGE 2] Building Game Module DLL...\n"
            "cmake --build . --config Debug --parallel\n\n"
            "if %ERRORLEVEL% NEQ 0 (\n"
            "    echo [ERROR] CMake Build Failed! Check compilation logs above.\n"
            "    pause\n"
            "    exit /b %ERRORLEVEL%\n"
            ")\n\n"
            "echo SUCCESS: Game Module Updated!\n"
            "pause\n"
            "ENDLOCAL\n";

        WriteFileContents(rootPath / "build.bat", batchContent);

        // Header Template (.h)
        std::string headerContent =
            "#pragma once\n"
            "#include <core/Layer.h>\n\n"
            "namespace Workspace\n"
            "{\n"
            "    class " + projName + " : public Cosmic::Layer\n"
            "    {\n"
            "    public:\n"
            "        " + projName + "();\n"
            "        virtual ~" + projName + "() = default;\n\n"
            "        virtual void OnAttach() override;\n"
            "        virtual void OnUpdate(float ts) override;\n"
            "        virtual void OnImGuiRender() override;\n"
            "    };\n"
            "}\n";

        WriteFileContents(srcPath / (projName + ".h"), headerContent);

        // Implementation Template (.cpp)
        std::string cppContent =
            "#include \"" + projName + ".h\"\n"
            "#include <Cosmic.h>\n"
            "#include <imgui.h>\n"
            "#include <utils/FileSystem.h>\n\n"
            "namespace Workspace\n"
            "{\n"
            "    " + projName + "::" + projName + "()\n"
            "        : Layer(\"" + projName + "\")\n"
            "    {\n"
            "        Cosmic::FileSystem::SetActiveProject(\"" + projName + "\");\n"
            "    }\n\n"
            "    void " + projName + "::OnAttach()\n"
            "    {\n"
            "        CS_INFO(\"" + projName + " Attached successfully!\");\n"
            "    }\n\n"
            "    void " + projName + "::OnUpdate(float ts)\n"
            "    {\n"
            "    }\n\n"
            "    void " + projName + "::OnImGuiRender()\n"
            "    {\n"
            "        ImGui::Begin(\"" + projName + " Toolset\");\n"
            "        ImGui::Text(\"Hello Cosmic Sandbox Engine!\");\n"
            "        ImGui::End();\n"
            "    }\n"
            "}\n\n"
            "extern \"C\"\n"
            "{\n"
            "    __declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context)\n"
            "    {\n"
            "        ImGui::SetCurrentContext(context.ImGuiCtx);\n"
            "        ImPlot::SetCurrentContext(context.ImPlotCtx);\n"
            "    }\n\n"
            "    __declspec(dllexport) Cosmic::Layer* CreatePluginLayer()\n"
            "    {\n"
            "        return new Workspace::" + projName + "();\n"
            "    }\n"
            "}\n";

        WriteFileContents(srcPath / (projName + ".cpp"), cppContent);
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

        // Header Layout
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

            // Right column panel
            ImGui::TableSetColumnIndex(1);
            ImGui::Indent(20.0f);

            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.03f, 0.03f, 0.06f, 0.6f));
            ImGui::BeginChild("ActionsPanel", ImVec2(0, 350), true);

            ImGui::Text("Quick Actions");
            ImGui::Separator(); ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.3f, 0.15f, 1.0f));

            // WIZARD EXECUTION TRIGGER BUTTON
            if (ImGui::Button("+ Create New Project Wizard", ImVec2(220, 40)))
            {
                std::string selectedDir = BrowseFolder();
                if (!selectedDir.empty())
                {
                    m_TargetGenerationPath = selectedDir;
                    ImGui::OpenPopup("Project Creation Wizard");
                }
            }
            ImGui::PopStyleColor();

            // RENDER DYNAMIC CREATION MODAL OVERLAY
            if (ImGui::BeginPopupModal("Project Creation Wizard", NULL, ImGuiWindowFlags_AlwaysAutoResize))
            {
                static char nameBuffer[128] = "MyCosmicSimulation";
                ImGui::Text("Configure Blueprint Attributes");
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::Text("Destination Directory: %s", m_TargetGenerationPath.c_str());
                ImGui::InputText("Project App Name", nameBuffer, IM_ARRAYSIZE(nameBuffer));

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (ImGui::Button("Generate Template", ImVec2(140, 30)))
                {
                    std::string finalName = nameBuffer;
                    if (!finalName.empty())
                    {
                        GenerateProjectTemplate(m_TargetGenerationPath, finalName);
                        m_StatusMessage = "Successfully generated project: " + finalName;
                        ScanForProjects(); // Refresh the launcher workspace options list
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(140, 30)))
                {
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();

            ImGui::EndTable();
        }

        // Footer Pipeline Bar
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