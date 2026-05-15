#pragma once

#include "../../Simulation.h"
#include "graphics/Material.h"

namespace Workspace
{
	class DinoStressLayer : public Simulation
	{
	public:
		DinoStressLayer(Cosmic::Ref<Cosmic::Material> material);
		virtual ~DinoStressLayer() = default;

		virtual void OnUpdate(float ts) override;
		virtual void OnFixedUpdate(float deltaFixedTime) override; 
		virtual void OnRender() override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Cosmic::Event& e) override { m_Cam.OnEvent(e); }
		virtual void SetViewportSize(float w, float h) override { m_Cam.OnResize(w, h); }

	private:
		Cosmic::Ref<Cosmic::Material> m_Material;
		Cosmic::OrthographicCameraController m_Cam;
		int m_GridSize = 40;
		uint32_t m_FixedUpdateCount = 0; // For tracking sim stability
	};
}