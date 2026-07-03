#pragma once

// viperfc/FlightComputer.h
//
// ============================================================================
// viper-fc — the flight computer (doc 04 §1 module list, top of the stack)
// ============================================================================
//
//   estimator (complementary) → mode machine (HOVER/TRANSITION/CRUISE/ORBIT/
//   RTL/FAILSAFE) → cascaded controllers → mixer, with the failsafe supervisor
//   ABOVE the mode machine (playbook §6.1).
//
// Step() is PURE with respect to hardware: SensorFrame in, ActuatorFrame out.
// The caller owns the HAL — SimHal feeds sim sensors, TeensyHal feeds real
// drivers, and replay-through-FC feeds a recorded log (regression tests with
// no simulator in the loop, doc 04 §1). No heap after construction.
// ============================================================================

#include "viperfc/Estimator.h"
#include "viperfc/AttitudeControl.h"
#include "viperfc/PositionControl.h"
#include "viperfc/Tecs.h"
#include "viperfc/Transition.h"
#include "viperfc/Orbit.h"
#include "viperfc/Failsafe.h"
#include "viperfc/Mixer.h"
#include "viperfc/TelemetrySchema.h"

namespace viperfc
{
	class FlightComputer
	{
	public:
		struct PilotInput
		{
			Vec3  velCmdNed{};       // hover stick command, m/s (NED)
			float yawRateCmd = 0.0f; // hover heading-spin rate, rad/s
		};

		explicit FlightComputer(const FcParams& params = {})
		{
			m_P = params;
			ApplyParams();
			Reset();
		}

		// --- configuration ----------------------------------------------------
		FcParams&       Params()       { return m_P; }
		const FcParams& Params() const { return m_P; }

		// Call after editing Params() (TuningScreen live-gain path).
		void ApplyParams()
		{
			m_Att.Configure(m_P);
			m_Pos.Configure(m_P);
		}

		void Reset(const Quat& att0 = {}, const Vec3& pos0 = {})
		{
			m_Est.Reset(att0, pos0);
			m_Att.Reset();
			m_Pos.Reset();
			m_Trans.Reset();
			m_Fs.Reset();
			m_Mode = FlightMode::Idle;
			m_Armed = false;
			m_Airborne = false;
			m_Landing = false;
			m_PosSp = pos0;
			m_HeadingSp = 0.0f;
			m_SinceHeartbeat = 0.0f;
			m_Home = { pos0.x, pos0.y, 0.0f };
			m_Roi = { 60.0f, 0.0f, 0.0f };
			m_Telem = TelemetrySnapshot{};
			m_PendingUiAlert = FcAlert::None;
		}

		// --- commands (GCS/pilot side) ------------------------------------------
		void Arm(bool armed)
		{
			if (armed && !m_Armed)
			{
				m_Armed = true;
				CaptureHover();
				SetModeInternal(FlightMode::Hover);
			}
			else if (!armed)
			{
				m_Armed = false;
				SetModeInternal(FlightMode::Idle);
			}
		}
		bool Armed() const { return m_Armed; }

		// Pilot mode request — the failsafe supervisor outranks it, and hover is
		// refused once the hover budget is spent (the cap is ENFORCED, §2.4.5).
		bool RequestMode(FlightMode m)
		{
			if (!m_Armed)
				return false;
			if (m_Fs.Active(FcAlert::HoverBudgetHit) &&
			    (m == FlightMode::Hover) && m_Mode != FlightMode::Failsafe)
				return false;

			switch (m)
			{
			case FlightMode::Hover:
				m_RtlActive = false;   // a pilot mode request cancels a pilot RTL
				if (m_Mode == FlightMode::Cruise || m_Mode == FlightMode::Orbit)
				{
					m_Trans.StartBack(CurrentHeading());
					SetModeInternal(FlightMode::Transition);
				}
				else
				{
					CaptureHover();
					SetModeInternal(FlightMode::Hover);
				}
				return true;

			case FlightMode::Cruise:
			case FlightMode::Orbit:
				m_RtlActive = false;
				m_PostTransition = m;
				if (m_Mode == FlightMode::Hover)
				{
					m_Trans.StartForward(m_HeadingSp);
					m_CruiseCmd.altSp    = m_Est.Get().altAgl > 15.0f ? m_Est.Get().altAgl : 15.0f;
					m_CruiseCmd.courseSp = m_HeadingSp;
					m_CruiseCmd.airspeedSp = m_P.cruise_airspeed;
					SetModeInternal(FlightMode::Transition);
				}
				else if (m_Mode == FlightMode::Cruise || m_Mode == FlightMode::Orbit)
				{
					SetModeInternal(m);
				}
				return true;

			case FlightMode::Rtl:
				BeginRtl();
				return true;

			default:
				return false;
			}
		}

