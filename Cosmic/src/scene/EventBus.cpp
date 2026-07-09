// scene/EventBus.cpp — the per-scene signal channel (Phase 17 / U2).

#include "scene/EventBus.h"
#include "scene/Entity.h"   // Entity must be complete where the std::functions run

namespace Cosmic
{
    EventBus::Handle EventBus::Connect(const std::string& signal, SignalHandler fn)
    {
        if (!fn) return 0;
        const Handle id = m_Next++;
        m_Named[signal].push_back({ id, std::move(fn) });
        return id;
    }

    EventBus::Handle EventBus::ConnectAny(AnySignalHandler fn)
    {
        if (!fn) return 0;
        const Handle id = m_Next++;
        m_Any.push_back({ id, std::move(fn) });
        return id;
    }

    void EventBus::Disconnect(Handle h)
    {
        if (h == 0) return;
        for (auto& [name, listeners] : m_Named)
        {
            for (size_t i = 0; i < listeners.size(); ++i)
            {
                if (listeners[i].Id == h)
                {
                    listeners.erase(listeners.begin() + i);
                    return;
                }
            }
        }
        for (size_t i = 0; i < m_Any.size(); ++i)
        {
            if (m_Any[i].Id == h)
            {
                m_Any.erase(m_Any.begin() + i);
                return;
            }
        }
    }

    bool EventBus::IsNamedLive(const std::string& signal, Handle h) const
    {
        auto it = m_Named.find(signal);
        if (it == m_Named.end()) return false;
        for (const auto& l : it->second)
            if (l.Id == h) return true;
        return false;
    }

    bool EventBus::IsAnyLive(Handle h) const
    {
        for (const auto& l : m_Any)
            if (l.Id == h) return true;
        return false;
    }

    void EventBus::Emit(const std::string& signal, Entity source)
    {
        // Snapshot (handle, fn) pairs so a handler that Connects/Disconnects during
        // dispatch cannot invalidate our iteration or resurrect/kill a listener mid-fire.
        // Named handlers first (in subscription order), then the any-handlers.
        {
            auto it = m_Named.find(signal);
            if (it != m_Named.end())
            {
                std::vector<Listener> snapshot = it->second;   // copy
                for (const auto& l : snapshot)
                    if (l.Fn && IsNamedLive(signal, l.Id))
                        l.Fn(source);
            }
        }
        {
            std::vector<AnyListener> snapshot = m_Any;         // copy
            for (const auto& l : snapshot)
                if (l.Fn && IsAnyLive(l.Id))
                    l.Fn(signal, source);
        }
    }

    void EventBus::Clear()
    {
        m_Named.clear();
        m_Any.clear();
    }

    size_t EventBus::ListenerCount(const std::string& signal) const
    {
        auto it = m_Named.find(signal);
        return it == m_Named.end() ? 0 : it->second.size();
    }

    size_t EventBus::TotalListeners() const
    {
        size_t n = m_Any.size();
        for (const auto& [name, listeners] : m_Named)
            n += listeners.size();
        return n;
    }
}
