// test_serial_lifecycle.cpp — W9 (plan doc 28 §9.6).
//
// SerialPort / SerialLink lifecycle under the failure modes that actually
// shipped bugs: an unreachable port blocking the render thread, and Close()
// racing an in-flight asynchronous connect. The async connect machinery
// (BeginOpen + m_Abandon + the stop event) exists because a Bluetooth SPP port
// can sit inside CreateFileA for 10-20 s; every test here is written so it
// cannot hang the suite if that machinery regresses — each one has a deadline.
//
// "COM999" is the workhorse: a port name that is well-formed but cannot exist,
// so CreateFileA fails immediately. That gives a deterministic unreachable-port
// path without touching whatever real hardware is plugged into the dev machine.
//
// COVERAGE NOTE — SerialLink's connected-state behaviour (auto-reconnect timing,
// the one-shot ConsumeJustConnected edge) needs a port that actually opens.
// SerialLink has no way to inject a byte transport and no public port setter, so
// those paths are NOT reachable headlessly; plan doc 28 §9.6 already defers the
// injectable transport ("Deferred and documented, not built"). What is reachable
// without hardware is covered below, and the unreachable-port policy itself is
// covered at the SerialPort level where COM999 can be named directly.

#include <doctest.h>

#include "serial/SerialPort.h"
#include "serial/SerialLink.h"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace Cosmic;

namespace
{
    constexpr const char* kUnreachablePort = "COM999";

    using Clock = std::chrono::steady_clock;

    long long MillisSince(Clock::time_point start)
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    }

    // Poll until the port leaves Connecting, or the deadline expires. Returns the
    // final state. Never spins forever — a regression in the connect worker shows
    // up as a failed CHECK, not a wedged test run.
    SerialPort::State WaitWhileConnecting(SerialPort& port, long long timeoutMs = 15000)
    {
        const auto start = Clock::now();
        while (port.GetState() == SerialPort::State::Connecting && MillisSince(start) < timeoutMs)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return port.GetState();
    }
}

// =============================================================================
// SerialPort — synchronous open
// =============================================================================

TEST_CASE("SerialPort::Open on an unreachable port fails fast and reports Failed")
{
    SerialPort port;
    CHECK(port.GetState() == SerialPort::State::Idle);
    CHECK_FALSE(port.IsOpen());

    const auto start = Clock::now();
    CHECK_FALSE(port.Open(kUnreachablePort, 115200));

    // A nonexistent port is an immediate CreateFileA failure. Anything in the
    // seconds range means we are blocking on something we should not be.
    CHECK(MillisSince(start) < 5000);
    CHECK(port.GetState() == SerialPort::State::Failed);
    CHECK_FALSE(port.IsOpen());
}

TEST_CASE("SerialPort::Write on a closed port returns false instead of touching the handle")
{
    SerialPort port;

    const std::string payload = "$R,25,1680,420,120,350*7F\n";
    CHECK_FALSE(port.Write(payload));
    CHECK_FALSE(port.Write(payload.data(), payload.size()));

    // Zero length is refused too — Write's contract is "every byte accepted",
    // and no bytes were.
    CHECK_FALSE(port.Write(payload.data(), 0));

    // Still false after a failed open, and after a close.
    port.Open(kUnreachablePort, 115200);
    CHECK_FALSE(port.Write(payload));
    port.Close();
    CHECK_FALSE(port.Write(payload));
}

TEST_CASE("SerialPort::Close is idempotent and safe when never opened")
{
    SerialPort port;

    // Close on a virgin port must not touch INVALID_HANDLE_VALUE or join a
    // thread that was never started.
    CHECK_NOTHROW(port.Close());
    CHECK_NOTHROW(port.Close());
    CHECK_NOTHROW(port.Close());
    CHECK(port.GetState() == SerialPort::State::Idle);

    // And after a failed open — Close resets Failed back to Idle.
    port.Open(kUnreachablePort, 115200);
    REQUIRE(port.GetState() == SerialPort::State::Failed);

    const auto start = Clock::now();
    CHECK_NOTHROW(port.Close());
    CHECK_NOTHROW(port.Close());
    CHECK(port.GetState() == SerialPort::State::Idle);
    CHECK(MillisSince(start) < 5000);

    // FlushBuffer on a closed port yields nothing rather than faulting.
    CHECK(port.FlushBuffer().empty());
}

TEST_CASE("SerialPort::GetAvailablePorts never crashes and returns plausible names")
{
    // Reads HKLM\HARDWARE\DEVICEMAP\SERIALCOMM. The key is absent on machines
    // with no serial hardware at all, which must be an empty list, not a fault.
    std::vector<std::string> ports;
    CHECK_NOTHROW(ports = SerialPort::GetAvailablePorts());

    for (const auto& p : ports)
    {
        CAPTURE(p);
        CHECK_FALSE(p.empty());
        CHECK(p.rfind("COM", 0) == 0);   // every Windows serial name starts COM
    }

    // Repeated calls are stable and leak no registry handles.
    for (int i = 0; i < 10; ++i)
        CHECK_NOTHROW((void)SerialPort::GetAvailablePorts());
}