		void SetHome(const Vec3& posNed) { m_Home = posNed; }
		void SetRoi(const Vec3& roiNed)  { m_Roi = roiNed; }
		void SetOrbitClockwise(bool cw)  { m_OrbitCw = cw; }
		void Heartbeat()                 { m_SinceHeartbeat = 0.0f; }
		void SetPilotInput(const PilotInput& in) { m_Pilot = in; }

		// Scripted-scenario hooks (gate demos + TuningScreen step commands).
		void SetHoverSetpoint(const Vec3& posNed)          { m_PosSp = posNed; m_HoldActive = true; }
		void SetHoverHeading(float rad)                    { m_HeadingSp = rad; }
		void SetCruiseCommand(const CruiseControl::Command& c) { m_CruiseCmd = c; }
		// Direct attitude step for tuning (deg): overrides hover attitude for
		// `holdSeconds`, then releases back to position hold.
		void CommandAttitudeStep(float rollDeg, float pitchOffsetDeg, float holdSeconds)
		{
			m_StepRoll = Rad(rollDeg);
			m_StepPitch = Rad(pitchOffsetDeg);
			m_StepTimer = holdSeconds;
		}

		// --- the control step ------------------------------------------------------
		void Step(const SensorFrame& f, ActuatorFrame& out, float dt)
		{
			m_Est.Update(f, m_P, dt);
			const Estimator::State& e = m_Est.Get();
			m_SinceHeartbeat += dt;

			UpdateAirborne(e);

			// ---- failsafe supervisor (above the mode machine) ------------------
			FailsafeSupervisor::Inputs fsIn;
			fsIn.mode      = m_Mode;
			fsIn.blend     = m_Telem.blend;
			fsIn.armed     = m_Armed;
			fsIn.airborne  = m_Airborne;
			fsIn.vbat_V    = f.vbat_V;
			fsIn.ibat_A    = f.ibat_A;
			fsIn.altAgl    = e.altAgl;
			fsIn.distHome  = HorizDist(e.posNed, m_Home);
			// Alpha guard only means something in steady wing-borne flight —
			// a back-transition pitch-up would false-alarm it otherwise.
			fsIn.alphaRad  = (m_Mode == FlightMode::Cruise || m_Mode == FlightMode::Orbit)
				? EstimateAlpha(e) : 0.0f;
			fsIn.gpsValid  = f.gps.valid;
			fsIn.sinceHeartbeat_s = m_SinceHeartbeat;
			const FailsafeSupervisor::Demand demand = m_Fs.Update(fsIn, m_P, dt);
			ApplyDemand(demand, e);

			// ---- per-mode command generation -------------------------------------
			Quat  qd = e.att;
			float thrust = 0.0f;
			Vec3  rateFF{};
			float blend = m_Telem.blend;

			switch (m_Mode)
			{
			case FlightMode::Idle:
				blend = 0.0f;
				break;

			case FlightMode::Hover:
			case FlightMode::Failsafe:   // failsafe == landing in place (hover-side)
			{
				blend = 0.0f;
				HoverCommands(e, qd, thrust, rateFF, dt);
				break;
			}

			case FlightMode::Transition:
			{
				const TransitionMachine::Status ts = m_Trans.Update(e.airspeed, m_P, dt);
				blend = ts.blend;
				m_LastPhase = ts.phase;

				// Hover-side target: scheduled pitch at the locked heading.
				const Quat qHover = FromEulerZYX(0.0f, ts.pitchSp, m_Trans.HeadingRad());

				// Cruise-side target from TECS at the same course.
				CruiseControl::Command cc = m_CruiseCmd;
				cc.courseSp = m_Trans.HeadingRad();
				const CruiseControl::Output co =
					m_Cruise.Update(cc, e.airspeed, e.altAgl, -e.velNed.z, e.velNed, m_P);

				qd     = Nlerp(qHover, co.attSp, ts.blend);
				thrust = (1.0f - ts.blend) * m_P.trans_accel_thr + ts.blend * co.throttle;

				if (ts.done)
				{
					if (ts.blend > 0.5f)
					{
						// Resume whatever asked for the wing: Orbit, RTL, or Cruise.
						SetModeInternal(m_PostTransition == FlightMode::Orbit ? FlightMode::Orbit
							: m_PostTransition == FlightMode::Rtl ? FlightMode::Rtl
							: FlightMode::Cruise);
					}
					else
					{
						CaptureHover();
						SetModeInternal(m_RtlActive ? FlightMode::Rtl : FlightMode::Hover);
					}
				}
				else if (ts.aborted)
				{
					if (ts.blend > 0.5f) { SetModeInternal(FlightMode::Cruise); }
					else                 { CaptureHover(); SetModeInternal(FlightMode::Hover); }
				}
				break;
			}

			case FlightMode::Cruise:
			{
				blend = 1.0f;
				const CruiseControl::Output co =
					m_Cruise.Update(m_CruiseCmd, e.airspeed, e.altAgl, -e.velNed.z, e.velNed, m_P);
				qd = co.attSp;
				thrust = co.throttle;
				break;
			}

			case FlightMode::Orbit:
			{
				blend = 1.0f;
				const CruiseControl::Command cc =
					m_Orbit.Update(m_Roi, e.posNed, m_CruiseCmd.altSp, m_OrbitCw, m_P);
				const CruiseControl::Output co =
					m_Cruise.Update(cc, e.airspeed, e.altAgl, -e.velNed.z, e.velNed, m_P);
				qd = co.attSp;
				thrust = co.throttle;
				break;
			}

			case FlightMode::Rtl:
			{
				RtlCommands(e, qd, thrust, rateFF, blend, dt);
				break;
			}
			}

			// ---- shared inner loop + allocation ------------------------------------
			const Vec3 torque = m_Att.Update(e.att, qd, f.gyro_rads, rateFF, m_P, dt);

			MixerInputs mix;
			mix.torque = torque;
			mix.thrust = thrust;
			MixTailsitter(mix, m_P, out);

			if (!m_Armed || m_Mode == FlightMode::Idle)
				out = ActuatorFrame{};   // motors off, servos centered

			// ---- telemetry snapshot ---------------------------------------------------
			m_Telem.mode      = m_Mode;
			m_Telem.phase     = m_Trans.Active() ? m_LastPhase : TransitionPhase::None;
			m_Telem.blend     = blend;
			m_Telem.attEst    = e.att;
			m_Telem.posEst    = e.posNed;
			m_Telem.velEst    = e.velNed;
			m_Telem.airspeedEst = e.airspeed;
			m_Telem.altAglEst = e.altAgl;
			m_Telem.motor[0]  = out.motor[0];
			m_Telem.motor[1]  = out.motor[1];
			m_Telem.servo[0]  = out.servo[0];
			m_Telem.servo[1]  = out.servo[1];
			m_Telem.attErrRad = m_Att.ErrorAngleRad();
			m_Telem.vbat_V    = f.vbat_V;
			m_Telem.ibat_A    = f.ibat_A;
			m_Telem.power_W   = f.vbat_V * f.ibat_A;
			m_Telem.energyUsed_wh = m_Fs.EnergyUsedWh();
			m_Telem.hoverElapsed_s = m_Fs.HoverElapsed();
			m_Telem.failsafeFlags  = m_Fs.Flags();
			m_Telem.armed     = m_Armed;
			if (m_Fs.PendingAlert() != FcAlert::None)
				m_PendingUiAlert = m_Fs.PendingAlert();
			m_Telem.lastAlert = m_PendingUiAlert;
		}

