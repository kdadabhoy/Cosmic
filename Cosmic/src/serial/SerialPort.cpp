#include "SerialPort.h"
#include <windows.h>
#include <string.h>
#include <iostream>
#include "core/Log.h"

namespace Cosmic
{
	SerialPort::SerialPort() : m_Handle(INVALID_HANDLE_VALUE) {}

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
		std::string fullPath = "\\\\.\\" + portName;
		// Opened with GENERIC_WRITE to support future command transmission (not yet exposed in the API).
		// FILE_FLAG_OVERLAPPED: reads are asynchronous so the read thread can wait on
		// our stop event and bail out the moment Close() is called — CancelIoEx alone
		// is unreliable on Bluetooth SPP ports and could hang the join() on shutdown.
		m_Handle = CreateFileA(fullPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL,
		                       OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

		if (m_Handle == INVALID_HANDLE_VALUE) return false;

		DCB dcbSerialParams = { 0 };
		dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
		if (!GetCommState(m_Handle, &dcbSerialParams))
		{
			CloseHandle(m_Handle); m_Handle = INVALID_HANDLE_VALUE; return false;
		}

		dcbSerialParams.BaudRate = baudRate;
		dcbSerialParams.ByteSize = 8;
		dcbSerialParams.StopBits = ONESTOPBIT;
		dcbSerialParams.Parity = NOPARITY;
		if (!SetCommState(m_Handle, &dcbSerialParams))
		{
			CloseHandle(m_Handle); m_Handle = INVALID_HANDLE_VALUE; return false;
		}

		// Return shortly after data arrives (10 ms inter-byte gap) or after a 100 ms
		// idle window — the overlapped wait below is what actually drives latency and
		// cancellation, so these just keep a pending read from lingering forever.
		COMMTIMEOUTS timeouts = { 0 };
		timeouts.ReadIntervalTimeout = 10;
		timeouts.ReadTotalTimeoutConstant = 100;
		timeouts.ReadTotalTimeoutMultiplier = 0;
		SetCommTimeouts(m_Handle, &timeouts);

		m_StopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr); // manual-reset, unsignalled

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

		char  buf[256];
		OVERLAPPED ov = {};
		ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr); // manual-reset

		while (m_Connected)
		{
			DWORD read = 0;
			ResetEvent(ov.hEvent);

			BOOL ok = ReadFile(m_Handle, buf, sizeof(buf), &read, &ov);
			if (!ok)
			{
				const DWORD err = GetLastError();
				if (err == ERROR_IO_PENDING)
				{
					// Wait until the read completes OR Close() signals stop.
					HANDLE waits[2] = { m_StopEvent, ov.hEvent };
					const DWORD w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
					if (w == WAIT_OBJECT_0) // stop requested — abort the pending read and exit
					{
						CancelIoEx(m_Handle, &ov);
						GetOverlappedResult(m_Handle, &ov, &read, TRUE); // drain before buf/ov die
						break;
					}
					if (!GetOverlappedResult(m_Handle, &ov, &read, FALSE))
					{
						const DWORD e2 = GetLastError();
						if (e2 != ERROR_OPERATION_ABORTED)
						{
							CS_CORE_WARN("SerialPort: read error {0} — device disconnected.", e2);
							m_Connected = false;
							m_State.store(State::Failed);
							break;
						}
					}
				}
				else // immediate failure — the device is gone
				{
					CS_CORE_WARN("SerialPort: ReadFile error {0} — device disconnected.", err);
					m_Connected = false;
					m_State.store(State::Failed);
					break;
				}
			}

			if (read > 0)
			{
				std::lock_guard<std::mutex> lock(m_BufferMutex);
				m_DataBuffer.append(buf, read); // exact bytes (may contain embedded NULs)
			}
		}

		CloseHandle(ov.hEvent);
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
		if (!m_Connected || m_Handle == INVALID_HANDLE_VALUE || length == 0)
			return false;

		OVERLAPPED ov = {};
		ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
		if (!ov.hEvent)
			return false;

		DWORD written = 0;
		BOOL ok = WriteFile(m_Handle, data, static_cast<DWORD>(length), &written, &ov);
		if (!ok)
		{
			if (GetLastError() == ERROR_IO_PENDING)
				ok = GetOverlappedResult(m_Handle, &ov, &written, TRUE);
			else
				CS_CORE_WARN("SerialPort: WriteFile error {0}.", GetLastError());
		}
		CloseHandle(ov.hEvent);
		return ok && written == static_cast<DWORD>(length);
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
		if (m_Handle != INVALID_HANDLE_VALUE)
			CancelIoEx(m_Handle, nullptr);  // belt-and-suspenders for the pending read
		if (m_ReadThread.joinable())
			m_ReadThread.join();

		if (m_Handle != INVALID_HANDLE_VALUE)
		{
			CloseHandle(m_Handle);
			m_Handle = INVALID_HANDLE_VALUE;
		}
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
		std::vector<std::string> ports;
		HKEY hKey;

		if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
		{
			char valueName[256];
			BYTE valueData[256];
			DWORD nameSize, dataSize, type;
			DWORD index = 0;

			while (true)
			{
				nameSize = sizeof(valueName);
				dataSize = sizeof(valueData);

				LSTATUS status = RegEnumValueA(hKey, index, valueName, &nameSize, NULL, &type, valueData, &dataSize);

				if (status == ERROR_SUCCESS)
				{
					ports.push_back(std::string((char*)valueData, strnlen((char*)valueData, dataSize)));
					index++;
				}
				else break;
			}
			RegCloseKey(hKey);
		}

		return ports;
	}
}