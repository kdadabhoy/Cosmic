#pragma once

// SimHub.h
//
// ============================================================================
// SimHub — the simulation backbone shared across all ViperSim screens (plan §3).
// ============================================================================
//
// Owns: config (E10 viper.toml) · ComposableDynamics (behind IDynamics) ·
// Sensors + Battery + Wind · the FC backend (SITL in-process / HIL Teensy —
// a dropdown, not a rebuild) · gamepad flying (E7) · fault injection ·
// audio alerts (doc 08) · DataRecorder/Player (truth + fc + sensors entities) ·
// scenario scripts including the GATE DEMOS G1/G2/G3 (playbook §5.2) and the
// legacy P1 drop test · the P7 rig output.
//
// Screens read/drive the sim through this one object so state persists across
// screen switches — the SF_Telem shared-hub pattern.
// ============================================================================

#include <Cosmic.h>

#include "sim/ComposableDynamics.h"
#include "sim/Sensors.h"
#include "sim/Battery.h"
#include "fc_glue/FcBackend.h"
#include "fc_glue/HilBridge.h"
#include "fc_glue/RigOutput.h"
#include "fc_glue/telemetry_schema.h"

#include <memory>
#include <string>
#include <vector>

namespace Viper
{
	enum class Scenario { None = 0, Drop, G1Hover, G2Transition, G3Failsafe };

	struct ScenarioReport
	{
		Scenario scenario = Scenario::None;
		bool running  = false;
		bool complete = false;
		bool passed   = false;
		std::vector<std::string> lines;      // per-check "[PASS]/[FAIL] ..." text
		std::string recordingPath;
	};

	class SimHub
	{
	public:
		SimHub();

		// Load viper.toml and (re)build dynamics/sensors/battery/FC params from
		// it. Safe to call again to hot-reload. Also loads the alert sounds.
		bool LoadConfig(const std::string& path = "project://config/viper.toml");

		Cosmic::Ref<Cosmic::Config> GetConfig() const { return m_Config; }
		const BodyParams& GetBodyParams() const { return m_Params; }

		// --- Vehicle / flight control ------------------------------------------
		void ResetToPad();                    // standing nose-up on the pad, disarmed
		void Arm(bool armed);
		bool Armed() const;
		void Takeoff(float altAglM = 10.0f);  // arm + hover climb setpoint
		void RequestMode(viperfc::FlightMode m);
		void SetRoi(const glm::vec3& roiNed);
		glm::vec3 RoiNed() const;

		viperfc::FlightMode FcMode() const;
		const viperfc::TelemetrySnapshot& FcTelem() const { return m_Backend->Telemetry(); }
		viperfc::FlightComputer* LocalFc() { return m_Backend->Local(); }

		// --- Backend (SITL <-> HIL dropdown, plan §1) -----------------------------
		IFcBackend& Backend() { return *m_Backend; }
		HilBackend& Hil()     { return m_Hil; }
		bool UsingHil() const { return m_UseHil; }
		void SetUseHil(bool useHil);

		// --- Environment + fault injection ---------------------------------------
		WindField&    Wind()       { return m_Dynamics->Wind(); }
		SensorFaults& Faults()     { return m_Sensors.Faults(); }
		Sensors&      GetSensors() { return m_Sensors; }
		Battery&      GetBattery() { return m_Battery; }
		RigOutput&    Rig()        { return m_Rig; }

		bool  linkKilled = false;             // GCS heartbeat kill switch
		bool  gamepadEnabled = true;          // E7 stick flying when a pad is present
		void  ForceBatteryLow();              // failsafe test: drain to reserve
		void  SetMotorOut(int index, bool out) { m_Dynamics->SetMotorOut(index, out); }
		bool  MotorOut(int index) const        { return m_Dynamics->MotorOut(index); }

		// Bitmask of every FcAlert raised this session — the "every failsafe
		// path exercised" checklist (G3).
		int32_t AlertsSeen() const { return m_AlertsSeen; }

