// Application.cpp
#include "Cosmic.h"
#include "core/Application.h"
#include "renderer/Renderer.h"
#include "renderer/RenderCommand.h"
#include "core/Timestep.h"
#include "graphics/FrameBuffer.h"
#include "core/Log.h"
#include "layers/WorkspaceLayer.h"
#include "layers/LauncherLayer.h"
#include "imgui_internal.h"


// Note: glfw3.h is kept only for glfwGetTime() in the Run() loop.
#include <GLFW/glfw3.h>

// Wrap Windows.h to isolate polluting win32 macro definitions
// Note: WIN32_LEAN_AND_MEAN removed here because it is already declared via the command line compiler flags
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	// Global pointer to the application instance allowing subsystems to access engine methods
	Application* Application::s_Instance = nullptr;

	Application& Application::Get()
	{
		return *s_Instance;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Constructor
	 * Sets up the engine singleton, initializes the core Logger, and triggers
	 * the internal subsystem initialization sequence.
	 */
	Application::Application()
		: m_Running(true), m_Minimized(false), m_UseFixedTimestep(true), m_TimeScale(1.0f), m_ImGuiLayer(nullptr)
	{
		Log::Init();
		CS_CORE_INFO("Cosmic Engine Logging Initialized");

		s_Instance = this;

		if (!Initialize())
		{
			CS_CORE_CRITICAL("Cosmic: Failed to initialize application!");
		}
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Destructor
	 * Ensures that the shutdown sequence is called to release hardware resources.
	 */
	Application::~Application()
	{
		Shutdown();
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Internal Initialization
	 * Orchestrates the creation of the Window, Renderer, Framebuffer, and ImGui.
	 * Returns: true if all subsystems started successfully.
	 */
	bool Application::Initialize()
	{
		CS_CORE_TRACE("Initializing Application Subsystems...");

		// 1. Create the window 
		m_Window = CreateScope<Window>(DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_WINDOW_TITLE);
		m_Window->SetEventCallback(GLCORE_BIND_EVENT_FN(Application::OnEvent));

		// 2. Initialize the Renderer
		Renderer::Init();

		// 3. Framebuffer Setup 
		FramebufferSpecification fbSpec;
		fbSpec.Width = DEFAULT_WIDTH;
		fbSpec.Height = DEFAULT_HEIGHT;
		m_Framebuffer = FrameBuffer::Create(fbSpec);

		// 4. Initialize ImGui Frame Layer Context Overlay
		m_ImGuiLayer = CreateScope<ImGuiLayer>();
		PushOverlay(m_ImGuiLayer.get());

		// 5. BOOT EXCLUSIVELY INTO THE LAUNCHER HUB HUB STATE
		// We completely skip mounting the editor layout or loading any projects out-of-the-box.
		PushLayer(new LauncherLayer());

		return true;
	}

	/////////////////////////////////////////////////////////////////////////////////

	void Application::TransitionFromLauncherToWorkspace(const std::string& dllPath)
	{
		m_PendingProjectDLL = dllPath; // Just cache the request
	}

	/////////////////////////////////////////////////////////////////////////////////

	void Application::TransitionToLauncher()
	{
		m_PendingReturnToLauncher = true; // Set the flag to process in the Safe Zone
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Global Event Entry Point
	 * Handles top-level window events and propagates remaining events through the LayerStack.
	 */
	void Application::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);

		// Handling "global" application events
		dispatcher.Dispatch<WindowCloseEvent>(GLCORE_BIND_EVENT_FN(Application::OnWindowClose));
		dispatcher.Dispatch<WindowResizeEvent>(GLCORE_BIND_EVENT_FN(Application::OnWindowResize));

		// Propagate events down the layer stack (top to bottom)
		for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
		{
			if (e.Handled)
			{
				break;
			}

			(*it)->OnEvent(e);
		}
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * THE CORE APPLICATION LOOP (The Engine's Heartbeat)
	 */
	void Application::Run()
	{
		float lastFrameTime = 0.0f;
		float accumulator = 0.0f;
		float fixedDeltaTime = 1.0f / 60.0f;

		while (m_Running && !m_Window->ShouldClose())
		{
			m_Window->PollEvents();

			float time = (float)glfwGetTime();
			Timestep rawTimestep = time - lastFrameTime;
			lastFrameTime = time;

			if (m_Minimized)
			{
				continue;
			}

			// 1A. Fixed Timestep Case (Physics/Deterministic Logic)
			if (m_UseFixedTimestep)
			{
				float frameTime = rawTimestep.GetSeconds();
				if (frameTime > 0.25f) frameTime = 0.25f;

				accumulator += (frameTime * m_TimeScale);
				while (accumulator >= fixedDeltaTime)
				{
					// Notify engine layers (Pushed layers handle physics routines independently)
					for (Layer* layer : m_LayerStack)
						layer->OnFixedUpdate(Timestep(fixedDeltaTime));

					accumulator -= fixedDeltaTime;
				}
			}

			// 1B. Variable Timestep (Animations/Smooth Visuals & Screen Clearing)
			Timestep scaledTimestep = rawTimestep.GetSeconds() * m_TimeScale;
			for (Layer* layer : m_LayerStack)
			{
				layer->OnUpdate(scaledTimestep);
			}

			// 2. UI Rendering Pass (The layout layers draw themselves automatically)
			m_ImGuiLayer->Begin();

			for (Layer* layer : m_LayerStack)
			{
				layer->OnImGuiRender();
			}

			m_ImGuiLayer->End();

			m_Window->SwapBuffers();

			// =================================================================
			// SAFE ZONE: No loops are running on m_LayerStack right now!
			// =================================================================
			
			////// Handle Return to Launcher Request ////// 
			if (m_PendingReturnToLauncher)
			{
				// 1. Unload the plugin first
				UnloadProjectDLL();

				if (m_WorkspaceLayer)
				{
					// Tell the layer to flag itself for cleanup. 
					// DO NOT delete it here yet!
					m_WorkspaceLayer->RequestLayoutReset();
				}

				// We do NOT call PopLayer/delete here.
				// The WorkspaceLayer will flag m_ShouldResetLayout, 
				// run its next ImGui frame to clean up, and then we need to remove it.

				m_PendingReturnToLauncher = false;
			}

			// After the UI render loop in Application::Run():
			if (m_WorkspaceLayer && m_WorkspaceLayer->IsReadyForDeletion())
			{
				m_LayerStack.PopLayer(m_WorkspaceLayer);
				delete m_WorkspaceLayer;
				m_WorkspaceLayer = nullptr;

				// Now push the launcher
				PushLayer(new LauncherLayer());
				SynchronizeRenderingState(); // a bit of a gerry-rigged way to get rid of an ImGui docking glitch
			}

			////// End Handle Return to Launcher Request ////// 


			// Handle Other Redirection Requests (go to .dll for example)
			if (!m_PendingProjectDLL.empty())
			{
				// 1. Find and pop the old LauncherLayer out of the active loop
				Layer* launcherTarget = nullptr;
				for (Layer* layer : m_LayerStack)
				{
					if (layer->GetName() == "LauncherLayer")
					{
						launcherTarget = layer;
						break;
					}
				}

				if (launcherTarget)
				{
					m_LayerStack.PopLayer(launcherTarget);
					delete launcherTarget; // Safely delete it since it's no longer being updated
				}

				// 2. Instantiate and mount the full developer Workspace environment
				m_WorkspaceLayer = new WorkspaceLayer();
				PushLayer(m_WorkspaceLayer);

				// 3. Load the project DLL and mount its viewport layout onto the workspace
				LoadProjectDLL(m_PendingProjectDLL);

				// 4. Reset the string so it waits for the next click event
				m_PendingProjectDLL = "";
			}
		}
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Window Close Handler
	 */
	bool Application::OnWindowClose(WindowCloseEvent& e)
	{
		m_Running = false;
		return true;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Window Resize Handler
	 */
	bool Application::OnWindowResize(WindowResizeEvent& e)
	{
		if (e.GetWidth() == 0 || e.GetHeight() == 0)
		{
			m_Minimized = true;
			return false;
		}

		m_Minimized = false;
		m_Framebuffer->Resize(e.GetWidth(), e.GetHeight());
		Renderer::OnWindowResize(e.GetWidth(), e.GetHeight());

		return false;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Logic Layer Registration
	 */
	void Application::PushLayer(Layer* inLayer)
	{
		m_LayerStack.PushLayer(inLayer);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Overlay Registration
	 */
	void Application::PushOverlay(Layer* inOverlay)
	{
		m_LayerStack.PushOverlay(inOverlay);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Engine Shutdown
	 *
	 * Orchestrates an explicit "soft-landing" for the engine components.
	 * 
	 * APPLICATION MEMORY OWNERSHIP POLICY:
	 * While LayerStack dictates runtime execution order and routes event logic via
	 * a borrow arrangement, the Application class retains explicit ultimate lifecycle
	 * ownership of all raw heap-allocated layers pushed into the stack.
	 * 
	 * This method sweeps through active layers to explicitly free their memory allocations
	 * while the hardware window context remains alive, preventing stray background threads,
	 * OS process leaks, or driver-level validation faults on context termination.
	 */
	void Application::Shutdown()
	{
		CS_CORE_TRACE("Shutting down Application Subsystems...");

		// 1. Unload the project DLL runtime if it's still attached
		UnloadProjectDLL();

		// 2. EXPLICITLY destroy remaining heap-allocated layers sitting in the stack
		// (This completely catches any unmanaged allocations like the bootup LauncherLayer)
		for (Layer* layer : m_LayerStack)
		{
			delete layer;
		}

		// 3. Notify remaining systems of full detachment and drop raw tracking pointer elements
		m_LayerStack.Clear();

		// 4. FORCE the ImGui layer overlay to destroy itself while context is alive
		m_ImGuiLayer.reset();

		// 5. Clean up core static graphics pipelines
		Renderer::Shutdown();

		// 6. FORCE the window to close and terminate the OpenGL Context 
		m_Window.reset();

		CS_CORE_TRACE("Application Subsystems safely terminated.");
	}

	/////////////////////////////////////////////////////////////////////////////////




	void Application::LoadProjectDLL(const std::string& filepath)
	{
		if (m_PluginHandle) UnloadProjectDLL();

		// 1. Load the DLL into Cosmic's memory space
		HMODULE handle = LoadLibraryA(filepath.c_str());
		if (!handle)
		{
			CS_CORE_ERROR("Failed to load plugin: {0}", filepath);
			return;
		}

		// 2. Find the function pointers inside the DLL
		auto initContexts = (void(*)(HostContext))GetProcAddress(handle, "InitializePluginContexts");

		// Change: The plugin export signature now drops custom abstractions 
		// and simply creates a standard engine Layer pointer.
		auto createPluginLayer = (Cosmic::Layer * (*)())GetProcAddress(handle, "CreatePluginLayer");

		if (!initContexts || !createPluginLayer)
		{
			CS_CORE_ERROR("Plugin is missing engine export signatures!");
			FreeLibrary(handle);
			return;
		}

		m_PluginHandle = handle;

		// 3. Share the exact memory address of ImGui/ImPlot contexts across boundaries
		HostContext ctx;
		ctx.ImGuiCtx = ImGui::GetCurrentContext();
		ctx.ImPlotCtx = ImPlot::GetCurrentContext();
		initContexts(ctx);

		// 4. Instantiate the plugin layer and assign it as the center layout viewport focus
		m_ActivePluginLayer = createPluginLayer();

		if (m_WorkspaceLayer)
		{
			m_WorkspaceLayer->SetViewportLayer(m_ActivePluginLayer);
			CS_CORE_INFO("Successfully loaded and mounted project DLL Layer!");
		}
		else
		{
			CS_CORE_WARN("Plugin Layer created but no active Workspace target was found to bind it to.");
		}
	}

	void Application::UnloadProjectDLL()
	{
		if (!m_PluginHandle) return;

		// 1. Decouple the canvas display safely before purging memory allocations
		if (m_WorkspaceLayer)
		{
			m_WorkspaceLayer->ClearViewportLayer();
		}

		// 2. Delete the dynamic active client layer instances safely
		if (m_ActivePluginLayer)
		{
			delete m_ActivePluginLayer;
			m_ActivePluginLayer = nullptr;
		}

		// 3. Drop library handles out of standard environment address structures
		FreeLibrary((HMODULE)m_PluginHandle);
		m_PluginHandle = nullptr;
		CS_CORE_INFO("Project DLL safely unmounted and unloaded.");
	}



	void Application::SynchronizeRenderingState()
	{
		// Query the hardware window directly
		int width, height;
		m_Window->GetSize(&width, &height);

		// Trigger the engine's internal resize pipeline
		WindowResizeEvent e(width, height);
		OnWindowResize(e);
	}

}