// =============================================================================
// SerialPort — asynchronous open
// =============================================================================

TEST_CASE("SerialPort::BeginOpen on an unreachable port returns immediately and ends Failed")
{
    SerialPort port;

    // The whole point of BeginOpen: the CALL returns at once even though the
    // open itself may block. This is the render-thread freeze that was fixed.
    const auto start = Clock::now();
    port.BeginOpen(kUnreachablePort, 115200);
    const long long callMs = MillisSince(start);
    CHECK(callMs < 1000);

    const SerialPort::State finalState = WaitWhileConnecting(port);
    CHECK(finalState == SerialPort::State::Failed);
    CHECK_FALSE(port.IsOpen());

    port.Close();
    CHECK(port.GetState() == SerialPort::State::Idle);
}

TEST_CASE("SerialPort::Open refuses to race an in-flight BeginOpen")
{
    SerialPort port;
    port.BeginOpen(kUnreachablePort, 115200);

    // If the worker is still inside CreateFileA, a synchronous Open must be
    // refused rather than have two threads writing m_Handle. (If the worker has
    // already finished — COM999 fails fast — Open runs normally and also fails,
    // so the assertion is on the absence of a crash and on the final state.)
    CHECK_FALSE(port.Open(kUnreachablePort, 115200));

    const SerialPort::State finalState = WaitWhileConnecting(port);
    CHECK(finalState == SerialPort::State::Failed);

    port.Close();
}

TEST_CASE("SerialPort survives the abandon race: BeginOpen then immediate Close")
{
    // The historical Bluetooth-freeze machinery. Close() sets m_Abandon and joins
    // the connect worker; if the worker opened the port after Close() decided to
    // tear down, the worker must self-close so nothing leaks. Repeated because
    // the interesting interleaving is timing dependent.
    for (int i = 0; i < 25; ++i)
    {
        SerialPort port;
        port.BeginOpen(kUnreachablePort, 115200);

        const auto start = Clock::now();
        CHECK_NOTHROW(port.Close());

        CAPTURE(i);
        // Close joins the worker, so this is bounded by the open attempt itself.
        CHECK(MillisSince(start) < 15000);
        CHECK(port.GetState() == SerialPort::State::Idle);
        CHECK_FALSE(port.IsOpen());
    }
}

TEST_CASE("SerialPort::BeginOpen does not stack connect attempts")
{
    SerialPort port;

    // Several BeginOpen calls back to back must never leave more than one worker
    // outstanding — the guard is "no-op while Connecting". On a fast-failing port
    // an earlier attempt may already have finished, in which case a later call
    // legitimately starts a fresh one; either way this must not terminate on a
    // thread reassignment or leave the port wedged in Connecting.
    const auto start = Clock::now();
    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW(port.BeginOpen(kUnreachablePort, 115200));
    CHECK(MillisSince(start) < 5000);

    const SerialPort::State finalState = WaitWhileConnecting(port);
    CHECK(finalState != SerialPort::State::Connecting);
    CHECK_FALSE(port.IsOpen());

    port.Close();
    CHECK(port.GetState() == SerialPort::State::Idle);
}

TEST_CASE("SerialPort destructor joins a connect worker that is still running")
{
    // ~SerialPort calls Close(), which must join the connect thread. Getting this
    // wrong is a std::terminate on a joinable thread, or a worker writing through
    // a destroyed `this`.
    const auto start = Clock::now();
    for (int i = 0; i < 15; ++i)
    {
        SerialPort port;
        port.BeginOpen(kUnreachablePort, 115200);
        // No Close(), no wait — destruction happens here, mid-connect.
    }
    CHECK(MillisSince(start) < 30000);
}

TEST_CASE("SerialPort reuse: repeated failed opens do not leak threads or wedge state")
{
    SerialPort port;

    for (int i = 0; i < 20; ++i)
    {
        CAPTURE(i);
        CHECK_FALSE(port.Open(kUnreachablePort, 115200));
        CHECK(port.GetState() == SerialPort::State::Failed);
        port.Close();
        CHECK(port.GetState() == SerialPort::State::Idle);
    }

    // Mixed sync/async reuse of the same object.
    for (int i = 0; i < 10; ++i)
    {
        port.BeginOpen(kUnreachablePort, 115200);
        WaitWhileConnecting(port);
        port.Close();
        CHECK_FALSE(port.Open(kUnreachablePort, 115200));
        port.Close();
    }
    CHECK(port.GetState() == SerialPort::State::Idle);
}

// =============================================================================
// SerialLink — policy layer
// =============================================================================

