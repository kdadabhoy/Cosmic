#pragma once
// Application.h
// Last Modified 5/24/2026

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
#include <glm/glm.hpp>
#include <memory>
#include <string>

// Forward-declare HMODULE to avoid pulling Windows.h into every file that includes Application.h.
// HMODULE is defined as DECLARE_HANDLE(HMODULE) which expands to: struct HMODULE__*
#if defined(_WIN32) && !defined(_WINDOWS_)
struct HINSTANCE__;
typedef struct HINSTANCE__* HMODULE;
#endif

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
		inline WorkspaceLayer*			GetWorkspaceLayer()					{ return m_WorkspaceLayer; }

		// Viewport bounds in GLFW window-space pixels (top-left of rendered image content).
		// Delegates to WorkspaceLayer; returns zero vectors when no workspace is active.
		glm::vec2			GetViewportPos()  const;
		glm::vec2			GetViewportSize() const;


		/////////////////////////////////////////////////////////////////////////////////
		// Time & Step Control (Mutators) 
		/////////////////////////////////////////////////////////////////////////////////

		void			UseFixedTimeStep(bool useFixedTimeStep)		{ m_UseFixedTimestep = useFixedTimeStep; }
		void			SetTimeScale(float timescale)				{ m_TimeScale = timescale; }
		float			GetTimeScale() const						{ return m_TimeScale; }

		inline float	GetAbsoluteTime() const						{ return m_AbsoluteTime; } // seconds


		/////////////////////////////////////////////////////////////////////////////////
		// UI & Application State
		/////////////////////////////////////////////////////////////////////////////////

		inline ImGuiLayer*			GetImGuiLayer()			{ return m_ImGuiLayer.get(); }
		void						Close()					{ m_Running = false; }


	private:
		/////////////////////////////////////////////////////////////////////////////////
		// Internal Event Handlers & Initialization
		/////////////////////////////////////////////////////////////////////////////////

		void		Initialize();
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

		const static int				DEFAULT_WIDTH		 = 1280;
		const static int				DEFAULT_HEIGHT		 = 720;
		const std::string				DEFAULT_WINDOW_TITLE = "Cosmic Engine";

		float			m_TimeScale		= 1.0f;
		float			m_AbsoluteTime  = 0.0f;


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
		HMODULE				m_PluginHandle = nullptr;  // typed as HMODULE — communicates "loaded DLL handle" clearly
		Layer*				m_ActivePluginLayer = nullptr;
		bool				m_PendingReturnToLauncher = false;

	};

	/////////////////////////////////////////////////////////////////////////////////
	// Client Application Entry-Point Hook
	/////////////////////////////////////////////////////////////////////////////////
	Application* CreateApplication();

}