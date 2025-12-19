#include "Application.h"


#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"



#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"


#include "graphics/VertexBuffer.h"
#include "graphics/VertexBufferLayout.h"
#include "graphics/IndexBuffer.h"
#include "graphics/VertexArray.h"
#include "graphics/Shader.h"


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
	while (isRunning && !window->shouldClose()) {
		// Event Pulling:
		window->pollEvents();


		// ImGui Functions from Documentation - Top of Loop:
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		ImGui::ShowDemoWindow(); // Show demo window! :)


		// Rendering:
			// abc I render stuff here efg



		// ImGui Functions from Documentation - Bottom of Loop:
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());


		// Buffer Swapping:
		window->swapBuffers();
	}



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











