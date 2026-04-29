#pragma once
#include "core/Core.h" 
#include "core/Window.h"
#include "core/LayerStack.h"
#include "events/Event.h"       
#include "events/ApplicationEvent.h"
#include "layers/ImGuiLayer.h"
#include "graphics/FrameBuffer.h" 
#include <memory>
#include <string>

namespace Cosmic
{
	class Application
	{
	public:
		Application();
		virtual ~Application();

		void							Run();
		void							Shutdown();
		void							OnEvent(Event& e);
		void							PushLayer(Layer* inLayer);
		void							PushOverlay(Layer* inOverlay);

		inline static Application& Get() { return *s_Instance; }
		inline Window& GetWindow() { return *m_Window; }

		// FIXED: Changed GetFramebuffer to GetFrameBuffer (Capital B)
		inline Ref<FrameBuffer>         GetFrameBuffer() { return m_Framebuffer; }

		void							UseFixedTimeStep(bool useFixedTimeStep) { m_UseFixedTimestep = useFixedTimeStep; }
		void							SetTimeScale(float timescale) { m_TimeScale = timescale; }

	private:
		bool							Initialize();
		bool							OnWindowClose(WindowCloseEvent& e);
		bool							OnWindowResize(WindowResizeEvent& e);

	private:
		Scope<Window>					m_Window;
		ImGuiLayer* m_ImGuiLayer;
		LayerStack						m_LayerStack;

		Ref<FrameBuffer>                m_Framebuffer;

		bool							m_Running = true;
		bool							m_UseFixedTimestep = true;
		bool							m_Minimized = false;

		static Application* s_Instance;

		float							m_TimeScale = 1.0f;
		const static int				DEFAULT_WIDTH = 1280;
		const static int				DEFAULT_HEIGHT = 720;
		const std::string				DEFAULT_WINDOW_TITLE = "Cosmic Engine";
	};

	Application* CreateApplication();
}