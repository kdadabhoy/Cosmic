#pragma once
// data/DataBus.h
//
// ============================================================================
// Cosmic DataBus — the host-owned, GL-free channel store (App Platform / AP-01,
// design contract §1).
// ============================================================================
//
// The DataBus connects app logic (services, scripts) to screens (bound widgets,
// flow channel guards, hosted panels). It is a member of the HOST — the standalone
// PlayerLayer, and StarforgeApp for editor Play — never of a module DLL, so its
// values, history and producer tags survive a module reload (the D-LIVE loop).
//
//   * Channels are plain strings holding a DataValue (number / bool / string). A
//     Set on a missing channel creates it (like FlowMachine::SetVar). Non-finite
//     numbers are stored as written: channels are data; consumers decide (widgets
//     show the placeholder, plots skip them, flow channel guards evaluate false).
//   * Numeric channels keep a ring history (kDefaultHistory samples unless
//     SetHistoryCapacity says otherwise; 0 = none), allocated lazily on the first
//     numeric Set, stamped with the bus clock.
//   * Time: the host calls Advance(dt) ONCE per frame with the UNSCALED frame
//     delta; Now() starts at 0 for the bus lifetime. Age() is the staleness a
//     widget shows. A non-finite or negative dt is ignored.
//   * Producers (D-LINKS): ServiceHost brackets every service callback with
//     SetProducer(name); each write remembers the tag so "Open producer" can
//     resolve a channel back to the service that wrote it last.
//   * Subscriptions are synchronous, main thread, fired inside Set after the
//     value is stored, with the EventBus discipline: the handler list is
//     snapshotted, each handler is liveness-checked before the call (unsubscribe
//     during dispatch prevents the call), a handler subscribed during dispatch
//     does not receive the in-flight value. A handler may Set another channel;
//     nesting deeper than kMaxNesting (64) logs one warning and drops the nested
//     dispatch (the value is still stored).
//
// THREADING: writes are MAIN THREAD ONLY in v1 (documented, not locked). Every
// method is safe on an empty bus. One hash lookup on the channel table per call
// (dispatch adds one on the listener table when named listeners exist); History
// copies into the caller's vector. No engine header other than core/Core.h.
// ============================================================================

#include "core/Core.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Cosmic
{
    /** @brief One channel value. Kind-tagged; the inactive members keep their
     *  defaults. AsNumber/AsString/AsBool are the coercions every consumer uses. */
    struct COSMIC_API DataValue
    {
        enum class Kind : int32_t { Number, Bool, String } ValueKind = Kind::Number;
        double      Number = 0.0;
        bool        Bool   = false;
        std::string String;

        static DataValue MakeNumber(double v);
        static DataValue MakeBool(bool b);
        static DataValue MakeString(std::string s);

        double      AsNumber() const;   // Bool -> 0/1; String -> strtod or 0.0
        std::string AsString() const;   // Number -> "%g"; Bool -> "true"/"false"
        bool        AsBool()   const;   // Number -> finite and non-zero; String -> "true" / "1"
    };

    /** @brief One ring-history entry (numeric channels only). */
    struct DataSample { double Time; double Value; };

    class COSMIC_API DataBus
    {
    public:
        using Handle  = uint64_t;                                   // 0 == invalid
        using Handler = std::function<void(const std::string& channel, const DataValue& value)>;
        static constexpr size_t kDefaultHistory = 1024;
        static constexpr int    kMaxNesting     = 64;                // nested dispatch depth ceiling

        DataBus() = default;
        DataBus(const DataBus&) = delete;              // owns subscriptions; never copied
        DataBus& operator=(const DataBus&) = delete;

        // ---- writes (MAIN THREAD ONLY in v1) --------------------------------
        void Set(const std::string& channel, double v);
        void SetBool(const std::string& channel, bool v);
        void SetString(const std::string& channel, std::string v);
        void Set(const std::string& channel, const DataValue& v);

        // ---- reads ----------------------------------------------------------
        bool        Has(const std::string& channel) const;
        DataValue   Get(const std::string& channel) const;                       // default DataValue when missing
        double      GetNumber(const std::string& channel, double fallback = 0.0) const;
        bool        GetBool(const std::string& channel, bool fallback = false) const;   // AsBool() coercion
        std::string GetString(const std::string& channel, const std::string& fallback = "") const;
        double      Age(const std::string& channel) const;                       // Now() - last write; +inf when missing
        double      LastWriteTime(const std::string& channel) const;             // -1 when missing

        // ---- history (numeric channels; allocated lazily on first Set) ------
        void   SetHistoryCapacity(const std::string& channel, size_t samples);   // 0 = keep no history; keeps the newest samples on shrink
        size_t HistoryCapacity(const std::string& channel) const;                // kDefaultHistory unless set
        // Copies oldest->newest into `out`; windowSeconds > 0 keeps only samples with Time >= Now()-window (inclusive).
        size_t History(const std::string& channel, std::vector<DataSample>& out, double windowSeconds = 0.0) const;

        // ---- time: the host calls Advance once per frame with the UNSCALED frame delta ----
        void   Advance(double dt);          // Now() += max(dt, 0); starts at 0 for the bus lifetime; non-finite dt ignored
        double Now() const { return m_Now; }

        // ---- producers (D-LINKS): ServiceHost brackets each service tick with SetProducer(name) ----
        void        SetProducer(const std::string& producer);                    // "" = none
        std::string Producer(const std::string& channel) const;                  // last writer's producer tag ("" if none)

        // ---- subscriptions: synchronous, main thread, fired inside Set after the value is stored ----
        Handle Subscribe(const std::string& channel, Handler fn);
        Handle SubscribeAny(Handler fn);
        void   Unsubscribe(Handle h);                                            // no-op on unknown

        // ---- maintenance ----------------------------------------------------
        std::vector<std::string> Channels() const;                                // sorted
        void   Remove(const std::string& channel);
        void   Clear();                                                          // channels + history + producers; subscriptions kept
        size_t ChannelCount() const;

    private:
        struct Channel
        {
            DataValue   Value;
            bool        HasValue    = false;   // SetHistoryCapacity/Subscribe never create a value
            double      LastWrite   = -1.0;
            std::string Producer;
            // Ring history (numeric writes only). `Ring` is sized to Capacity on the
            // first numeric write; Head indexes the oldest sample, Count <= Capacity.
            std::vector<DataSample> Ring;
            size_t      Capacity    = kDefaultHistory;
            size_t      Head        = 0;
            size_t      Count       = 0;
        };
        struct Listener { Handle Id = 0; Handler Fn; };

        void AppendSample(Channel& c, double value);
        void Dispatch(const std::string& channel, const DataValue& value);
        bool IsNamedLive(const std::string& channel, Handle h) const;
        bool IsAnyLive(Handle h) const;

        std::unordered_map<std::string, Channel>                m_Channels;
        std::unordered_map<std::string, std::vector<Listener>>  m_Named;
        std::vector<Listener>                                   m_Any;
        Handle      m_Next     = 1;
        double      m_Now      = 0.0;
        std::string m_CurrentProducer;
        int         m_Depth    = 0;       // in-flight dispatch depth
        bool        m_NestingWarned = false;
    };
}
