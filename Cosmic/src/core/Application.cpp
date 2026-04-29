#include "core/Application.h"
#include "renderer/Renderer.h"
#include "renderer/RenderCommand.h"
#include "core/Timestep.h"
#include "graphics/FrameBuffer.h"

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

			// 2. Rendering into Framebuffer
			m_Framebuffer->Bind();
			RenderCommand::Clear(0.1f, 0.1f, 0.1f);

			for (Layer* layer : m_LayerStack)
			{
				layer->OnRender();
			}
			m_Framebuffer->Unbind();

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