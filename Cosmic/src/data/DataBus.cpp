// data/DataBus.cpp — the host-owned channel store (App Platform / AP-01). See DataBus.h.

#include "data/DataBus.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace Cosmic
{
    // ========================================================================
    // DataValue
    // ========================================================================

    DataValue DataValue::MakeNumber(double v)      { DataValue d; d.ValueKind = Kind::Number; d.Number = v; return d; }
    DataValue DataValue::MakeBool(bool b)          { DataValue d; d.ValueKind = Kind::Bool;   d.Bool = b;   return d; }
    DataValue DataValue::MakeString(std::string s) { DataValue d; d.ValueKind = Kind::String; d.String = std::move(s); return d; }

    double DataValue::AsNumber() const
    {
        switch (ValueKind)
        {
            case Kind::Number: return Number;
            case Kind::Bool:   return Bool ? 1.0 : 0.0;
            case Kind::String:
            {
                const char* begin = String.c_str();
                char* end = nullptr;
                const double v = std::strtod(begin, &end);
                return (end == begin) ? 0.0 : v;
            }
        }
        return 0.0;
    }

    std::string DataValue::AsString() const
    {
        switch (ValueKind)
        {
            case Kind::String: return String;
            case Kind::Bool:   return Bool ? "true" : "false";
            case Kind::Number:
            {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%g", Number);
                return buf;
            }
        }
        return std::string();
    }

    bool DataValue::AsBool() const
    {
        switch (ValueKind)
        {
            case Kind::Bool:   return Bool;
            case Kind::Number: return std::isfinite(Number) && Number != 0.0;
            case Kind::String: return String == "true" || String == "1";
        }
        return false;
    }

    // ========================================================================
    // DataBus — writes
    // ========================================================================

    void DataBus::Set(const std::string& channel, double v)        { Set(channel, DataValue::MakeNumber(v)); }
    void DataBus::SetBool(const std::string& channel, bool v)      { Set(channel, DataValue::MakeBool(v)); }
    void DataBus::SetString(const std::string& channel, std::string v) { Set(channel, DataValue::MakeString(std::move(v))); }

    void DataBus::Set(const std::string& channel, const DataValue& v)
    {
        Channel& c  = m_Channels[channel];          // creates the channel (one lookup)
        c.Value     = v;
        c.HasValue  = true;
        c.LastWrite = m_Now;
        c.Producer  = m_CurrentProducer;
        if (v.ValueKind == DataValue::Kind::Number)
            AppendSample(c, v.Number);              // non-finite stored as written

        // Fire the subscriptions with the STORED value (a copy: a nested Set on the
        // same channel must not change what the remaining handlers of this dispatch see).
        if (m_Any.empty() && m_Named.find(channel) == m_Named.end())
            return;                                  // nobody listening: no copy, no dispatch
        const DataValue stored = c.Value;
        Dispatch(channel, stored);
    }

    void DataBus::AppendSample(Channel& c, double value)
    {
        if (c.Capacity == 0)
            return;
        if (c.Ring.size() != c.Capacity)             // lazy allocation on the first numeric write
        {
            c.Ring.assign(c.Capacity, DataSample{ 0.0, 0.0 });
            c.Head  = 0;
            c.Count = 0;
        }
        if (c.Count < c.Capacity)
        {
            c.Ring[(c.Head + c.Count) % c.Capacity] = DataSample{ m_Now, value };
            ++c.Count;
        }
        else
        {
            c.Ring[c.Head] = DataSample{ m_Now, value };   // overwrite the oldest
            c.Head = (c.Head + 1) % c.Capacity;
        }
    }

    // ========================================================================
    // DataBus — reads
    // ========================================================================

    bool DataBus::Has(const std::string& channel) const
    {
        auto it = m_Channels.find(channel);
        return it != m_Channels.end() && it->second.HasValue;
    }

    DataValue DataBus::Get(const std::string& channel) const
    {
        auto it = m_Channels.find(channel);
        return (it != m_Channels.end() && it->second.HasValue) ? it->second.Value : DataValue{};
    }

    double DataBus::GetNumber(const std::string& channel, double fallback) const
    {
        auto it = m_Channels.find(channel);
        return (it != m_Channels.end() && it->second.HasValue) ? it->second.Value.AsNumber() : fallback;
    }

    bool DataBus::GetBool(const std::string& channel, bool fallback) const
    {
        auto it = m_Channels.find(channel);
        return (it != m_Channels.end() && it->second.HasValue) ? it->second.Value.AsBool() : fallback;
    }

    std::string DataBus::GetString(const std::string& channel, const std::string& fallback) const
    {
        auto it = m_Channels.find(channel);
        return (it != m_Channels.end() && it->second.HasValue) ? it->second.Value.AsString() : fallback;
    }

    double DataBus::Age(const std::string& channel) const
    {
        auto it = m_Channels.find(channel);
        if (it == m_Channels.end() || !it->second.HasValue)
            return std::numeric_limits<double>::infinity();
        return m_Now - it->second.LastWrite;
    }

    double DataBus::LastWriteTime(const std::string& channel) const
    {
        auto it = m_Channels.find(channel);
        return (it != m_Channels.end() && it->second.HasValue) ? it->second.LastWrite : -1.0;
    }

    // ========================================================================
    // DataBus — history
    // ========================================================================

    void DataBus::SetHistoryCapacity(const std::string& channel, size_t samples)
    {
        Channel& c = m_Channels[channel];             // a valueless record is fine (Has() stays false)
        if (samples == c.Capacity)
            return;
        // Keep the newest min(Count, samples) in order across the resize.
        std::vector<DataSample> kept;
        const size_t keep = std::min(c.Count, samples);
        kept.reserve(keep);
        for (size_t i = c.Count - keep; i < c.Count; ++i)
            kept.push_back(c.Ring[(c.Head + i) % c.Capacity]);
        c.Capacity = samples;
        c.Ring.clear();
        c.Head  = 0;
        c.Count = 0;
        if (samples > 0 && !kept.empty())
        {
            c.Ring.assign(samples, DataSample{ 0.0, 0.0 });
            for (const DataSample& s : kept)
                c.Ring[c.Count++] = s;
        }
    }

    size_t DataBus::HistoryCapacity(const std::string& channel) const
    {
        auto it = m_Channels.find(channel);
        return it == m_Channels.end() ? kDefaultHistory : it->second.Capacity;
    }

    size_t DataBus::History(const std::string& channel, std::vector<DataSample>& out, double windowSeconds) const
    {
        out.clear();
        auto it = m_Channels.find(channel);
        if (it == m_Channels.end() || it->second.Count == 0)
            return 0;
        const Channel& c = it->second;
        const bool   windowed = windowSeconds > 0.0;
        const double cutoff   = m_Now - windowSeconds;
        out.reserve(c.Count);
        for (size_t i = 0; i < c.Count; ++i)
        {
            const DataSample& s = c.Ring[(c.Head + i) % c.Capacity];
            if (windowed && !(s.Time >= cutoff))      // inclusive boundary; NaN times are dropped
                continue;
            out.push_back(s);
        }
        return out.size();
    }

    // ========================================================================
    // DataBus — time + producers
    // ========================================================================

    void DataBus::Advance(double dt)
    {
        if (!std::isfinite(dt) || dt <= 0.0)
            return;
        m_Now += dt;
    }

    void DataBus::SetProducer(const std::string& producer)
    {
        m_CurrentProducer = producer;
    }

    std::string DataBus::Producer(const std::string& channel) const
    {
        auto it = m_Channels.find(channel);
        return (it != m_Channels.end() && it->second.HasValue) ? it->second.Producer : std::string();
    }

    // ========================================================================
    // DataBus — subscriptions (EventBus discipline)
    // ========================================================================

    DataBus::Handle DataBus::Subscribe(const std::string& channel, Handler fn)
    {
        if (!fn) return 0;
        const Handle id = m_Next++;
        m_Named[channel].push_back({ id, std::move(fn) });
        return id;
    }

    DataBus::Handle DataBus::SubscribeAny(Handler fn)
    {
        if (!fn) return 0;
        const Handle id = m_Next++;
        m_Any.push_back({ id, std::move(fn) });
        return id;
    }

    void DataBus::Unsubscribe(Handle h)
    {
        if (h == 0) return;
        for (auto& [name, listeners] : m_Named)
        {
            for (size_t i = 0; i < listeners.size(); ++i)
            {
                if (listeners[i].Id == h)
                {
                    listeners.erase(listeners.begin() + i);
                    if (listeners.empty()) m_Named.erase(name);
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

    bool DataBus::IsNamedLive(const std::string& channel, Handle h) const
    {
        auto it = m_Named.find(channel);
        if (it == m_Named.end()) return false;
        for (const Listener& l : it->second)
            if (l.Id == h) return true;
        return false;
    }

    bool DataBus::IsAnyLive(Handle h) const
    {
        for (const Listener& l : m_Any)
            if (l.Id == h) return true;
        return false;
    }

    void DataBus::Dispatch(const std::string& channel, const DataValue& value)
    {
        if (m_Depth >= kMaxNesting)
        {
            if (!m_NestingWarned)
            {
                m_NestingWarned = true;
                CS_CORE_WARN("DataBus: nested Set dispatch deeper than {0} on channel '{1}' — nested dispatch dropped (value stored).",
                             kMaxNesting, channel);
            }
            return;
        }
        ++m_Depth;
        // Snapshot BOTH (handle, fn) lists before the first call, so a handler that
        // subscribes/unsubscribes during dispatch cannot invalidate the iteration,
        // kill a listener mid-fire, or (a subscription made from a named handler)
        // receive the value that is already in flight.
        std::vector<Listener> named;
        if (auto it = m_Named.find(channel); it != m_Named.end())
            named = it->second;                                // copy
        const std::vector<Listener> any = m_Any;               // copy
        for (const Listener& l : named)
            if (l.Fn && IsNamedLive(channel, l.Id))
                l.Fn(channel, value);
        for (const Listener& l : any)
            if (l.Fn && IsAnyLive(l.Id))
                l.Fn(channel, value);
        --m_Depth;
    }

    // ========================================================================
    // DataBus — maintenance
    // ========================================================================

    std::vector<std::string> DataBus::Channels() const
    {
        std::vector<std::string> out;
        out.reserve(m_Channels.size());
        for (const auto& [name, c] : m_Channels)
            if (c.HasValue)
                out.push_back(name);
        std::sort(out.begin(), out.end());
        return out;
    }

    void DataBus::Remove(const std::string& channel)
    {
        m_Channels.erase(channel);
    }

    void DataBus::Clear()
    {
        m_Channels.clear();          // values + history + per-channel producer tags; subscriptions and the clock stay
    }

    size_t DataBus::ChannelCount() const
    {
        size_t n = 0;
        for (const auto& [name, c] : m_Channels)
            if (c.HasValue) ++n;
        return n;
    }
}
