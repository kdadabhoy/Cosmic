#pragma once

#include "core/Layer.h"
#include "graphics/Texture.h"
#include "camera/OrthographicCamera.h"
#include "graphics/Material.h"
#include <string>
#include <vector>
#include <memory>
#include <filesystem>

namespace Cosmic
{
	class LauncherLayer : public Layer
	{
	public:
		LauncherLayer();
		virtual ~LauncherLayer() = default;

		virtual void OnAttach()             override;
		virtual void OnDetach()             override;
		virtual void OnUpdate(float dt)     override;
		virtual void OnImGuiRender()        override;

	private:
		// Project scanning
		void ScanForProjects();

		// Template generation
		std::string BrowseFolder();
		void GenerateProjectTemplate(const std::string& baseDir, const std::string& projName);
		void WriteFileContents(const std::filesystem::path& filepath, const std::string& content);

		// Background rendering helpers
		void RenderBackground(float dt);

	private:
		// Scan / state
		float                    m_ScanTimer = 0.0f;
		std::vector<std::string> m_DiscoveredProjects;
		std::string              m_StatusMessage = "Ready.";
		std::string              m_SelectedProject;
		bool                     m_TransitionTriggered = false;

		// Background rendering
		OrthographicCamera       m_Camera;
		Ref<Material>            m_BgMaterial;         // animated GLSL background
		float                    m_BgTime = 0.0f;

		// Template wizard state
		std::string              m_TargetGenerationPath;
	};
}