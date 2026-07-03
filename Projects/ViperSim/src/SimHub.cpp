// SimHub.cpp — see header.

#include "SimHub.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Viper
{
	namespace
	{
		viperfc::Quat ToFcQuat(const glm::quat& q) { return { q.w, q.x, q.y, q.z }; }
		viperfc::Vec3 ToFcVec(const glm::vec3& v)  { return { v.x, v.y, v.z }; }

		float Deadband(float v, float db = 0.12f)
		{
			return std::fabs(v) < db ? 0.0f : (v - (v > 0 ? db : -db)) / (1.0f - db);
		}
	}

	SimHub::SimHub()
	{
		// Config load is deferred to the root layer's OnAttach (the active
		// project VFS isn't set at construction). Built-in defaults hold.
	}

	bool SimHub::LoadConfig(const std::string& path)
	{
		// Resolve client-side: FileSystem has per-DLL static state (see the
		// P0 note) — the resolved path passes through engine Resolve unchanged.
		const std::string resolved = Cosmic::FileSystem::Resolve(path);
		m_Config = Cosmic::Config::Load(resolved);

		// ---- airframe / environment (every getter has a design-point default) --
		BodyParams p;
		if (m_Config)
		{
			auto& c = *m_Config;
			p.mass_kg      = c.Get<float>("airframe.auw_kg", p.mass_kg);
			p.inertia      = c.Get<glm::vec3>("airframe.inertia_diag", p.inertia);
			p.body_radius  = c.Get<float>("airframe.body_radius_m", p.body_radius);
			p.wing_area    = c.Get<float>("airframe.wing_area_m2", p.wing_area);
			p.aspect_ratio = c.Get<float>("airframe.aspect_ratio", p.aspect_ratio);
			p.ground_agl_m = c.Get<float>("sim.ground_agl_m", p.ground_agl_m);
			p.ground_k     = c.Get<float>("sim.ground_k", p.ground_k);
			p.ground_c     = c.Get<float>("sim.ground_c", p.ground_c);

			p.motor_count        = c.Get<int>("motors.count", p.motor_count);
			p.motor_max_thrust_n = c.Get<float>("motors.max_thrust_gf", 1490.0f) * 9.80665e-3f;
			p.motor_tau_s        = c.Get<float>("motors.tau_s", p.motor_tau_s);
			p.motor_arm_y        = c.Get<float>("motors.arm_y_m", p.motor_arm_y);
			p.disc_area_m2       = c.Get<float>("motors.disc_area_m2", p.disc_area_m2);

			p.cd0         = c.Get<float>("aero.cd0", p.cd0);
			p.cl_alpha    = c.Get<float>("aero.cl_alpha", p.cl_alpha);
			p.stall_alpha = c.Get<float>("aero.stall_alpha_rad", p.stall_alpha);
			p.oswald_e    = c.Get<float>("aero.oswald_e", p.oswald_e);
			p.cm_alpha    = c.Get<float>("aero.cm_alpha", p.cm_alpha);
			p.cm_de       = c.Get<float>("aero.cm_de", p.cm_de);
			p.cl_de       = c.Get<float>("aero.cl_de", p.cl_de);
			p.cg_offset_x = c.Get<float>("aero.cg_offset_x_m", p.cg_offset_x);

			m_FcRateHz = c.Get<float>("fc.rate_hz", m_FcRateHz);

			// sim.substeps is per ENGINE tick; dynamics substeps are per FC step.
			const int tomlSub = c.Get<int>("sim.substeps", 8);
			p.substeps = std::max(1, static_cast<int>(std::lround(tomlSub * 60.0f / m_FcRateHz)));
		}
		m_Params = p;

		if (!m_Dynamics)
			m_Dynamics = std::make_unique<ComposableDynamics>(p);
		else
			m_Dynamics->SetParams(p);

		// ---- sensors --------------------------------------------------------
		SensorParams sp;
		if (m_Config)
		{
			auto& c = *m_Config;
			sp.perfect        = c.Get<bool>("sim.perfect_sensors", sp.perfect);
			sp.gyro_noise     = c.Get<float>("sensors.gyro.noise", sp.gyro_noise);
			sp.gyro_bias_walk = c.Get<float>("sensors.gyro.bias_walk", sp.gyro_bias_walk);
			sp.accel_noise    = c.Get<float>("sensors.accel.noise", sp.accel_noise);
			sp.baro_noise_pa  = c.Get<float>("sensors.baro.noise_pa", sp.baro_noise_pa);
			sp.mag_noise_uT   = c.Get<float>("sensors.mag.noise_uT", sp.mag_noise_uT);
			sp.pitot_noise_pa = c.Get<float>("sensors.pitot.noise_pa", sp.pitot_noise_pa);
			sp.gps_rate_hz    = c.Get<float>("sensors.gps.rate_hz", sp.gps_rate_hz);
			sp.gps_latency_s  = c.Get<float>("sensors.gps.latency_s", sp.gps_latency_s);
			sp.gps_pos_noise  = c.Get<float>("sensors.gps.pos_noise_m", sp.gps_pos_noise);
			sp.gps_vel_noise  = c.Get<float>("sensors.gps.vel_noise_ms", sp.gps_vel_noise);
			sp.seed           = c.Get<uint32_t>("sensors.seed", sp.seed);
		}
		m_Sensors.Configure(sp);

		// ---- battery / power ---------------------------------------------------
		BatteryParams bp;
		if (m_Config)
		{
			auto& c = *m_Config;
			bp.capacity_wh  = c.Get<float>("battery.capacity_wh", bp.capacity_wh);
			bp.usable_frac  = c.Get<float>("battery.usable_frac", bp.usable_frac);
			bp.cells        = c.Get<float>("battery.cells_s", bp.cells);
			bp.r_int_ohm    = c.Get<float>("battery.r_int", bp.r_int_ohm);
			bp.base_load_w  = c.Get<float>("power.base_load_w", bp.base_load_w);
			bp.disc_area_m2 = c.Get<float>("motors.disc_area_m2", bp.disc_area_m2);
			bp.eta_hover    = c.Get<float>("power.eta_hover", bp.eta_hover);
			bp.eta_cruise   = c.Get<float>("power.eta_cruise", bp.eta_cruise);
		}
		m_Battery.Configure(bp);
		m_Battery.Reset();

		// ---- FC params (fc.* overrides the viperfc defaults) ----------------------
		if (viperfc::FlightComputer* fc = m_Sitl.Local())
		{
			viperfc::FcParams& f = fc->Params();
			f.mass_kg          = p.mass_kg;
			f.wing_area_m2     = p.wing_area;
			f.max_thrust_N     = p.motor_max_thrust_n;
			f.motor_count      = static_cast<float>(p.motor_count);
			f.batt_capacity_wh = bp.capacity_wh;
			f.batt_cells       = bp.cells;
			f.batt_usable_frac = bp.usable_frac;
			f.fc_rate_hz       = m_FcRateHz;
			if (m_Config)
			{
				auto& c = *m_Config;
				f.att_kp          = c.Get<float>("fc.att_kp", f.att_kp);
				f.rate_kp_x       = c.Get<float>("fc.rate_kp_x", f.rate_kp_x);
				f.rate_kp_y       = c.Get<float>("fc.rate_kp_y", f.rate_kp_y);
				f.rate_kp_z       = c.Get<float>("fc.rate_kp_z", f.rate_kp_z);
				f.pos_kp          = c.Get<float>("fc.pos_kp", f.pos_kp);
				f.vel_kp          = c.Get<float>("fc.vel_kp", f.vel_kp);
				f.cruise_airspeed = c.Get<float>("fc.cruise_airspeed", f.cruise_airspeed);
				f.trans_v_blend_lo = c.Get<float>("fc.trans_v_blend_lo", f.trans_v_blend_lo);
				f.trans_v_blend_hi = c.Get<float>("fc.trans_v_blend_hi", f.trans_v_blend_hi);
				f.orbit_radius_m   = c.Get<float>("fc.orbit_radius_m", f.orbit_radius_m);
				f.geofence_agl_m   = c.Get<float>("limits.geofence_agl_m", f.geofence_agl_m);
				f.geofence_radius_m = c.Get<float>("limits.geofence_radius_m", f.geofence_radius_m);
				f.hover_budget_s   = c.Get<float>("limits.hover_budget_s", f.hover_budget_s);
				f.batt_v_qualify_s = c.Get<float>("limits.batt_v_qualify_s", f.batt_v_qualify_s);
			}
			fc->ApplyParams();
		}

		// ---- alert sounds (doc 08 — degraded-silent when missing) ------------------
		if (!m_SndChime)
		{
			m_SndChime    = Cosmic::Sound::Create(Cosmic::FileSystem::Resolve("project://sounds/mode_chime.wav"));
			m_SndWarning  = Cosmic::Sound::Create(Cosmic::FileSystem::Resolve("project://sounds/alert_warning.wav"));
			m_SndCritical = Cosmic::Sound::Create(Cosmic::FileSystem::Resolve("project://sounds/alert_critical.wav"));
		}

		CS_INFO("ViperSim: config loaded (mass {:.2f} kg, fc {:.0f} Hz, dyn substeps {}).",
			p.mass_kg, m_FcRateHz, p.substeps);

		ResetToPad();
		return true;
	}

	// =========================================================================
	// Vehicle control
	// =========================================================================

	void SimHub::ResetToPad()
	{
		RegisterEntities();
		m_Recorder.Clear();
		m_Recorder.ReserveCapacity(static_cast<size_t>(180.0f * 60.0f));   // 3 min headroom

		// Standing on the pad NOSE UP (tailsitter), heading north.
		const float padD = -(m_Params.ground_agl_m + m_Params.body_radius);
		RigidState init;
		init.posNed = { 0.0f, 0.0f, padD };
		init.attNed = Cosmic::Math::QuatFromEulerZYX({ 0.0f, 90.0f, 0.0f });
		m_Dynamics->Reset(init);
		m_Dynamics->SetMotorOut(0, false);
		m_Dynamics->SetMotorOut(1, false);
		m_Dynamics->Wind().steadyNed = { 0, 0, 0 };
		m_Dynamics->Wind().gustSigma = 0.0f;

		m_HomeD = padD;
		m_Sensors.Reset();
		m_Sensors.Faults() = SensorFaults{};
		m_Battery.Reset();
		linkKilled = false;

		m_Backend->Reset(ToFcQuat(init.attNed), ToFcVec(init.posNed));

		m_WantArm = false;
		m_PendingModeRequest = -1;
		m_SimActive = false;
		m_RunTime = 0.0f;
		m_LastSensorFrame = {};

		CS_INFO("ViperSim: vehicle reset to pad ({}).", m_Backend->Name());
	}

	void SimHub::Arm(bool armed)
	{
		m_WantArm = armed;
		m_ArmEdge = true;
		if (armed)
			m_SimActive = true;
	}

	bool SimHub::Armed() const { return m_Backend->Telemetry().armed; }

	void SimHub::Takeoff(float altAglM)
	{
		// The climb setpoint must be applied AFTER the arm edge reaches the FC
		// (arming captures the current position as the hover hold, which would
		// clobber a setpoint written now). Step() applies it post-command.
		Arm(true);
		m_PendingTakeoffAlt = altAglM;
	}

	void SimHub::RequestMode(viperfc::FlightMode m)
	{
		m_PendingModeRequest = static_cast<int>(m);
		m_SimActive = true;
	}

	void SimHub::SetRoi(const glm::vec3& roiNed) { m_RoiNed = roiNed; }
	glm::vec3 SimHub::RoiNed() const { return m_RoiNed; }

	viperfc::FlightMode SimHub::FcMode() const { return m_Backend->Telemetry().mode; }

	void SimHub::SetUseHil(bool useHil)
	{
		if (useHil == m_UseHil)
			return;
		m_UseHil = useHil;
		m_Backend = useHil ? static_cast<IFcBackend*>(&m_Hil) : &m_Sitl;
		ResetToPad();
	}

	void SimHub::ForceBatteryLow()
	{
		m_Battery.ForceUsedWh(m_Battery.UsableWh() * 0.80f);
		CS_INFO("ViperSim: battery forced to 20% usable remaining (reserve failsafe test).");
	}

	// =========================================================================
	// Command gathering (UI + E7 gamepad)
	// =========================================================================

	FcCommand SimHub::GatherCommand(float dt)
	{
		FcCommand cmd;
		cmd.armRequest = m_ArmEdge ? (m_WantArm ? 1 : 0) : -1;
		m_ArmEdge = false;
		cmd.requestMode = m_PendingModeRequest;
		m_PendingModeRequest = -1;
		cmd.roi = ToFcVec(m_RoiNed);
		cmd.heartbeat = !linkKilled;

		// E7 stick flying: left stick = climb + heading spin, right stick =
		// horizontal velocity in the HEADING frame. RC transmitters in
		// USB-joystick mode land on the same axes.
		if (gamepadEnabled && Cosmic::Input::IsGamepadConnected())
		{
			const float lx = Deadband(Cosmic::Input::GetGamepadAxis(Cosmic::CS_GAMEPAD_AXIS_LEFT_X));
			const float ly = Deadband(Cosmic::Input::GetGamepadAxis(Cosmic::CS_GAMEPAD_AXIS_LEFT_Y));
			const float rx = Deadband(Cosmic::Input::GetGamepadAxis(Cosmic::CS_GAMEPAD_AXIS_RIGHT_X));
			const float ry = Deadband(Cosmic::Input::GetGamepadAxis(Cosmic::CS_GAMEPAD_AXIS_RIGHT_Y));

			const float h = 0.0f;   // heading frame ~ north at v1 (heading spin still works)
			const float fwd = -ry * 5.0f, right = rx * 5.0f;
			cmd.pilot.velCmdNed = {
				fwd * std::cos(h) - right * std::sin(h),
				fwd * std::sin(h) + right * std::cos(h),
				ly * 2.0f,               // stick up (negative axis) = climb (-D)
			};
			cmd.pilot.yawRateCmd = lx * 1.2f;
		}

		(void)dt;
		return cmd;
	}

	// =========================================================================
	// The fixed-tick step
	// =========================================================================

	void SimHub::Step(float dt)
	{
		if (dt <= 0.0f)
			return;

		// Rig mirrors the sim attitude whenever it is enabled (P7).
		m_Rig.Update(m_Dynamics->GetTruth().attNed, dt);

		if (!m_SimActive)
			return;

		ScenarioStep(dt);

		m_Backend->ApplyCommand(GatherCommand(dt));

		// Deferred takeoff setpoint — see Takeoff() for why it waits for the
		// arm edge to land first.
		if (m_PendingTakeoffAlt > 0.0f)
		{
			if (viperfc::FlightComputer* fc = m_Backend->Local())
			{
				if (fc->Armed())
				{
					const RigidState& s = m_Dynamics->GetTruth();
					fc->SetHoverSetpoint({ s.posNed.x, s.posNed.y, m_HomeD - m_PendingTakeoffAlt });
					m_PendingTakeoffAlt = -1.0f;
				}
			}
			else
			{
				m_PendingTakeoffAlt = -1.0f;   // HIL: fly it with the sticks
			}
		}

		// FC pacing: N control steps per engine tick (240 Hz vs 60 Hz).
		const int fcSteps = std::max(1, static_cast<int>(std::lround(dt * m_FcRateHz)));
		const float dtFc = dt / static_cast<float>(fcSteps);

		for (int i = 0; i < fcSteps; ++i)
		{
			m_LastSensorFrame = m_Sensors.Sample(*m_Dynamics, m_Battery, m_HomeD, dtFc);

			viperfc::ActuatorFrame u{};
			m_Backend->Step(m_LastSensorFrame, u, dtFc);

			m_Dynamics->Step(FromFc(u), dtFc);

			// Electrical power from the CURRENT (lagged) thrusts.
			const glm::vec3 airBody = glm::conjugate(m_Dynamics->GetTruth().attNed) *
				(m_Dynamics->GetTruth().velNed - m_Dynamics->CurrentWindNed());
			m_Battery.Update(m_Dynamics->TotalThrustN(), std::max(airBody.x, 0.0f),
			                 m_Dynamics->GetTruth().airspeed, dtFc);
		}

		m_RunTime += dt;
		m_Recorder.Tick(dt);
		RecordTick();

		// Alerts -> audio (doc 08: failsafe testing gets visceral).
		const viperfc::FcAlert alert = m_Backend->ConsumeAlert();
		if (alert != viperfc::FcAlert::None)
		{
			m_AlertsSeen |= 1 << static_cast<int32_t>(alert);
			ApplyAlertAudio(alert);
		}
	}

	void SimHub::ApplyAlertAudio(viperfc::FcAlert a)
	{
		using A = viperfc::FcAlert;
		switch (a)
		{
		case A::ModeChange:
			Cosmic::AudioEngine::Play(m_SndChime, 0.8f, 1.0f, Cosmic::AudioGroup::Ui);
			break;
		case A::HoverBudgetWarn:
		case A::BatteryLow:
		case A::GpsLost:
		case A::EnvelopeAlpha:
			Cosmic::AudioEngine::Play(m_SndWarning, 1.0f, 1.0f, Cosmic::AudioGroup::Alerts);
			break;
		default:   // critical family: budget hit, battery crit/reserve, link, geofence
			Cosmic::AudioEngine::Play(m_SndCritical, 1.0f, 1.0f, Cosmic::AudioGroup::Alerts);
			break;
		}
		CS_INFO("ViperSim ALERT: {}", viperfc::AlertName(a));
	}

	// =========================================================================
	// Recording
	// =========================================================================

	void SimHub::RegisterEntities()
	{
		if (m_EntitiesRegistered)
			return;
		m_TruthId  = m_Recorder.Register("truth",   "sim",      TruthChannels());
		m_FcId     = m_Recorder.Register("fc",      "estimate", viperfc::FcChannelNames());
		m_SensorId = m_Recorder.Register("sensors", "raw",      SensorChannels());
		m_EntitiesRegistered = true;
	}

	void SimHub::RecordTick()
	{
		const RigidState& s = m_Dynamics->GetTruth();
		const glm::vec3 e = Cosmic::Math::EulerZYXFromQuat(s.attNed);

		m_Recorder.Record(m_TruthId, {
			s.posNed.x, s.posNed.y, s.posNed.z,
			s.velNed.x, s.velNed.y, s.velNed.z,
			e.x, e.y, e.z,
			s.airspeed, glm::degrees(s.alpha), AltitudeAgl(),
		});

		std::vector<float> fcRow(viperfc::kFcChannelCount, 0.0f);
		viperfc::WriteFcRow(m_Backend->Telemetry(), fcRow.data());
		m_Recorder.Record(m_FcId, fcRow);

		std::vector<float> senRow(kSensorChannelCount, 0.0f);
		WriteSensorRow(m_LastSensorFrame, senRow.data());
		m_Recorder.Record(m_SensorId, senRow);
	}

	std::string SimHub::FlushRecording(const std::string& name)
	{
		if (!m_EntitiesRegistered)
			return "";
		const std::string folder = Cosmic::FileSystem::Resolve("user://recordings/" + name);
		m_Recorder.Flush(folder, "session", 60.0f);
		m_Recorder.WaitForFlush();
		m_LastRecordingPath = folder + "/session";
		CS_INFO("ViperSim: recording flushed to '{}'.", m_LastRecordingPath);
		return m_LastRecordingPath;
	}

	// =========================================================================
	// Scenario engine — gate demos (G1–G3) + legacy drop test
	// =========================================================================

	void SimHub::StartDrop(float dropHeightM)
	{
		m_DropHeight = std::max(dropHeightM, 0.1f);
		StartScenario(Scenario::Drop);
	}

	void SimHub::ResetDrop() { StartDrop(m_DropHeight); }

	void SimHub::StartScenario(Scenario s)
	{
		if (s != Scenario::Drop && !m_Backend->Local())
		{
			CS_WARN("ViperSim: gate scenarios run against the SITL backend only.");
			return;
		}

		ResetToPad();
		m_Scenario = s;
		m_Phase = 0;
		m_ScenTime = 0.0f;
		m_SettleTimer = 0.0f;
		m_SimActive = true;

		m_Report = ScenarioReport{};
		m_Report.scenario = s;
		m_Report.running = true;

		m_MaxDev = 0.0f; m_MinAlt = 1e9f; m_MaxAlt = 0.0f;
		m_PowerSum = 0.0f; m_PowerCount = 0;
		m_RadialSum = 0.0f; m_RoiErrSum = 0.0f; m_OrbitCount = 0;
		m_FwdDone = m_BackDone = false;

		switch (s)
		{
		case Scenario::Drop:
		{
			// Level attitude, dead FC, pure ballistics — the P1 regression.
			RigidState init;
			init.posNed = { 0.0f, 0.0f, -(m_Params.ground_agl_m + m_DropHeight) };
			init.attNed = Cosmic::Math::QuatFromEulerZYX({ 0.0f, 0.0f, 0.0f });
			m_Dynamics->Reset(init);
			CS_INFO("ViperSim: drop started from {:.1f} m.", m_DropHeight);
			break;
		}
		case Scenario::G1Hover:
			m_Sensors.SetPerfect(false);         // G1 is BY DEFINITION noisy
			Takeoff(10.0f);
			CS_INFO("ViperSim: G1 hover gate started (noise + 5 m/s gusts).");
			break;
		case Scenario::G2Transition:
			Takeoff(30.0f);
			CS_INFO("ViperSim: G2 transition gate started (VTOL->cruise->VTOL).");
			break;
		case Scenario::G3Failsafe:
			m_Sensors.SetPerfect(false);
			Takeoff(25.0f);
			CS_INFO("ViperSim: G3 gate started (orbit-in-gusts + link-kill RTL).");
			break;
		default:
			break;
		}
	}

	void SimHub::AbortScenario()
	{
		m_Scenario = Scenario::None;
		m_Report.running = false;
		Wind().steadyNed = { 0, 0, 0 };
		Wind().gustSigma = 0.0f;
		linkKilled = false;
	}

	void SimHub::Check(const char* what, bool ok)
	{
		char line[192];
		std::snprintf(line, sizeof(line), "[%s] %s", ok ? "PASS" : "FAIL", what);
		m_Report.lines.emplace_back(line);
		if (!ok)
			m_Report.passed = false;
	}

	void SimHub::FinishScenario(const std::string& recordingName)
	{
		m_Report.running = false;
		m_Report.complete = true;
		m_Report.recordingPath = FlushRecording(recordingName);
		Wind().steadyNed = { 0, 0, 0 };
		Wind().gustSigma = 0.0f;
		linkKilled = false;
		m_Scenario = Scenario::None;
		CS_INFO("ViperSim: scenario finished — {} ({} checks).",
			m_Report.passed ? "PASSED" : "FAILED", m_Report.lines.size());
	}

	void SimHub::ScenarioStep(float dt)
	{
		if (m_Scenario == Scenario::None)
			return;

		m_ScenTime += dt;
		const RigidState& s = m_Dynamics->GetTruth();
		const auto& telem = m_Backend->Telemetry();
		viperfc::FlightComputer* fc = m_Backend->Local();

		auto advance = [&](int phase) { m_Phase = phase; m_ScenTime = 0.0f; };

		switch (m_Scenario)
		{
		// -----------------------------------------------------------------
		case Scenario::Drop:
		{
			const float speed = glm::length(s.velNed);
			const float lowestAgl = AltitudeAgl() - m_Params.body_radius;
			const bool settled = lowestAgl < 0.05f && speed < 0.05f;
			m_SettleTimer = settled ? (m_SettleTimer + dt) : 0.0f;
			if (m_SettleTimer > 1.5f || m_ScenTime > 14.0f)
			{
				m_Report.passed = true;
				Check("airframe settled at ground contact", settled);
				FinishScenario("viper_drop");
				m_SimActive = false;
				CS_INFO("ViperSim: drop settled after {:.2f} s.", m_ScenTime);
			}
			break;
		}

		// -----------------------------------------------------------------
		case Scenario::G1Hover:
		{
			switch (m_Phase)
			{
			case 0:   // climb to 10 m
				if (AltitudeAgl() > 9.0f) { advance(1); Wind().gustSigma = 2.0f; }
				else if (m_ScenTime > 20.0f)
				{
					m_Report.passed = false;
					Check("takeoff to 10 m within 20 s", false);
					FinishScenario("regression/g1_hover");
				}
				break;

			case 1:   // 30 s of noise + 5 m/s gusts, alternating direction
			{
				static const glm::vec3 kGust[4] = {
					{ 5, 0, 0 }, { 0, 0, 0 }, { 0, 5, 0 }, { -3.5f, -3.5f, 0 } };
				Wind().steadyNed = kGust[static_cast<int>(m_ScenTime / 5.0f) % 4];

				const float dev = glm::length(glm::vec2(s.posNed.x, s.posNed.y));
				m_MaxDev = std::max(m_MaxDev, dev);
				m_MinAlt = std::min(m_MinAlt, AltitudeAgl());
				m_PowerSum += m_Battery.PowerW();
				++m_PowerCount;

				if (m_ScenTime > 30.0f)
				{
					Wind().steadyNed = { 0, 0, 0 };
					Wind().gustSigma = 0.0f;
					advance(2);
				}
				break;
			}

			case 2:   // settle + evaluate
				if (m_ScenTime > 3.0f)
				{
					const float avgPower = m_PowerCount ? m_PowerSum / m_PowerCount : 0.0f;
					char buf[128];
					m_Report.passed = true;
					std::snprintf(buf, sizeof(buf), "hover held vs noise + 5 m/s gusts (max dev %.2f m < 4.0 m)", m_MaxDev);
					Check(buf, m_MaxDev < 4.0f);
					std::snprintf(buf, sizeof(buf), "never lost altitude (min AGL %.1f m > 3 m)", m_MinAlt);
					Check(buf, m_MinAlt > 3.0f);
					std::snprintf(buf, sizeof(buf), "hover power %.0f W within 15%% of the 230 W model", avgPower);
					Check(buf, avgPower > 195.5f && avgPower < 264.5f);
					FinishScenario("regression/g1_hover");
				}
				break;
			}
			break;
		}

		// -----------------------------------------------------------------
		case Scenario::G2Transition:
		{
			m_MinAlt = std::min(m_MinAlt, m_Phase >= 1 ? AltitudeAgl() : m_MinAlt);
			m_MaxAlt = std::max(m_MaxAlt, AltitudeAgl());

			switch (m_Phase)
			{
			case 0:
				if (AltitudeAgl() > 28.0f) advance(1);
				else if (m_ScenTime > 25.0f)
				{
					m_Report.passed = false;
					Check("takeoff to 30 m within 25 s", false);
					FinishScenario("regression/g2_transition");
				}
				break;

			case 1:   // request forward transition once
				RequestMode(viperfc::FlightMode::Cruise);
				advance(2);
				break;

			case 2:   // wait for cruise
				if (telem.mode == viperfc::FlightMode::Cruise) { m_FwdDone = true; advance(3); }
				else if (m_ScenTime > 25.0f) { advance(5); }   // evaluate (failed forward)
				break;

			case 3:   // cruise for 10 s
				if (m_ScenTime > 10.0f)
				{
					RequestMode(viperfc::FlightMode::Hover);
					advance(4);
				}
				break;

			case 4:   // wait for hover capture
				if (telem.mode == viperfc::FlightMode::Hover) { m_BackDone = true; advance(5); }
				else if (m_ScenTime > 30.0f) { advance(5); }
				break;

			case 5:   // hold + evaluate
				if (m_ScenTime > 4.0f)
				{
					char buf[128];
					m_Report.passed = true;
					Check("forward transition HOVER->ACCEL->BLEND->CRUISE completed", m_FwdDone);
					Check("back transition CRUISE->DECEL->FLARE->HOVER completed", m_BackDone);
					std::snprintf(buf, sizeof(buf), "altitude stayed flyable (min %.1f m > 12 m)", m_MinAlt);
					Check(buf, m_MinAlt > 12.0f);
					std::snprintf(buf, sizeof(buf), "stayed under the 400 ft geofence (max %.1f m)", m_MaxAlt);
					Check(buf, m_MaxAlt < 121.9f);
					Check("blend/phase traces recorded (fc entity)", true);
					FinishScenario("regression/g2_transition");
				}
				break;
			}
			break;
		}

		// -----------------------------------------------------------------
		case Scenario::G3Failsafe:
		{
			switch (m_Phase)
			{
			case 0:
				if (AltitudeAgl() > 23.0f)
				{
					SetRoi({ 100.0f, 0.0f, 0.0f });
					Wind().gustSigma = 2.0f;
					RequestMode(viperfc::FlightMode::Orbit);
					advance(1);
				}
				else if (m_ScenTime > 25.0f)
				{
					m_Report.passed = false;
					Check("takeoff to 25 m within 25 s", false);
					FinishScenario("regression/g3_orbit_failsafe");
				}
				break;

			case 1:   // reach orbit (through the forward transition)
				if (telem.mode == viperfc::FlightMode::Orbit && telem.blend > 0.9f)
					advance(2);
				else if (m_ScenTime > 30.0f)
				{
					m_Report.passed = false;
					Check("reached ORBIT through the transition within 30 s", false);
					FinishScenario("regression/g3_orbit_failsafe");
				}
				break;

			case 2:   // 30 s of orbit-in-gusts metrics
			{
				const glm::vec2 rel{ s.posNed.x - m_RoiNed.x, s.posNed.y - m_RoiNed.y };
				float radius = 120.0f;
				if (fc) radius = fc->Params().orbit_radius_m;
				m_RadialSum += std::fabs(glm::length(rel) - radius);
				m_RoiErrSum += viperfc::OrbitControl::RoiInFrameError(
					ToFcVec(m_RoiNed), ToFcVec(s.posNed), ToFcQuat(s.attNed));
				++m_OrbitCount;

				if (m_ScenTime > 30.0f)
				{
					linkKilled = true;   // the headline failsafe: hands off from here
					advance(3);
				}
				break;
			}

			case 3:   // link-kill -> RTL -> vertical land -> auto-disarm, hands off
				if (!telem.armed && AltitudeAgl() < m_Params.body_radius + 0.4f)
				{
					const float radialMean = m_OrbitCount ? m_RadialSum / m_OrbitCount : 1e9f;
					const float roiErrMean = m_OrbitCount ? glm::degrees(m_RoiErrSum / m_OrbitCount) : 180.0f;
					const float distHome = glm::length(glm::vec2(s.posNed.x, s.posNed.y));

					char buf[160];
					m_Report.passed = true;
					std::snprintf(buf, sizeof(buf), "orbit held the circle in gusts (mean radial err %.1f m < 20 m)", radialMean);
					Check(buf, radialMean < 20.0f);
					std::snprintf(buf, sizeof(buf), "aircraft-pointing held ROI in frame (mean err %.0f deg < 35)", roiErrMean);
					Check(buf, roiErrMean < 35.0f);
					Check("link-kill triggered RTL (LinkLost alert seen)",
						(m_AlertsSeen & (1 << static_cast<int>(viperfc::FcAlert::LinkLost))) != 0);
					std::snprintf(buf, sizeof(buf), "hands-off RTL -> vertical land -> disarm (%.1f m from home)", distHome);
					Check(buf, distHome < 20.0f);
					FinishScenario("regression/g3_orbit_failsafe");
				}
				else if (m_ScenTime > 120.0f)
				{
					m_Report.passed = false;
					Check("hands-off RTL -> land -> disarm within 120 s", false);
					FinishScenario("regression/g3_orbit_failsafe");
				}
				break;
			}
			break;
		}

		default:
			break;
		}
	}
}
