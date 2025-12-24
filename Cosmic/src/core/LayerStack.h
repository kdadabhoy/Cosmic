#pragma once

#include "core/Layer.h"
#include <vector>
#include <cstdint> // For uint32_t 



namespace Cosmic 
{
	class LayerStack 
	{
	public:
		LayerStack();
		~LayerStack();

		void PushLayer(Layer* layer);
		void PushOverlay(Layer* overlay);
		void PopLayer(Layer* layer);
		void PopOverlay(Layer* overlay);

		// Standard iterators (for rendering: bottom to top)
		std::vector<Layer*>::iterator begin()					{ return m_Layers.begin();  }
		std::vector<Layer*>::iterator end()						{ return m_Layers.end();    }

		// Reverse iterators (for events: top to bottom)
		std::vector<Layer*>::reverse_iterator rbegin()			{ return m_Layers.rbegin(); }
		std::vector<Layer*>::reverse_iterator rend()			{ return m_Layers.rend();   }

	private:
		std::vector<Layer*> m_Layers;
		uint32_t m_LayerInsertIndex = 0;
	};

}



/*	Documentation:

	A Layer is added to the LayerStack... Application only has 1 LayerStack used at a time.
	- Need to be able:
		- Add (Push) Layers and Overlays
		- Remove (Pop) Layers and Overlays
		- Iterate from the bottom layers to the top layers (for draw calls)
		- Iterate from the top layers to the bottom layers (for event calls)

	* GUI/Overlay layers need to be at the top... this class guarantees that


*/