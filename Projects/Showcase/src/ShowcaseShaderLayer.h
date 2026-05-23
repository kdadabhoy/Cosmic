#pragma once
// ShowcaseShaderLayer.h

#include "IShowcaseMode.h"
#include <filesystem>

namespace Showcase
{
	class ShowcaseShaderLayer : public IShowcaseMode
	{
	public:
		ShowcaseShaderLayer(const std::string& shaderDirectory);
		virtual ~ShowcaseShaderLayer() = default;

		virtual const std::string& GetName() const override
		{
			static std::string s_Name = "Shader Browser";
			return s_Name;
		}

		virtual void OnUpdate(float ts) override;
		virtual void OnRender() override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Cosmic::Event& e) override;
		virtual void SetViewportSize(float w, float h) override;

	private:
		void ScanShaderDirectory();
		void LoadShader(const std::string& filepath);
		bool OnKeyPressed(Cosmic::KeyPressedEvent& e);

	private:
		std::string m_ShaderDirectory;
		Cosmic::OrthographicCameraController m_Camera;

		std::vector<std::string> m_ShaderPaths;
		std::vector<std::string> m_ShaderNames;

		Cosmic::Ref<Cosmic::Material> m_Material;
		int m_SelectedIndex = -1;

		float m_AccumulatedTime = 0.0f;
		glm::vec2 m_ViewportSize = { 1280.0f, 720.0f };

		bool m_LoadError = false;
		std::string m_ErrorMsg;
	};
}