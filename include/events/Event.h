
#ifndef EVENT_H
#define EVENT_H

#include <string>
#include <functional>

enum class EventType {
    None = 0,
    WindowClose, WindowResize,
    KeyPressed, KeyReleased, 
    MouseButtonPressed, MouseButtonReleased, MouseMoved
};




class Event {
public:
    virtual ~Event() = default;
    virtual EventType GetEventType() const = 0;
    virtual const char* GetName() const = 0;

    bool Handled = false; // Has this event been "used" by a layer?
};

#endif