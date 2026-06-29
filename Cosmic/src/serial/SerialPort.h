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
		// Connection lifecycle state for the asynchronous (non-blocking) open path.
		// Idle      — never opened, or fully closed.
		// Connecting— a background worker is running the blocking CreateFileA.
		// Open       — the port is open and the read thread is live.
		// Failed     — the last open attempt failed, or the device dropped.
		enum class State { Idle, Connecting, Open, Failed };

		////////////////////////////////
		// Life Cycle
		///////////////////////////////

		SerialPort();
		~SerialPort();

		////////////////////////////////
		// Connection Management
		///////////////////////////////

		bool		Open(const std::string& portName, uint32_t baudRate = 115200);

		// Non-blocking connect: spawns a one-shot worker thread to run the blocking
		// CreateFileA so the caller (main/render thread) never stalls on an
		// unreachable Bluetooth port — which could otherwise hang ~10-20 s. Poll
		// GetState() for progress. No-op while already Connecting.
		void		BeginOpen(const std::string& portName, uint32_t baudRate = 115200);

		void		Close();
		bool		IsOpen() const															{ return m_Connected; }
		State		GetState() const														{ return m_State.load(); }

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

		// Core open work: CreateFileA -> DCB/timeouts -> start read thread. Does NOT
		// tear down a previous session (callers must CloseReadSession first) and does
		// NOT touch the connect thread (so the worker can call it without self-join).
		bool		DoOpen(const std::string& portName, uint32_t baudRate);

		// Tear down only the read session (thread + handle + stop event). Unlike
		// Close() this does not join the connect thread, so it is safe to call from
		// inside the connect worker.
		void		CloseReadSession();

	private:
		////////////////////////////////
		// State & Synchronization
		///////////////////////////////

		std::atomic<bool>		m_Connected			{ false };
		std::atomic<State>		m_State				{ State::Idle };
		std::atomic<bool>		m_Abandon			{ false };  // set by Close() so an in-flight connect self-closes
		std::thread				m_ReadThread;
		std::thread				m_ConnectThread;
		std::mutex				m_BufferMutex;
		std::string				m_DataBuffer;

		////////////////////////////////
		// Platform Handle
		///////////////////////////////

#ifdef _WIN32
		HANDLE		m_Handle;
		// Manual-reset event signalled by Close() to wake the overlapped read
		// thread instantly, so join() can never hang on a stalled port.
		HANDLE		m_StopEvent = nullptr;
#endif
	};
}