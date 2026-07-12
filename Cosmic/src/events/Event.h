#pragma once

// Event.h
// Last Modified 5/14/2026

/**
 * General Description:
 * Event.h defines the infrastructure for the Cosmic Engine's "Nervous System."
 * It provides a base Event class and a Dispatcher mechanism to route hardware
 * and software signals throughout the engine's layers.
 *
 * The system is strictly "Reactive"—it packages data into an object and sends
 * it through the LayerStack until a layer marks it as 'Handled'.
 *
 * Architecture Components:
 * 1. EventType: An enum used to identify the specific nature of an event.
 * 2. EventCategory: Bit-masked flags allowing one event to belong to multiple
 *    groups (e.g., MouseButtonPressed is both 'Mouse' and 'Input').
 * 3. EventDispatcher: A template-based helper that allows layers to "subscribe"
 *    to specific event types without complex switch statements.
 */

#include "core/Core.h"
#include <string>

namespace Cosmic
{
	/**
	 * EventType
	 * Defines every unique event the engine can recognize.
	 */
	enum class EventType
	{
		None = 0,
		WindowClose, WindowResize, WindowFocus, WindowLostFocus, WindowMoved, WindowFileDrop,
		KeyPressed, KeyReleased, KeyTyped,
		MouseButtonPressed, MouseButtonReleased, MouseMoved, MouseScrolled
	};

	/**
	 * EventCategory
	 * Used for broad filtering. Using BIT() from Core.h allows us to filter
	 * for "all Keyboard events" or "all Input events" simultaneously.
	 */
	enum EventCategory
	{
		None = 0,
		EventCategoryApplication = BIT(0),
		EventCategoryInput = BIT(1),
		EventCategoryKeyboard = BIT(2),
		EventCategoryMouse = BIT(3),
		EventCategoryMouseButton = BIT(4)
	};



	/////////////////////////////////////////////////////////////////////////////////
	// HELPER MACROS
	// These macros reduce "Boilerplate" code in specialized Event classes.
	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * EVENT_CLASS_TYPE
	 * Implements the required virtual functions for identifying the event type.
	 * Includes a 'StaticType' so the Dispatcher can compare types without an instance.
	 */
#define EVENT_CLASS_TYPE(type) static EventType GetStaticType() { return EventType::type; }\
								virtual EventType GetEventType() const override { return GetStaticType(); }\
								virtual const char* GetName() const override { return #type; }

	 /**
	  * EVENT_CLASS_CATEGORY
	  * Implements the category flag getter for specific event classes.
	  */
#define EVENT_CLASS_CATEGORY(category) virtual int GetCategoryFlags() const override { return category; }



	/////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////

	  /**
	   * Event (Base Class)
	   * The abstract blueprint for all signals in the engine.
	   */
	class COSMIC_API Event
	{
	public:
		// When true, the event stops moving through the LayerStack.
		// UI layers typically set this to 'true' to block clicks from hitting the game.
		bool Handled = false;

		virtual EventType GetEventType() const = 0;
		virtual const char* GetName() const = 0;
		virtual int GetCategoryFlags() const = 0;
		virtual std::string ToString() const { return GetName(); }

		/**
		 * IsInCategory
		 * Uses bitwise AND to check if this event belongs to a specific group.
		 */
		inline bool IsInCategory(EventCategory category) const
		{
			return GetCategoryFlags() & category;
		}
	};



	/////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * EventDispatcher
	 * A utility used inside 'OnEvent' functions to route events to specific methods.
	 */
	class EventDispatcher
	{
	public:
		EventDispatcher(Event& event)
			: m_Event(event)
		{
		}

		/**
		 * Dispatch
		 * Compares the incoming event's type with the template type T.
		 * If they match, it runs the provided function 'func' and sets the 'Handled' status.
		 */
		template<typename T, typename F>
		bool Dispatch(const F& func)
		{
			if (m_Event.GetEventType() == T::GetStaticType())
			{
				// Cast the base event to the specific type (e.g., KeyPressedEvent)
				// so the function can access specialized data like 'KeyCode'.
				// Only set Handled to true — never clear a true set by a prior handler.
				if (func(static_cast<T&>(m_Event)))
					m_Event.Handled = true;
				return true;
			}
			return false;
		}
	private:
		Event& m_Event;
	};

	/**
	 * operator<<
	 * Allows events to be printed directly to logging streams (e.g., CS_CORE_TRACE(e)).
	 */
	inline std::ostream& operator<<(std::ostream& os, const Event& e)
	{
		return os << e.ToString();
	}
}