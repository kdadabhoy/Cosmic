// test_databus.cpp — AP-01 V01: the host-owned DataBus (design contract §1).
//
// Headless. Every bullet of the V01 acceptance row is a CHECK below: round-trips and
// coercions, Set-creates, the history ring (wrap, oldest dropped, order), the inclusive
// History(window) boundary, Age across Advance, Producer following SetProducer, the
// subscription discipline (once per Set with the stored value, unsubscribe during
// dispatch prevents the call, nested Set allowed and the depth-65 dispatch dropped with
// exactly one warning), Clear keeping subscriptions, non-finite values stored as
// written, and the 100,000-Set timing (MESSAGE, not asserted).

#include <doctest.h>

#include "data/DataBus.h"
#include "core/Log.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace Cosmic;

namespace
{
    // Counts engine WARN lines mentioning the DataBus while installed (RAII).
    struct WarnCounter
    {
        std::shared_ptr<CallbackSink> Sink;
        std::atomic<int> Count{ 0 };
        WarnCounter()
        {
            Sink = std::make_shared<CallbackSink>([this](spdlog::level::level_enum lvl, const std::string& text)
            {
                if (lvl == spdlog::level::warn && text.find("DataBus") != std::string::npos)
                    ++Count;
            });
            Log::AddSink(Sink);
        }
        ~WarnCounter() { Log::RemoveSink(Sink); }
    };
}

