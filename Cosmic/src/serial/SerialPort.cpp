#include "SerialPort.h"
#include <windows.h> 
#include <iostream>

namespace Cosmic
{
	SerialPort::SerialPort() : m_Handle(INVALID_HANDLE_VALUE) {}

	SerialPort::~SerialPort() { Close(); }

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Open
	 * * THE HARDWARE HANDSHAKE:
	 * 1. Opens the COM port via CreateFileA. The "\\\\.\\" prefix is used to
	 * support port numbers higher than COM9.
	 * 2. Configures the Device Control Block (DCB) for 8N1 (8 data bits,
	 * No parity, 1 stop bit).
	 * 3. Sets non-blocking timeouts to ensure the ReadFile call doesn't
	 * hang the background thread indefinitely.
	 */
	bool SerialPort::Open(const std::string& portName, uint32_t baudRate)
	{
		if (m_Connected) Close();

		std::string fullPath = "\\\\.\\" + portName;
		m_Handle = CreateFileA(fullPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

		if (m_Handle == INVALID_HANDLE_VALUE) return false;

		DCB dcbSerialParams = { 0 };
		dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
		if (!GetCommState(m_Handle, &dcbSerialParams)) return false;

		dcbSerialParams.BaudRate = baudRate;
		dcbSerialParams.ByteSize = 8;
		dcbSerialParams.StopBits = ONESTOPBIT;
		dcbSerialParams.Parity = NOPARITY;
		if (!SetCommState(m_Handle, &dcbSerialParams)) return false;

		COMMTIMEOUTS timeouts = { 0 };
		timeouts.ReadIntervalTimeout = 50;
		timeouts.ReadTotalTimeoutConstant = 50;
		timeouts.ReadTotalTimeoutMultiplier = 10;
		SetCommTimeouts(m_Handle, &timeouts);

		m_Connected = true;
		m_ReadThread = std::thread(&SerialPort::ReadLoop, this);
		return true;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * ReadLoop
	 * * ASYNC INGESTION: This function runs on its own thread. It continuously
	 * polls the hardware handle for new data. When data arrives, it is
	 * appended to the thread-safe buffer.
	 */
	void SerialPort::ReadLoop()
	{
		DWORD winThreadId = GetCurrentThreadId();
		int core = GetCurrentProcessorNumber();
		printf("[SERIAL THREAD] Started with ID: %lu on Core: %d\n", winThreadId, core);

		char szBuff[256];
		DWORD dwBytesRead = 0;

		while (m_Connected)
		{
			if (ReadFile(m_Handle, szBuff, sizeof(szBuff) - 1, &dwBytesRead, NULL) && dwBytesRead > 0)
			{
				szBuff[dwBytesRead] = '\0';

				// Lock only when appending to the shared buffer
				std::lock_guard<std::mutex> lock(m_BufferMutex);
				m_DataBuffer += szBuff;
			}
		}

		printf("[SERIAL THREAD] Shutting down.\n");
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
	 * * CLEAN SHUTDOWN: Sets the connection flag to false, ensuring the
	 * ReadLoop exits gracefully before closing the Win32 hardware handle.
	 */
	void SerialPort::Close()
	{
		m_Connected = false;
		if (m_ReadThread.joinable()) m_ReadThread.join();

		if (m_Handle != INVALID_HANDLE_VALUE)
		{
			CloseHandle(m_Handle);
			m_Handle = INVALID_HANDLE_VALUE;
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
					ports.push_back(std::string((char*)valueData));
					index++;
				}
				else break;
			}
			RegCloseKey(hKey);
		}

		return ports;
	}
}