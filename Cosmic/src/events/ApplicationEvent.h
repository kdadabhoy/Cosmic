#pragma once

// ApplicationEvent.h
// Last Modified 5/14/2026

/**
 * General Description:
 * ApplicationEvent.h contains specialized Event classes that deal with the
 * lifecycle of the application and its window. These events are categorized
 * under 'EventCategoryApplication'.
 *
 * These signals are vital for the coordination between the hardware-facing
 * Window class and the high-level Application logic.
 */

#include "events/Event.h"
#include <sstream>
#include <string>

namespace Cosmic
{

	/////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////
	/**
	 * WindowResizeEvent
	 * Dispatched when the native window changes size.
	 * Contains the new width and height in pixels.
	 */
	class WindowResizeEvent : public Event
	{
	public:
		WindowResizeEvent(uint32_t width, uint32_t height)
			: m_Width(width), m_Height(height)
		{
		}

		inline uint32_t GetWidth() const { return m_Width; }
		inline uint32_t GetHeight() const { return m_Height; }

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "WindowResizeEvent: " << m_Width << ", " << m_Height;
			return ss.str();
		}

		EVENT_CLASS_TYPE(WindowResize)
			EVENT_CLASS_CATEGORY(EventCategoryApplication)
	private:
		uint32_t m_Width, m_Height;
	};


	/////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////
	/**
	 * WindowCloseEvent
	 * Dispatched when the OS window is signaled to close.
	 */
	class WindowCloseEvent : public Event
	{
	public:
		WindowCloseEvent() {}

		EVENT_CLASS_TYPE(WindowClose)
			EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};


	/////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////
	/**
	 * AppTick / AppUpdate / AppRender
	 * Internal heartbeat events that can be used to synchronize
	 * logic or external debugging tools with the main engine loop.
	 */
	/////////////////////////////////////////////////////////////////////////////////

	class AppTickEvent : public Event
	{
	public:
		AppTickEvent() {}

		EVENT_CLASS_TYPE(AppTick)
			EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	/////////////////////////////////////////////////////////////////////////////////

	class AppUpdateEvent : public Event
	{
	public:
		AppUpdateEvent() {}

		EVENT_CLASS_TYPE(AppUpdate)
			EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	/////////////////////////////////////////////////////////////////////////////////

	class AppRenderEvent : public Event
	{
	public:
		AppRenderEvent() {}

		EVENT_CLASS_TYPE(AppRender)
			EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	/////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////

}