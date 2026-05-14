#pragma once
// Application.h
// Last Modified 5/14/2026

/**
 * General Description:
 * The Application class serves as the singleton "Host" and central controller of the
 * Cosmic Engine. It manages the application lifecycle, including initialization,
 * the main execution loop (heartbeat), and shutdown. It owns the primary window
 * context, the layer stack, and the event dispatching mechanism. It provides
 * global access to engine-level subsystems and handles the synchronization between
 * variable and fixed timesteps.
 *
 * 
 * Public Function Prototypes (Pre and Post Conditions):
 *
 * 1. Application()
 *    Pre:  None.
 *    Post: The application singleton is initialized, logging is started, and subsystems
 *          (Window, Renderer, ImGui) are ready for use.
 *
 * 2. virtual ~Application()
 *    Pre:  The Application instance exists.
 *    Post: All allocated resources (Window, Layers, Framebuffers) are safely released.
 *
 * 3. void Run()
 *    Pre:  The Application has been successfully initialized.
 *    Post: Enters the continuous main loop, processing events and updates until the
 *          application is signaled to close.
 *
 * 4. void Shutdown()
 *    Pre:  The Application is currently running or initialized.
 *    Post: Stops the main loop and cleans up all engine subsystems.
 *
 * 5. void OnEvent(Event& e)
 *    Pre:  A valid Event object is passed.
 *    Post: The event is dispatched to the internal handler functions and propagated
 *          down through the LayerStack until handled.
 *
 * 6. void PushLayer(Layer* inLayer)
 *    Pre:  A valid pointer to a Layer object.
 *    Post: The layer is added to the LayerStack and initialized via OnAttach().
 *
 * 7. void PushOverlay(Layer* inOverlay)
 *    Pre:  A valid pointer to a Layer object.
 *    Post: The layer is added to the end of the LayerStack (rendered last) and initialized.
 *
 * 8. static Application& Get()
 *    Pre:  The Application singleton has been instantiated.
 *    Post: Returns a reference to the current Application instance.
 *
 * 9. Window& GetWindow()
 *    Pre:  The Window subsystem has been initialized.
 *    Post: Returns a reference to the engine's Window object.
 *
 * 10. Ref<FrameBuffer> GetFrameBuffer()
 *     Pre:  None.
 *     Post: Returns a reference-counted pointer to the primary application Framebuffer.
 *
 * 11. void UseFixedTimeStep(bool useFixedTimeStep)
 *     Pre:  None.
 *     Post: Enables or disables the fixed-interval logic update (accumulator) system.
 *
 * 12. void SetTimeScale(float timescale)
 *     Pre:  timescale should ideally be >= 0 (0.0 represents a paused state).
 *     Post: Updates the internal time multiplier affecting both variable and fixed updates.
 *
 * 13. ImGuiLayer* GetImGuiLayer()
 *     Pre:  None.
 *     Post: Returns a pointer to the current ImGui overlay layer.
 *
 * 14. void Close()
 *     Pre:  None.
 *     Post: Sets the internal running state to false, causing the application to exit
 *           on the next loop iteration.
 */

#include "core/Core.h" 
#include "core/Window.h"
#include "core/LayerStack.h"
#include "events/Event.h"       
#include "events/ApplicationEvent.h"
#include "layers/ImGuiLayer.h"
#include "graphics/FrameBuffer.h" 
#include <memory>
#include <string>

namespace Cosmic
{
	class Application
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

		inline static Application&		Get()						{ return *s_Instance; }


		///////////////////////////////
		// Subsystem Accessors 
		///////////////////////////////

		inline Window&					GetWindow()					{ return *m_Window; }
		inline Ref<FrameBuffer>         GetFrameBuffer()			{ return m_Framebuffer; }

		///////////////////////////////
		// Time & Step Control (Mutators) 
		///////////////////////////////
		void		UseFixedTimeStep(bool useFixedTimeStep)			{ m_UseFixedTimestep = useFixedTimeStep; }
		void		SetTimeScale(float timescale)					{ m_TimeScale = timescale; }

		///////////////////////////////
		// UI & Application State
		///////////////////////////////

		// Returns a raw pointer with the intent "Here is the pointer, don't try deleting it"
		inline ImGuiLayer*			GetImGuiLayer()					{ return m_ImGuiLayer.get(); } 
		void						Close()							{ m_Running = false; }



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

		///////////////////////////////
		// Application State Flags
		///////////////////////////////

		bool							m_Running = true;
		bool							m_UseFixedTimestep = true;
		bool							m_Minimized = false;

		///////////////////////////////
		// Singleton Pointer
		///////////////////////////////

		static Application*				s_Instance;

		///////////////////////////////
		// Default Configuration Constants
		///////////////////////////////

		float							m_TimeScale = 1.0f;
		const static int				DEFAULT_WIDTH = 1280;
		const static int				DEFAULT_HEIGHT = 720;
		const std::string				DEFAULT_WINDOW_TITLE = "Cosmic Engine";
	};

	///////////////////////////////
	// To be defined by the client application
	///////////////////////////////
	Application* CreateApplication();
}