TEST_SUITE("AP-01 V01 DataBus")
{
    TEST_CASE("V01: every method is safe on an empty bus")
    {
        DataBus bus;
        std::vector<DataSample> h;
        CHECK_FALSE(bus.Has("x"));
        CHECK(bus.Get("x").ValueKind == DataValue::Kind::Number);
        CHECK(bus.Get("x").Number == 0.0);
        CHECK(bus.GetNumber("x") == 0.0);
        CHECK(bus.GetNumber("x", 7.5) == 7.5);
        CHECK(bus.GetBool("x", true) == true);
        CHECK(bus.GetString("x", "fb") == "fb");
        CHECK(std::isinf(bus.Age("x")));
        CHECK(bus.Age("x") > 0.0);
        CHECK(bus.LastWriteTime("x") == -1.0);
        CHECK(bus.HistoryCapacity("x") == DataBus::kDefaultHistory);
        CHECK(bus.History("x", h) == 0);
        CHECK(h.empty());
        CHECK(bus.Producer("x").empty());
        CHECK(bus.Channels().empty());
        CHECK(bus.ChannelCount() == 0);
        CHECK(bus.Now() == 0.0);
        bus.Unsubscribe(0);
        bus.Unsubscribe(12345);
        bus.Remove("x");
        bus.Clear();
        bus.SetProducer("");
        CHECK(bus.ChannelCount() == 0);
    }

    TEST_CASE("V01: number / bool / string round-trips and coercions")
    {
        DataBus bus;
        bus.Set("n", 2.5);
        bus.SetBool("b", true);
        bus.SetString("s", "3.75");
        bus.SetString("t", "hello");

        // Round-trips through the typed getters and the DataValue kinds.
        CHECK(bus.Get("n").ValueKind == DataValue::Kind::Number);
        CHECK(bus.GetNumber("n") == 2.5);
        CHECK(bus.Get("b").ValueKind == DataValue::Kind::Bool);
        CHECK(bus.GetBool("b") == true);
        CHECK(bus.Get("s").ValueKind == DataValue::Kind::String);
        CHECK(bus.GetString("s") == "3.75");

        // Coercions (DataValue::AsNumber / AsString / AsBool).
        CHECK(bus.GetNumber("b") == 1.0);                 // Bool -> 0/1
        CHECK(bus.GetNumber("s") == 3.75);                // String -> strtod
        CHECK(bus.GetNumber("t") == 0.0);                 // non-numeric string -> 0.0
        CHECK(bus.GetString("n") == "2.5");               // Number -> "%g"
        CHECK(bus.GetString("b") == "true");              // Bool -> "true"/"false"
        CHECK(bus.GetBool("n") == true);                  // non-zero number -> true
        CHECK(bus.GetBool("s") == false);                 // "3.75" is not "true"/"1"
        bus.SetString("one", "1");
        CHECK(bus.GetBool("one") == true);
        bus.SetBool("f", false);
        CHECK(bus.GetString("f") == "false");
        CHECK(bus.GetNumber("f") == 0.0);
        bus.Set("z", 0.0);
        CHECK(bus.GetBool("z") == false);

        // The typed DataValue setter stores exactly what it is given.
        bus.Set("v", DataValue::MakeString("x"));
        CHECK(bus.Get("v").String == "x");
        CHECK(bus.Get("v").ValueKind == DataValue::Kind::String);
        CHECK(DataValue::MakeNumber(4.0).AsString() == "4");
        CHECK(DataValue::MakeBool(true).AsNumber() == 1.0);
        CHECK(DataValue::MakeString("  12.5xyz").AsNumber() == 12.5);
    }

    TEST_CASE("V01: Set creates a missing channel (like FlowMachine::SetVar); Remove / Channels / ChannelCount")
    {
        DataBus bus;
        CHECK_FALSE(bus.Has("speed"));
        bus.Set("speed", 1.0);
        CHECK(bus.Has("speed"));
        CHECK(bus.ChannelCount() == 1);
        bus.SetBool("zeta", true);
        bus.SetString("alpha", "a");
        const auto names = bus.Channels();
        REQUIRE(names.size() == 3);
        CHECK(names[0] == "alpha");        // sorted
        CHECK(names[1] == "speed");
        CHECK(names[2] == "zeta");
        bus.Remove("speed");
        CHECK_FALSE(bus.Has("speed"));
        CHECK(bus.ChannelCount() == 2);
        // Re-setting an existing channel overwrites in place (still one channel).
        bus.Set("alpha", 9.0);
        CHECK(bus.Get("alpha").ValueKind == DataValue::Kind::Number);
        CHECK(bus.ChannelCount() == 2);
    }

    TEST_CASE("V01: history ring wraps at capacity — oldest dropped, order preserved; capacity rules")
    {
        DataBus bus;
        std::vector<DataSample> h;

        // Default capacity; history allocates lazily on the first numeric Set.
        CHECK(bus.HistoryCapacity("r") == DataBus::kDefaultHistory);
        bus.SetHistoryCapacity("r", 4);
        CHECK(bus.HistoryCapacity("r") == 4);
        CHECK_FALSE(bus.Has("r"));                       // a capacity setting never creates a value

        for (int i = 1; i <= 6; ++i)                     // 6 writes into a ring of 4
        {
            bus.Set("r", (double)i);
            bus.Advance(1.0);
        }
        REQUIRE(bus.History("r", h) == 4);
        CHECK(h[0].Value == 3.0);                        // 1 and 2 dropped (oldest)
        CHECK(h[1].Value == 4.0);
        CHECK(h[2].Value == 5.0);
        CHECK(h[3].Value == 6.0);                        // newest last
        CHECK(h[0].Time == 2.0);                         // stamped with the bus clock at write time
        CHECK(h[3].Time == 5.0);

        // Non-numeric writes do not add samples (numeric channels only).
        bus.SetString("r", "text");
        CHECK(bus.History("r", h) == 4);
        CHECK(h[3].Value == 6.0);

        // Shrinking keeps the newest samples in order; growing keeps them all.
        bus.SetHistoryCapacity("r", 2);
        REQUIRE(bus.History("r", h) == 2);
        CHECK(h[0].Value == 5.0);
        CHECK(h[1].Value == 6.0);
        bus.SetHistoryCapacity("r", 8);
        REQUIRE(bus.History("r", h) == 2);
        CHECK(h[0].Value == 5.0);
        bus.Set("r", 7.0);
        REQUIRE(bus.History("r", h) == 3);
        CHECK(h[2].Value == 7.0);

        // Capacity 0 keeps no history at all.
        bus.SetHistoryCapacity("none", 0);
        bus.Set("none", 1.0);
        bus.Set("none", 2.0);
        CHECK(bus.History("none", h) == 0);
        CHECK(bus.GetNumber("none") == 2.0);             // the value itself is still stored

        // Remove drops the ring with the channel.
        bus.Remove("r");
        CHECK(bus.History("r", h) == 0);
        CHECK(bus.HistoryCapacity("r") == DataBus::kDefaultHistory);
    }

    TEST_CASE("V01: History(window) keeps samples with Time >= Now()-window (inclusive boundary)")
    {
        DataBus bus;
        std::vector<DataSample> h;
        // Samples at t = 0, 0.5, 1.0, ..., 4.0 (binary-exact times), Now() ends at 4.0.
        for (int i = 0; i <= 8; ++i)
        {
            bus.Set("w", (double)i);
            if (i < 8) bus.Advance(0.5);
        }
        CHECK(bus.Now() == 4.0);
        // Window 2.0 => cutoff 2.0; the sample AT 2.0 (value 4) is kept.
        REQUIRE(bus.History("w", h, 2.0) == 5);
        CHECK(h[0].Time == 2.0);
        CHECK(h[0].Value == 4.0);
        CHECK(h[4].Value == 8.0);
        // Slightly narrower window excludes the boundary sample.
        REQUIRE(bus.History("w", h, 1.75) == 4);
        CHECK(h[0].Time == 2.5);
        // Window 0 (default) returns everything.
        CHECK(bus.History("w", h) == 9);
        CHECK(bus.History("w", h, 0.0) == 9);
        // A window larger than the history returns everything.
        CHECK(bus.History("w", h, 100.0) == 9);
    }

    TEST_CASE("V01: Age before and after Advance; LastWriteTime; Advance ignores negative and non-finite deltas")
    {
        DataBus bus;
        CHECK(bus.Now() == 0.0);
        bus.Set("a", 1.0);
        CHECK(bus.Age("a") == 0.0);
        CHECK(bus.LastWriteTime("a") == 0.0);
        bus.Advance(0.25);
        CHECK(bus.Now() == 0.25);
        CHECK(bus.Age("a") == 0.25);
        bus.Advance(0.75);
        CHECK(bus.Age("a") == 1.0);
        bus.Set("a", 2.0);                                // a write resets the age
        CHECK(bus.Age("a") == 0.0);
        CHECK(bus.LastWriteTime("a") == 1.0);
        bus.Advance(-5.0);                                // max(dt, 0)
        CHECK(bus.Now() == 1.0);
        bus.Advance(std::numeric_limits<double>::quiet_NaN());
        bus.Advance(std::numeric_limits<double>::infinity());
        CHECK(bus.Now() == 1.0);
        CHECK(std::isinf(bus.Age("missing")));
        // Clear keeps the clock running (bus-lifetime time).
        bus.Clear();
        CHECK(bus.Now() == 1.0);
        bus.Advance(0.5);
        CHECK(bus.Now() == 1.5);
    }

    TEST_CASE("V01: Producer follows SetProducer per write")
    {
        DataBus bus;
        bus.Set("p", 1.0);
        CHECK(bus.Producer("p").empty());
        bus.SetProducer("PendulumService");
        bus.Set("p", 2.0);
        bus.SetString("q", "x");
        CHECK(bus.Producer("p") == "PendulumService");
        CHECK(bus.Producer("q") == "PendulumService");
        bus.SetProducer("Other");
        bus.Set("p", 3.0);
        CHECK(bus.Producer("p") == "Other");
        CHECK(bus.Producer("q") == "PendulumService");    // untouched channel keeps its tag
        bus.SetProducer("");
        bus.Set("p", 4.0);
        CHECK(bus.Producer("p").empty());
        CHECK(bus.Producer("missing").empty());
    }

    TEST_CASE("V01: Subscribe / SubscribeAny fire once per Set with the stored value")
    {
        DataBus bus;
        int namedHits = 0, anyHits = 0;
        std::string lastAnyChannel;
        DataValue lastNamed, lastAny;
        const DataBus::Handle hn = bus.Subscribe("n", [&](const std::string& ch, const DataValue& v) { ++namedHits; lastNamed = v; CHECK(ch == "n"); });
        const DataBus::Handle ha = bus.SubscribeAny([&](const std::string& ch, const DataValue& v) { ++anyHits; lastAnyChannel = ch; lastAny = v; });
        CHECK(hn != 0);
        CHECK(ha != 0);
        CHECK(hn != ha);

        bus.Set("n", 5.0);
        CHECK(namedHits == 1);
        CHECK(anyHits == 1);
        CHECK(lastNamed.Number == 5.0);
        CHECK(lastAny.Number == 5.0);
        CHECK(lastAnyChannel == "n");

        bus.SetString("m", "v");                          // named listener on "n" does not fire
        CHECK(namedHits == 1);
        CHECK(anyHits == 2);
        CHECK(lastAnyChannel == "m");
        CHECK(lastAny.String == "v");

        bus.Set("n", 6.0);
        CHECK(namedHits == 2);
        CHECK(anyHits == 3);

        bus.Unsubscribe(hn);
        bus.Set("n", 7.0);
        CHECK(namedHits == 2);                            // unsubscribed
        CHECK(anyHits == 4);
        bus.Unsubscribe(ha);
        bus.Set("n", 8.0);
        CHECK(anyHits == 4);

        // Null handlers are rejected with an invalid handle.
        CHECK(bus.Subscribe("n", nullptr) == 0);
        CHECK(bus.SubscribeAny(nullptr) == 0);
    }

    TEST_CASE("V01: unsubscribe during dispatch prevents the call; subscribe during dispatch misses the in-flight value")
    {
        DataBus bus;
        int firstHits = 0, secondHits = 0, lateHits = 0;
        DataBus::Handle second = 0, late = 0;
        DataBus::Handle first = bus.Subscribe("c", [&](const std::string&, const DataValue&)
        {
            ++firstHits;
            bus.Unsubscribe(second);                          // the later listener must NOT fire this dispatch
            if (late == 0)                                    // subscribe once, from inside a dispatch
                late = bus.SubscribeAny([&](const std::string&, const DataValue&) { ++lateHits; });
        });
        second = bus.Subscribe("c", [&](const std::string&, const DataValue&) { ++secondHits; });
        CHECK(first != 0);

        bus.Set("c", 1.0);
        CHECK(firstHits == 1);
        CHECK(secondHits == 0);                           // removed mid-dispatch: never called
        CHECK(lateHits == 0);                             // connected mid-dispatch: not for this value

        bus.Set("c", 2.0);                                // next Set: the late listener is live
        CHECK(firstHits == 2);
        CHECK(secondHits == 0);
        CHECK(lateHits == 1);
        bus.Unsubscribe(first);
        bus.Unsubscribe(late);
        bus.Set("c", 3.0);
        CHECK(firstHits == 2);
        CHECK(lateHits == 1);
    }

    TEST_CASE("V01: nested Set from a handler is allowed; the depth-65 dispatch is dropped with one warning")
    {
        WarnCounter warns;
        DataBus bus;

        // A handler that writes ANOTHER channel (the documented nested case).
        int mirrored = 0;
        bus.Subscribe("src", [&](const std::string&, const DataValue& v) { bus.Set("mirror", v.AsNumber() * 2.0); });
        bus.Subscribe("mirror", [&](const std::string&, const DataValue&) { ++mirrored; });
        bus.Set("src", 21.0);
        CHECK(bus.GetNumber("mirror") == 42.0);
        CHECK(mirrored == 1);
        CHECK(warns.Count == 0);

        // A self-recursive handler: each dispatch re-Sets its own channel. Dispatch
        // depth 1..64 runs; the 65th nested dispatch is dropped (value still stored).
        int depthHits = 0;
        bus.Subscribe("deep", [&](const std::string&, const DataValue& v)
        {
            ++depthHits;
            bus.Set("deep", v.AsNumber() + 1.0);
        });
        bus.Set("deep", 1.0);
        CHECK(depthHits == DataBus::kMaxNesting);          // 64 dispatches ran
        CHECK(bus.GetNumber("deep") == 65.0);              // the 65th value was stored, its dispatch dropped
        CHECK(warns.Count == 1);                           // exactly one warning

        // A second overflow does not warn again (one warning per bus).
        depthHits = 0;
        bus.Set("deep", 1.0);
        CHECK(depthHits == DataBus::kMaxNesting);
        CHECK(warns.Count == 1);

        // Depth resets after dispatch: ordinary Sets keep dispatching.
        int plain = 0;
        bus.Subscribe("plain", [&](const std::string&, const DataValue&) { ++plain; });
        bus.Set("plain", 1.0);
        CHECK(plain == 1);
    }

    TEST_CASE("V01: Clear drops channels, history and producers but keeps subscriptions")
    {
        DataBus bus;
        std::vector<DataSample> h;
        int hits = 0;
        bus.SubscribeAny([&](const std::string&, const DataValue&) { ++hits; });
        bus.SetProducer("Svc");
        bus.Set("a", 1.0);
        bus.Set("a", 2.0);
        bus.SetString("b", "x");
        CHECK(hits == 3);
        CHECK(bus.History("a", h) == 2);
        CHECK(bus.Producer("a") == "Svc");

        bus.Clear();
        CHECK(bus.ChannelCount() == 0);
        CHECK_FALSE(bus.Has("a"));
        CHECK_FALSE(bus.Has("b"));
        CHECK(bus.History("a", h) == 0);
        CHECK(bus.Producer("a").empty());
        CHECK(bus.Channels().empty());

        bus.Set("a", 3.0);                                // the subscription survived Clear
        CHECK(hits == 4);
        CHECK(bus.History("a", h) == 1);                  // history starts fresh
    }

    TEST_CASE("V01: non-finite numbers are stored as written (channels are data)")
    {
        DataBus bus;
        std::vector<DataSample> h;
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        bus.Set("x", nan);
        CHECK(bus.Has("x"));
        CHECK(std::isnan(bus.GetNumber("x")));
        CHECK(std::isnan(bus.Get("x").Number));
        bus.Set("x", inf);
        CHECK(std::isinf(bus.GetNumber("x")));
        bus.Set("x", -inf);
        CHECK(bus.GetNumber("x") < 0.0);
        CHECK(std::isinf(bus.GetNumber("x")));
        REQUIRE(bus.History("x", h) == 3);
        CHECK(std::isnan(h[0].Value));
        CHECK(std::isinf(h[1].Value));
        CHECK(bus.GetBool("x") == false);                 // non-finite coerces to false
        CHECK(bus.GetString("x") == "-inf");
    }

    TEST_CASE("V01: 100,000 Set calls on 100 channels — timing recorded (not gating)")
    {
        DataBus bus;
        std::vector<std::string> names;
        for (int i = 0; i < 100; ++i)
            names.push_back("ch" + std::to_string(i));
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 100000; ++i)
            bus.Set(names[i % 100], (double)i);
        const auto t1 = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        CHECK(bus.ChannelCount() == 100);
        CHECK(bus.GetNumber("ch99") == 99999.0);
        std::vector<DataSample> h;
        CHECK(bus.History("ch0", h) == 1000);            // 1,000 writes per channel fit the default ring
#ifdef NDEBUG
        const std::string config = "Release";
#else
        const std::string config = "Debug";
#endif
        MESSAGE("V01 timing: 100,000 Set on 100 channels = " << ms << " ms (" << config
                << "; the 50 ms Release bar is recorded, not asserted)");
    }
}
