// test_serial_shutdown_race.cpp — WO-05a (2D stability campaign).
//
// DIAGNOSIS SEAT for the reported COM close / link-loss crash. See
//   docs/plans/2d-stability-2026-09-16/evidence/WO-05a/hazard-analysis.md
//   docs/plans/2d-stability-2026-09-16/evidence/WO-05a/transport-seam-spec.md
//
// The reported bug lives in the *connected* state — a port that opens, receives,
// and later drops — reached in SF_Telem through the shared-ownership chain
//   WorkspaceLayer::OnUpdate -> SF_Telem::OnUpdate -> SerialLink::OnUpdate
//     -> (auto-reconnect) SerialPort::BeginOpen           (a background worker)
// and torn down through
//   Application::Shutdown -> UnloadProjectDLL -> WorkspaceLayer::ClearViewportLayer
//     -> SF_Telem::OnDetach -> SerialLink::Shutdown -> SerialPort::Close (joins the worker).
//
// WO-05 activates the former ENVIRONMENT_BLOCKED H1/H3 seats on the WO-04
// injectable OS transport. They use the actual root-owned link and shared hub.
// Physical driver corroboration remains explicitly ENVIRONMENT_BLOCKED in T04.
//
// What IS reachable now, on the real production API and without reimplementing the
// state machine, is the exact "a connect worker spawned by a reconnect-style
// BeginOpen is still in flight when teardown's Close() joins it" contract that the
// exit-hang rides on. COM999 is well-formed but cannot exist, so its CreateFileA
// fails immediately — the join is bounded here, and the in-code notes mark where a
// *blocking* open (a real Bluetooth SPP port) turns this same join into H1.

#include <doctest.h>

#include "serial/SerialPort.h"
#include "serial/SerialLink.h"
#include "FakeSerialTransport.h"
#include "../Projects/SF_Telem/src/SF_Telem.h"

#include <chrono>
#include <string>
#include <thread>

using namespace Cosmic;

namespace
{
    constexpr const char* kUnreachablePort = "COM999";
    using Clock = std::chrono::steady_clock;

    long long MillisSince(Clock::time_point start)
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    }
}

// =============================================================================
// Reachable guard — the ownership-chain teardown contract (COM999, deterministic)
// =============================================================================

TEST_CASE("WO-05a: teardown joins a reconnect worker that is in flight (reachable path)")
{
    // Mirror the reported chain: the final OnUpdate before the window closes fires
    // an auto-reconnect BeginOpen (SerialLink.cpp:67 calls exactly this), then the
    // very next thing the host does is tear the link down (SerialLink::Shutdown ->
    // SerialPort::Close). Close() MUST join the worker so nothing outlives the port.
    for (int i = 0; i < 25; ++i)
    {
        CAPTURE(i);
        SerialPort port;

        port.BeginOpen(kUnreachablePort, 115200);   // reconnect worker spawned (SerialPort.cpp:59)

        const auto start = Clock::now();
        // Teardown while the worker may still be inside DoOpen/CreateFileA. On COM999
        // that open fails immediately, so the join is bounded and this is fast. On a
        // real Bluetooth SPP port CreateFileA blocks ~10-20 s and m_Abandon does NOT
        // cancel it (SerialPort.cpp:80-82, :294-301), so THIS SAME JOIN becomes H1's
        // multi-second exit hang. That timing is the env-blocked half, below.
        CHECK_NOTHROW(port.Close());
        const long long joinMs = MillisSince(start);

        CHECK(port.GetState() == SerialPort::State::Idle);
        CHECK_FALSE(port.IsOpen());
        CHECK(joinMs < 5000);   // bounded for a fast-failing port; documents the H1 boundary
    }
}

TEST_CASE("WO-05a: repeated reconnect churn then teardown stays bounded and leak-free")
{
    // The 3 s auto-reconnect retry (SerialLink k_ReconnectInterval) drives BeginOpen
    // over and over after a drop; each spins up a worker and CloseReadSession joins
    // the prior one. Reproduce the churn shape on one port object, then tear down.
    SerialPort port;

    const auto start = Clock::now();
    for (int r = 0; r < 6; ++r)
    {
        CAPTURE(r);
        CHECK_NOTHROW(port.BeginOpen(kUnreachablePort, 115200));
        std::this_thread::sleep_for(std::chrono::milliseconds(2));  // let the worker start/finish
    }
    CHECK_NOTHROW(port.Close());

    CHECK(port.GetState() == SerialPort::State::Idle);
    CHECK_FALSE(port.IsOpen());
    CHECK(MillisSince(start) < 15000);
}

