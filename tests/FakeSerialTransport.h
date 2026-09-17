#pragma once
// FakeSerialTransport.h — WO-04 (2D stability campaign).
//
// TEST-ONLY. Lives under tests/ and is compiled ONLY into CosmicTests — never into
// Cosmic.dll and never into a package. It is the fake ISerialTransport that lets a
// test drive the REAL SerialPort / SerialLink connection state machine (both
// std::threads, m_Abandon, the stop event, every State transition, and SerialLink's
// auto-reconnect policy) without any serial hardware.
//
// It fakes ONLY the OS boundary: open / read / write / close / port-discovery. It
// reimplements no parser and no state machine. The controls are exactly those the
// seam spec lists (transport-seam-spec.md §4):
//   SetOpenResult / SetOpenBlockMs   — open success/failure, and the KI-4 exit-hang
//                                       lever (block inside Open, honouring the stop).
//   PushBytes / SignalDrop           — feed the connected-state read stream / drop it.
//   SetAvailablePorts                — let SerialLink::RefreshPorts select a fake port.
//   SetWriteResult                   — write success/failure (feeds WO-06).
//   OpenCount / CloseCount / Written — observers.

#include "serial/ISerialTransport.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace Cosmic
{
	class FakeSerialTransport final : public ISerialTransport
	{
	public:
		FakeSerialTransport()
		{
			// Manual-reset: signalled by PushBytes / SignalDrop, reset by Read once the
			// state it announced has been consumed.
			m_DataEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
		}

		~FakeSerialTransport() override
		{
			if (m_DataEvent) { CloseHandle(m_DataEvent); m_DataEvent = nullptr; }
		}

		// ---- Test controls -------------------------------------------------------

		void SetOpenResult(bool ok)        { m_OpenResult.store(ok); }
		// Block N ms inside Open(), honouring the stop handle — the KI-4 exit-hang
		// lever. Today SerialPort::Close does NOT signal the stop until AFTER it joins
		// the connect worker, so this still stalls the join; that is exactly KI-4, and
		// it is what WO-05's fix will flip (cancel before the join → prompt return).
		void SetOpenBlockMs(int ms)        { m_OpenBlockMs.store(ms); }
		void SetWriteResult(bool ok)       { m_WriteResult.store(ok); }

		void PushBytes(std::string_view bytes)
		{
			{
				std::lock_guard<std::mutex> lock(m_Mutex);
				m_Rx.append(bytes.data(), bytes.size());
			}
			SetEvent(m_DataEvent);
		}

		void SignalDrop()
		{
			{
				std::lock_guard<std::mutex> lock(m_Mutex);
				m_Dropped = true;
			}
			SetEvent(m_DataEvent);
		}

		void SetAvailablePorts(std::vector<std::string> ports)
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Ports = std::move(ports);
		}

		// ---- Observers -----------------------------------------------------------

		int OpenCount()  const { return m_OpenCount.load(); }
		int CloseCount() const { return m_CloseCount.load(); }
		std::string Written() const
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			return m_Written;
		}

		// ---- ISerialTransport ----------------------------------------------------

		bool Open(const std::string& /*portName*/, std::uint32_t /*baudRate*/, void* stopEvent) override
		{
			m_OpenCount.fetch_add(1);

			const int blockMs = m_OpenBlockMs.load();
			if (blockMs > 0)
			{
				// Honour the stop handle so a *fixed* Close (WO-05) can cancel the open
				// and return promptly. Unfixed, the stop is not signalled until after the
				// join, so this waits the full duration → the observable exit hang.
				if (stopEvent)
					WaitForSingleObject(static_cast<HANDLE>(stopEvent), static_cast<DWORD>(blockMs));
				else
					Sleep(static_cast<DWORD>(blockMs));
			}

			if (!m_OpenResult.load())
				return false;

			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Rx.clear();
			m_Dropped = false;
			ResetEvent(m_DataEvent);
			return true;
		}

		ReadResult Read(char* buf, std::size_t cap, void* stopEvent) override
		{
			HANDLE stop = static_cast<HANDLE>(stopEvent);
			for (;;)
			{
				{
					std::lock_guard<std::mutex> lock(m_Mutex);
					if (m_Dropped)
					{
						m_Dropped = false;
						ResetEvent(m_DataEvent);
						return { 0, ReadResult::Status::Dropped };
					}
					if (!m_Rx.empty())
					{
						const std::size_t n = cap < m_Rx.size() ? cap : m_Rx.size();
						std::memcpy(buf, m_Rx.data(), n);
						m_Rx.erase(0, n);
						if (m_Rx.empty())
							ResetEvent(m_DataEvent);
						return { n, ReadResult::Status::Data };
					}
				}

				// Nothing to deliver yet — wait for the stop handle or a state change.
				HANDLE waits[2] = { stop, m_DataEvent };
				const DWORD count = stop ? 2u : 1u;
				const DWORD w = WaitForMultipleObjects(count, stop ? waits : &m_DataEvent, FALSE, INFINITE);
				if (stop && w == WAIT_OBJECT_0)
					return { 0, ReadResult::Status::Aborted };
				ResetEvent(m_DataEvent); // re-check state on the next iteration
			}
		}

		bool Write(const void* data, std::size_t length) override
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Written.append(static_cast<const char*>(data), length);
			return m_WriteResult.load();
		}

		void Close() override
		{
			m_CloseCount.fetch_add(1);
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Rx.clear();
			m_Dropped = false;
			ResetEvent(m_DataEvent);
		}

		std::vector<std::string> List() override
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			return m_Ports;
		}

	private:
		mutable std::mutex       m_Mutex;
		std::string              m_Rx;                 // pending bytes for Read
		std::string              m_Written;            // everything Write received
		std::vector<std::string> m_Ports;              // what List() reports
		bool                     m_Dropped = false;    // a mid-session drop is pending

		std::atomic<bool> m_OpenResult { true };
		std::atomic<int>  m_OpenBlockMs { 0 };
		std::atomic<bool> m_WriteResult { true };
		std::atomic<int>  m_OpenCount  { 0 };
		std::atomic<int>  m_CloseCount { 0 };

		HANDLE m_DataEvent = nullptr; // signalled by PushBytes / SignalDrop
	};
}
