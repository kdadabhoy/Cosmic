#pragma once

// Application.h
// Last Modified 5/19/2026

/**
 * General Description:
 * The Application class serves as the singleton "Host" and central controller of the
 * Cosmic Engine. It manages the application lifecycle, including initialization,
 * the main execution loop (heartbeat), and shutdown. It owns the primary window
 * context, the layer stack, and the event dispatching mechanism. It provides
 * global access to engine-level subsystems and handles the synchronization between
 * variable and fixed timesteps.
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

 // Forward declaration of the generalized engine layout shell context
namespace Workspace { class WorkspaceLayer; }

namespace Cosmic
{
	class COSMIC_API Application
	{
	public:
		////////////////////////////////
		// Main Life Cycle & Execution 
		///////////////////////////////

		Application();
		virtual ~Application();

		void		Run();
		void		Shutdown();
		void		OnEvent(Event& e);
		void		PushLayer(Layer* inLayer);
		void		PushOverlay(Layer* inOverlay);


		///////////////////////////////
		// Static Accessors (Singleton)
		///////////////////////////////

		static Application&			Get();

		///////////////////////////////
		// Subsystem Accessors 
		///////////////////////////////

		inline Window& GetWindow() { return *m_Window; }
		inline Ref<FrameBuffer>         GetFrameBuffer() { return m_Framebuffer; }


		///////////////////////////////
		// Time & Step Control (Mutators) 
		///////////////////////////////

		void		UseFixedTimeStep(bool useFixedTimeStep) { m_UseFixedTimestep = useFixedTimeStep; }
		void		SetTimeScale(float timescale) { m_TimeScale = timescale; }


		///////////////////////////////
		// UI & Application State
		///////////////////////////////

		inline ImGuiLayer* GetImGuiLayer() { return m_ImGuiLayer.get(); }
		void						Close() { m_Running = false; }


	private:
		///////////////////////////////
		// Internal Event Handlers & Initialization
		///////////////////////////////

		bool		Initialize();
		bool		OnWindowClose(WindowCloseEvent& e);
		bool		OnWindowResize(WindowResizeEvent& e);


	private:
		///////////////////////////////
		// Core Engine Subsystems
		///////////////////////////////

		Scope<Window>					m_Window;
		Scope<ImGuiLayer>				m_ImGuiLayer;
		LayerStack						m_LayerStack;
		Ref<FrameBuffer>                m_Framebuffer;

		// Project independent tracking reference for the editor shell interface layout
		Workspace::WorkspaceLayer* m_WorkspaceLayer = nullptr;

		///////////////////////////////
		// Application State Flags
		///////////////////////////////

		bool							m_Running = true;
		bool							m_UseFixedTimestep = true;
		bool							m_Minimized = false;


		///////////////////////////////
		// Singleton Pointer
		///////////////////////////////

		static Application* s_Instance;


		///////////////////////////////
		// Default Configuration Constants
		///////////////////////////////

		float							m_TimeScale = 1.0f;
		const static int				DEFAULT_WIDTH = 1280;
		const static int				DEFAULT_HEIGHT = 720;
		const std::string				DEFAULT_WINDOW_TITLE = "Cosmic Engine";


	private:
		void LoadProjectDLL(const std::string& filepath);
		void UnloadProjectDLL();

		// --- Dynamic Guest Module Allocation Data Handlers ---
		void* m_PluginHandle = nullptr;            // Holds raw OS Win32 HMODULE pointer
		Cosmic::Layer* m_ActivePluginLayer = nullptr; // Live project plugin runtime layer pointer
		char m_DLLPathBuffer[256] = "DinoProject.dll";
	};

	///////////////////////////////
	// To be defined by the client application
	///////////////////////////////
	Application* CreateApplication();
}