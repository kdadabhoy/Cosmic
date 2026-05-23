#pragma once

#include "core/Layer.h"
#include "graphics/Texture.h" 
#include "camera/OrthographicCamera.h"
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

		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnUpdate(float deltaTime) override;
		virtual void OnImGuiRender() override;

	private:
		// Core Engine Hub Utilities
		void ScanForProjects();
		std::string BrowseFolder();
		void GenerateProjectTemplate(const std::string& baseDir, const std::string& projName);
		void WriteFileContents(const std::filesystem::path& filepath, const std::string& content);

	private:
		float m_ScanTimer = 0.0f;

		std::vector<std::string> m_DiscoveredProjects;
		std::string m_StatusMessage = "Ready to launch.";
		std::string m_SelectedProject = "";
		bool m_TransitionTriggered = false;

		Ref<Texture2D> m_BackgroundTexture;
		OrthographicCamera m_Camera;

		std::string m_TargetGenerationPath = "";
	};
}