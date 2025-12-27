#pragma once

// Constructor takes in seconds

namespace Cosmic
{
	class Timestep
	{
	public:
		Timestep(float time = 0.0f)
			: m_Time(time)
		{

		}

		
		float				GetSeconds() const					{ return m_Time; }
		float				GetMilliseconds() const				{ return m_Time * 1000.0f; }


		// Float operator overload
		operator float() const									{ return m_Time; }

	private:
		float m_Time;		// Stored in seconds
	};

}





