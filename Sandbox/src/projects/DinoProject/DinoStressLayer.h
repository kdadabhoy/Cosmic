#pragma once
#include "../../Simulation.h"
#include "graphics/Material.h"

namespace Workspace
{
	class DinoStressLayer : public Simulation
	{
	public:
		// Changed: Now accepts Material Ref
		DinoStressLayer(Cosmic::Ref<Cosmic::Material> material);

		void OnUpdate(float ts) override;
		void OnRender() override;
		void OnImGuiRender() override;
		void SetViewportSize(float w, float h) override { m_Cam.OnResize(w, h); }

	private:
		Cosmic::Ref<Cosmic::Material> m_Material; // Changed from Texture2D
		Cosmic::OrthographicCameraController m_Cam;
		int m_GridSize = 40;
	};
}