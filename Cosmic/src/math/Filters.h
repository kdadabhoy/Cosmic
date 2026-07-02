#pragma once

// Filters.h
// Last Modified 7/1/2026
//
// E12 (docs/plans/03-simulation-engine-plan.md): the signal-conditioning
// toolbox every estimator, controller, and telemetry display reaches for.
// Header-only, allocation-free, engine-independent.
//
// Common contract: `Reset(value)` re-seeds internal state; `Update(sample, dt)`
// advances by dt seconds and returns the filtered value. dt <= 0 returns the
// current output unchanged.

#include <cmath>
#include <cstddef>
#include <array>
#include <algorithm>

namespace Cosmic
{
	constexpr float kFilterTwoPi = 6.28318530717958647692f;

	// =========================================================================
	// LowPassFilter — first-order exponential smoothing.
	//
	// y += alpha * (x - y),  alpha = 1 - exp(-dt / tau)
	// Step response reaches 63.2% at t = tau (the defining property).
	// =========================================================================
	class LowPassFilter
	{
	public:
		LowPassFilter() = default;

		// tau: time constant in seconds.
		explicit LowPassFilter(float tau) : m_Tau(tau) {}

		// Named constructor for the "I think in bandwidth" caller.
		static LowPassFilter FromCutoffHz(float hz)
		{
			return LowPassFilter(1.0f / (kFilterTwoPi * hz));
		}

		void SetTau(float tau)         { m_Tau = tau; }
		void SetCutoffHz(float hz)     { m_Tau = 1.0f / (kFilterTwoPi * hz); }
		float GetTau() const           { return m_Tau; }

		void Reset(float value = 0.0f) { m_Y = value; m_Primed = true; }

		float Update(float sample, float dt)
		{
			if (!m_Primed)
			{
				// first sample seeds the state — no startup transient from 0
				m_Y = sample;
				m_Primed = true;
				return m_Y;
			}
			if (dt <= 0.0f || m_Tau <= 0.0f)
			{
				if (m_Tau <= 0.0f) m_Y = sample; // tau=0: passthrough
				return m_Y;
			}

			const float alpha = 1.0f - std::exp(-dt / m_Tau);
			m_Y += alpha * (sample - m_Y);
			return m_Y;
		}

		float GetValue() const { return m_Y; }

	private:
		float m_Tau = 1.0f;
		float m_Y = 0.0f;
		bool  m_Primed = false;
	};

	// =========================================================================
	// Derivative — filtered differentiator (raw finite difference through a
	// first-order LPF so sensor noise does not explode).
	// =========================================================================
	class Derivative
	{
	public:
		Derivative() = default;

		// tau: smoothing time constant for the derivative estimate (0 = raw).
		explicit Derivative(float tau) : m_Filter(tau) {}

		void Reset(float value = 0.0f)
		{
			m_PrevSample = value;
			m_Primed = false;
			m_Filter.Reset(0.0f);
		}

		float Update(float sample, float dt)
		{
			if (!m_Primed)
			{
				m_PrevSample = sample;
				m_Primed = true;
				m_Filter.Reset(0.0f);
				return 0.0f;
			}
			if (dt <= 0.0f)
				return m_Filter.GetValue();

			const float raw = (sample - m_PrevSample) / dt;
			m_PrevSample = sample;
			return m_Filter.Update(raw, dt);
		}

		float GetValue() const { return m_Filter.GetValue(); }

	private:
		LowPassFilter m_Filter{ 0.02f };
		float m_PrevSample = 0.0f;
		bool  m_Primed = false;
	};

	// =========================================================================
	// RateLimiter — clamps the output slew rate to ±maxRatePerSecond.
	// =========================================================================
	class RateLimiter
	{
	public:
		RateLimiter() = default;
		explicit RateLimiter(float maxRatePerSecond) : m_MaxRate(maxRatePerSecond) {}

		void SetMaxRate(float ratePerSecond) { m_MaxRate = ratePerSecond; }

		void Reset(float value = 0.0f) { m_Y = value; m_Primed = true; }

		float Update(float target, float dt)
		{
			if (!m_Primed)
			{
				m_Y = target;
				m_Primed = true;
				return m_Y;
			}
			if (dt <= 0.0f)
				return m_Y;

			const float maxStep = m_MaxRate * dt;
			m_Y += std::clamp(target - m_Y, -maxStep, maxStep);
			return m_Y;
		}

		float GetValue() const { return m_Y; }

	private:
		float m_MaxRate = 1.0f;
		float m_Y = 0.0f;
		bool  m_Primed = false;
	};

	// =========================================================================
	// MovingAverage<N> — fixed-window boxcar. O(1) update via running sum.
	// =========================================================================
	template<size_t N>
	class MovingAverage
	{
		static_assert(N >= 1, "MovingAverage window must hold at least one sample");
	public:
		// Seeds the WHOLE window with `value` (GetValue() == value immediately),
		// matching the other filters' Reset(value) contract. m_Count must be N
		// here: with a pre-filled sum but count 0, the next Update would ADD its
		// sample on top of the phantom fill instead of replacing an element.
		void Reset(float value = 0.0f)
		{
			m_Buffer.fill(value);
			m_Sum = value * static_cast<float>(N);
			m_Head = 0;
			m_Count = N;
		}

