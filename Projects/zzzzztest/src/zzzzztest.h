#pragma once
#include <Cosmic.h>
#include <vector>
#include <memory>
#include <string>

// TODO: Want to showcase
// - Multiple camera views 
// - Sprites

// Forward declarations
namespace Workspace
{
	class TemplateRenderLayer;
	class TemplateSimLayer;
}

// ============================================================================
// zzzzztest — Root Manager Layer
//
// Architecture mirrors ShowcaseProject: this layer owns and drives two
// child layers internally. Neither child is pushed onto the engine
// LayerStack — they are driven exclusively by this root layer.
// ============================================================================
namespace Workspace
{
	class zzzzztest : public Cosmic::Layer
	{
	public:
		zzzzztest();
		virtual ~zzzzztest() override = default;

		virtual void OnAttach()                          override;
		virtual void OnDetach()                          override;
		virtual void OnUpdate(float ts)                  override;
		virtual void OnFixedUpdate(float deltaFixedTime) override;
		virtual void OnImGuiRender()                     override;
		virtual void OnEvent(Cosmic::Event& e)           override;

	private:
		// -----------------------------------------------------------------------
		// Child layers — owned here, NOT pushed onto the engine LayerStack.
		// Driven manually through this root layer's hooks.
		// -----------------------------------------------------------------------
		std::vector<std::shared_ptr<Cosmic::Layer>> m_Modes;
		int m_ActiveModeIndex = 0;

		// -----------------------------------------------------------------------
		// Shared assets — created once, passed by Ref<> to child layers
		// -----------------------------------------------------------------------
		Cosmic::Ref<Cosmic::Scene>    m_Scene;
		Cosmic::Ref<Cosmic::Material> m_SharedMaterial;
		std::string                   m_ShaderDir;
	};
}