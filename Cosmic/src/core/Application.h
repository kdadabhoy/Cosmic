#pragma once

#include "core/Core.h" // For Ref and Scope
#include "core/Window.h"
#include "core/LayerStack.h"
#include "events/Event.h"       
#include "events/ApplicationEvent.h"
#include "layers/ImGuiLayer.h"

#include <memory>
#include <string>

namespace Cosmic
{
	class Application
	{
	public:
		Application();
		virtual ~Application();

		void Run();
		void Shutdown();

		void OnEvent(Event& e);

		void PushLayer(Layer* inLayer);
		void PushOverlay(Layer* inOverlay);

		inline Window& GetWindow() { return *m_Window; }
		inline static Application& Get() { return *s_Instance; }

	private:
		bool Initialize();
		bool OnWindowClose(WindowCloseEvent& e);
		bool OnWindowResize(WindowResizeEvent& e);

	private:
		Scope<Window> m_Window;
		ImGuiLayer* m_ImGuiLayer;
		LayerStack m_LayerStack;
		bool m_Running = true;
		bool m_Minimized = false;

		static Application* s_Instance;

		const static int DEFAULT_WIDTH = 1280;
		const static int DEFAULT_HEIGHT = 720;
		const std::string DEFAULT_WINDOW_TITLE = "Cosmic Engine";
	};

	// To be defined in CLIENT (Sandbox)
	Application* CreateApplication();
}