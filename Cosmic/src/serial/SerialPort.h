#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>

namespace Cosmic
{

	class SerialPort
	{
	public:
		SerialPort();
		~SerialPort();

		// Connects to "COM3", etc.
		bool Open(const std::string& portName, uint32_t baudRate = 115200);
		void Close();
		bool IsOpen() const { return m_Connected; }

		// Thread-safe way to grab all data since the last frame
		std::string FlushBuffer();


		static std::vector<std::string> GetAvailablePorts();

	private:
		void ReadLoop(); // Runs on the background thread

	private:
		std::atomic<bool> m_Connected{ false };
		std::thread m_ReadThread;

		std::mutex m_BufferMutex;
		std::string m_DataBuffer;

		void* m_Handle; // Internal Win32 HANDLE
	};
}