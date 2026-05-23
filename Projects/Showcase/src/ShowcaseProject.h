#pragma once
#include <Cosmic.h>
#include <vector>
#include <memory>
#include "IShowcaseMode.h"

namespace Showcase
{
	class ShowcaseProject : public Cosmic::Layer
	{
	public:
		ShowcaseProject();
		virtual ~ShowcaseProject() override = default;

		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnUpdate(float ts) override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Cosmic::Event& e) override;

	private:
		std::vector<std::shared_ptr<IShowcaseMode>> m_Modes;
		int m_ActiveModeIndex = 0;

		Cosmic::Ref<Cosmic::Scene> m_Scene;
		Cosmic::Ref<Cosmic::Material> m_DinoMaterial;
		std::string m_ShaderDir;
	};
}