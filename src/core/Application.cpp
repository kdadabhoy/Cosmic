#include "core/Application.h"

// ImGui
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"


// Math
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

// Graphics & Layers
#include <GLFW/glfw3.h>
#include "layers/MenuLayer.h"

// Other
#include <iostream>




// --- FIX: Initialize the static instance ---
Application* Application::s_Instance = nullptr;


Application::Application() 
	: isRunning(false)
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
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;    // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;     // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;        // IF using Docking Branch

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForOpenGL(window->getHandle(), true); 
	ImGui_ImplOpenGL3_Init();


	isRunning = true;
	return true;
}









// --- EVENT DISPATCHER ---
void Application::OnEvent(Event& e) {

	if (e.GetEventType() == EventType::WindowResize) {
		auto& re = static_cast<WindowResizeEvent&>(e);
		// This tells OpenGL to use the ENTIRE new window area
		glViewport(0, 0, re.GetWidth(), re.GetHeight());
	}


	// 1. Dispatch specific application-level logic
	if (e.GetEventType() == EventType::WindowClose)
		OnWindowClose(static_cast<WindowCloseEvent&>(e));

	// 2. Pass events to layers from top to bottom
	for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it) {
		if (e.Handled)
			break;
		(*it)->OnEvent(e);
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
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

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
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

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

	
	// ImGui Shutdown
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();


	// Window Shutdown
	window.reset();

	isRunning = false;
}











