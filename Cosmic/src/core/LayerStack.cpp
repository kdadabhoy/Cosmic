#include "core/LayerStack.h"
#include "core/Log.h"
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
	 * CRITICAL LIFECYCLE NOTE: Structural vector elements are cleared passively.
	 * No deletion loops are triggered here to safeguard hardware graphics context stability
	 * and prevent double-free violations on engine exit.
	 */
	LayerStack::~LayerStack()
	{
		// Memory cleanup and detachment are now driven explicitly by Application::Shutdown
		// or Clear() to protect context lifecycles.
		m_Layers.clear();
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
		// Layers are deleted through Layer* — virtual dtor on the base is required.
		// This assert fires at compile time if virtual is ever accidentally removed from Layer::~Layer().
		// Derived classes inherit virtual destruction automatically via the base.
		static_assert(std::has_virtual_destructor_v<Layer>,
			"Layer::~Layer() must be virtual. Deleting a derived layer through Layer* without it is UB.");
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
	 * Purges the layer registration array structures instantly. This is used by the
	 * Application during system state changes or shutdown sequences to prevent event
	 * execution loops from traversing expired addresses before raw allocations are freed.
	 */
	void LayerStack::Clear()
	{
		// Safe public path: all layers must have been detached via PopLayer/PopOverlay first.
		CS_CORE_ASSERT(m_Layers.empty(),
			"LayerStack::Clear() called while layers are still attached. "
			"Call PopLayer/PopOverlay to detach each layer first, or use ForceCleanForShutdown() in shutdown.");

		m_Layers.clear();
		m_LayerInsertIndex = 0;
	}

	void LayerStack::ForceCleanForShutdown()
	{
		// Shutdown-only raw wipe. Ownership and deletion are handled externally by Application::Shutdown()
		// after snapshotting the layer list — OnDetach() is intentionally not called here.
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