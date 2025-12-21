#ifndef APPLICATION_H
#define APPLICATION_H

#include "core/Window.h"
#include "core/LayerStack.h"
#include "graphics/Renderer.h"
#include "events/Event.h"       
#include "events/WindowEvent.h"
#include <memory>
#include <string>

class Application {
public:
	Application();
	~Application();

	bool initialize();
	void run();
	void shutdown();

	void OnEvent(Event& e);

	// --- ADDED FOR DYNAMIC LAYERS ---
	inline Window& GetWindow() { return *window; }
	inline static Application& Get() { return *s_Instance; }

private:
	bool OnWindowClose(WindowCloseEvent& e);

	std::unique_ptr<Window> window;
	Renderer m_Renderer;
	LayerStack m_LayerStack;
	bool isRunning;

	// Singleton instance
	static Application* s_Instance;

	const static int DEFAULT_WIDTH = 1280;
	const static int DEFAULT_HEIGHT = 720;
	std::string DEFAULT_WINDOW_TITLE = "AirplaneSim";
};

#endif




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