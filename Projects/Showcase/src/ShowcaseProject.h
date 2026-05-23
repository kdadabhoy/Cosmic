#pragma once

#include <Cosmic.h>
#include <vector>
#include <memory>

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
		// Clean engine design: The workspace manages an array of actual engine layers
		std::vector<std::shared_ptr<Cosmic::Layer>> m_Modes;
		int m_ActiveModeIndex = 0;

		Cosmic::Ref<Cosmic::Scene> m_Scene;
		Cosmic::Ref<Cosmic::Material> m_DinoMaterial;
		std::string m_ShaderDir;
	};
}