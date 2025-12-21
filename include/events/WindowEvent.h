#ifndef WINDOW_EVENT_H
#define WINDOW_EVENT_H

#include "events/Event.h"


class WindowResizeEvent : public Event {
public:
    WindowResizeEvent(unsigned int width, unsigned int height)
        : m_Width(width), m_Height(height) {
    }

    static EventType GetStaticType() { return EventType::WindowResize; }
    virtual EventType GetEventType() const override { return GetStaticType(); }
    virtual const char* GetName() const override { return "WindowResize"; }

    inline unsigned int GetWidth() const { return m_Width; }
    inline unsigned int GetHeight() const { return m_Height; }

private:
    unsigned int m_Width, m_Height;
};



// ADD THIS CLASS BELOW IT
class WindowCloseEvent : public Event {
public:
    WindowCloseEvent() = default;

    static EventType GetStaticType() { return EventType::WindowClose; }
    virtual EventType GetEventType() const override { return GetStaticType(); }
    virtual const char* GetName() const override { return "WindowClose"; }
};



#endif