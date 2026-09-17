#include "SerialPort.h"
#include "serial/Win32SerialTransport.h"

#include <windows.h>
#include <string.h>
#include <iostream>
#include <utility>
#include "core/Log.h"

namespace Cosmic
{
	// Default: the shipping Win32 transport. This is the only path production takes.
	SerialPort::SerialPort()
		: m_Transport(std::make_unique<Win32SerialTransport>()) {}

	// Test-only seam: run the real state machine over an injected transport.
	SerialPort::SerialPort(std::unique_ptr<ISerialTransport> transport)
		: m_Transport(std::move(transport)) {}

	SerialPort::~SerialPort() { Close(); }

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Open
	 * * THE HARDWARE HANDSHAKE:
	 * 1. Opens the COM port via CreateFileA (OVERLAPPED so reads can be cancelled
	 * deterministically). The "\\\\.\\" prefix supports port numbers above COM9.
	 * 2. Configures the Device Control Block (DCB) for 8N1.
	 * 3. Creates a manual-reset stop event the read thread waits on alongside the
	 * pending read, so Close() can wake it instantly even on a stalled port.
	 */
	bool SerialPort::Open(const std::string& portName, uint32_t baudRate)
	{
		// Refuse to race an in-flight asynchronous connect: BeginOpen's worker may be
		// inside DoOpen right now, and running a second DoOpen here would have both
		// threads writing m_Handle concurrently. (BeginOpen has the same guard.)
		if (m_State.load() == State::Connecting)
		{
			CS_CORE_WARN("SerialPort::Open: an asynchronous connect is already in flight — ignored.");
			return false;
		}

		// Synchronous (blocking) open. Tear down any previous read session first —
		// after an auto-disconnect (device unplugged) m_Connected is already false
		// but the read thread is still joinable and m_Handle is still valid, so
		// skipping this would leak the handle and then std::terminate() when we
		// reassign m_ReadThread below. CloseReadSession() is safe to call when idle.
		CloseReadSession();

		m_Abandon.store(false);
		m_State.store(State::Connecting);
		const bool ok = DoOpen(portName, baudRate);
		m_State.store(ok ? State::Open : State::Failed);
		return ok;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * BeginOpen
	 * * NON-BLOCKING CONNECT: CreateFileA on an unreachable Bluetooth SPP port can
	 * block for 10-20 s before failing. Running it on the main/render thread froze
	 * the UI (and, via the 3 s auto-reconnect retry, kept refreezing it). BeginOpen
	 * moves the blocking open onto a one-shot worker thread so the UI stays live;
	 * callers poll GetState().
	 */
	void SerialPort::BeginOpen(const std::string& portName, uint32_t baudRate)
	{
		// Never stack connect attempts — one in-flight worker at a time.
		if (m_State.load() == State::Connecting) return;

		// We are not Connecting, so the previous worker (if any) has finished:
		// joining it here cannot block.
		if (m_ConnectThread.joinable()) m_ConnectThread.join();

		// Drop any current session synchronously — fast, the read thread wakes on
		// the stop event. Only the CreateFileA below is slow, and it runs off-thread.
		CloseReadSession();

		m_Abandon.store(false);
		m_State.store(State::Connecting);
		m_ConnectThread = std::thread([this, portName, baudRate]()
		{
			const bool ok = DoOpen(portName, baudRate);
			m_State.store(ok ? State::Open : State::Failed);
			// If Close() was requested while we were blocked in CreateFileA, tear the
			// freshly-opened session back down so nothing leaks.
			if (ok && m_Abandon.load())
				CloseReadSession();
		});
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * DoOpen
	 * * The actual blocking open. Assumes any previous read session was already torn
	 * down by the caller (Open/BeginOpen call CloseReadSession first).
	 */
	bool SerialPort::DoOpen(const std::string& portName, uint32_t baudRate)
	{
		// Create the stop event (synchronization owned by the state machine) BEFORE the
		// open, so a transport that honours cancellation (the test seam) can observe it
		// while inside a blocking open. The Win32 transport ignores it — CreateFileA
		// cannot be cancelled, which is KI-4 — so shipping behaviour is unchanged.
		m_StopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr); // manual-reset, unsignalled

		if (!m_Transport->Open(portName, baudRate, m_StopEvent))
		{
			if (m_StopEvent) { CloseHandle(m_StopEvent); m_StopEvent = nullptr; }
			return false;
		}

		m_Connected = true;
		m_ReadThread = std::thread(&SerialPort::ReadLoop, this);
		return true;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * ReadLoop
	 * * ASYNC INGESTION: runs on its own thread. Issues an overlapped ReadFile and
	 * waits on BOTH the read-completion event and the stop event, so a Close() can
	 * abort a pending read instantly — no matter how wedged the port is. Data is
	 * appended to the thread-safe buffer.
	 */
	void SerialPort::ReadLoop()
	{
		DWORD winThreadId = GetCurrentThreadId();
		int core = GetCurrentProcessorNumber();
		CS_CORE_INFO("[SERIAL THREAD] Started with ID: {0} on Core: {1}", winThreadId, core);

		char buf[256];

		// The loop and buffer-append (state machine + data bridge) stay here; the
		// overlapped ReadFile + wait-on-stop is the transport's job now.
		while (m_Connected)
		{
			ReadResult r = m_Transport->Read(buf, sizeof(buf), m_StopEvent);

			if (r.status == ReadResult::Status::Aborted) // stop requested — exit
				break;

			if (r.status == ReadResult::Status::Dropped) // the device is gone
			{
				m_Connected = false;
				m_State.store(State::Failed);
				break;
			}

			if (r.bytes > 0)
			{
				std::lock_guard<std::mutex> lock(m_BufferMutex);
				m_DataBuffer.append(buf, r.bytes); // exact bytes (may contain embedded NULs)
			}
		}

		CS_CORE_INFO("[SERIAL THREAD] Shutting down.");
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * FlushBuffer
	 * * BRIDGE TO MAIN THREAD: Safely extracts all data collected by the
	 * background thread and clears the source buffer in one atomic-like operation.
	 */
	std::string SerialPort::FlushBuffer()
	{
		std::lock_guard<std::mutex> lock(m_BufferMutex);
		std::string temp = m_DataBuffer;
		m_DataBuffer.clear();
		return temp;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Write
	 * * TRANSMISSION: the port was opened FILE_FLAG_OVERLAPPED, so writes must be
	 * overlapped too. Each call uses its own OVERLAPPED + event and waits for
	 * completion — bounded by the OS transmit buffer, and safe to run while the
	 * read thread has its own pending overlapped read on the same handle.
	 */
	bool SerialPort::Write(const void* data, size_t length)
	{
		// Guard on connected-state here (unchanged contract); the transport performs
		// the actual overlapped write.
		if (!m_Connected || length == 0)
			return false;

		return m_Transport->Write(data, length);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * CloseReadSession
	 * * Tears down the read thread + handle only. Signals the stop event (which the
	 * read thread waits on), so ReadLoop returns immediately even if a read is
	 * pending on a stalled port — join() is therefore prompt. Does NOT touch the
	 * connect thread, so the connect worker can call it safely (no self-join).
	 */
	void SerialPort::CloseReadSession()
	{
		m_Connected = false;
		if (m_StopEvent)
			SetEvent(m_StopEvent);          // wake the read thread instantly
		if (m_ReadThread.joinable())
			m_ReadThread.join();

		// Release the device only after the reader has joined, so CloseHandle can never
		// race a pending read. (The transport also CancelIoEx's here — redundant on the
		// reachable path since the stop event already woke the reader, but kept as the
		// device-release primitive.)
		m_Transport->Close();

		if (m_StopEvent)
		{
			CloseHandle(m_StopEvent);
			m_StopEvent = nullptr;
		}
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Close
	 * * CLEAN SHUTDOWN: joins any in-flight connect worker, then tears down the read
	 * session. m_Abandon tells a worker still blocked in CreateFileA to self-close
	 * the moment it returns, so the connect join here stays bounded even against a
	 * dead Bluetooth port. Safe to call when already idle, and from the destructor.
	 */
	void SerialPort::Close()
	{
		m_Abandon.store(true);
		if (m_ConnectThread.joinable())
			m_ConnectThread.join();
		CloseReadSession();
		m_State.store(State::Idle);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * GetAvailablePorts
	 * * REGISTRY DISCOVERY: Windows does not have a simple "ListPorts" function.
	 * This method parses the Windows Registry at 'SERIALCOMM' to find
	 * which hardware communication ports are currently recognized by the OS.
	 */
	std::vector<std::string> SerialPort::GetAvailablePorts()
	{
		// The registry scan lives in the Win32 transport now (single implementation).
		// This static keeps the long-standing public API and is what a caller without a
		// SerialPort instance uses.
		return Win32SerialTransport{}.List();
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * ListPorts
	 * * Instance discovery through this port's transport. For the default Win32
	 * transport this is identical to GetAvailablePorts(); for an injected fake it
	 * returns the test-set list, which is how SerialLink::RefreshPorts can select a
	 * fake port without hardware.
	 */
	std::vector<std::string> SerialPort::ListPorts()
	{
		return m_Transport->List();
	}
}