#pragma once

#include "core/Window.h"
#include "core/LayerStack.h"
#include "graphics/Renderer.h"
#include "events/Event.h"       
#include "events/ApplicationEvent.h"
#include "layers/ImGuiLayer.h"


#include <memory>
#include <string>



namespace Cosmic{
	class Application {
	public:
		Application();
		~Application();

		void Run();
		void Shutdown();

		void OnEvent(Event& e);
		void PushLayer(Layer* inLayer);

		// --- ADDED FOR DYNAMIC LAYERS ---
		inline Window& GetWindow() { return *window; }
		inline static Application& Get() { return *s_Instance; }

	private:
		bool Initialize();
		bool OnWindowClose(WindowCloseEvent& e);

		std::unique_ptr<Window> window;
		Renderer m_Renderer;
		LayerStack m_LayerStack;
		ImGuiLayer* m_ImGuiLayer; // Pointer to the overlay layer
		bool isRunning;

		// Singleton instance
		static Application* s_Instance;

		const static int DEFAULT_WIDTH = 1280;
		const static int DEFAULT_HEIGHT = 720;
		std::string DEFAULT_WINDOW_TITLE = "CosmicEngine";
	};
}




/*

Documentation:


	A word on unique_ptr:
		- Only one unique_ptr can own a specific memory address at one time
		- It is essentially a wrapper around a ptr,
			which basically calls the object's destructor
			when the ptr goes out of scope



	Application()
		- Just initializes private member variables



	~Application()
		- Calls shutdown()




	bool initialize()
		- Creates a window
		- will initialize ImGUI and other stuff




	void run()
		- Contains the render loop
		-




	void shutdown()
		- Explicitly deletes the window
			- Technically not needed bc unique_ptr
		-






*/