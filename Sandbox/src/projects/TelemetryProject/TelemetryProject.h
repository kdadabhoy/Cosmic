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
		virtual void OnUpdate(float ts) override;
		virtual void OnRender() override {}
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Cosmic::Event& e) override {}
		virtual void SetViewportSize(float w, float h) override {}

	private:
		Cosmic::SerialPort m_Serial;

		// Data for the Graph
		std::vector<float> m_DataPoints;

		// Data for the Serial Monitor (The Log)
		std::string m_Log;
		std::string m_AccumulatedString;

		// Connection Settings
		char m_PortName[16] = "COM3";
		int m_BaudRate = 115200;
	};
}