#include "core/LayerStack.h"
#include <algorithm> // For std::find



namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	LayerStack::LayerStack()
	{
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Destructor
	 *
	 * CRITICAL OWNERSHIP NOTE: The LayerStack does NOT own the memory of the layers
	 * it points to. It only manages their order of execution.
	 *
	 * We only trigger OnDetach() to allow layers to release their own internal resources.
	 * We do NOT call 'delete' here because layers are owned by the Application
	 * (via Scope/unique_ptr) or the client. Deleting here would cause a double-free.
	 */
	LayerStack::~LayerStack()
	{
		for (Layer* layer : m_Layers)
		{
			layer->OnDetach();
		}
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * PushLayer
	 *
	 * Adds a "Game Logic" layer to the first half of the stack.
	 * Layers are inserted at the m_LayerInsertIndex, ensuring they always sit
	 * underneath Overlays (UI/Debug tools).
	 */
	void LayerStack::PushLayer(Layer* layer)
	{
		m_Layers.emplace(m_Layers.begin() + m_LayerInsertIndex, layer);
		m_LayerInsertIndex++;
		layer->OnAttach();
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * PushOverlay
	 *
	 * Adds an "Overlay" (like ImGui or a Console) to the end of the stack.
	 * Overlays are always rendered last (on top) and receive events first.
	 */
	void LayerStack::PushOverlay(Layer* overlay)
	{
		m_Layers.emplace_back(overlay);
		overlay->OnAttach();
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * PopLayer
	 *
	 * Removes a logic layer from the stack without destroying it.
	 * Use this when you want to "unplug" a layer from the engine update loop
	 * but keep it alive in memory.
	 */
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

	/**
	 * Clear
	 *
	 * Orchestrates a full detachment of all layers. This is the primary method
	 * used during Application::Shutdown() to ensure all layers clean up their
	 * GPU/API resources before the Engine Subsystems (Renderer/Window) are destroyed.
	 */
	void LayerStack::Clear()
	{
		for (Layer* layer : m_Layers)
		{
			layer->OnDetach();
		}

		m_Layers.clear();
		m_LayerInsertIndex = 0;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * PopOverlay
	 *
	 * Removes an overlay from the end of the stack. Similar to PopLayer,
	 * this notifies the overlay of detachment but does not free its memory.
	 */
	void LayerStack::PopOverlay(Layer* overlay)
	{
		auto it = std::find(m_Layers.begin() + m_LayerInsertIndex, m_Layers.end(), overlay);

		if (it != m_Layers.end())
		{
			overlay->OnDetach();
			m_Layers.erase(it);
		}
	}

	/////////////////////////////////////////////////////////////////////////////////
}