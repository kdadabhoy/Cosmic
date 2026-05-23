#pragma once
// WorkspaceLayer.h
// Last Modified: 5/23/2026

#include "Cosmic.h"
#include <glm/glm.hpp>

namespace Cosmic
{
	class WorkspaceLayer : public Cosmic::Layer
	{
	public:
		WorkspaceLayer();
		virtual ~WorkspaceLayer() override = default;

		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnUpdate(float ts) override;
		virtual void OnFixedUpdate(float deltaFixedTime) override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Cosmic::Event& e) override;

		// --- Project-Agnostic Viewport Layer Management ---
		// Crucial Lifecycle Hook Fixes: Promoted to source file implementations
		void SetViewportLayer(Cosmic::Layer* layer);
		void ClearViewportLayer();
		inline bool HasViewportLayer() const { return m_ClientViewportLayer != nullptr; }

		void RequestLayoutReset() { m_ShouldResetLayout = true; }
		bool IsReadyForDeletion() const { return m_ReadyForDeletion; }

	private:
		Cosmic::Layer* m_ClientViewportLayer = nullptr;

		// UI Component Layout Tracking Properties
		glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
		bool m_ViewportFocused = false;
		bool m_ViewportHovered = false;

	private:
		bool m_ShouldResetLayout = false;
		bool m_ReadyForDeletion = false;
	};
}