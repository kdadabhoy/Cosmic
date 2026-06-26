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
		// Always tear down any previous session before opening a new one. After an
		// auto-disconnect (device unplugged) m_Connected is already false but the
		// read thread is still joinable and m_Handle is still valid — so guarding
		// on m_Connected would leak the handle and then std::terminate() when we
		// reassign m_ReadThread below. Close() is safe to call when idle.
		Close();

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
							break;
						}
					}
				}
				else // immediate failure — the device is gone
				{
					CS_CORE_WARN("SerialPort: ReadFile error {0} — device disconnected.", err);
					m_Connected = false;
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
	 * Close
	 * * CLEAN SHUTDOWN: Signals the stop event (which the read thread waits on), so
	 * the ReadLoop returns immediately even if a read is pending on a stalled port.
	 * join() is therefore guaranteed to be prompt — this is what keeps app shutdown
	 * (and the frequent auto-reconnect Close/Open cycle) from hanging.
	 */
	void SerialPort::Close()
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