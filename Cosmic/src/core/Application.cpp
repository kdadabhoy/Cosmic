#include "core/Application.h"
#include "layers/MenuLayer.h"
#include "layers/ImGuiLayer.h"
#include <GLFW/glfw3.h>
#include <iostream>


namespace Cosmic {

	// --- FIX: Initialize the static instance ---
	Application* Application::s_Instance = nullptr;


	Application::Application()
		: isRunning(false), m_ImGuiLayer(nullptr)
	{
		s_Instance = this;
	}





	Application::~Application()
	{
		shutdown();
	}







	bool Application::initialize() {

		/*
			Window Initialization
		*/
		window = std::make_unique<Window>(DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_WINDOW_TITLE);

		if (!window->getHandle()) {
			std::cout << "Failed to Create a Window" << std::endl;
			return false;
		}

		window->setVSync(true);




		/*
			Event Initialization
			This tells the window to send its events to Application::OnEvent
		*/
		window->setEventCallback([this](Event& e) {
			this->OnEvent(e);
			});




		/*
			ImGui Initialization - from Documentation
		*/
		m_ImGuiLayer = new ImGuiLayer();
		m_LayerStack.PushOverlay(m_ImGuiLayer);


		isRunning = true;
		return true;
	}









	// --- EVENT DISPATCHER ---
	void Application::OnEvent(Event& e) {
		// Handle window resizing globally
		if (e.GetEventType() == EventType::WindowResize) {
			auto& re = static_cast<WindowResizeEvent&>(e);
			glViewport(0, 0, re.GetWidth(), re.GetHeight());
		}



		if (e.GetEventType() == EventType::WindowClose) {
			OnWindowClose(static_cast<WindowCloseEvent&>(e));
		}


		// Pass events through the layer stack (Overlay/ImGui is usually top)
		for (auto it = m_LayerStack.begin(); it != m_LayerStack.end(); ++it) {
			if (e.Handled) {
				break;
			}

			(*it)->OnEvent(e); // Derefernces the Layer and then calls it's OnEvent
		}
	}







	bool Application::OnWindowClose(WindowCloseEvent& e) {
		isRunning = false;
		return true; // Event handled
	}








	void Application::run() {
		// Initial State: Push the Menu Layer
		m_LayerStack.PushLayer(new MenuLayer());

		float lastFrameTime = 0.0f;

		while (isRunning && !window->shouldClose()) {
			// Calculate Delta Time
			float time = (float)glfwGetTime();
			float deltaTime = time - lastFrameTime;
			lastFrameTime = time;

			// Event Pulling:
			window->pollEvents();

			// Start ImGui
			m_ImGuiLayer->Begin();


			// Logic Updates:
			for (Layer* layer : m_LayerStack) {
				layer->OnUpdate(deltaTime);
			}


			// Rendering ("World"):
			m_Renderer.clear();
			for (Layer* layer : m_LayerStack) {
				layer->OnRender();
			}

			// UI Rendering (Screens and menus)
			for (Layer* layer : m_LayerStack) {
				layer->OnImGuiRender();
			}

			// End ImGui
			m_ImGuiLayer->End();

			// Buffer Swapping:
			window->swapBuffers();
		}

		return;
	}







	void Application::shutdown()
	{
		if (!isRunning) {
			return;
		}


		// Window Shutdown
		window.reset();

		isRunning = false;
	}










}
