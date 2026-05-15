#pragma once

// ImGuiLayer.h
// Last Modified 5/14/2026

/**
 * General Description:
 * 
 * ImGuiLayer is a specialized engine layer responsible for initializing,
 * managing, and rendering the Dear ImGui user interface. It acts as the
 * bridge between the engine's event system and ImGui's internal state.
 * 
 * This layer provides the infrastructure for docking, multi-viewport
 * management (allowing ImGui windows to leave the main application window),
 * and hardware-accelerated UI rendering via OpenGL 3. It also includes
 * ImPlot integration for high-performance data visualization.
 * 
 * 
 * Public Function Prototypes (Pre and Post Conditions):
 * 
 * 1. void OnAttach()
 * Pre:  A valid GLFW window and OpenGL context exist.
 * Post: ImGui and ImPlot contexts are created, and platform/renderer
 * backends are initialized.
 * 
 * 2. void OnDetach()
 * Pre:  ImGui context was successfully initialized.
 * Post: Backends are shut down and contexts are destroyed to prevent memory leaks.
 * 
 * 3. void Begin()
 * Pre:  The layer is attached and active.
 * Post: Signals the start of a new UI frame for both GLFW and OpenGL backends.
 *
 * 4. void End()
 * Pre:  Begin() was called and UI commands were issued.
 * Post: Finalizes the frame, renders draw data, and manages platform
 * viewports (multi-window support).
 * 
 * 5. void OnEvent(Event& event)
 * Pre:  An event has been dispatched by the Application.
 * Post: If BlockEvents is enabled, the event is marked as "Handled" if
 * ImGui currently wants to capture mouse or keyboard input.
 */

#include "core/Layer.h"
#include "events/ApplicationEvent.h"
#include "events/KeyEvent.h"
#include "events/MouseEvent.h"

namespace Cosmic
{
	class ImGuiLayer : public Layer
	{
	public:
		////////////////////////////////
		// Life Cycle & Initialization
		///////////////////////////////

		ImGuiLayer();
		~ImGuiLayer();

		virtual void	OnAttach() override;
		virtual void	OnDetach() override;
		virtual void	OnEvent(Event& event) override;

		////////////////////////////////
		// Frame Control
		///////////////////////////////

		void		Begin();
		void		End();

		////////////////////////////////
		// Event Configuration
		///////////////////////////////

		void		BlockEvents(bool block) { m_BlockEvents = block; }

	private:
		////////////////////////////////
		// Internal Event Handlers
		///////////////////////////////

		bool		OnMouseButtonPressed(MouseButtonPressedEvent& e);

	private:
		bool		m_BlockEvents = true;
	};
}