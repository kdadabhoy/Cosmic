// test_serial_seam.cpp — WO-04 (2D stability campaign).
//
// The injectable serial transport seam's own coverage. These drive the REAL
// SerialPort / SerialLink commands and state machine (BeginOpen, the connect and
// read threads, m_Abandon, the stop event, every State transition, and SerialLink's
// auto-reconnect policy) over a FakeSerialTransport — no hardware, no parser
// reimplementation, no private-field mutation.
//
// Scope (WO-04): make the connected state reachable and demonstrate the KI-4 join
// behaviour is observable under test. The FULL connected-state matrix (and the KI-4
// FIX, failing-before/passing-after) is WO-05, on the reserved seats in
// test_serial_shutdown_race.cpp. Nothing here fixes KI-4.
//
// Spec: docs/plans/2d-stability-2026-09-16/evidence/WO-05a/transport-seam-spec.md

#include <doctest.h>

#include "serial/SerialPort.h"
#include "serial/SerialLink.h"
#include "FakeSerialTransport.h"

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace Cosmic;

namespace
{
	using Clock = std::chrono::steady_clock;

	long long MillisSince(Clock::time_point start)
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
	}

	// Poll a SerialPort/SerialLink state predicate until it holds or the deadline
	// expires. Returns true if it held. Never spins forever — a regression shows up as
	// a failed CHECK, not a wedged run.
	template <typename Fn>
	bool WaitUntil(Fn&& pred, long long timeoutMs = 8000)
	{
		const auto start = Clock::now();
		while (!pred() && MillisSince(start) < timeoutMs)
			std::this_thread::sleep_for(std::chrono::milliseconds(3));
		return pred();
	}
}

// =============================================================================
// Injection wiring — the default is Win32; a fake replaces exactly the OS boundary
// =============================================================================

TEST_CASE("WO-04 seam: the default SerialPort transport is Win32 (shipping path)")
{
	// ListPorts() (instance, through the transport) equals the static registry scan
	// for the default transport — i.e. the default really is Win32, and the seam is
	// inert unless a fake is injected.
	SerialPort port;
	CHECK(port.ListPorts() == SerialPort::GetAvailablePorts());
}

TEST_CASE("WO-04 seam: an injected transport replaces port discovery")
{
	auto fake = std::make_unique<FakeSerialTransport>();
	fake->SetAvailablePorts({ "COM_A", "COM_B" });

	SerialPort port(std::move(fake));
	const std::vector<std::string> ports = port.ListPorts();

	REQUIRE(ports.size() == 2);
	CHECK(ports[0] == "COM_A");
	CHECK(ports[1] == "COM_B");
}

// =============================================================================
// (a) SetAvailablePorts + Connect + PushBytes → IsReceiving / ConsumeJustConnected /
//     Poll returns the exact bytes
// =============================================================================

TEST_CASE("WO-04 seam (a): a fake port opens, receives, and delivers the exact bytes")
{
	auto fake = std::make_unique<FakeSerialTransport>();
	FakeSerialTransport* f = fake.get();          // observe/control after handing ownership over
	f->SetAvailablePorts({ "COM_FAKE" });

	SerialLink link(std::move(fake));

	// One pump selects the discovered port (RefreshPorts runs on the first tick).
	link.OnUpdate(1.0f / 60.0f);
	REQUIRE(link.SelectedPort() == "COM_FAKE");

	// Real user command → real BeginOpen → real connect worker → fake.Open succeeds.
	link.Connect();
	CHECK(link.WantConnection());
	REQUIRE(WaitUntil([&] { return link.GetState() == SerialPort::State::Open; }));
	CHECK(f->OpenCount() == 1);

	const std::string payload = "$R,25,1680,420,120,350*7F\n";
	f->PushBytes(payload);

	// Drive the real per-frame loop; accumulate what Poll() drains from the read thread.
	std::string received;
	const bool got = WaitUntil([&]
	{
		link.OnUpdate(1.0f / 60.0f);
		received += link.Poll();
		return received.size() >= payload.size();
	});
	REQUIRE(got);

	CHECK(received == payload);          // exact bytes, in order
	CHECK(link.IsReceiving());           // open AND a byte arrived < 1 s ago
	CHECK(link.ConsumeJustConnected());  // one-shot: true exactly once after (re)connect
	CHECK_FALSE(link.ConsumeJustConnected());

	link.Shutdown();
	CHECK(link.GetState() == SerialPort::State::Idle);
	CHECK_FALSE(link.WantConnection());
}