TEST_CASE("WO-05a: SerialLink pumped without Connect never opens a port (KI-2 boundary)")
{
    // The KI-2 boundary from the policy layer, stated hardware-independently:
    // OnUpdate's auto-reconnect BeginOpen (SerialLink.cpp:56-67) is gated on
    // m_WantConnection, which is set ONLY by Connect(). Without a Connect() the link
    // pumps forever, may *select* a port it discovered (this machine exposes a legacy
    // COM1 = \Device\Serial0), but never sets the intent and never opens anything.
    // We deliberately do NOT call Connect(): that would open a real device, exactly
    // what the suite avoids — and it is why the connected-state repro is env-blocked
    // (no Bluetooth SPP / virtual COM whose stream we control).
    SerialLink link;
    for (int f = 0; f < 300; ++f)   // 5 s simulated: crosses port-scan + reconnect windows
        link.OnUpdate(1.0f / 60.0f);

    CHECK_FALSE(link.WantConnection());   // no Connect() -> reconnect BeginOpen never fires
    CHECK_FALSE(link.IsOpen());
    CHECK(link.GetState() == SerialPort::State::Idle);
    CHECK_NOTHROW(link.Shutdown());
    CHECK(link.GetState() == SerialPort::State::Idle);
}

// =============================================================================
// Activated WO-05a regressions — real root, controlled OS transport
// =============================================================================

TEST_CASE("WO-05a H1: exit hang - close while a reconnect worker blocks in CreateFileA")
{
    auto fake = std::make_unique<FakeSerialTransport>();
    auto* f = fake.get();
    f->SetAvailablePorts({ "COM_FAKE" });
    f->SetOpenBlockMs(2500);
    auto root = std::make_unique<Workspace::SF_Telem>(std::move(fake));
    root->InitializeServices();
    root->OnUpdate(0.0f);
    root->Link().Connect();
    REQUIRE(f->WaitOpenEntered());
    const auto start = Clock::now();
    root->OnDetach();
    CHECK(MillisSince(start) <= 2000);
    CHECK(root->Link().GetState() == SerialPort::State::Idle);
    CHECK_FALSE(root->Link().WantConnection());

}

TEST_CASE("WO-05a H3: connected-state drop - lose an open port then close")
{
    auto fake = std::make_unique<FakeSerialTransport>();
    auto* f = fake.get();
    auto counts = f->Counts();
    f->SetAvailablePorts({"COM_FAKE"});
    auto root = std::make_unique<Workspace::SF_Telem>(std::move(fake));
    root->InitializeServices(); root->SetScreen(Workspace::SF_Telem::SCREEN_MAIN);
    root->OnUpdate(0.0f); root->Link().Connect();
    auto wait = [&](auto predicate)
    {
        const auto deadline = Clock::now() + std::chrono::seconds(2);
        while (!predicate() && Clock::now() < deadline) std::this_thread::yield();
        return predicate();
    };
    REQUIRE(wait([&] { return root->Link().GetState() == SerialPort::State::Open; }));
    root->OnUpdate(0.0f);
    REQUIRE(wait([&] { return counts->reads == 1; }));
    const int reads = counts->readCalls;
    f->PushBytes("hello\n");
    REQUIRE(wait([&] { return counts->readCalls > reads; }));
    root->OnUpdate(0.0f); CHECK(root->Link().IsReceiving());
    f->SignalDrop();
    REQUIRE(wait([&] { return root->Link().GetState() == SerialPort::State::Failed; }));
    f->SetOpenBarrier(); root->OnUpdate(3.0f);
    REQUIRE(f->WaitOpenEntered());
    const auto start = Clock::now(); root->OnDetach();
    CHECK(MillisSince(start) <= 2000);
    CHECK_FALSE(root->Link().WantConnection());
    root.reset();
    CHECK(counts->destroyed.load()); CHECK(counts->handles == 0);
    CHECK(counts->opens == 0); CHECK(counts->reads == 0);
}