		// --- accessors -------------------------------------------------------------
		const TelemetrySnapshot& Telemetry() const { return m_Telem; }
		const Estimator::State&  Est() const       { return m_Est.Get(); }
		FlightMode Mode() const                    { return m_Mode; }
		const FailsafeSupervisor& Failsafe() const { return m_Fs; }
		const Vec3& HoverSetpoint() const          { return m_PosSp; }
		const Vec3& Roi() const                    { return m_Roi; }
		const Vec3& Home() const                   { return m_Home; }
		float RoiFrameErrorRad() const
		{
			return OrbitControl::RoiInFrameError(m_Roi, m_Est.Get().posNed, m_Est.Get().att);
		}

		// One alert per consumer read (UI chime / tone trigger).
		FcAlert ConsumeAlert()
		{
			const FcAlert a = m_PendingUiAlert;
			m_PendingUiAlert = FcAlert::None;
			return a;
		}

	private:
		// --- helpers ---------------------------------------------------------------
		static float HorizDist(const Vec3& a, const Vec3& b)
		{
			const float dn = a.x - b.x, de = a.y - b.y;
			return std::sqrt(dn * dn + de * de);
		}

		float CurrentHeading() const
		{
			// Hover heading = belly (+Z body) ground direction; cruise heading =
			// nose ground direction. Pick by which is closer to horizontal.
			const Vec3 nose  = Rotate(m_Est.Get().att, { 1, 0, 0 });
			const Vec3 belly = Rotate(m_Est.Get().att, { 0, 0, 1 });
			const bool hoverish = std::fabs(nose.z) > 0.7f;
			const Vec3& h = hoverish ? belly : nose;
			return std::atan2(h.y, h.x);
		}

