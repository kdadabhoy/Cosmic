#pragma once
// WINDOWS ONLY RIGHT NOW

// SerialPort.h
// Last Modified 5/14/2026

/**
 * General Description:
 * 
 * The SerialPort class provides a simplified interface for RS-232 serial communication
 * on Windows systems. It encapsulates the complex Win32 File I/O and Registry APIs
 * into an owner-thread lifecycle with mutex-protected byte reads/writes.
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
 * Write support: port is opened GENERIC_READ | GENERIC_WRITE. Write(data, len)
 * performs an overlapped write, requests cancellation after 1 s or session stop,
 * and drains completion. A driver may ignore cancellation; see serial-ownership.md.
 */

#include "core/Core.h"   // COSMIC_API — export across the engine DLL boundary
#include "serial/ISerialTransport.h"   // the OS-boundary seam (WO-04)

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Cosmic
{
	class COSMIC_API SerialPort
	{
	public:
		// Lifecycle/status: one owner thread, including const status accessors
		// (which adopt completed opens). Write/FlushBuffer alone may be concurrent;
		// callers must join their own writers before destroying this object.
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

		// Test-only seam (WO-04): run the REAL connection state machine (threads,
		// cancellation jobs, stop events, every State transition) over an injected
		// transport, so the connected-state paths are reachable without hardware.
		// Production never uses this — the default constructor installs the shipping
		// Win32 transport. See transport-seam-spec.md.
		explicit SerialPort(std::unique_ptr<ISerialTransport> transport);

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
		bool IsOpen() const;
		State GetState() const;
		static constexpr size_t ReceiveCapacity = 1024 * 1024;
		uint64_t ReceivedBytes() const { return m_ReceivedBytes.load(); }
		uint64_t OverflowBytes() const { return m_OverflowBytes.load(); }
		uint64_t DiscardedOnCloseBytes() const { return m_DiscardedOnCloseBytes.load(); }
		uint64_t ConnectionGeneration() const { return m_ConnectionGeneration.load(); }

		////////////////////////////////
		// Data Retrieval
		///////////////////////////////

		std::string		FlushBuffer();

		////////////////////////////////
		// Data Transmission
		///////////////////////////////

		// Write raw bytes (may contain NULs — binary-framing safe). Returns
		// true when every byte was accepted. Overlapped + waited, so it is a
		// cancellation-aware blocking call; false when the port is closed or the device
		// dropped mid-write.
		bool		Write(const void* data, size_t length);
		bool		Write(const std::string& data) { return Write(data.data(), data.size()); }

		////////////////////////////////
		// Hardware Discovery
		///////////////////////////////

		static std::vector<std::string>		 GetAvailablePorts();

		// Instance discovery through this port's transport — the seam SerialLink uses
		// so an injected fake can offer a test port list. Identical to the static
		// GetAvailablePorts() for the default Win32 transport.
		std::vector<std::string>			 ListPorts();

	private:
		////////////////////////////////
		// Internal Threading
		///////////////////////////////

		void		ReadLoop();

		// Owner-thread handoff of a completed OS open; joins before starting a reader.
		void AdoptOpen();

		// Owner-thread teardown of an adopted session, after the open worker finishes.
		void		CloseReadSession();

	private:
		////////////////////////////////
		// State & Synchronization
		///////////////////////////////

		std::atomic<bool>		m_Connected			{ false };
		std::atomic<State>		m_State				{ State::Idle };
		struct OpenJob;
		std::shared_ptr<OpenJob> m_OpenJob;
		std::thread				m_ReadThread;
		std::thread				m_ConnectThread;
		std::mutex				m_BufferMutex;
		std::mutex m_WriteMutex;
		std::atomic<uint64_t> m_ReceivedBytes{0}, m_OverflowBytes{0}, m_DiscardedOnCloseBytes{0};
		std::atomic<uint64_t> m_ConnectionGeneration{0};
		std::string				m_DataBuffer;

		////////////////////////////////
		// Transport (OS boundary)
		///////////////////////////////

		// The four OS calls (open/read/write/close) + port discovery, behind the
		// WO-04 seam. Defaults to Win32SerialTransport (the shipping path); a test
		// injects a fake. Never null after construction.
		std::shared_ptr<ISerialTransport> m_Transport;

		////////////////////////////////
		// Platform Handle
		///////////////////////////////

#ifdef _WIN32
		// Borrowed from OpenJob. Signalled before draining reader/writer completion;
		// job ownership keeps it alive through an uncooperative late open.
		HANDLE		m_StopEvent = nullptr;
#endif
	};
}
