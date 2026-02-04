#include "core/Application.h"
#include "renderer/Renderer.h"
#include "renderer/RenderCommand.h"
#include "core/Timestep.h"

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
		s_Instance = this;

		if (!Initialize())
		{
			std::cout << "Cosmic: Failed to initialize application!" << std::endl;
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
		// 1. Create the window 
		m_Window = std::make_unique<Window>(DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_WINDOW_TITLE);

		// 2. Bind events to the OnEvent function
		m_Window->SetEventCallback(GLCORE_BIND_EVENT_FN(Application::OnEvent));

		// 3. Initialize the Renderer (Dispatcher and API)
		Renderer::Init();

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

	void Application::Run()
	{
		float lastFrameTime = 0.0f;

		float accumulator = 0.0f;
		float fixedDeltaTime = 1.0f / 60.0f;

		while (m_Running && !m_Window->ShouldClose())
		{
			// 0.1 Poll Events:
			m_Window->PollEvents();

			// 0.2 Calculate Delta Time:
			float time = (float)glfwGetTime();				//glfwGetTime returns seconds	// Should make this platform agnostic at some point
			Timestep rawTimestep = time - lastFrameTime;
			lastFrameTime = time;

			// 0.3 Skipping the loop if minimized:
			if (m_Minimized)
			{
				continue;									// jumps back to top of while loop
			}



			// 1A. Fixed Timestep Case (Simulations):
			if (m_UseFixedTimestep)
			{
				float frameTime = rawTimestep.GetSeconds();

				// Caps the physics "catch-up" time to prevent the CPU from freezing during major lag spikes.
				if (frameTime > 0.25f)
				{
					frameTime = 0.25f;
				}

				accumulator += (frameTime * m_TimeScale);
				while (accumulator >= fixedDeltaTime)
				{
					for (Layer* layer : m_LayerStack)
					{
						layer->OnFixedUpdate(Timestep(fixedDeltaTime));
					}
					accumulator -= fixedDeltaTime;
				}
			}
			

			// 1B. Variable Timestep... Always runs so we can have smooth camera... but physics stuff in layers should use OnFixedUpdate instead of OnUpdate
			Timestep scaledTimestep = rawTimestep.GetSeconds() * m_TimeScale;
			for (Layer* layer : m_LayerStack)
			{
				layer->OnUpdate(scaledTimestep);
			}

		

			// 2. Rendering:
			RenderCommand::Clear(0.1f, 0.1f, 0.1f);

			for (Layer* layer : m_LayerStack)
			{
				layer->OnRender();
			}
			


			// 3. UI Rendering:
			m_ImGuiLayer->Begin();
			for (Layer* layer : m_LayerStack)
			{
				layer->OnImGuiRender();
			}
			m_ImGuiLayer->End();



			// 4. Update Window:
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
		// Handle minimization (don't render if window is 0x0)
		if (e.GetWidth() == 0 || e.GetHeight() == 0)
		{
			m_Minimized = true;
			return false;
		}

		m_Minimized = false;

		// Update the Graphics API Viewport
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