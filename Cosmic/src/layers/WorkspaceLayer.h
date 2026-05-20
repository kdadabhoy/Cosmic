#pragma once

// WorkspaceLayer.h
// Last Modified: 5/19/2026

#include "Cosmic.h"
#include <glm/glm.hpp>

namespace Cosmic
{
	class WorkspaceLayer : public Cosmic::Layer
	{
	public:
		WorkspaceLayer();
		virtual ~WorkspaceLayer() = default;

		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnUpdate(float ts) override;
		virtual void OnFixedUpdate(float deltaFixedTime) override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Cosmic::Event& e) override;

		// --- Project-Agnostic Viewport Layer Management ---
		inline void SetViewportLayer(Cosmic::Layer* layer) { m_ClientViewportLayer = layer; }
		inline void ClearViewportLayer() { m_ClientViewportLayer = nullptr; }
		inline bool HasViewportLayer() const { return m_ClientViewportLayer != nullptr; }

	private:
		Cosmic::Layer* m_ClientViewportLayer = nullptr;

		// UI Component Layout Tracking Properties
		glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
		bool m_ViewportFocused = false;
		bool m_ViewportHovered = false;
	};
}