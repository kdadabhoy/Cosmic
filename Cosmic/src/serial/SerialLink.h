#pragma once

// SerialLink.h
//
// ============================================================================
// SerialLink — reusable serial-connectivity component (transport + policy + UI)
// ============================================================================
//
// Wraps a SerialPort and owns everything an app needs to manage a COM/Bluetooth
// link: the discovered port list, the selected port + baud, the auto-reconnect
// policy, last-byte tracking, and the "Serial Link" connection menu (rendered by
// DrawConnectionUI). Apps feed received bytes through Poll() and run their own
// protocol parsing on top.
//
// Connecting is asynchronous (SerialPort::BeginOpen) so the main/render thread
// never stalls on an unreachable Bluetooth port — the historical cause of the
// "app froze / crashed on BT drop" reports.
//
// Usage:
//   m_Link.OnUpdate(dt);                       // port scan + async auto-reconnect
//   std::string chunk = m_Link.Poll();         // drain bytes; parse them yourself
//   if (m_Link.ConsumeJustConnected()) ...     // reset your RX accumulator
//   ... inside your own ImGui window:
//   m_Link.DrawConnectionUI();                 // Refresh / COM / Baud / status
// ============================================================================

#include "core/Core.h"   // COSMIC_API
#include "serial/SerialPort.h"

#include <string>
#include <vector>

namespace Cosmic
{
	class COSMIC_API SerialLink
	{
	public:
		SerialLink() = default;

		// ---- Per-frame drive ----
		void        OnUpdate(float dt);   // advance clock, ~1 Hz port scan, async auto-reconnect
		std::string Poll();               // returns received bytes (empty if none); updates last-byte time

		// ---- Connection control ----
		void Connect();      // user-initiated: BeginOpen the selected port + keep trying
		void Disconnect();   // user-initiated: stop trying and close
		void Shutdown();     // teardown for app exit / return-to-launcher (clears reconnect intent)

		// ---- Status ----
		bool             IsOpen() const       { return m_Port.IsOpen(); }
		bool             IsReceiving() const;                 // open AND a byte arrived < 1 s ago
		SerialPort::State GetState() const    { return m_Port.GetState(); }
		float            SecondsSinceLastByte() const { return m_Clock - m_LastByteTime; }

		// One-shot: true exactly once after each fresh (re)connect, so the caller can
		// clear its RX accumulator.
		bool ConsumeJustConnected();

		// ---- Selection (read-only access for app UI / firmware helpers) ----
		const std::string& SelectedPort() const { return m_Selected; }
		bool  AutoReconnect() const { return m_AutoReconnect; }
		bool  WantConnection() const { return m_WantConnection; }

		// ---- UI ----
		// Renders the connection widgets (Refresh / COM combo / Baud combo /
		// Auto-reconnect / status + Connect|Disconnect). No Begin/End — drop it
		// inside the caller's own window so each app can add its own extras.
		void DrawConnectionUI();

	private:
		void RefreshPorts();   // re-scan available ports; keep selection valid

	private:
		SerialPort               m_Port;
		std::vector<std::string> m_Ports;
		std::string              m_Selected;                 // selected by NAME, not index
		const std::vector<int>   m_BaudRates = { 9600, 19200, 38400, 57600,
		                                         115200, 230400, 460800, 921600 };
		int                      m_BaudIndex = 4;            // 115200

		bool   m_AutoReconnect  = true;     // re-open the link when data stops
		bool   m_WantConnection = false;    // user intends to stay connected
		bool   m_JustConnected  = false;    // set on a closed->open transition
		bool   m_WasOpen        = false;    // previous-frame open state (edge detect)

		float  m_Clock          = 0.0f;
		float  m_LastByteTime   = -100.0f;  // m_Clock of the last byte received
		float  m_PortScanClock  = 1.0f;     // start >= interval so the first scan is immediate
		float  m_ReconnectClock = 0.0f;

		static constexpr float k_ReconnectInterval = 3.0f;  // s of no-data before each retry
	};
}
