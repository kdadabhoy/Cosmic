#pragma once

// viperfc/Transition.h
//
// ============================================================================
// Transition state machine — THE deliverable and the project's top risk
// (doc 04 §2.4.2). Explicit states, both directions:
//
//   forward:  HOVER → ACCEL (pitch-over schedule) → BLEND (mixer/command
//             crossfade on airspeed) → CRUISE
//   back:     CRUISE → DECEL (pitch-up brake) → FLARE (hover capture) → HOVER
//
// The machine owns ONLY phase/blend/pitch-schedule state; the FlightComputer
// turns its output into attitude+thrust commands. Everything here is
// instrumented (phase, blend, scheduled pitch → TelemetrySnapshot) because the
// plan's rule is "instrument everything" for exactly this code.
//
// Pure and deterministic → unit-testable as a transition TABLE (tests/).
// ============================================================================

#include "viperfc/Params.h"
#include "viperfc/TelemetrySchema.h"

namespace viperfc
{
	class TransitionMachine
	{
	public:
		struct Status
		{
			TransitionPhase phase = TransitionPhase::None;
			float blend    = 0.0f;   // 0 = hover-side commands, 1 = cruise-side
			float pitchSp  = 0.0f;   // scheduled nose pitch (rad, +90° = vertical)
			bool  done     = false;  // reached the target regime this step
			bool  aborted  = false;  // timed out / airspeed lost — reverted
		};

		void Reset()
		{
			m_Phase = TransitionPhase::None;
			m_Blend = 0.0f;
			m_Pitch = kPi * 0.5f;
			m_Timer = 0.0f;
		}

		// Begin a forward (hover→cruise) transition from vertical flight.
		void StartForward(float headingRad)
		{
			m_Phase   = TransitionPhase::Accel;
			m_Blend   = 0.0f;
			m_Pitch   = kPi * 0.5f;
			m_Timer   = 0.0f;
			m_Heading = headingRad;
		}

		// Begin a back (cruise→hover) transition.
		void StartBack(float headingRad)
		{
			m_Phase   = TransitionPhase::Decel;
			m_Blend   = 1.0f;
			m_Pitch   = 0.0f;
			m_Timer   = 0.0f;
			m_Heading = headingRad;
		}

		bool Active() const { return m_Phase != TransitionPhase::None; }
		float HeadingRad() const { return m_Heading; }

		// Advance the schedule. airspeed = estimator airspeed (m/s).
		Status Update(float airspeed, const FcParams& p, float dt)
		{
			Status s;
			m_Timer += dt;

			const float span = p.trans_v_blend_hi - p.trans_v_blend_lo;
			const float lam  = span > 0.1f
				? Clampf((airspeed - p.trans_v_blend_lo) / span, 0.0f, 1.0f)
				: 1.0f;

			switch (m_Phase)
			{
			case TransitionPhase::Accel:
				// Pitch the nose over toward the accel attitude; hold until the
				// pitot wakes up and the blend can start.
				m_Pitch = m_Pitch - p.trans_accel_pitch_rate * dt;
				if (m_Pitch < Rad(15.0f)) m_Pitch = Rad(15.0f);
				m_Blend = 0.0f;
				if (airspeed >= p.trans_v_blend_lo)
					m_Phase = TransitionPhase::Blend;
				if (m_Timer > p.trans_timeout_s)
				{
					// Never built airspeed — revert to vertical flight.
					s.aborted = true;
					m_Phase = TransitionPhase::None;
				}
				break;

			case TransitionPhase::Blend:
				// Crossfade tracks airspeed monotonically (never snaps back on a
				// gust lull — max() latch).
				m_Blend = lam > m_Blend ? lam : m_Blend;
				m_Pitch = Rad(15.0f) * (1.0f - m_Blend);   // relax toward level
				if (m_Blend >= 1.0f)
				{
					// Cruise regime reached (CruiseLocked). Like Flare below,
					// report done and go inactive in the SAME step — the FC
					// switches mode on `done` and stops calling Update, so any
					// phase left behind here would latch Active() forever.
					m_Blend = 1.0f;
					m_Pitch = 0.0f;
					s.done  = true;
					m_Phase = TransitionPhase::None;
				}
				else if (m_Timer > p.trans_timeout_s)
				{
					s.aborted = true;
					m_Phase = TransitionPhase::None;
				}
				break;

			case TransitionPhase::Decel:
				// Pitch up toward vertical; blend follows the FALLING airspeed.
				m_Pitch = m_Pitch + p.trans_decel_pitch_rate * dt;
				if (m_Pitch > kPi * 0.5f) m_Pitch = kPi * 0.5f;
				m_Blend = lam < m_Blend ? lam : m_Blend;   // min() latch
				if (airspeed <= p.trans_v_blend_lo || m_Blend <= 0.0f)
					m_Phase = TransitionPhase::Flare;
				if (m_Timer > p.trans_timeout_s)
				{
					s.aborted = true;      // still fast — stay cruise-side
					m_Phase = TransitionPhase::None;
				}
				break;

			case TransitionPhase::Flare:
				m_Blend = 0.0f;
				m_Pitch = kPi * 0.5f;
				s.done  = true;
				m_Phase = TransitionPhase::None;
				break;

			case TransitionPhase::None:
			default:
				break;
			}

			s.phase   = m_Phase;
			s.blend   = m_Blend;
			s.pitchSp = m_Pitch;
			return s;
		}

	private:
		TransitionPhase m_Phase = TransitionPhase::None;
		float m_Blend   = 0.0f;
		float m_Pitch   = kPi * 0.5f;
		float m_Timer   = 0.0f;
		float m_Heading = 0.0f;
	};
}
