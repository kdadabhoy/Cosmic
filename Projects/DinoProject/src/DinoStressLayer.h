#pragma once
#include "ISimulationMode.h"
#include <vector>

namespace Workspace
{
	class DinoStressLayer : public ISimulationMode
	{
	public:
		DinoStressLayer(Cosmic::Ref<Cosmic::Scene> scene, Cosmic::Ref<Cosmic::Material> material);
		virtual ~DinoStressLayer() = default;

		virtual void OnUpdate(float ts) override;
		virtual void OnFixedUpdate(float deltaFixedTime) override;
		virtual void OnRender() override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Cosmic::Event& e) override { m_Cam.OnEvent(e); }
		virtual void SetViewportSize(float w, float h) override { m_Cam.OnResize(w, h); }

	public:
		virtual const std::string& GetName() const override
		{
			static std::string name = "DinoStressLayer";
			return name;
		}

	private:
		void RegenerateGrid();

	private:
		Cosmic::Ref<Cosmic::Scene> m_Scene;
		Cosmic::Ref<Cosmic::Material> m_Material;
		Cosmic::OrthographicCameraController m_Cam;

		std::vector<Cosmic::Entity> m_GridEntities;
		int m_GridSize = 25; // Balanced size for ECS iteration tracking
		uint32_t m_FixedUpdateCount = 0;
		float m_Time = 0.0f; // <-- ADD THIS LINE
	};
}