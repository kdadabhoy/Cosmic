#pragma once

namespace Cosmic
{
	class GraphicsContext
	{
	public:
		virtual ~GraphicsContext() = default;

		virtual void	Init() = 0;
		virtual void	SwapBuffers() = 0;
	};

}



/*	Documentation:

	Interface for window/context creation

	*** Every Platform will derive it's own Context Class from this ***

*/

