#pragma once

#include "graphics/GraphicsContext.h"
#include "events/Event.h"
#include <string>
#include <functional>

struct GLFWwindow; // Forward declaration instead of including glfw3.h

namespace Cosmic
{
	class Window
	{
	public:
		using EventCallbackFn = std::function<void(Event&)>;

		Window(int width, int height, const std::string& title);
		~Window();

		void pollEvents();
		void swapBuffers(); // This will now call m_Context->SwapBuffers()

		inline unsigned int GetWidth() const { return m_Data.Width; }
		inline unsigned int GetHeight() const { return m_Data.Height; }
		inline GLFWwindow* getHandle() const { return m_Handle; }

		void setEventCallback(const EventCallbackFn& callback) { m_Data.EventCallback = callback; }
		void setVSync(bool enabled);
		bool IsVSync() const { return m_Data.VSync; }

		bool shouldClose() const;
		void getSize(int* width, int* height) const;

	private:
		GLFWwindow* m_Handle;
		GraphicsContext* m_Context; // Added to manage API-specific context

		struct WindowData
		{
			std::string Title;
			unsigned int Width, Height;
			bool VSync;
			EventCallbackFn EventCallback;
		};

		WindowData m_Data;
	};
}