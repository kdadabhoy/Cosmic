#pragma once
#include "Simulation.h"
#include <vector>
#include <string>

namespace Workspace
{
	class TelemetryProject : public Simulation
	{
	public:
		TelemetryProject();
		~TelemetryProject();

		virtual void OnUpdate(float ts) override;
		virtual void OnRender() override {}
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Cosmic::Event& e) override {}
		virtual void SetViewportSize(float w, float h) override {}


	private:
		Cosmic::SerialPort m_Serial;

		// Port Scanning
		std::vector<std::string> m_AvailablePorts;
		int m_SelectedPortIndex = 0;

		// Common Baud Rates
		const std::vector<int> m_BaudRates = { 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600 };
		int m_SelectedBaudIndex = 4; // Defaults to 115200

		// Data for the Graph
		std::vector<float> m_DataPoints;

		// Data for the Serial Monitor (The Log)
		std::string m_Log;
		std::string m_AccumulatedString;
	};
}