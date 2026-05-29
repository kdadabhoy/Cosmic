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

#ifndef COSMIC_DIST
		// Template generation
		std::string BrowseFolder();
		void GenerateProjectTemplate(const std::string& baseDir, const std::string& projName);
		void WriteFileContents(const std::filesystem::path& filepath, const std::string& content);
#endif

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

#ifndef COSMIC_DIST
		// Template wizard state
		std::string              m_TargetGenerationPath;
#endif

		glm::vec4 m_ShaderColor = glm::vec4(0.06f, 0.16f, 0.14f, 1.0f); // Default elegant emerald sky accent
		glm::vec4 m_MountainColor = glm::vec4(0.04f, 0.08f, 0.11f, 1.0f); // Default dark navy mountain color
	};
}