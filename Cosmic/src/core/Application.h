#pragma once
// Application.h
// Last Modified 5/20/2026

/**
 * @brief Master Controller and Resource Host for the Cosmic Engine.
 *
 * The Application class serves as the root singleton execution context of the engine.
 * It drives the structural heartbeat loop (Run), intercepting global hardware signals
 * and multiplexing engine updates into asynchronous variable and deterministic fixed timesteps.
 * 
 * 
 * =================================================================================
 * CRITICAL MEMORY OWNERSHIP ARCHITECTURE & POLICY:
 * =================================================================================
 * The Application layer holds ABSOLUTE OWNERSHIP over unmanaged raw pointers (`Layer*`)
 * injected into the engine runtime. While the `LayerStack` manages execution priorities and
 * loop structures via temporary borrow mechanics, this class assumes complete, structural
 * responsibility for managing the lifecycles, safely deferred transitions, and
 * destruction of layers.
 * 
 * Dynamic runtime DLL plugins (guest workspace environments) are completely unmounted,
 * explicitly destroyed, and isolated safely inside execution loop "Safe Zones" to prevent
 * memory corruption, dangling pointer exceptions, or OpenGL state failures.
 */

#include "core/Core.h" 
#include "core/Window.h"
#include "core/LayerStack.h"
#include "events/Event.h"       
#include "events/ApplicationEvent.h"
#include "layers/ImGuiLayer.h"
#include "graphics/FrameBuffer.h" 
#include "core/Timestep.h"
#include <memory>
#include <string>

namespace Cosmic
{
	// Forward Declarations (Prevent Compiler Issues):
	class WorkspaceLayer;

	class COSMIC_API Application
	{
	public:
		/////////////////////////////////////////////////////////////////////////////////
		// Main Life Cycle & Execution 
		/////////////////////////////////////////////////////////////////////////////////

		Application();
		virtual ~Application();

		void		Run();
		void		Shutdown();
		void		OnEvent(Event& e);
		void		PushLayer(Layer* inLayer);
		void		PushOverlay(Layer* inOverlay);

		void		TransitionFromLauncherToWorkspace(const std::string& projectDllFilename);
		void		TransitionToLauncher();


		/////////////////////////////////////////////////////////////////////////////////
		// Static Accessors (Singleton)
		/////////////////////////////////////////////////////////////////////////////////

		static Application& Get();


		/////////////////////////////////////////////////////////////////////////////////
		// Subsystem Accessors 
		/////////////////////////////////////////////////////////////////////////////////

		inline Window&					GetWindow()							{ return *m_Window; }
		inline Ref<FrameBuffer>         GetFrameBuffer()					{ return m_Framebuffer; }


		/////////////////////////////////////////////////////////////////////////////////
		// Time & Step Control (Mutators) 
		/////////////////////////////////////////////////////////////////////////////////

		void		UseFixedTimeStep(bool useFixedTimeStep)		{ m_UseFixedTimestep = useFixedTimeStep; }
		void		SetTimeScale(float timescale)				{ m_TimeScale = timescale; }


		/////////////////////////////////////////////////////////////////////////////////
		// UI & Application State
		/////////////////////////////////////////////////////////////////////////////////

		inline ImGuiLayer*			GetImGuiLayer()			{ return m_ImGuiLayer.get(); }
		void						Close()					{ m_Running = false; }


	private:
		/////////////////////////////////////////////////////////////////////////////////
		// Internal Event Handlers & Initialization
		/////////////////////////////////////////////////////////////////////////////////

		bool		Initialize();
		bool		OnWindowClose(WindowCloseEvent& e);
		bool		OnWindowResize(WindowResizeEvent& e);


	private:
		/////////////////////////////////////////////////////////////////////////////////
		// Core Engine Subsystems
		/////////////////////////////////////////////////////////////////////////////////

		Scope<Window>					m_Window;
		Scope<ImGuiLayer>				m_ImGuiLayer;
		LayerStack						m_LayerStack;
		Ref<FrameBuffer>                m_Framebuffer;


		/////////////////////////////////////////////////////////////////////////////////
		// Application State Flags
		/////////////////////////////////////////////////////////////////////////////////

		bool							m_Running = true;
		bool							m_UseFixedTimestep = true;
		bool							m_Minimized = false;


		/////////////////////////////////////////////////////////////////////////////////
		// Singleton Pointer
		/////////////////////////////////////////////////////////////////////////////////

		static Application* s_Instance;


		/////////////////////////////////////////////////////////////////////////////////
		// Dynamic Workspace / Project State Strings
		/////////////////////////////////////////////////////////////////////////////////

		std::string                     m_PendingProjectDLL = "";


		/////////////////////////////////////////////////////////////////////////////////
		// Default Configuration Constants
		/////////////////////////////////////////////////////////////////////////////////

		float							m_TimeScale		= 1.0f;
		const static int				DEFAULT_WIDTH	= 1280;
		const static int				DEFAULT_HEIGHT	= 720;
		const std::string				DEFAULT_WINDOW_TITLE = "Cosmic Engine";


	private:
		/////////////////////////////////////////////////////////////////////////////////
		// Dynamic Project DLL Assembly Linking & Unlinking Subsystems
		/////////////////////////////////////////////////////////////////////////////////

		/**
		 * @brief Maps a client compiled module into the host memory space and hooks engine endpoints.
		 */
		void LoadProjectDLL(const std::string& filepath);

		/**
		 * @brief Deletes dynamic client layers and safely clears dynamic window linkages.
		 */
		void UnloadProjectDLL();

		/**
		 * @brief Dispatches synchronous WindowResize signals to re-dock and align UI layout dimensions.
		 */
		void SynchronizeRenderingState();


	private:
		/////////////////////////////////////////////////////////////////////////////////
		// Dynamic Guest Module Allocation Handlers
		/////////////////////////////////////////////////////////////////////////////////

		WorkspaceLayer*		m_WorkspaceLayer = nullptr;
		void*				m_PluginHandle = nullptr;
		Layer*				m_ActivePluginLayer = nullptr;
		bool				m_PendingReturnToLauncher = false;
	};

	/////////////////////////////////////////////////////////////////////////////////
	// Client Application Entry-Point Hook
	/////////////////////////////////////////////////////////////////////////////////
	Application* CreateApplication();

}