		// --- Scenarios (gate demos + legacy drop test) -----------------------------
		void StartScenario(Scenario s);
		void AbortScenario();
		Scenario ActiveScenario() const { return m_Scenario; }
		const ScenarioReport& Report() const { return m_Report; }

		// Legacy P1 drop-test API (FlightScreen buttons).
		void StartDrop(float dropHeightM);
		void ResetDrop();

		bool IsRunning() const { return m_SimActive; }

		// Advance the sim by one engine fixed tick.
		void Step(float dt);

		// --- Truth access ------------------------------------------------------------
		const RigidState& Truth() const { return m_Dynamics->GetTruth(); }
		ComposableDynamics& Dynamics() { return *m_Dynamics; }
		float AltitudeAgl() const { return m_Dynamics->AltitudeAgl(); }
		float RunTime() const { return m_RunTime; }
		float HomeD() const { return m_HomeD; }

		// --- Recording / replay ---------------------------------------------------------
		Cosmic::DataRecorder& Recorder() { return m_Recorder; }
		Cosmic::DataPlayer&   Player()   { return m_Player; }

		std::string FlushRecording(const std::string& name = "viper_session");
		const std::string& LastRecordingPath() const { return m_LastRecordingPath; }

	private:
		void RegisterEntities();
		void RecordTick();
		void ApplyAlertAudio(viperfc::FcAlert a);
		FcCommand GatherCommand(float dt);
		void ScenarioStep(float dt);
		void ScenarioEvaluate();
		void Check(const char* what, bool ok);
		void FinishScenario(const std::string& recordingName);

		// --- config + models -------------------------------------------------------
		Cosmic::Ref<Cosmic::Config>          m_Config;
		BodyParams                           m_Params;
		std::unique_ptr<ComposableDynamics>  m_Dynamics;
		Sensors                              m_Sensors;
		Battery                              m_Battery;
		RigOutput                            m_Rig;

		// --- FC backends -------------------------------------------------------------
		SitlBackend m_Sitl;
		HilBackend  m_Hil;
		IFcBackend* m_Backend = &m_Sitl;
		bool        m_UseHil = false;
		float       m_FcRateHz = 240.0f;

		// UI-side command state (merged with gamepad each tick).
		bool m_WantArm = false;
		bool m_ArmEdge = false;     // arm/disarm is edge-triggered toward the FC
		int  m_PendingModeRequest = -1;
		float m_PendingTakeoffAlt = -1.0f;   // applied after the arm edge lands
		glm::vec3 m_RoiNed{ 60.0f, 0.0f, 0.0f };

		// --- recording ------------------------------------------------------------------
		Cosmic::DataRecorder m_Recorder;
		Cosmic::DataPlayer   m_Player;
		bool     m_EntitiesRegistered = false;
		uint32_t m_TruthId = 0, m_FcId = 0, m_SensorId = 0;
		viperfc::SensorFrame m_LastSensorFrame;   // recorded at tick rate
		std::string m_LastRecordingPath;

		// --- sim state ----------------------------------------------------------------
		bool  m_SimActive = false;
		float m_RunTime = 0.0f;
		float m_HomeD = 0.0f;          // NED D of the pad CG (baro/home reference)
		int32_t m_AlertsSeen = 0;

		// --- audio (doc 08 A2 groups: chime = Ui, alerts = Alerts) -------------------
		Cosmic::Ref<Cosmic::Sound> m_SndChime, m_SndWarning, m_SndCritical;

		// --- scenario engine --------------------------------------------------------------
		Scenario m_Scenario = Scenario::None;
		int      m_Phase = 0;
		float    m_ScenTime = 0.0f;    // time in current phase
		ScenarioReport m_Report;
		float    m_DropHeight = 6.0f;
		float    m_SettleTimer = 0.0f;

		// G-gate metric accumulators.
		float m_MaxDev = 0.0f, m_MinAlt = 1e9f, m_MaxAlt = 0.0f;
		float m_PowerSum = 0.0f;  int m_PowerCount = 0;
		float m_RadialSum = 0.0f, m_RoiErrSum = 0.0f;  int m_OrbitCount = 0;
		bool  m_FwdDone = false, m_BackDone = false;
	};
}
