// EntitySelection.cpp
// Last Modified: 5/29/2026

#include "telemetry/EntitySelection.h"

#include <algorithm>   // std::find_if (Unsubscribe / Notify liveness check)

namespace Cosmic
{
    // =========================================================================
    // Static member definitions
    // =========================================================================

    std::string  EntitySelection::s_Name;
    std::string  EntitySelection::s_Tag;
    Entity       EntitySelection::s_Entity;
    std::vector<EntitySelection::Subscription> EntitySelection::s_Callbacks;
    std::mutex   EntitySelection::s_Mutex;
    EntitySelection::SubscriptionHandle EntitySelection::s_NextHandle = 1u;

    // =========================================================================
    // Setters
    // =========================================================================

    void EntitySelection::Set(Entity entity, const std::string& name, const std::string& tag)
    {
        {
            std::lock_guard<std::mutex> lock(s_Mutex);
            s_Entity = entity;
            s_Name   = name;
            s_Tag    = tag;
        }
        Notify(name, tag);
    }

    void EntitySelection::SetByName(const std::string& name, const std::string& tag)
    {
        {
            std::lock_guard<std::mutex> lock(s_Mutex);
            s_Entity = Entity{};
            s_Name   = name;
            s_Tag    = tag;
        }
        Notify(name, tag);
    }

    void EntitySelection::Clear()
    {
        {
            std::lock_guard<std::mutex> lock(s_Mutex);
            s_Entity = Entity{};
            s_Name.clear();
            s_Tag.clear();
        }
        Notify("", "");
    }

    // =========================================================================
    // Getters
    // =========================================================================

    bool EntitySelection::HasSelection()
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        return !s_Name.empty();
    }

    std::string EntitySelection::GetName()
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        return s_Name;
    }

    std::string EntitySelection::GetTag()
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        return s_Tag;
    }

    Entity EntitySelection::GetEntity()
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        return s_Entity;
    }

    // =========================================================================
    // Subscription
    // =========================================================================

    EntitySelection::SubscriptionHandle EntitySelection::OnChanged(
        std::function<void(const std::string&, const std::string&)> cb)
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        SubscriptionHandle id = s_NextHandle++;
        s_Callbacks.push_back({ id, std::move(cb) });
        return id;
    }

    void EntitySelection::Unsubscribe(SubscriptionHandle id)
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        auto it = std::find_if(s_Callbacks.begin(), s_Callbacks.end(),
            [id](const Subscription& s) { return s.id == id; });
        if (it != s_Callbacks.end())
            s_Callbacks.erase(it);
    }

    // =========================================================================
    // Internal: fire all callbacks outside the mutex to prevent re-entrant deadlock
    // =========================================================================

    void EntitySelection::Notify(const std::string& name, const std::string& tag)
    {
        // Snapshot the HANDLES so new subscriptions registered inside a callback
        // don't invalidate the iterator, and so the mutex is not held while callbacks
        // execute. Then re-fetch each callback under the mutex right before calling
        // it: a listener that an earlier callback of this same dispatch unsubscribed
        // (typically because its owner was torn down) must NOT fire — the rule the
        // per-scene EventBus already follows (WO-07 / KI-32; the old code copied the
        // callbacks themselves and invoked removed ones).
        std::vector<SubscriptionHandle> handles;
        {
            std::lock_guard<std::mutex> lock(s_Mutex);
            handles.reserve(s_Callbacks.size());
            for (const auto& sub : s_Callbacks)
                handles.push_back(sub.id);
        }

        for (SubscriptionHandle id : handles)
        {
            std::function<void(const std::string&, const std::string&)> cb;
            {
                std::lock_guard<std::mutex> lock(s_Mutex);
                auto it = std::find_if(s_Callbacks.begin(), s_Callbacks.end(),
                    [id](const Subscription& s) { return s.id == id; });
                if (it == s_Callbacks.end())
                    continue;   // unsubscribed since the snapshot — do not fire
                cb = it->cb;
            }
            cb(name, tag);
        }
    }

} // namespace Cosmic
