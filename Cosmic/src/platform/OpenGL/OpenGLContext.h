#pragma once

#include "graphics/GraphicsContext.h"

struct GLFWwindow;

namespace Cosmic
{
	class OpenGLContext : public GraphicsContext
	{
	public:
		OpenGLContext(GLFWwindow* windowHandle);

		void Init()			override;
		void SwapBuffers()	override;


	private:
		GLFWwindow* m_WindowHandle;
	};

}