// We need a way to basically create a stack of layers that we can easily iterate through
// Without this, the render loop would get quite messy

// Possibly rework this to a header and .cpp later

#ifndef LAYERSTACK_H
#define LAYERSTACK_H

#include "core/Layer.h"
#include <vector>
#include <algorithm>

class LayerStack {
public:
    LayerStack() = default;
    ~LayerStack() {
        for (Layer* layer : m_Layers) {
            layer->OnDetach();
            delete layer;
        }
    }

    void PushLayer(Layer* layer) {
        m_Layers.emplace(m_Layers.begin() + m_LayerInsertIndex, layer);
        m_LayerInsertIndex++;
        layer->OnAttach();
    }

    void PushOverlay(Layer* overlay) {
        m_Layers.emplace_back(overlay);
        overlay->OnAttach();
    }

    void PopLayer(Layer* layer) {
        auto it = std::find(m_Layers.begin(), m_Layers.begin() + m_LayerInsertIndex, layer);
        if (it != m_Layers.begin() + m_LayerInsertIndex) {
            layer->OnDetach();
            m_Layers.erase(it);
            m_LayerInsertIndex--;
        }
    }

    // Standard iterators for rendering (bottom to top)
    std::vector<Layer*>::iterator begin() { return m_Layers.begin(); }
    std::vector<Layer*>::iterator end() { return m_Layers.end(); }

    // Reverse iterators for events (top to bottom)
    std::vector<Layer*>::reverse_iterator rbegin() { return m_Layers.rbegin(); }
    std::vector<Layer*>::reverse_iterator rend() { return m_Layers.rend(); }

private:
    std::vector<Layer*> m_Layers;
    unsigned int m_LayerInsertIndex = 0;
};

#endif