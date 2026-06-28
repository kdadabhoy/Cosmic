// SerialLink.cpp

#include "serial/SerialLink.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace Cosmic
{
	namespace
	{
		const ImVec4 kGreen (0.30f, 1.00f, 0.40f, 1.0f);  // receiving
		const ImVec4 kYellow(1.00f, 0.80f, 0.20f, 1.0f);  // open-no-data / connecting / retrying
	}

	// =========================================================================
	// Port discovery
	// =========================================================================
	void SerialLink::RefreshPorts()
	{
		m_Ports = SerialPort::GetAvailablePorts();
		if (m_Ports.empty()) { m_Selected.clear(); return; }
		if (std::find(m_Ports.begin(), m_Ports.end(), m_Selected) == m_Ports.end())
			m_Selected = m_Ports.front();
	}

	// =========================================================================
	// Per-frame drive
	// =========================================================================
	void SerialLink::OnUpdate(float dt)
	{
		m_Clock += std::fabs(dt);

		// Detect a fresh (re)connect so the caller can reset its RX accumulator.
		const bool open = m_Port.IsOpen();
		if (open && !m_WasOpen) m_JustConnected = true;
		m_WasOpen = open;

		// Auto-refresh the port list (~1 Hz) so freshly paired / unplugged devices
		// show up without clicking Refresh. Skip while a session is live so the
		// selection isn't disturbed mid-connection.
		m_PortScanClock += std::fabs(dt);
		if (m_PortScanClock >= 1.0f)
		{
			m_PortScanClock = 0.0f;
			if (!open) RefreshPorts();
		}

		// Auto-reconnect: if the user asked to stay connected but data has stopped
		// (soft Bluetooth stall while still "open", or a hard unplug), periodically
		// re-open the selected port until the stream returns. BeginOpen is
		// non-blocking, so a dead/unreachable port never freezes the UI. Skip while
		// a connect attempt is already in flight so we don't stack them.
		if (m_AutoReconnect && m_WantConnection)
		{
			if (IsReceiving()) m_ReconnectClock = 0.0f;
			else if (m_Port.GetState() != SerialPort::State::Connecting)
			{
				m_ReconnectClock += std::fabs(dt);
				if (m_ReconnectClock >= k_ReconnectInterval)
				{
					m_ReconnectClock = 0.0f;
					RefreshPorts();
					if (!m_Selected.empty())
						m_Port.BeginOpen(m_Selected, (uint32_t)m_BaudRates[m_BaudIndex]);
				}
			}
		}
	}

	std::string SerialLink::Poll()
	{
		if (!m_Port.IsOpen()) return {};
		std::string chunk = m_Port.FlushBuffer();
		if (!chunk.empty()) m_LastByteTime = m_Clock;
		return chunk;
	}

	// =========================================================================
	// Connection control
	// =========================================================================
	void SerialLink::Connect()
	{
		if (m_Selected.empty()) return;
		m_WantConnection = true;
		m_ReconnectClock = 0.0f;
		m_Port.BeginOpen(m_Selected, (uint32_t)m_BaudRates[m_BaudIndex]);
	}

	void SerialLink::Disconnect()
	{
		m_WantConnection = false;
		m_ReconnectClock = 0.0f;
		m_Port.Close();
	}

	void SerialLink::Shutdown()
	{
		// Clear reconnect intent BEFORE closing so a return-to-launcher never leaves
		// a stale "keep reconnecting" flag behind.
		m_WantConnection = false;
		m_Port.Close();
	}

	// =========================================================================
	// Status
	// =========================================================================
	bool SerialLink::IsReceiving() const
	{
		return m_Port.IsOpen() && (m_Clock - m_LastByteTime) < 1.0f;
	}

	bool SerialLink::ConsumeJustConnected()
	{
		const bool j = m_JustConnected;
		m_JustConnected = false;
		return j;
	}

	// =========================================================================
	// UI — connection widgets only (no Begin/End)
	// =========================================================================
	void SerialLink::DrawConnectionUI()
	{
		if (ImGui::Button("Refresh Ports"))
			RefreshPorts();
		ImGui::SameLine();
		ImGui::TextDisabled("(auto-refreshes)");

		const char* curPort = m_Selected.empty() ? "No Ports Found" : m_Selected.c_str();

		ImGui::SetNextItemWidth(160.0f);
		if (ImGui::BeginCombo("COM Port", curPort))
		{
			for (const auto& p : m_Ports)
				if (ImGui::Selectable(p.c_str(), p == m_Selected)) m_Selected = p;
			ImGui::EndCombo();
		}
		ImGui::SetNextItemWidth(160.0f);
		if (ImGui::BeginCombo("Baud", std::to_string(m_BaudRates[m_BaudIndex]).c_str()))
		{
			for (int i = 0; i < (int)m_BaudRates.size(); ++i)
				if (ImGui::Selectable(std::to_string(m_BaudRates[i]).c_str(), m_BaudIndex == i)) m_BaudIndex = i;
			ImGui::EndCombo();
		}

		ImGui::Separator();
		ImGui::Checkbox("Auto-reconnect", &m_AutoReconnect);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Automatically re-open the selected port and resume the stream\n"
			                  "if data stops (e.g. the ESP32 was unplugged or power-cycled).\n"
			                  "Reconnects in the background, so the UI no longer freezes while\n"
			                  "it retries. Turn off to only connect manually.");

		const SerialPort::State st = m_Port.GetState();
		if (st == SerialPort::State::Connecting)
		{
			ImGui::TextColored(kYellow, "CONNECTING (%s)...", m_Selected.c_str());
			if (ImGui::Button("Cancel", ImVec2(-1, 0))) Disconnect();
		}
		else if (m_Port.IsOpen())
		{
			// Distinguish "port open" from "actually receiving": a Bluetooth COM
			// port opens fine even when the ESP32 isn't streaming, so flag a dead
			// link instead of falsely claiming a good connection.
			const bool receiving = IsReceiving();
			const bool retrying  = m_AutoReconnect && m_WantConnection;
			if (receiving)
				ImGui::TextColored(kGreen, "RECEIVING (%s)", m_Selected.c_str());
			else
				ImGui::TextColored(kYellow, retrying ? "OPEN - no data (%s) - retrying..."
				                                     : "OPEN - no data (%s)", m_Selected.c_str());
			if (ImGui::Button("Disconnect", ImVec2(-1, 0))) Disconnect();
		}
		else if (m_AutoReconnect && m_WantConnection)
		{
			// Hard drop (port closed) while the user still wants to be connected —
			// OnUpdate is retrying the reopen in the background.
			ImGui::TextColored(kYellow, "RECONNECTING (%s)...", m_Selected.c_str());
			if (ImGui::Button("Stop / Disconnect", ImVec2(-1, 0))) Disconnect();
		}
		else
		{
			ImGui::BeginDisabled(m_Selected.empty());
			if (ImGui::Button("Connect", ImVec2(-1, 0))) Connect();
			ImGui::EndDisabled();
		}
	}
}
