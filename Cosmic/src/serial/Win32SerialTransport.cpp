// Win32SerialTransport.cpp
//
// The shipping serial transport: the same CreateFileA / overlapped ReadFile /
// overlapped WriteFile / CancelIoEx+CloseHandle / SERIALCOMM registry calls that
// used to live in SerialPort.cpp, moved behind ISerialTransport (WO-04).
// WO-05 adds cancellation-aware write completion and late-open cleanup; wire
// bytes and port parameters remain unchanged. SerialPort owns the state machine.

#include "serial/Win32SerialTransport.h"

#include <windows.h>
#include <string.h>

#include "core/Log.h"

namespace Cosmic
{
	Win32SerialTransport::~Win32SerialTransport() { Close(); }

	/////////////////////////////////////////////////////////////////////////////////

	// CreateFileA -> DCB (8N1) -> COMMTIMEOUTS, exactly as the old SerialPort::DoOpen.
	// SerialPort requests synchronous cancellation; a driver may ignore it. The
	// worker's shared-owned cancellation block then handles any late result safely.
	bool Win32SerialTransport::Open(const std::string& portName, std::uint32_t baudRate, void* stopEvent)
	{
		std::string fullPath = "\\\\.\\" + portName;
		// Opened with GENERIC_WRITE to support command transmission. FILE_FLAG_OVERLAPPED:
		// reads are asynchronous so the read thread can wait on SerialPort's stop event
		// and bail out the moment Close() is called — CancelIoEx alone is unreliable on
		// Bluetooth SPP ports and could hang the join() on shutdown.
		m_Handle = CreateFileA(fullPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL,
		                       OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

		if (m_Handle == INVALID_HANDLE_VALUE) return false;
		if (WaitForSingleObject(static_cast<HANDLE>(stopEvent), 0) == WAIT_OBJECT_0)
		{
			Close();
			return false;
		}

		DCB dcbSerialParams = { 0 };
		dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
		if (!GetCommState(m_Handle, &dcbSerialParams))
		{
			CloseHandle(m_Handle); m_Handle = INVALID_HANDLE_VALUE; return false;
		}

		dcbSerialParams.BaudRate = baudRate;
		dcbSerialParams.ByteSize = 8;
		dcbSerialParams.StopBits = ONESTOPBIT;
		dcbSerialParams.Parity   = NOPARITY;
		if (!SetCommState(m_Handle, &dcbSerialParams))
		{
			CloseHandle(m_Handle); m_Handle = INVALID_HANDLE_VALUE; return false;
		}

		// Return shortly after data arrives (10 ms inter-byte gap) or after a 100 ms
		// idle window — the overlapped wait below is what actually drives latency and
		// cancellation, so these just keep a pending read from lingering forever.
		COMMTIMEOUTS timeouts = { 0 };
		timeouts.ReadIntervalTimeout        = 10;
		timeouts.ReadTotalTimeoutConstant   = 100;
		timeouts.ReadTotalTimeoutMultiplier = 0;
		SetCommTimeouts(m_Handle, &timeouts);

		// The overlapped-read completion event is reused across reads for this session
		// (the old ReadLoop created it once per session too); manual-reset.
		m_ReadOv = {};
		m_ReadOv.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
		if (!m_ReadOv.hEvent) { Close(); return false; }
		return true;
	}

	/////////////////////////////////////////////////////////////////////////////////

	// One overlapped ReadFile + WaitForMultipleObjects(stop, readDone), the body of
	// the old ReadLoop's inner iteration. The while-loop and the buffer append stay in
	// SerialPort::ReadLoop; this returns one outcome per call.
	ReadResult Win32SerialTransport::Read(char* buf, std::size_t cap, void* stopEvent)
	{
		HANDLE stop = static_cast<HANDLE>(stopEvent);
		DWORD  read = 0;
		ResetEvent(m_ReadOv.hEvent);

		BOOL ok = ReadFile(m_Handle, buf, static_cast<DWORD>(cap), &read, &m_ReadOv);
		if (!ok)
		{
			const DWORD err = GetLastError();
			if (err == ERROR_IO_PENDING)
			{
				// Wait until the read completes OR the stop event signals.
				HANDLE waits[2] = { stop, m_ReadOv.hEvent };
				const DWORD w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
				if (w == WAIT_OBJECT_0) // stop requested — abort the pending read and exit
				{
					CancelIoEx(m_Handle, &m_ReadOv);
					GetOverlappedResult(m_Handle, &m_ReadOv, &read, TRUE); // drain before buf dies
					return { 0, ReadResult::Status::Aborted };
				}
				if (!GetOverlappedResult(m_Handle, &m_ReadOv, &read, FALSE))
				{
					const DWORD e2 = GetLastError();
					if (e2 != ERROR_OPERATION_ABORTED)
					{
						CS_CORE_WARN("SerialPort: read error {0} — device disconnected.", e2);
						return { 0, ReadResult::Status::Dropped };
					}
					// ERROR_OPERATION_ABORTED with no stop signalled — benign; no data,
					// let the read loop iterate again.
					return { 0, ReadResult::Status::Data };
				}
			}
			else // immediate failure — the device is gone
			{
				CS_CORE_WARN("SerialPort: ReadFile error {0} — device disconnected.", err);
				return { 0, ReadResult::Status::Dropped };
			}
		}

		return { static_cast<std::size_t>(read), ReadResult::Status::Data };
	}

	/////////////////////////////////////////////////////////////////////////////////

	// Overlapped WriteFile — the body of the old SerialPort::Write.
	bool Win32SerialTransport::Write(const void* data, std::size_t length, void* stopEvent)
	{
		if (m_Handle == INVALID_HANDLE_VALUE || length == 0)
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
			{
				HANDLE waits[] = { static_cast<HANDLE>(stopEvent), ov.hEvent };
				const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, 1000);
				if (wait != WAIT_OBJECT_0 + 1)
				{
					CancelIoEx(m_Handle, &ov);
					GetOverlappedResult(m_Handle, &ov, &written, TRUE);
					ok = FALSE;
				}
				else ok = GetOverlappedResult(m_Handle, &ov, &written, FALSE);
			}
			else
				CS_CORE_WARN("SerialPort: WriteFile error {0}.", GetLastError());
		}
		CloseHandle(ov.hEvent);
		return ok && written == static_cast<DWORD>(length);
	}

	/////////////////////////////////////////////////////////////////////////////////

	// CancelIoEx + CloseHandle — the device-release half of the old CloseReadSession.
	// The stop-event signalling and the read-thread join stay in SerialPort, which
	// calls this only after the read thread has joined, so CancelIoEx is a no-op here
	// for the reachable path and CloseHandle never races the reader.
	void Win32SerialTransport::Close()
	{
		if (m_Handle != INVALID_HANDLE_VALUE)
		{
			CancelIoEx(m_Handle, nullptr);
			CloseHandle(m_Handle);
			m_Handle = INVALID_HANDLE_VALUE;
		}
		if (m_ReadOv.hEvent)
		{
			CloseHandle(m_ReadOv.hEvent);
			m_ReadOv.hEvent = nullptr;
		}
	}

	/////////////////////////////////////////////////////////////////////////////////

	// SERIALCOMM registry scan — the body of the old SerialPort::GetAvailablePorts.
	std::vector<std::string> Win32SerialTransport::List()
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