		// dt unused — kept for the common Update(sample, dt) contract.
		float Update(float sample, float /*dt*/ = 0.0f)
		{
			if (m_Count < N)
			{
				m_Buffer[m_Head] = sample;
				m_Sum += sample;
				++m_Count;
			}
			else
			{
				m_Sum += sample - m_Buffer[m_Head];
				m_Buffer[m_Head] = sample;
			}
			m_Head = (m_Head + 1) % N;
			return GetValue();
		}

		float GetValue() const
		{
			return m_Count ? m_Sum / static_cast<float>(m_Count) : 0.0f;
		}

	private:
		std::array<float, N> m_Buffer{};
		float  m_Sum = 0.0f;
		size_t m_Head = 0;
		size_t m_Count = 0;
	};

	// =========================================================================
	// Biquad — direct-form-I two-pole filter with RBJ cookbook coefficient
	// setups (LPF / HPF / notch). Sample-rate dependent: configure with the
	// rate you will call Update at; call SetLowPass/... again if it changes.
	// (Fixed-step sims have exactly this — pass 1/dt.)
	// =========================================================================
	class Biquad
	{
	public:
		enum class Type { LowPass, HighPass, Notch };

		Biquad() { SetLowPass(100.0f, 1000.0f); }

		// q: 0.7071 = Butterworth for LPF/HPF; for a notch, higher q = narrower.
		void SetLowPass (float cutoffHz, float sampleRateHz, float q = 0.70710678f) { Configure(Type::LowPass,  cutoffHz, sampleRateHz, q); }
		void SetHighPass(float cutoffHz, float sampleRateHz, float q = 0.70710678f) { Configure(Type::HighPass, cutoffHz, sampleRateHz, q); }
		void SetNotch   (float centerHz, float sampleRateHz, float q = 10.0f)       { Configure(Type::Notch,    centerHz, sampleRateHz, q); }

		void Reset(float value = 0.0f)
		{
			m_X1 = m_X2 = value;
			// steady-state output for a constant input = value * H(DC)
			const float dc = (m_B0 + m_B1 + m_B2) / (1.0f + m_A1 + m_A2);
			m_Y1 = m_Y2 = value * dc;
		}

		// dt unused — the rate was baked in via Set*(…, sampleRateHz).
		float Update(float sample, float /*dt*/ = 0.0f)
		{
			const float y = m_B0 * sample + m_B1 * m_X1 + m_B2 * m_X2
			              - m_A1 * m_Y1 - m_A2 * m_Y2;
			m_X2 = m_X1; m_X1 = sample;
			m_Y2 = m_Y1; m_Y1 = y;
			return y;
		}

		float GetValue() const { return m_Y1; }

	private:
		void Configure(Type type, float freqHz, float sampleRateHz, float q)
		{
			const float w0    = kFilterTwoPi * freqHz / sampleRateHz;
			const float cw    = std::cos(w0);
			const float sw    = std::sin(w0);
			const float alpha = sw / (2.0f * q);

			float b0, b1, b2, a0, a1, a2;
			switch (type)
			{
			case Type::LowPass:
				b0 = (1.0f - cw) * 0.5f; b1 = 1.0f - cw; b2 = b0;
				a0 = 1.0f + alpha;       a1 = -2.0f * cw; a2 = 1.0f - alpha;
				break;
			case Type::HighPass:
				b0 = (1.0f + cw) * 0.5f; b1 = -(1.0f + cw); b2 = b0;
				a0 = 1.0f + alpha;       a1 = -2.0f * cw;   a2 = 1.0f - alpha;
				break;
			case Type::Notch:
			default:
				b0 = 1.0f;         b1 = -2.0f * cw; b2 = 1.0f;
				a0 = 1.0f + alpha; a1 = -2.0f * cw; a2 = 1.0f - alpha;
				break;
			}

			m_B0 = b0 / a0; m_B1 = b1 / a0; m_B2 = b2 / a0;
			m_A1 = a1 / a0; m_A2 = a2 / a0;
		}

		float m_B0 = 0, m_B1 = 0, m_B2 = 0, m_A1 = 0, m_A2 = 0;
		float m_X1 = 0, m_X2 = 0, m_Y1 = 0, m_Y2 = 0;
	};

	// =========================================================================
	// Washout — first-order high-pass. Passes transients, washes out steady
	// state to zero: the motion-cue filter (also useful for removing sensor
	// bias from rate signals).
	//
	// y = alpha * (y_prev + x - x_prev),  alpha = tau / (tau + dt)
	// =========================================================================
	class Washout
	{
	public:
		Washout() = default;
		explicit Washout(float tau) : m_Tau(tau) {}

		void SetTau(float tau) { m_Tau = tau; }

		void Reset(float value = 0.0f)
		{
			m_PrevX = value;
			m_Y = 0.0f;
			m_Primed = true;
		}

		float Update(float sample, float dt)
		{
			if (!m_Primed)
			{
				m_PrevX = sample;   // start washed-out: steady input -> 0 out
				m_Primed = true;
				m_Y = 0.0f;
				return m_Y;
			}
			if (dt <= 0.0f)
				return m_Y;

			const float alpha = m_Tau / (m_Tau + dt);
			m_Y = alpha * (m_Y + sample - m_PrevX);
			m_PrevX = sample;
			return m_Y;
		}

		float GetValue() const { return m_Y; }

	private:
		float m_Tau = 1.0f;
		float m_PrevX = 0.0f;
		float m_Y = 0.0f;
		bool  m_Primed = false;
	};
}