		float EstimateAlpha(const Estimator::State& e) const
		{
			// Crude alpha for the envelope guard: pitch minus flight-path angle,
			// meaningful only in cruise-side flight with real airspeed.
			if (!e.airspeedValid)
				return 0.0f;
			float r, p, y;
			ToEulerZYX(e.att, r, p, y);
			const float vxy = std::sqrt(e.velNed.x * e.velNed.x + e.velNed.y * e.velNed.y);
			const float gamma = std::atan2(-e.velNed.z, vxy > 0.5f ? vxy : 0.5f);
			return p - gamma;
		}

		void UpdateAirborne(const Estimator::State& e)
		{
			if (!m_Airborne && e.altAgl > 0.6f)
				m_Airborne = true;
			else if (m_Airborne && e.altAgl < 0.25f && std::fabs(e.velNed.z) < 0.4f)
				m_Airborne = false;
		}

		void CaptureHover()
		{
			m_PosSp = m_Est.Get().posNed;
			m_HeadingSp = CurrentHeading();
			m_HoldActive = true;
			m_Pos.Reset();
		}

		void SetModeInternal(FlightMode m)
		{
			if (m == m_Mode)
				return;
			m_Mode = m;
			if (m_PendingUiAlert == FcAlert::None)
				m_PendingUiAlert = FcAlert::ModeChange;
			if (m != FlightMode::Transition)
				m_LastPhase = TransitionPhase::None;
		}

