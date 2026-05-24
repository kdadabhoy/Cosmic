#pragma once
#include <Cosmic.h>
#include <vector>
#include <string>
#include <filesystem>

namespace Showcase
{
	class ShowcaseShaderLayer : public Cosmic::Layer
	{
	public:
		ShowcaseShaderLayer(const std::string& shaderDirectory);
		virtual ~ShowcaseShaderLayer() override = default;

		// Standard engine lifecycle overrides
		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnUpdate(float ts) override;
		virtual void OnFixedUpdate(float deltaFixedTime) override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Cosmic::Event& e) override;

	private:
		void ScanShaderDirectory();
		void LoadShader(const std::string& filepath);
		bool OnKeyPressed(Cosmic::KeyPressedEvent& e);
		bool OnWindowResize(Cosmic::WindowResizeEvent& e);

	private:
		std::string m_ShaderDirectory;
		Cosmic::OrthographicCameraController m_Camera;

		std::vector<std::string> m_ShaderPaths;
		std::vector<std::string> m_ShaderNames;

		Cosmic::Ref<Cosmic::Material> m_Material;
		int m_SelectedIndex = -1;

		glm::vec2 m_ViewportSize = { 1280.0f, 720.0f };

		bool m_LoadError = false;
		std::string m_ErrorMsg;
	};
}