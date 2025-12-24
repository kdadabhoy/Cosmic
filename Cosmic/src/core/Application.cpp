#include "core/Application.h"
#include "renderer/Renderer.h"
#include "renderer/RenderCommand.h"

#include <GLFW/glfw3.h>
#include <iostream>

namespace Cosmic
{
	Application* Application::s_Instance = nullptr;

	Application::Application()
		: m_Running(true), m_ImGuiLayer(nullptr)
	{
		s_Instance = this;

		if (!Initialize())
		{
			std::cout << "Cosmic: Failed to initialize application!" << std::endl;
		}
	}

	Application::~Application()
	{
		Shutdown();
	}

	bool Application::Initialize()
	{
		// 1. Create the window - Name matched to m_Window from header
		m_Window = std::make_unique<Window>(DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_WINDOW_TITLE);

		// 2. Bind events to our OnEvent function
		m_Window->setEventCallback(GLCORE_BIND_EVENT_FN(Application::OnEvent));

		// 3. Initialize the Renderer (Dispatcher and API)
		Renderer::Init();

		// 4. Initialize ImGui
		m_ImGuiLayer = new ImGuiLayer();
		PushOverlay(m_ImGuiLayer);

		return true;
	}

	void Application::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<WindowCloseEvent>(GLCORE_BIND_EVENT_FN(Application::OnWindowClose));
		dispatcher.Dispatch<WindowResizeEvent>(GLCORE_BIND_EVENT_FN(Application::OnWindowResize));

		// Propagate events down the layer stack (top to bottom)
		for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
		{
			if (e.Handled)
				break;
			(*it)->OnEvent(e);
		}
	}

	void Application::Run()
	{
		float lastFrameTime = 0.0f;

		while (m_Running && !m_Window->shouldClose())
		{
			// Calculate Delta Time
			float time = (float)glfwGetTime();
			float deltaTime = time - lastFrameTime;
			lastFrameTime = time;

			if (!m_Minimized)
			{
				// 1. Logic Updates
				for (Layer* layer : m_LayerStack)
					layer->OnUpdate(deltaTime);

				// 2. Rendering ("World")
				RenderCommand::Clear(0.1f, 0.1f, 0.1f);

				for (Layer* layer : m_LayerStack)
					layer->OnRender();

				// 3. UI Rendering
				m_ImGuiLayer->Begin();
				for (Layer* layer : m_LayerStack)
					layer->OnImGuiRender();
				m_ImGuiLayer->End();
			}

			// 4. Update Window
			m_Window->pollEvents();
			m_Window->swapBuffers();
		}
	}

	bool Application::OnWindowClose(WindowCloseEvent& e)
	{
		m_Running = false;
		return true;
	}

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

	void Application::PushLayer(Layer* inLayer)
	{
		m_LayerStack.PushLayer(inLayer);
	}

	void Application::PushOverlay(Layer* inOverlay)
	{
		m_LayerStack.PushOverlay(inOverlay);
	}

	void Application::Shutdown()
	{
		// GLFW/Window cleanup is handled by Scope (unique_ptr)
	}
}