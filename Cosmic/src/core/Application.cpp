#include "core/Application.h"
#include "renderer/Renderer.h"
#include "renderer/RenderCommand.h"
#include "core/Timestep.h"
#include "graphics/FrameBuffer.h"
#include "core/Log.h"

#include <GLFW/glfw3.h>
#include <iostream>

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	// The singleton so that other layers/classes can call the Application's getters/setters
	Application* Application::s_Instance = nullptr;

	/////////////////////////////////////////////////////////////////////////////////

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

	Application::~Application()
	{
		Shutdown();
	}

	/////////////////////////////////////////////////////////////////////////////////

	bool Application::Initialize()
	{
		// 0. Log statement
		CS_CORE_TRACE("Initializing Application Subsystems...");

		// 1. Create the window 
		m_Window = std::make_unique<Window>(DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_WINDOW_TITLE);

		// 2. Bind events to the OnEvent function
		m_Window->SetEventCallback(GLCORE_BIND_EVENT_FN(Application::OnEvent));

		// 3. Initialize the Renderer (Dispatcher and API)
		Renderer::Init();

		// --- NEW: Framebuffer Setup ---
		FramebufferSpecification fbSpec;
		fbSpec.Width = DEFAULT_WIDTH;
		fbSpec.Height = DEFAULT_HEIGHT;
		m_Framebuffer = FrameBuffer::Create(fbSpec);
		// ------------------------------

		// 4. Initialize ImGui
		m_ImGuiLayer = new ImGuiLayer();
		PushOverlay(m_ImGuiLayer);

		return true;
	}

	/////////////////////////////////////////////////////////////////////////////////

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

	/*
	 * THE CORE APPLICATION LOOP (The Engine's Heartbeat)
	 *
	 * CONCEPT: This loop follows a "Host/Client" architecture similar to Unity or Unreal.
	 *
	 * 1. TIMING: It manages both Variable Timestep (smooth visuals) and Fixed Timestep
	 *    (stable physics/logic) to ensure consistent behavior across different hardware.
	 *
	 * 2. DELEGATION (The "Why"): Notice that the main Render pass is commented out here.
	 *    In a professional Editor-based engine, the Application doesn't decide WHEN or
	 *    HOW to draw the game. Instead, it provides the heartbeat, and the "Editor Layer"
	 *    (the Host) takes over.
	 *
	 * 3. THE PIPELINE:
	 *    - Logic Update: Layers update their state (Dino movement, etc.)
	 *    - Render: The SandboxLayer binds the Framebuffer, renders the scene, and unbinds.
	 *    - UI: ImGui begins, grabs the texture from that Framebuffer, and displays it
	 *      inside the "Viewport" window.
	 *
	 * This separation prevents "Redundant Clearing" and allows for a Dockable Editor
	 * where the Game is just one of many windows being managed.
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

			// 1A. Fixed Timestep Case
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

			// 1B. Variable Timestep
			Timestep scaledTimestep = rawTimestep.GetSeconds() * m_TimeScale;
			for (Layer* layer : m_LayerStack)
			{
				layer->OnUpdate(scaledTimestep);
			}

			// THE Client should handle this... :)
			// 2. Rendering into Framebuffer
			// m_Framebuffer->Bind();
			// RenderCommand::Clear(0.1f, 0.1f, 0.1f);
			// for (Layer* layer : m_LayerStack) { layer->OnRender(); }
			// m_Framebuffer->Unbind();

			// 3. UI Rendering (Displays the Framebuffer texture)
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

	bool Application::OnWindowClose(WindowCloseEvent& e)
	{
		m_Running = false;
		return true;
	}

	/////////////////////////////////////////////////////////////////////////////////

	bool Application::OnWindowResize(WindowResizeEvent& e)
	{
		// 1. Handle minimization (don't render if window is 0x0)
		if (e.GetWidth() == 0 || e.GetHeight() == 0)
		{
			m_Minimized = true;
			return false;
		}

		m_Minimized = false;

		// 2. Resize the Framebuffer!
		// This recreates the texture at the new resolution so the game stays sharp.
		m_Framebuffer->Resize(e.GetWidth(), e.GetHeight());

		// 3. Update the Graphics API Viewport
		Renderer::OnWindowResize(e.GetWidth(), e.GetHeight());

		return false;
	}

	/////////////////////////////////////////////////////////////////////////////////

	void Application::PushLayer(Layer* inLayer)
	{
		m_LayerStack.PushLayer(inLayer);
	}

	/////////////////////////////////////////////////////////////////////////////////

	void Application::PushOverlay(Layer* inOverlay)
	{
		m_LayerStack.PushOverlay(inOverlay);
	}

	/////////////////////////////////////////////////////////////////////////////////

	void Application::Shutdown()
	{
		// GLFW/Window cleanup is handled by Scope (unique_ptr)
	}

	/////////////////////////////////////////////////////////////////////////////////

}