TEST_CASE("SerialLink starts idle and stays bounded while pumped")
{
    SerialLink link;

    CHECK_FALSE(link.IsOpen());
    CHECK_FALSE(link.IsReceiving());
    CHECK(link.GetState() == SerialPort::State::Idle);
    CHECK_FALSE(link.WantConnection());
    CHECK(link.Poll().empty());

    // 600 frames at 60 Hz = 10 s of simulated time, which crosses both the ~1 Hz
    // port rescan and several k_ReconnectInterval windows. Real time must stay
    // tiny: nothing on this path may block, even with no hardware present.
    const auto start = Clock::now();
    for (int i = 0; i < 600; ++i)
    {
        link.OnUpdate(1.0f / 60.0f);
        CHECK(link.Poll().empty());
    }
    CHECK(MillisSince(start) < 15000);

    CHECK_FALSE(link.IsOpen());
    CHECK_FALSE(link.IsReceiving());
}

TEST_CASE("SerialLink::SecondsSinceLastByte advances with the clock while nothing arrives")
{
    SerialLink link;

    const float before = link.SecondsSinceLastByte();
    for (int i = 0; i < 120; ++i)
        link.OnUpdate(1.0f / 60.0f);
    const float after = link.SecondsSinceLastByte();

    // Two seconds of simulated time with no bytes.
    CHECK(after > before);
    CHECK((after - before) == doctest::Approx(2.0f).epsilon(0.05));

    // Negative dt is absorbed (OnUpdate uses |dt|) rather than winding the clock
    // backwards into a permanently "receiving" state.
    const float beforeNeg = link.SecondsSinceLastByte();
    link.OnUpdate(-1.0f);
    CHECK(link.SecondsSinceLastByte() >= beforeNeg);
}

TEST_CASE("SerialLink::ConsumeJustConnected is a one-shot that starts false")
{
    SerialLink link;

    // No connection has ever been made, so there is no edge to report — and it
    // must not latch true on its own during pumping.
    CHECK_FALSE(link.ConsumeJustConnected());
    for (int i = 0; i < 120; ++i)
        link.OnUpdate(1.0f / 60.0f);
    CHECK_FALSE(link.ConsumeJustConnected());
    CHECK_FALSE(link.ConsumeJustConnected());
}

TEST_CASE("SerialLink::Shutdown and Disconnect clear the reconnect intent")
{
    SerialLink link;

    // Connect() is a no-op with no port selected, which is exactly the state on a
    // machine with no serial hardware — the intent flag must not be set by a
    // connect that never happened.
    if (SerialPort::GetAvailablePorts().empty())
    {
        link.OnUpdate(1.0f / 60.0f);   // lets the port rescan run and find nothing
        link.Connect();
        CHECK_FALSE(link.WantConnection());
        CHECK(link.SelectedPort().empty());
    }
    else
    {
        MESSAGE("Serial hardware present - skipping the no-port Connect() case so the "
                "suite never opens a real device.");
    }

    // Both teardown paths must leave the link inert, whatever came before, and
    // must be safe to call repeatedly and without a prior Connect.
    CHECK_NOTHROW(link.Disconnect());
    CHECK_FALSE(link.WantConnection());
    CHECK_FALSE(link.IsOpen());

    CHECK_NOTHROW(link.Shutdown());
    CHECK_FALSE(link.WantConnection());
    CHECK_FALSE(link.IsOpen());
    CHECK(link.GetState() == SerialPort::State::Idle);

    CHECK_NOTHROW(link.Shutdown());
    CHECK_NOTHROW(link.Disconnect());
    CHECK_FALSE(link.WantConnection());

    // Pumping after shutdown stays inert and bounded.
    const auto start = Clock::now();
    for (int i = 0; i < 300; ++i)
        link.OnUpdate(1.0f / 60.0f);
    CHECK(MillisSince(start) < 10000);
    CHECK_FALSE(link.IsOpen());
}

TEST_CASE("SerialLink::Write on a closed link fails without opening anything")
{
    SerialLink link;

    const std::string payload = "$W,30,1650,900,240,700*4C\n";
    CHECK_FALSE(link.Write(payload));
    CHECK_FALSE(link.Write(payload.data(), payload.size()));
    CHECK_FALSE(link.IsOpen());

    link.Shutdown();
    CHECK_FALSE(link.Write(payload));
}

TEST_CASE("SerialLink destruction while pumping is clean")
{
    // The link owns its SerialPort by value, so destruction has to unwind the
    // port's threads. No hardware involved — this is the plain teardown path.
    const auto start = Clock::now();
    for (int i = 0; i < 20; ++i)
    {
        SerialLink link;
        for (int f = 0; f < 30; ++f)
            link.OnUpdate(1.0f / 60.0f);
    }
    CHECK(MillisSince(start) < 20000);
}
