#pragma once

// LayerStack.h
// Last Modified 5/14/2026

/**
 * General Description:
 * 
 * The LayerStack functions strictly as an execution router and prioritization engine.
 * It borrows raw pointers (`Layer*`) to adjust execution pipelines, render layering,
 * and hardware input dispatch order. It does NOT own the memory of the layers it references.
 *
 * To eliminate use-after-free bugs and context destruction race conditions, the
 * underlying allocation lifecycles are explicitly driven by the `Application` subsystem
 * during its runtime state transitions and final termination routines.
 *
 * Public Function Prototypes (Pre and Post Conditions):
 *
 * 1. LayerStack()
 *    Post: Clears structural pointer maps natively. Does not free layer memory.
 *
 * 2. ~LayerStack()
 *    Post: Clears internal pointer vectors. Does NOT call OnDetach() or free layer memory.
 *          Detachment and deletion are driven explicitly by Application::Shutdown().
 *
 * 3. void PushLayer(Layer* layer)
 *    Pre:  `layer` must point to a valid heap allocation.
 *    Post:  Appends the overlay to the back of the stack (on top of logic). Calls `OnAttach()`.
 *
 * 4. void PushOverlay(Layer* overlay)
 *    Pre:  `overlay` must point to a valid heap allocation.
 *    Post: The overlay is appended to the very end of the stack and OnAttach() is called.
 *
 * 5. void PopLayer(Layer* layer)
 *    Pre:  `layer` must exist within the active logic sequence range.
 *    Post: Triggers `OnDetach()`, removes the reference tracking, decrements the insert index.
 *
 * 6. void PopOverlay(Layer* overlay)
 *    Pre: `overlay` must exist within the active overlay sequence range.
 *    Post: Triggers `OnDetach()`, removes the tracking reference from the back of the vector.
 *
 * 7. void Clear()
 *    Pre:  All layers must have been detached via PopLayer/PopOverlay first (asserts this in debug).
 *    Post: Purges structural vectors and clears the insert index.
 *
 * 8. void ForceCleanForShutdown()
 *    Post: Raw wipe of structural vectors. Intentionally skips OnDetach(). Only for Application::Shutdown()
 *          after layers have been snapshotted and ownership transferred for external deletion.
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
		void		Clear();                  // Asserts all layers are already detached
		void		ForceCleanForShutdown();  // Raw wipe for Application::Shutdown() only — skips OnDetach()


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