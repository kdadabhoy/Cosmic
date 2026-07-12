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
#include <utility>
#include <vector>

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
	 * WindowFileDropEvent
	 * Dispatched when the user drops one or more OS files onto the window (T3 /
	 * gap analysis §14.5). Carries the dropped absolute paths. This is a GENERIC
	 * signal with no engine-side consumer — Starforge (T8) routes it to the
	 * Content Browser, and a shipped app may ignore it entirely.
	 */
	class WindowFileDropEvent : public Event
	{
	public:
		explicit WindowFileDropEvent(std::vector<std::string> paths)
			: m_Paths(std::move(paths))
		{
		}

		inline const std::vector<std::string>& GetPaths() const { return m_Paths; }

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "WindowFileDropEvent: " << m_Paths.size() << " file(s)";
			if (!m_Paths.empty())
				ss << " (" << m_Paths.front() << (m_Paths.size() > 1 ? ", …" : "") << ")";
			return ss.str();
		}

		EVENT_CLASS_TYPE(WindowFileDrop)
			EVENT_CLASS_CATEGORY(EventCategoryApplication)
	private:
		std::vector<std::string> m_Paths;
	};


}