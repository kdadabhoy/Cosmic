#pragma once

// Timestep.h
// Last Modified 5/14/2026

/**
 * General Description:
 * The Timestep class is a lightweight utility wrapper designed to handle frame timing
 * and delta-time calculations within the Cosmic Engine. Its primary purpose is to
 * provide a unified representation of time that can be easily converted between
 * seconds and milliseconds, ensuring consistency across different engine subsystems.
 *
 * By encapsulating a float value, it prevents "unit-confusion" (accidentally mixing
 * seconds and milliseconds) in update loops. It also includes an implicit conversion
 * operator, allowing it to be treated as a raw float when performing direct
 * mathematical operations.
 *
 * Public Function Prototypes (Pre and Post Conditions):
 *
 * 1. Timestep(float time = 0.0f)
 *    Post: Internal time is initialized to the provided value (assumed to be in seconds).
 *
 * 2. float GetSeconds()
 *    Post: Returns the stored time value in seconds.
 *
 * 3. float GetMilliseconds()
 *    Post: Returns the stored time value multiplied by 1000.0.
 *
 * 4. operator float()
 *    Post: Implicitly returns the internal time in seconds when the object is used
 *          in a float context.
 */

namespace Cosmic
{
	class COSMIC_API Timestep
	{
	public:
		////////////////////////////////
		// Construction
		///////////////////////////////

		Timestep(float time = 0.0f)
			: m_Time(time)
		{
		}

		////////////////////////////////
		// Time Conversions
		///////////////////////////////

		float GetSeconds() const				{ return m_Time; }
		float GetMilliseconds() const			{ return m_Time * 1000.0f; }


		////////////////////////////////
		// Operators
		///////////////////////////////

		operator float() const					{ return m_Time; }


	private:
		// The duration of the frame, stored internally as seconds.
		float m_Time;
	};
}