#pragma once
// ISerialTransport.h
//
// ============================================================================
// The OS-boundary seam for SerialPort (WO-04, 2D stability campaign)
// ============================================================================
//
// SerialPort's connection state machine — the two std::threads, m_Abandon, the
// manual-reset stop event, and every State transition — is the code that shipped
// the Bluetooth-drop bugs, so it is exactly what tests must exercise. But its
// interesting states (a port that opens, receives bytes, then drops) have no
// headless entry point: SerialPort only opens a real device via CreateFileA and
// SerialLink only discovers ports from the Windows registry (KI-2).
//
// This interface extracts ONLY the four Win32 syscalls SerialPort makes plus the
// registry scan, behind one boundary, so a test can drive the REAL state machine
// over an injected transport (a fake) without hardware. The parser, the state
// machine, and SerialLink's auto-reconnect policy stay exactly as they ship.
//
// The default is Win32SerialTransport, which is today's code verbatim; the
// shipping build is byte-identical in behaviour. A test injects a fake through
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
	// synchronization lives here: the stop event is owned and signalled by SerialPort,
	// and merely passed in as an opaque handle so a transport that honours cancellation
	// (the fake) can observe it.
	class ISerialTransport
	{
	public:
		virtual ~ISerialTransport() = default;

		// Blocking open (CreateFileA + DCB + timeouts on Win32). `stopEvent` is the
		// SerialPort-owned cancel handle: an implementation that can cancel a stalled
		// open returns promptly once it is signalled. The Win32 transport CANNOT
		// cancel CreateFileA and so ignores it — that inability is KI-4, and honouring
		// `stopEvent` in a fake is what lets WO-05 assert the bounded-close contract.
		virtual bool Open(const std::string& portName, std::uint32_t baudRate, void* stopEvent) = 0;

		// Blocking read. Returns on data, a device drop, or `stopEvent` being signalled
		// (overlapped ReadFile + WaitForMultipleObjects(stop, readDone) + CancelIoEx on
		// Win32). Writes at most `cap` bytes into `buf`.
		virtual ReadResult Read(char* buf, std::size_t cap, void* stopEvent) = 0;

		// Bounded blocking write (overlapped WriteFile on Win32). True only when every
		// byte was accepted.
		virtual bool Write(const void* data, std::size_t length, void* stopEvent) = 0;

		// Release the device and unblock any pending Read (CancelIoEx + CloseHandle on
		// Win32). Safe to call when never opened.
		virtual void Close() = 0;

		// Discover available ports (the HKLM SERIALCOMM registry scan on Win32).
		virtual std::vector<std::string> List() = 0;
	};
}
