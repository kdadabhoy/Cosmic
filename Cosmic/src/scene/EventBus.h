#pragma once
// scene/EventBus.h
//
// ============================================================================
// Cosmic scene signal bus (Phase 17 / U2).
// ============================================================================
//
// A tiny per-Scene publish/subscribe channel for named signals carrying an
// optional source Entity. It is the ONE channel that connects UI buttons (U1),
// the screen-flow FlowMachine (U5) and gameplay scripts: a button emits its
// Signal, the flow reacts to transitions, a script's OnSignal / Signals()
// listeners fire — all same-frame, ordered, on the main thread.
//
// Signals are plain strings (author-friendly; serialized in buttons + flow).
// Subscriptions return a Handle for explicit unsubscribe (the telemetry
// unsubscribe-handle discipline); a scene with no subscribers makes Emit a
// cheap no-op, so shipped apps that never touch UI/flow are unaffected.
//
// GL-free and headless-testable.
// ============================================================================

#include "core/Core.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Cosmic
{
    class Entity;   // scene/Entity.h — a lightweight (handle, Scene*) value

    class COSMIC_API EventBus
    {
    public:
        using Handle           = uint64_t;   // 0 == invalid
        using SignalHandler    = std::function<void(Entity source)>;
        using AnySignalHandler = std::function<void(const std::string& signal, Entity source)>;

        /** @brief Subscribe to ONE named signal. Returns a handle for Disconnect. */
        Handle Connect(const std::string& signal, SignalHandler fn);

        /** @brief Subscribe to EVERY signal (name is passed to the handler) — used
         *  by the FlowMachine and by ScriptHost to route ScriptableEntity::OnSignal. */
        Handle ConnectAny(AnySignalHandler fn);

        /** @brief Remove a subscription (named or any). No-op on an unknown handle. */
        void Disconnect(Handle h);

        /** @brief Fire `signal` to every current listener: matching named handlers
         *  first (subscription order), then any-handlers. Safe to Disconnect/Connect
         *  from within a handler — a listener removed mid-dispatch does not fire. */
        void Emit(const std::string& signal, Entity source);

        /** @brief Drop all subscriptions. */
        void Clear();

        size_t ListenerCount(const std::string& signal) const;
        size_t TotalListeners() const;

    private:
        bool IsNamedLive(const std::string& signal, Handle h) const;
        bool IsAnyLive(Handle h) const;

        struct Listener { Handle Id = 0; SignalHandler Fn; };
        struct AnyListener { Handle Id = 0; AnySignalHandler Fn; };

        std::unordered_map<std::string, std::vector<Listener>> m_Named;
        std::vector<AnyListener>                               m_Any;
        Handle m_Next = 1;
    };
}
