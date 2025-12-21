#pragma once

#include "core/Layer.h"
#include <vector>
#include <cstdint> // Fixes: uint32_t undefined

class LayerStack {
public:
	LayerStack();
	~LayerStack();

	void PushLayer(Layer* layer);
	void PushOverlay(Layer* overlay);
	void PopLayer(Layer* layer);
	void PopOverlay(Layer* overlay);

	// Standard iterators (for rendering: bottom to top)
	std::vector<Layer*>::iterator begin() { return m_Layers.begin(); }
	std::vector<Layer*>::iterator end() { return m_Layers.end(); }

	// Reverse iterators (for events: top to bottom)
	std::vector<Layer*>::reverse_iterator rbegin() { return m_Layers.rbegin(); }
	std::vector<Layer*>::reverse_iterator rend() { return m_Layers.rend(); }

private:
	std::vector<Layer*> m_Layers;
	uint32_t m_LayerInsertIndex = 0;
};