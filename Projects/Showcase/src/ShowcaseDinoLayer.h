#pragma once

#include <Cosmic.h>
#include <vector>
#include <string>

namespace Showcase
{
	struct DinoData
	{
		Cosmic::Entity EntityHandle;
		Cosmic::Ref<Cosmic::Texture2D> ActiveAtlas;
		Cosmic::Ref<Cosmic::SubTexture2D> SubTexture;
		glm::vec2 AtlasCoords = { 0.0f, 0.0f };
		std::string Name;
		int SelectedAtlasIndex = 0; // Tracks which file variation is selected in ImGui

		glm::vec3 BasePosition = { 0.0f, 0.0f, 0.0f };
	};

	class ShowcaseDinoLayer : public Cosmic::Layer
	{
	public:
		ShowcaseDinoLayer(Cosmic::Ref<Cosmic::Scene> scene);
		virtual ~ShowcaseDinoLayer() = default;

		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnUpdate(float ts) override;
		virtual void OnFixedUpdate(float deltaFixedTime) override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Cosmic::Event& e) override;

	private:
		bool OnWindowResize(Cosmic::WindowResizeEvent& e);
		void UpdateDinoSubTexture(size_t index);

	private:
		Cosmic::Ref<Cosmic::Scene> m_Scene;
		Cosmic::OrthographicCameraController m_CameraController;
		glm::vec2 m_ViewportSize = { 1280.0f, 720.0f };

		// Shared pool of all available texture variants loaded via VFS
		std::vector<Cosmic::Ref<Cosmic::Texture2D>> m_TexturePool;
		std::vector<std::string> m_TextureNames;

		// Array holding our 4 simulation dinos (0: Main, 1-3: Companions)
		std::vector<DinoData> m_Dinos;

		// Global configurations
		float m_MoveSpeed = 5.0f;
		glm::vec2 m_SpriteCellSize = { 24.0f, 24.0f }; // Arks Dino assets are exactly 24x24 px

		// Procedural Circle Showcase Configurations
		bool m_ShowTrackingRing = true;
		bool m_ShowBackgroundRings = true;
		glm::vec4 m_RingColor = { 0.0f, 0.95f, 0.85f, 0.85f };
		float m_BaseRingThickness = 0.08f;
		float m_BaseRingFade = 0.01f;
		float m_PulseFrequency = 4.0f;
		float m_PulseAmplitude = 0.10f;
	};
}