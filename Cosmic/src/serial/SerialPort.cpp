#include "SerialPort.h"
#include <windows.h> // Win32 API
#include <iostream>

namespace Cosmic
{

	SerialPort::SerialPort() : m_Handle(INVALID_HANDLE_VALUE) {}

	SerialPort::~SerialPort() { Close(); }

	bool SerialPort::Open(const std::string& portName, uint32_t baudRate)
	{
		if (m_Connected) Close();

		// 1. Open the COM port (Prefix with \\.\ for COM ports > 9)
		std::string fullPath = "\\\\.\\" + portName;
		m_Handle = CreateFileA(fullPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

		if (m_Handle == INVALID_HANDLE_VALUE) return false;

		// 2. Configure Port (Baud rate, 8N1)
		DCB dcbSerialParams = { 0 };
		dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
		if (!GetCommState(m_Handle, &dcbSerialParams)) return false;

		dcbSerialParams.BaudRate = baudRate;
		dcbSerialParams.ByteSize = 8;
		dcbSerialParams.StopBits = ONESTOPBIT;
		dcbSerialParams.Parity = NOPARITY;
		if (!SetCommState(m_Handle, &dcbSerialParams)) return false;

		// 3. Set Timeouts (Non-blocking style)
		COMMTIMEOUTS timeouts = { 0 };
		timeouts.ReadIntervalTimeout = 50;
		timeouts.ReadTotalTimeoutConstant = 50;
		timeouts.ReadTotalTimeoutMultiplier = 10;
		SetCommTimeouts(m_Handle, &timeouts);

		m_Connected = true;
		m_ReadThread = std::thread(&SerialPort::ReadLoop, this);
		return true;
	}




	void SerialPort::ReadLoop()
	{


		// Log once when the thread starts
		DWORD winThreadId = GetCurrentThreadId();
		int core = GetCurrentProcessorNumber();
		printf("[SERIAL THREAD] Started with ID: %lu on Core: %d\n", winThreadId, core);

		char szBuff[256];
		DWORD dwBytesRead = 0;

		while (m_Connected)
		{
			// This will block or wait based on the timeouts set above
			if (ReadFile(m_Handle, szBuff, sizeof(szBuff) - 1, &dwBytesRead, NULL) && dwBytesRead > 0)
			{
				szBuff[dwBytesRead] = '\0';

				std::lock_guard<std::mutex> lock(m_BufferMutex);
				m_DataBuffer += szBuff;
			}
		}

		printf("[SERIAL THREAD] Shutting down.\n");
	}



	std::string SerialPort::FlushBuffer()
	{
		std::lock_guard<std::mutex> lock(m_BufferMutex);
		std::string temp = m_DataBuffer;
		m_DataBuffer.clear();
		return temp;
	}

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

	std::vector<std::string> SerialPort::GetAvailablePorts()
	{
		std::vector<std::string> ports;
		HKEY hKey;

		// Open the Registry Key where Windows lists active serial ports
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

				// Enumerate the values in the key
				LSTATUS status = RegEnumValueA(hKey, index, valueName, &nameSize, NULL, &type, valueData, &dataSize);

				if (status == ERROR_SUCCESS)
				{
					// The "Data" of the registry value is the port name (e.g., "COM3")
					ports.push_back(std::string((char*)valueData));
					index++;
				}
				else
				{
					break; // No more ports found
				}
			}
			RegCloseKey(hKey);
		}

		return ports;
	}
}