#include "core/LayerStack.h"
#include <algorithm> // Fixes: std::find undefined


namespace Cosmic 
{
	LayerStack::LayerStack() 
	{

	}

	/////////////////////////////////////////////////////////////////////////////////

	LayerStack::~LayerStack()
	{
		// Clean up all layers when the stack is destroyed
		for (Layer* layer : m_Layers)
		{
			layer->OnDetach();
			delete layer;
		}
	}

	/////////////////////////////////////////////////////////////////////////////////

	void LayerStack::PushLayer(Layer* layer)
	{
		// Layers are inserted at the 'middle' before the overlays
		m_Layers.emplace(m_Layers.begin() + m_LayerInsertIndex, layer);
		m_LayerInsertIndex++;
		layer->OnAttach();
	}

	/////////////////////////////////////////////////////////////////////////////////

	void LayerStack::PushOverlay(Layer* overlay)
	{
		// Overlays are always at the very end (top) of the stack
		m_Layers.emplace_back(overlay);
		overlay->OnAttach();
	}

	/////////////////////////////////////////////////////////////////////////////////

	void LayerStack::PopLayer(Layer* layer)
	{
		auto it = std::find(m_Layers.begin(), m_Layers.begin() + m_LayerInsertIndex, layer);

		if (it != m_Layers.begin() + m_LayerInsertIndex) 
		{
			layer->OnDetach();
			m_Layers.erase(it);
			m_LayerInsertIndex--;
		}
	}

	/////////////////////////////////////////////////////////////////////////////////

	void LayerStack::PopOverlay(Layer* overlay)
	{
		auto it = std::find(m_Layers.begin() + m_LayerInsertIndex, m_Layers.end(), overlay);

		if (it != m_Layers.end())
		{
			overlay->OnDetach();
			m_Layers.erase(it);
		}
	}


}