		void ApplyDemand(FailsafeSupervisor::Demand d, const Estimator::State& e)
		{
			switch (d)
			{
			case FailsafeSupervisor::Demand::Land:
				if (m_Mode != FlightMode::Failsafe)
				{
					m_Landing = true;
					CaptureHover();
					SetModeInternal(FlightMode::Failsafe);
				}
				break;

			case FailsafeSupervisor::Demand::Rtl:
				if (m_Mode != FlightMode::Rtl && m_Mode != FlightMode::Failsafe)
					BeginRtl();
				break;

			case FailsafeSupervisor::Demand::ExitHover:
				if (m_Mode == FlightMode::Hover)
				{
					// Budget spent: land if near home, otherwise go fly a wing.
					if (HorizDist(e.posNed, m_Home) < 30.0f)
					{
						m_Landing = true;
						SetModeInternal(FlightMode::Failsafe);
					}
					else
					{
						m_PostTransition = FlightMode::Cruise;
						m_Trans.StartForward(m_HeadingSp);
						SetModeInternal(FlightMode::Transition);
					}
				}
				break;

			case FailsafeSupervisor::Demand::None:
			default:
				break;
			}
		}

		void BeginRtl()
		{
			m_RtlActive = true;
			m_RtlStage = RtlStage::Enroute;
			SetModeInternal(FlightMode::Rtl);
		}

		// Hover / land shared command generation.
		void HoverCommands(const Estimator::State& e, Quat& qd, float& thrust,
		                   Vec3& rateFF, float dt)
		{
			// Stick flying: velocity command overrides hold; releasing recaptures.
			const bool sticksActive = Norm(m_Pilot.velCmdNed) > 0.05f ||
			                          std::fabs(m_Pilot.yawRateCmd) > 0.02f;
			Vec3 velFF{};
			if (m_Mode == FlightMode::Failsafe && m_Landing)
			{
				// Vertical land: hold xy, command a steady descent.
				velFF = { 0, 0, m_P.land_speed_ms };
				m_PosSp.z = e.posNed.z;   // don't fight the descent
				if (!m_Airborne)
				{
					Arm(false);            // touchdown → disarm
					m_Landing = false;
				}
			}
			else if (sticksActive)
			{
				velFF = m_Pilot.velCmdNed;
				m_PosSp = e.posNed;        // hold follows while flying by stick
				m_HeadingSp = WrapPi(m_HeadingSp + m_Pilot.yawRateCmd * dt);
			}

			// Tuning-step override: fixed attitude offset for the hold window.
			if (m_StepTimer > 0.0f)
			{
				m_StepTimer -= dt;
				const Quat qHover = FromEulerZYX(0.0f, kPi * 0.5f, m_HeadingSp);
				// Roll about the thrust axis is a HEADING change in hover; the
				// classic "20° roll step" tips the thrust vector — that is a
				// body-Z rotation here (see Mixer.h axis notes).
				const Quat qStep = FromEulerZYX(0.0f, kPi * 0.5f - m_StepPitch, m_HeadingSp)
				                 * FromAxisAngle({ 0, 0, 1 }, m_StepRoll);
				qd = qStep;
				const PositionControl::Output po =
					m_Pos.Update(m_PosSp, e.posNed, e.velNed, m_HeadingSp, velFF, m_P, dt);
				thrust = po.thrust;
				(void)qHover;
				return;
			}

			const PositionControl::Output po =
				m_Pos.Update(m_PosSp, e.posNed, e.velNed, m_HeadingSp, velFF, m_P, dt);
			qd = po.attSp;
			thrust = po.thrust;

			// Heading-spin feed-forward maps to body-X rate in hover.
			rateFF = RotateInv(e.att, { 0, 0, m_Pilot.yawRateCmd });
		}

		enum class RtlStage { Enroute, BackTransition, HoverHome, Land };

