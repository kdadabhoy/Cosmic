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
#include "core/MenuLayer.h"

// Other
#include <iostream>







Application::Application() 
	: isRunning(false)
{}





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
		Rendering Initialization?
	*/





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
		//ImGui::ShowDemoWindow(); // Show demo window! :)


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











