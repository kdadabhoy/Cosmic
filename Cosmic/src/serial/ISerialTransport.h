#pragma once
// ISerialTransport.h
//
// ============================================================================
// The OS-boundary seam for SerialPort (WO-04, 2D stability campaign)
// ============================================================================
//
// SerialPort's connection state machine — the workers, cancellation jobs,
// manual-reset stop events, and every State transition — is the code that shipped
// the Bluetooth-drop bugs, so it is exactly what tests must exercise. But its
// interesting states (a port that opens, receives bytes, then drops) originally
// had no headless entry point (KI-2); WO-04 made those states reachable here.
//
// This interface extracts ONLY the four Win32 syscalls SerialPort makes plus the
// registry scan, behind one boundary, so a test can drive the REAL state machine
// over an injected transport (a fake) without hardware. The parser, the state
// machine, and SerialLink's auto-reconnect policy stay exactly as they ship.
//
// The default is Win32SerialTransport. WO-05 changes cancellation and lifetime
// ordering while preserving wire bytes/port parameters. A test injects a fake through
// SerialPort's / SerialLink's test-only constructor. Nothing test-only (the fake)
// lives in the engine or ships in a package — it lives under tests/.
//
// Spec of record:
//   docs/plans/2d-stability-2026-09-16/evidence/WO-05a/transport-seam-spec.md
// ============================================================================

#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>

namespace Cosmic
{
	// Outcome of a single blocking Read on the transport.
	struct ReadResult
	{
		// Number of valid bytes written to the caller's buffer. May be 0 even on
		// Status::Data (a benign timeout / spurious wake) — the read loop simply
		// iterates again.
		std::size_t bytes = 0;

		enum class Status
		{
			Data,     // `bytes` were delivered (possibly 0); keep reading.
			Dropped,  // the device went away — drives SerialPort into State::Failed.
			Aborted   // the stop event was signalled — the session is tearing down.
		} status = Status::Data;
	};

	// The transport boundary. Every method maps to exactly one place in the shipping
	// SerialPort (see the mapping table in transport-seam-spec.md §2). Nothing about
	// lifecycle policy lives here: the stop event is owned and signalled by SerialPort,
	// and merely passed in as an opaque handle so a transport that honours cancellation
	// (the fake) can observe it.
	class ISerialTransport
	{
	public:
		virtual ~ISerialTransport() = default;

		// Blocking open (CreateFileA + DCB + timeouts on Win32). `stopEvent` is the
		// SerialPort-owned cancel handle: an implementation that can cancel a stalled
		// open observes cancellation. SerialPort also requests CancelSynchronousIo
		// on Win32; unsupported driver cancellation uses shared-owned late cleanup.
		virtual bool Open(const std::string& portName, std::uint32_t baudRate, void* stopEvent) = 0;

		// Blocking read. Returns on data, a device drop, or `stopEvent` being signalled
		// (overlapped ReadFile + WaitForMultipleObjects(stop, readDone) + CancelIoEx on
		// Win32). Writes at most `cap` bytes into `buf`.
		virtual ReadResult Read(char* buf, std::size_t cap, void* stopEvent) = 0;

		// Blocking write observes stopEvent, then drains completion before returning.
		// True only when every byte was accepted; never return with borrowed data live.
		virtual bool Write(const void* data, std::size_t length, void* stopEvent) = 0;

		// Release the device and unblock any pending Read (CancelIoEx + CloseHandle on
		// Win32). Safe to call when never opened.
		virtual void Close() = 0;

		// Discover available ports (the HKLM SERIALCOMM registry scan on Win32).
		virtual std::vector<std::string> List() = 0;
	};
}
