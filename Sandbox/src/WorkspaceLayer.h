#pragma once

// WorkspaceLayer.h
// Last Modified 5/14/2026

/**
 * General Description:
 * 
 * The WorkspaceLayer is the "Conductor" of the Cosmic Engineering Suite. It acts
 * as the primary Host interface (The Editor) that manages the lifecycle of
 * individual engineering simulations (The Clients).
 * 
 * 
 * Why does this file exist?
 * While the Engine handles the heavy lifting (rendering, input, logging), the
 * WorkspaceLayer provides the high-level logic for the Editor experience. It
 * manages the ImGui DockSpace, captures the simulation's output into a Viewport
 * window, and handles the hot-swapping of different projects (like switching
 * from a Dino simulation to a Structural Analysis project) at runtime.
 * 
 * Note for Beginners:
 * 
 * This is the "Shell" of your workspace. If you want to change how the editor
 * looks (add a new menu or a new panel), you work here. If you want to change
 * how a specific simulation behaves, you should look into the 'projects/'
 * folder and the Simulation interface instead.
 * 
 * 
 * Public Function Prototypes (Pre and Post Conditions):
 * 
 * 1. void OnUpdate(float ts)
 * Pre:  A project has been loaded into m_ActiveSim.
 * Post: Forwards the engine's heartbeat to the active simulation for physics
 * and logic processing.
 * 
 * 2. void OnImGuiRender()
 * Pre:  ImGui context is initialized.
 * Post: Renders the DockSpace, the Viewport (via the Framebuffer), and the
 * Project Inspector panels.
 * 
 * 3. void LoadProject<T>() [Internal Template]
 * Pre:  T must be a class that inherits from Workspace::Simulation.
 * Post: The old project is destroyed, the new project is instantiated, and
 * the current viewport dimensions are synchronized.
 */

#include "Cosmic.h"
#include "Simulation.h"
#include <memory>

namespace Workspace
{
	class WorkspaceLayer : public Cosmic::Layer
	{
	public:
		////////////////////////////////
		// Layer Lifecycle
		///////////////////////////////

		WorkspaceLayer();
		virtual ~WorkspaceLayer() = default;

		virtual void OnAttach() override;
		virtual void OnDetach() override;

		////////////////////////////////
		// The Heartbeat (Game Loop)
		///////////////////////////////

		virtual void OnUpdate(float ts) override;
		//virtual void OnFixedUpdate(float deltaFixedTime) override; // need to implement
		virtual void OnImGuiRender() override;

		////////////////////////////////
		// Input & Interaction
		///////////////////////////////

		virtual void OnEvent(Cosmic::Event& e) override;

	private:
		////////////////////////////////
		// Project Management Helpers
		///////////////////////////////

		/**
		 * LoadProject
		 * * THE HOT-SWAPPER: This template method allows the engine to switch
		 * between entirely different engineering modules without a restart.
		 */
		template<typename T>
		void LoadProject()
		{
			// Resetting the unique_ptr automatically destroys the old simulation
			m_ActiveSim = std::make_unique<T>();

			// Ensure the new simulation knows the size of the editor window 
			// so the aspect ratio is correct immediately.
			if (m_ViewportSize.x > 0)
				m_ActiveSim->SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
		}

	private:
		////////////////////////////////
		// Active State & Simulation Logic
		///////////////////////////////

		std::unique_ptr<Simulation> m_ActiveSim;

		////////////////////////////////
		// Viewport & UI Metadata
		///////////////////////////////

		glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
		bool      m_ViewportFocused = false;
		bool      m_ViewportHovered = false;
	};
}