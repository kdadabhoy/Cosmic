#pragma once
#include "../../Simulation.h"

namespace Workspace
{
	class DinoStressLayer : public Simulation
	{
	public:
		DinoStressLayer(Cosmic::Ref<Cosmic::Texture2D> tex);
		void OnUpdate(float ts) override;
		void OnRender() override;
		void OnImGuiRender() override;
		void SetViewportSize(float w, float h) override { m_Cam.OnResize(w, h); }
	private:
		Cosmic::Ref<Cosmic::Texture2D> m_Tex;
		Cosmic::OrthographicCameraController m_Cam;
		int m_GridSize = 40;
	};
}