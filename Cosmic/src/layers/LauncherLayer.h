#pragma once


// TODO
// Need to add a way to select projects that aren't auto-scanned... well maybe not bc projects .dlls build to the folder, so they should always get scanned
// Need to implement New Project implementation (scripting for the CMake, built, common folders, and a template project or something)
// Need to make look nicer :)



#include "core/Layer.h"
#include "graphics/Texture.h" 
#include "camera/OrthographicCamera.h"
#include <string>
#include <vector>
#include <memory>

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
		void ScanForProjects();

	private:
		std::vector<std::string> m_DiscoveredProjects;
		std::string m_StatusMessage = "Ready to launch.";
		std::string m_SelectedProject = "";
		bool m_TransitionTriggered = false;

		// Cleaned up tracking definitions
		Ref<Texture2D> m_BackgroundTexture;
		OrthographicCamera m_Camera;


		std::string m_TargetGenerationPath = "";
	};
}