		void RtlCommands(const Estimator::State& e, Quat& qd, float& thrust,
		                 Vec3& rateFF, float& blend, float dt)
		{
			const float dist = HorizDist(e.posNed, m_Home);

			switch (m_RtlStage)
			{
			case RtlStage::Enroute:
			{
				if (blend < 0.5f && dist < 50.0f)
				{
					// Close-in hover return: translate home directly.
					blend = 0.0f;
					m_PosSp = { m_Home.x, m_Home.y, e.posNed.z };
					HoverCommands(e, qd, thrust, rateFF, dt);
					if (dist < m_P.rtl_capture_m)
						m_RtlStage = RtlStage::Land;
					return;
				}
				if (blend < 0.5f)
				{
					// Far out on the hover side: transition to the wing first.
					m_PostTransition = FlightMode::Rtl;
					m_Trans.StartForward(std::atan2(m_Home.y - e.posNed.y, m_Home.x - e.posNed.x));
					SetModeInternal(FlightMode::Transition);
					return;
				}

				// Cruise home at RTL altitude.
				blend = 1.0f;
				CruiseControl::Command cc;
				cc.airspeedSp = m_P.cruise_airspeed;
				cc.altSp      = m_P.rtl_altitude_agl;
				cc.courseSp   = std::atan2(m_Home.y - e.posNed.y, m_Home.x - e.posNed.x);
				const CruiseControl::Output co =
					m_Cruise.Update(cc, e.airspeed, e.altAgl, -e.velNed.z, e.velNed, m_P);
				qd = co.attSp;
				thrust = co.throttle;

				if (dist < 60.0f)
				{
					m_Trans.StartBack(cc.courseSp);
					SetModeInternal(FlightMode::Transition);
					m_RtlStage = RtlStage::HoverHome;
				}
				break;
			}

			case RtlStage::BackTransition:   // (transition mode handles it; kept for clarity)
			case RtlStage::HoverHome:
			{
				blend = 0.0f;
				m_PosSp = { m_Home.x, m_Home.y, e.posNed.z };
				HoverCommands(e, qd, thrust, rateFF, dt);
				if (dist < m_P.rtl_capture_m)
					m_RtlStage = RtlStage::Land;
				break;
			}

			case RtlStage::Land:
			{
				blend = 0.0f;
				m_Landing = true;
				Vec3 ff{ 0, 0, m_P.land_speed_ms };
				m_PosSp = { m_Home.x, m_Home.y, e.posNed.z };
				const PositionControl::Output po =
					m_Pos.Update(m_PosSp, e.posNed, e.velNed, m_HeadingSp, ff, m_P, dt);
				qd = po.attSp;
				thrust = po.thrust;
				if (!m_Airborne)
				{
					Arm(false);
					m_RtlActive = false;
					m_Landing = false;
				}
				break;
			}
			}
		}

		// --- state ---------------------------------------------------------------
		FcParams m_P{};
		Estimator          m_Est;
		AttitudeControl    m_Att;
		PositionControl    m_Pos;
		CruiseControl      m_Cruise;
		OrbitControl       m_Orbit;
		TransitionMachine  m_Trans;
		FailsafeSupervisor m_Fs;

		FlightMode m_Mode = FlightMode::Idle;
		FlightMode m_PostTransition = FlightMode::Cruise;
		TransitionPhase m_LastPhase = TransitionPhase::None;
		RtlStage m_RtlStage = RtlStage::Enroute;
		bool m_RtlActive = false;

		bool m_Armed = false;
		bool m_Airborne = false;
		bool m_Landing = false;
		bool m_HoldActive = false;
		bool m_OrbitCw = true;

		Vec3  m_PosSp{};
		float m_HeadingSp = 0.0f;
		Vec3  m_Home{};
		Vec3  m_Roi{ 60.0f, 0.0f, 0.0f };
		CruiseControl::Command m_CruiseCmd{};
		PilotInput m_Pilot{};

		float m_StepRoll = 0.0f, m_StepPitch = 0.0f, m_StepTimer = 0.0f;
		float m_SinceHeartbeat = 0.0f;

		TelemetrySnapshot m_Telem{};
		FcAlert m_PendingUiAlert = FcAlert::None;
	};
}
