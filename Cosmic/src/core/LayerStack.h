#pragma once

// LayerStack.h
// Last Modified 5/14/2026

/**
 * General Description:
 * The LayerStack is a specialized container designed to manage the execution order of
 * the Cosmic Engine's update and event loops. It organizes "Layers" (game logic/scenes)
 * and "Overlays" (UI/Debug tools) into a coherent stack.
 *
 * Logic Layers are kept at the bottom/middle of the stack, while Overlays are strictly
 * maintained at the end (the "top"). This architecture ensures that UI elements are
 * always rendered last (on top) and receive hardware events first.
 *
 * OWNERSHIP POLICY:
 * This class operates on a "Borrow" model. It stores raw pointers for performance and
 * iteration logic, but it does NOT own the memory of the layers. Lifetime management
 * is handled by the Application class using smart pointers (Scope/unique_ptr).
 *
 * Public Function Prototypes (Pre and Post Conditions):
 *
 * 1. LayerStack()
 *    Post: Internal storage is initialized and the insertion index is reset to 0.
 *
 * 2. ~LayerStack()
 *    Post: Iterates through all remaining layers and triggers OnDetach() for cleanup.
 *          Internal memory pointers are cleared.
 *
 * 3. void PushLayer(Layer* layer)
 *    Pre:  The provided layer pointer must be valid.
 *    Post: The layer is inserted at the current m_LayerInsertIndex and OnAttach() is called.
 *
 * 4. void PushOverlay(Layer* overlay)
 *    Pre:  The provided overlay pointer must be valid.
 *    Post: The overlay is appended to the very end of the stack and OnAttach() is called.
 *
 * 5. void PopLayer(Layer* layer)
 *    Pre:  The layer must currently exist within the "logic" section of the stack.
 *    Post: OnDetach() is called on the layer and its pointer is removed from the stack.
 *
 * 6. void PopOverlay(Layer* overlay)
 *    Pre:  The overlay must currently exist within the "overlay" section of the stack.
 *    Post: OnDetach() is called on the overlay and its pointer is removed from the stack.
 *
 * 7. void Clear()
 *    Post: Every layer in the stack is detached (OnDetach) and the container is emptied.
 *
 * 8. Iterators (begin, end, rbegin, rend)
 *    Post: Returns standard vector iterators. Use rbegin/rend for event propagation
 *          (top-to-bottom) and begin/end for rendering (bottom-to-top).
 */

#include "core/Layer.h"
#include <vector>
#include <cstdint>


namespace Cosmic
{
	class LayerStack
	{
	public:
		LayerStack();
		~LayerStack();

		////////////////////////////////
		// Stack Management
		///////////////////////////////

		void		PushLayer(Layer* layer);
		void		PushOverlay(Layer* overlay);
		void		PopLayer(Layer* layer);
		void		PopOverlay(Layer* overlay);
		void		Clear();


		////////////////////////////////
		// Iteration Support
		///////////////////////////////

		// Rendering order: Bottom-to-Top (0 to N)
		std::vector<Layer*>::iterator				begin()			{ return m_Layers.begin(); }
		std::vector<Layer*>::iterator				end()			{ return m_Layers.end(); }

		// Event order: Top-to-Bottom (N to 0)
		std::vector<Layer*>::reverse_iterator		rbegin()		{ return m_Layers.rbegin(); }
		std::vector<Layer*>::reverse_iterator		rend()			{ return m_Layers.rend(); }


	private:
		// Internal contiguous storage of layer pointers
		std::vector<Layer*>		m_Layers;

		// Tracks where game layers end and overlays begin
		uint32_t				m_LayerInsertIndex	= 0;
	};

}