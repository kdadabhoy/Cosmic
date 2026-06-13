#pragma once
// WINDOWS ONLY RIGHT NOW

// SerialPort.h
// Last Modified 5/14/2026

/**
 * General Description:
 * 
 * The SerialPort class provides a simplified interface for RS-232 serial communication
 * on Windows systems. It encapsulates the complex Win32 File I/O and Registry APIs
 * into a high-level, thread-safe subsystem.
 * 
 * Design:
 * It utilizes a dedicated background thread to poll the hardware port, preventing
 * serial latency from stalling the engine's main render loop. Data is collected
 * into an internal buffer and can be retrieved by the main thread using a
 * "Flush" pattern.
 * 
 * 
 * Public Function Prototypes (Pre and Post Conditions):
 * 
 * 1. bool Open(const std::string& portName, uint32_t baudRate)
 * Pre:  The specified COM port is not currently in use by another application.
 * Post: Opens the hardware handle, configures 8N1 parameters, and spawns the
 * background read thread. Returns true on success.
 * 
 * 2. void Close()
 * Pre:  None.
 * Post: Signals the read thread to stop, joins it, and releases the Win32 handle.
 * 
 * 3. std::string FlushBuffer()
 * Pre:  None.
 * Post: Returns all accumulated serial data and clears the internal buffer
 * in a thread-safe manner.
 * 
 * 4. static std::vector<std::string> GetAvailablePorts()
 * Pre:  None.
 * Post: Queries the Windows Registry to return a list of active COM ports
 * (e.g., {"COM3", "COM4"}).
 *
 * Write support: port is opened GENERIC_READ | GENERIC_WRITE. A Write(const std::string&)
 * method is planned but not yet implemented.
 */

#include "core/Core.h"   // COSMIC_API — export across the engine DLL boundary

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Cosmic
{
	class COSMIC_API SerialPort
	{
	public:
		////////////////////////////////
		// Life Cycle
		///////////////////////////////

		SerialPort();
		~SerialPort();

		////////////////////////////////
		// Connection Management
		///////////////////////////////

		bool		Open(const std::string& portName, uint32_t baudRate = 115200);
		void		Close();
		bool		IsOpen() const															{ return m_Connected; }

		////////////////////////////////
		// Data Retrieval
		///////////////////////////////

		std::string		FlushBuffer();

		////////////////////////////////
		// Hardware Discovery
		///////////////////////////////

		static std::vector<std::string>		 GetAvailablePorts();

	private:
		////////////////////////////////
		// Internal Threading
		///////////////////////////////

		void		ReadLoop();

	private:
		////////////////////////////////
		// State & Synchronization
		///////////////////////////////

		std::atomic<bool>		m_Connected			{ false };
		std::thread				m_ReadThread;
		std::mutex				m_BufferMutex;
		std::string				m_DataBuffer;

		////////////////////////////////
		// Platform Handle
		///////////////////////////////

#ifdef _WIN32
		HANDLE		m_Handle;
#endif
	};
}