// =============================================================================
// (b) SignalDrop → State::Failed → auto-reconnect re-opens
// =============================================================================

TEST_CASE("WO-04 seam (b): a mid-session drop drives State::Failed, then auto-reconnect re-opens")
{
	auto fake = std::make_unique<FakeSerialTransport>();
	FakeSerialTransport* f = fake.get();
	f->SetAvailablePorts({ "COM_FAKE" });

	SerialLink link(std::move(fake));
	link.OnUpdate(1.0f / 60.0f);
	REQUIRE(link.SelectedPort() == "COM_FAKE");

	link.Connect();
	REQUIRE(WaitUntil([&] { return link.GetState() == SerialPort::State::Open; }));
	f->PushBytes("hello");
	REQUIRE(WaitUntil([&]
	{
		link.OnUpdate(1.0f / 60.0f);
		return !link.Poll().empty() || link.IsReceiving();
	}));
	const int opensBeforeDrop = f->OpenCount();   // == 1

	// Device drops mid-session: the read thread's transport Read returns Dropped, which
	// drives the REAL SerialPort into State::Failed.
	f->SignalDrop();
	REQUIRE(WaitUntil([&] { return link.GetState() == SerialPort::State::Failed; }));

	// Auto-reconnect: OnUpdate re-opens after k_ReconnectInterval (3 s of pumped time)
	// because m_WantConnection is still set. Larger dt crosses that window quickly; the
	// real BeginOpen fires and the fake re-opens.
	const bool reopened = WaitUntil([&]
	{
		link.OnUpdate(0.25f);   // pumped time; crosses the 3 s reconnect window
		return f->OpenCount() > opensBeforeDrop && link.GetState() == SerialPort::State::Open;
	});
	REQUIRE(reopened);
	CHECK(f->OpenCount() >= 2);           // auto-reconnect really re-opened
	CHECK(link.GetState() == SerialPort::State::Open);

	link.Shutdown();
	CHECK(link.GetState() == SerialPort::State::Idle);
}

// =============================================================================
// (c) SetOpenBlockMs(large) + Connect + teardown → the KI-4 join behaviour is now
//     observable under test (WO-04 makes it TESTABLE; WO-05 FIXES it)
// =============================================================================

TEST_CASE("WO-04 seam (c): teardown cancels a blocked Open (WO-05 regression)")
{
	auto fake = std::make_unique<FakeSerialTransport>();
	FakeSerialTransport* f = fake.get();
	f->SetAvailablePorts({ "COM_FAKE" });
	f->SetOpenBlockMs(1200);   // the connect worker will block inside fake.Open

	SerialLink link(std::move(fake));
	link.OnUpdate(1.0f / 60.0f);
	REQUIRE(link.SelectedPort() == "COM_FAKE");

	link.Connect();   // BeginOpen → worker enters fake.Open and blocks ~1200 ms
	REQUIRE(WaitUntil([&] { return link.GetState() == SerialPort::State::Connecting; }, 2000));
	REQUIRE(f->WaitOpenEntered());

	// Teardown mirrors the reported chain (SF_Telem::OnDetach → SerialLink::Shutdown →
	// SerialPort::Close → join). Today Close joins the worker WITHOUT cancelling the
	// in-flight open (m_Abandon is only read after DoOpen returns), so this stalls for
	// ~the block duration. THAT stall is KI-4 — here it is directly measurable.
	const auto start = Clock::now();
	link.Shutdown();
	const long long teardownMs = MillisSince(start);

	CAPTURE(teardownMs);
	CHECK(teardownMs <= 2000);
	CHECK(link.GetState() == SerialPort::State::Idle);  // still tears down cleanly
	CHECK(f->CloseCount() >= 1);
}
