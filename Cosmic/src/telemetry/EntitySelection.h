#pragma once

// EntitySelection.h
// Last Modified: 5/29/2026

/**
 * @brief Global static service tracking the single currently selected entity.
 *
 * THREAD SAFETY
 * -------------
 * All state is guarded by s_Mutex. In practice selection changes only on the
 * main thread (mouse-click picking or ImGui combo). The mutex is present so
 * that subscriber callbacks fired during Set/Clear are safe to call from any
 * thread, and so background threads reading GetName() for display don't race.
 *
 * CALLBACK LIFETIME
 * -----------------
 * OnChanged() returns a SubscriptionHandle. Call Unsubscribe(handle) from the
 * subscriber's destructor to remove the callback before the captured pointer
 * becomes dangling. Failing to unsubscribe is safe only when the subscriber
 * outlives the EntitySelection singleton (i.e. the process lifetime).
 *
 * REPLAY MODE
 * -----------
 * SetByName() sets the name and tag without a live entity handle, so
 * GetEntity() returns an invalid Entity during replay. Callers should
 * always check `if (GetEntity()) { ... }` before dereferencing.
 */

#include "core/Core.h"
#include "scene/Entity.h"
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <cstdint>

namespace Cosmic
{
    class COSMIC_API EntitySelection
    {
    public:
        // -------------------------------------------------------------------------
        // Subscription handle — returned by OnChanged, used by Unsubscribe
        // -------------------------------------------------------------------------
        using SubscriptionHandle = uint32_t;

        // -------------------------------------------------------------------------
        // Setters
        // -------------------------------------------------------------------------

        /** @brief Select a live entity (during simulation). */
        static void Set(Entity entity, const std::string& name, const std::string& tag = "");

        /** @brief Select by name only — no live entity (during replay). */
        static void SetByName(const std::string& name, const std::string& tag = "");

        /** @brief Clear the selection. Fires callbacks with empty name/tag. */
        static void Clear();

        // -------------------------------------------------------------------------
        // Getters
        // -------------------------------------------------------------------------

        static bool        HasSelection();
        static std::string GetName();
        static std::string GetTag();

        /**
         * @brief Returns the live entity handle.
         * May be invalid (operator bool == false) during replay or after
         * SetByName() was called. Always validity-check before use.
         */
        static Entity GetEntity();

        // -------------------------------------------------------------------------
        // Subscription
        // -------------------------------------------------------------------------

        /**
         * @brief Register a callback that fires whenever the selection changes.
         *
         * The callback fires on the same thread that called Set/SetByName/Clear.
         * Returns a SubscriptionHandle that MUST be passed to Unsubscribe() when
         * the subscriber is destroyed, unless the subscriber outlives the process.
         *
         * @param cb  void(const std::string& name, const std::string& tag)
         * @return    Opaque handle to be stored by the caller.
         */
        static SubscriptionHandle OnChanged(
            std::function<void(const std::string& name, const std::string& tag)> cb);

        /**
         * @brief Remove a previously registered callback.
         *
         * Safe to call even if the handle is stale or was already removed (no-op).
         * Call from the subscriber's destructor to prevent dangling captures.
         *
         * @param id  Handle returned by OnChanged().
         */
        static void Unsubscribe(SubscriptionHandle id);

    private:
        static void Notify(const std::string& name, const std::string& tag);

        struct Subscription
        {
            SubscriptionHandle                                        id;
            std::function<void(const std::string&, const std::string&)> cb;
        };

        static std::string s_Name;
        static std::string s_Tag;
        static Entity      s_Entity;
        static std::vector<Subscription> s_Callbacks;
        static std::mutex  s_Mutex;
        static SubscriptionHandle s_NextHandle;
    };

} // namespace Cosmic
