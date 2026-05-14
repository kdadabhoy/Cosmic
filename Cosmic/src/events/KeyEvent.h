#pragma once

// KeyEvent.h
// Last Modified 5/14/2026

/**
 * General Description:
 * KeyEvent.h provides the classes for keyboard-based interactions.
 * All classes in this file are categorized as both 'Keyboard' and 'Input' events.
 *
 * The base KeyEvent class is protected and cannot be instantiated directly,
 * ensuring that all keyboard signals are specifically Pressed, Released, or Typed.
 */

#include "events/Event.h"
#include <string>
#include <sstream>

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * KeyEvent (Base Class)
	 * Abstract base for all keyboard interactions. Stores the fundamental
	 * KeyCode used by all subclasses.
	 */
	class KeyEvent : public Event
	{
	public:
		inline int GetKeyCode() const { return m_KeyCode; }

		// Dual categorization allows for broad input filtering or specific keyboard logic.
		EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)

	protected:
		KeyEvent(int keycode)
			: m_KeyCode(keycode)
		{
		}

		int m_KeyCode;
	};

	/////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * KeyPressedEvent
	 * Fired when a key is initially pressed or held down (repeated).
	 */
	class KeyPressedEvent : public KeyEvent
	{
	public:
		KeyPressedEvent(int keycode, int repeatCount)
			: KeyEvent(keycode), m_RepeatCount(repeatCount)
		{
		}

		inline int GetRepeatCount() const { return m_RepeatCount; }

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "KeyPressedEvent: " << m_KeyCode << " (" << m_RepeatCount << " repeats)";
			return ss.str();
		}

		EVENT_CLASS_TYPE(KeyPressed)
	private:
		int m_RepeatCount;
	};

	/////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * KeyReleasedEvent
	 * Fired when a key is let go by the user.
	 */
	class KeyReleasedEvent : public KeyEvent
	{
	public:
		KeyReleasedEvent(int keycode)
			: KeyEvent(keycode)
		{
		}

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "KeyReleasedEvent: " << m_KeyCode;
			return ss.str();
		}

		EVENT_CLASS_TYPE(KeyReleased)
	};

	/////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * KeyTypedEvent
	 * Specialized event for text-input scenarios. Captures the character
	 * intended by the key combination.
	 */
	class KeyTypedEvent : public KeyEvent
	{
	public:
		KeyTypedEvent(int keycode)
			: KeyEvent(keycode)
		{
		}

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "KeyTypedEvent: " << m_KeyCode;
			return ss.str();
		}

		EVENT_CLASS_TYPE(KeyTyped)
	};

	/////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////
}