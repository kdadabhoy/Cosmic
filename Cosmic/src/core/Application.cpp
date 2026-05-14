// Application.cpp
#include "core/Application.h"
#include "renderer/Renderer.h"
#include "renderer/RenderCommand.h"
#include "core/Timestep.h"
#include "graphics/FrameBuffer.h"
#include "core/Log.h"

// Note: glfw3.h is kept only for glfwGetTime() in the Run() loop.
#include <GLFW/glfw3.h>

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	// Global pointer to the application instance allowing subsystems to access engine methods
	Application* Application::s_Instance = nullptr;

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Constructor
	 * Sets up the engine singleton, initializes the core Logger, and triggers
	 * the internal subsystem initialization sequence.
	 */
	Application::Application()
		: m_Running(true), m_ImGuiLayer(nullptr)
	{
		// Initialize Log
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
		// 0. Log statement
		CS_CORE_TRACE("Initializing Application Subsystems...");

		// 1. Create the window 
		m_Window = CreateScope<Window>(DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_WINDOW_TITLE);

		// 2. Bind events to the OnEvent function
		m_Window->SetEventCallback(GLCORE_BIND_EVENT_FN(Application::OnEvent));

		// 3. Initialize the Renderer (Dispatcher and API)
		Renderer::Init();

		// 4. Framebuffer Setup 
		FramebufferSpecification fbSpec;
		fbSpec.Width = DEFAULT_WIDTH;
		fbSpec.Height = DEFAULT_HEIGHT;
		m_Framebuffer = FrameBuffer::Create(fbSpec);

		// 5. Initialize ImGui
		// We use CreateScope so the Application owns the ImGuiLayer's memory.
		m_ImGuiLayer = CreateScope<ImGuiLayer>();
		PushOverlay(m_ImGuiLayer.get());

		return true;
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
					for (Layer* layer : m_LayerStack)
						layer->OnFixedUpdate(Timestep(fixedDeltaTime));
					accumulator -= fixedDeltaTime;
				}
			}

			// 1B. Variable Timestep (Animations/Smooth Visuals)
			Timestep scaledTimestep = rawTimestep.GetSeconds() * m_TimeScale;
			for (Layer* layer : m_LayerStack)
			{
				layer->OnUpdate(scaledTimestep);
			}

			// 3. UI Rendering
			m_ImGuiLayer->Begin();
			for (Layer* layer : m_LayerStack)
			{
				layer->OnImGuiRender();
			}
			m_ImGuiLayer->End();

			m_Window->SwapBuffers();
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
	 * Manually clears layers before the Scopes go out of context to prevent
	 * dangling pointers during the destruction sequence.
	 */
	void Application::Shutdown()
	{
		// Force the layers to detach while subsystems (like Renderer) still exist.
		for (Layer* layer : m_LayerStack)
		{
			layer->OnDetach();
		}
	}

	/////////////////////////////////////////////////////////////////////////////////

}