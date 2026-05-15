#pragma once

// Simulation.h
// Last Modified 5/14/2026

/**
 * General Description:
 * The Simulation class is the foundational "Client" interface in the Cosmic
 * Engineering Suite's Host-Client architecture. It defines the standardized
 * contract that any engineering project must follow to be compatible with
 * the WorkspaceLayer (The Host).
 * * Why does this interface exist?
 * It allows the engine to decouple the "Editor Shell" from the "Engineering Logic."
 * By inheriting from Simulation, complex modules (such as aerodynamics,
 * structural analysis, or fluid dynamics) can be hot-swapped at runtime.
 * This ensures that the engine doesn't need to know the specifics of a project
 * to render its viewport or provide it with input.
 * * Implementation Requirements:
 * Any derived class must implement the core lifecycle methods (OnUpdate, OnRender,
 * OnImGuiRender, and SetViewportSize) to ensure the engine's heartbeat and
 * windowing systems remain synchronized with the project logic.
 */

#include "Cosmic.h"

namespace Workspace
{
	class Simulation
	{
	public:
		virtual ~Simulation() = default;

		////////////////////////////////
		// The Heartbeat (Logic & Physics)
		///////////////////////////////

		/**
		 * OnUpdate
		 * * Purpose: Handles frame-variable logic and physics.
		 * * @param ts: TimeStep (time elapsed since the last frame).
		 */
		virtual void	OnUpdate(float ts) = 0;

		/**
		 * OnFixedUpdate
		 * * Purpose: Handles constant-time physics calculations.
		 * * @param deltaFixedTime: The fixed interval (e.g., 1/60th of a second).
		 */
		virtual void	OnFixedUpdate(float deltaFixedTime) = 0;



		////////////////////////////////
		// The Visuals (Graphics)
		///////////////////////////////

		/**
		 * OnRender
		 * * Purpose: Contains all Renderer2D or Renderer3D draw calls.
		 * * Note: This is called while the Workspace's Framebuffer is bound.
		 */
		virtual void	OnRender() = 0;



		////////////////////////////////
		// The Interface (UI)
		///////////////////////////////

		/**
		 * OnImGuiRender
		 * * Purpose: Used to "inject" project-specific sliders and buttons
		 * into the Workspace's Project Inspector panel.
		 */
		virtual void	OnImGuiRender() = 0;



		////////////////////////////////
		// Synchronization & Events
		///////////////////////////////

		/**
		 * SetViewportSize
		 * * Purpose: Notifies the simulation when the ImGui Viewport panel
		 * resizes, allowing for Camera Aspect Ratio corrections.
		 */
		virtual void	SetViewportSize(float width, float height) = 0;

		/**
		 * OnEvent
		 * * Purpose: Optional override to handle keyboard, mouse, or system
		 * events within the simulation context.
		 */
		virtual void	OnEvent(Cosmic::Event& e) {};
	};
}