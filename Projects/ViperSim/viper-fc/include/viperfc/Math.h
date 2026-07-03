#pragma once

// viperfc/Math.h
//
// ============================================================================
// viper-fc — portable vector/quaternion math (NO glm, NO STL containers)
// ============================================================================
//
// The flight-computer library compiles unmodified in the ViperSim DLL (MSVC),
// in plain doctest unit tests, and in the PlatformIO Teensy 4.1 project — so
// it depends on nothing but <cmath>/<cstdint>. Conventions (must match the
// engine's Spatial.h):
//
//   WORLD:  NED, right-handed. +X North, +Y East, +Z Down. Gravity +Z.
//   BODY:   FRD. +X out the nose (thrust axis), +Y right, +Z belly.
//           The Viper is a tailsitter: in HOVER the nose (+X body) points UP
//           (world -Z); in CRUISE it points along the flight path.
//   QUAT:   q rotates BODY -> WORLD. (w, x, y, z). q1*q2 applies q2 first.
//   EULER:  ZYX intrinsic (yaw, pitch, roll), radians here (degrees only at UI).
//
// All functions are pure, allocation-free, and constexpr-friendly.
// ============================================================================

#include <cmath>
#include <cstdint>

namespace viperfc
{
	constexpr float kPi       = 3.14159265358979323846f;
	constexpr float kTwoPi    = 6.28318530717958647692f;
	constexpr float kGravity  = 9.80665f;   // m/s^2, world +Z (Down)
	constexpr float kRhoAir   = 1.225f;     // kg/m^3 sea-level standard

	inline float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
	inline float Deg(float rad) { return rad * (180.0f / kPi); }
	inline float Rad(float deg) { return deg * (kPi / 180.0f); }

	// Wrap an angle to (-pi, pi].
	inline float WrapPi(float a)
	{
		while (a >  kPi) a -= kTwoPi;
		while (a <= -kPi) a += kTwoPi;
		return a;
	}

	// =========================================================================
	// Vec3
	// =========================================================================
	struct Vec3
	{
		float x = 0.0f, y = 0.0f, z = 0.0f;

		Vec3() = default;
		Vec3(float X, float Y, float Z) : x(X), y(Y), z(Z) {}

		Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
		Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
		Vec3 operator-()              const { return { -x, -y, -z }; }
		Vec3 operator*(float s)       const { return { x * s, y * s, z * s }; }
		Vec3 operator/(float s)       const { return { x / s, y / s, z / s }; }
		Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
		Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
		Vec3& operator*=(float s)       { x *= s; y *= s; z *= s; return *this; }
	};

	inline Vec3  operator*(float s, const Vec3& v) { return v * s; }
	inline float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	inline Vec3  Cross(const Vec3& a, const Vec3& b)
	{
		return { a.y * b.z - a.z * b.y,
		         a.z * b.x - a.x * b.z,
		         a.x * b.y - a.y * b.x };
	}
	inline float NormSq(const Vec3& v) { return Dot(v, v); }
	inline float Norm(const Vec3& v)   { return std::sqrt(NormSq(v)); }
	inline Vec3  Normalized(const Vec3& v)
	{
		const float n = Norm(v);
		return (n > 1e-9f) ? v / n : Vec3{ 0, 0, 0 };
	}
	inline Vec3 ClampNorm(const Vec3& v, float maxNorm)
	{
		const float n = Norm(v);
		return (n > maxNorm && n > 1e-9f) ? v * (maxNorm / n) : v;
	}

	// =========================================================================
	// Quat — body->world attitude
	// =========================================================================
	struct Quat
	{
		float w = 1.0f, x = 0.0f, y = 0.0f, z = 0.0f;

		Quat() = default;
		Quat(float W, float X, float Y, float Z) : w(W), x(X), y(Y), z(Z) {}
	};

	inline Quat operator*(const Quat& a, const Quat& b)   // applies b first
	{
		return {
			a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
			a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
			a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
			a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
		};
	}

	inline Quat Conj(const Quat& q) { return { q.w, -q.x, -q.y, -q.z }; }

	inline Quat Normalized(const Quat& q)
	{
		const float n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
		if (n < 1e-9f) return {};
		return { q.w / n, q.x / n, q.y / n, q.z / n };
	}

	// Rotate a BODY vector into WORLD (q ⊗ v ⊗ q*).
	inline Vec3 Rotate(const Quat& q, const Vec3& v)
	{
		const Vec3 u{ q.x, q.y, q.z };
		const Vec3 t = Cross(u, v) * 2.0f;
		return v + t * q.w + Cross(u, t);
	}

	// Rotate a WORLD vector into BODY.
	inline Vec3 RotateInv(const Quat& q, const Vec3& v) { return Rotate(Conj(q), v); }

	inline Quat FromAxisAngle(const Vec3& axisUnit, float angleRad)
	{
		const float h = angleRad * 0.5f;
		const float s = std::sin(h);
		return { std::cos(h), axisUnit.x * s, axisUnit.y * s, axisUnit.z * s };
	}

	// ZYX (yaw-pitch-roll) Euler -> quaternion, radians.
	inline Quat FromEulerZYX(float rollRad, float pitchRad, float yawRad)
	{
		const Quat qy = FromAxisAngle({ 0, 0, 1 }, yawRad);
		const Quat qp = FromAxisAngle({ 0, 1, 0 }, pitchRad);
		const Quat qr = FromAxisAngle({ 1, 0, 0 }, rollRad);
		return qy * qp * qr;
	}

	// Quaternion -> ZYX Euler (roll, pitch, yaw), radians. Pitch clamped ±90°.
	inline void ToEulerZYX(const Quat& q, float& rollRad, float& pitchRad, float& yawRad)
	{
		const float sinp = Clampf(2.0f * (q.w * q.y - q.z * q.x), -1.0f, 1.0f);
		rollRad  = std::atan2(2.0f * (q.w * q.x + q.y * q.z), 1.0f - 2.0f * (q.x * q.x + q.y * q.y));
		pitchRad = std::asin(sinp);
		yawRad   = std::atan2(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (q.y * q.y + q.z * q.z));
	}

	// First-order body-rate integration (q̇ = ½ q ⊗ ω), renormalized — the same
	// kinematics as the engine's Spatial.h so sim truth and FC estimate agree.
	inline Quat IntegrateBodyRate(const Quat& q, const Vec3& omegaBody, float dt)
	{
		const Quat om{ 0.0f, omegaBody.x, omegaBody.y, omegaBody.z };
		const Quat qd = q * om;
		return Normalized({ q.w + 0.5f * qd.w * dt,
		                    q.x + 0.5f * qd.x * dt,
		                    q.y + 0.5f * qd.y * dt,
		                    q.z + 0.5f * qd.z * dt });
	}

	// Attitude error as a BODY-frame rotation vector (axis * angle, rad) taking
	// `q` to `qd` — the quantity a quaternion attitude P-loop feeds rate PIDs.
	// Shortest path (sign-corrected for the double cover).
	inline Vec3 AttitudeErrorBody(const Quat& q, const Quat& qd)
	{
		Quat e = Conj(q) * qd;
		if (e.w < 0.0f) { e.w = -e.w; e.x = -e.x; e.y = -e.y; e.z = -e.z; }
		const float sinHalf = std::sqrt(e.x * e.x + e.y * e.y + e.z * e.z);
		if (sinHalf < 1e-9f)
			return { 0, 0, 0 };
		const float angle = 2.0f * std::atan2(sinHalf, e.w);
		return Vec3{ e.x, e.y, e.z } * (angle / sinHalf);
	}

	// Normalized lerp between attitudes (shortest path). Good enough for the
	// transition BLEND crossfade — angles between the two targets stay small.
	inline Quat Nlerp(const Quat& a, const Quat& b, float t)
	{
		const float d = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
		const float s = d < 0.0f ? -1.0f : 1.0f;   // double-cover fix
		return Normalized({
			a.w + (s * b.w - a.w) * t,
			a.x + (s * b.x - a.x) * t,
			a.y + (s * b.y - a.y) * t,
			a.z + (s * b.z - a.z) * t });
	}

	// Build a body->world quaternion from an orthonormal body basis expressed in
	// world axes (columns of the rotation matrix). Robust Shepperd's method.
	inline Quat FromBasis(const Vec3& xb, const Vec3& yb, const Vec3& zb)
	{
		// Rotation matrix R (world<-body): columns xb, yb, zb.
		const float m00 = xb.x, m01 = yb.x, m02 = zb.x;
		const float m10 = xb.y, m11 = yb.y, m12 = zb.y;
		const float m20 = xb.z, m21 = yb.z, m22 = zb.z;
		const float tr = m00 + m11 + m22;

		Quat q;
		if (tr > 0.0f)
		{
			const float s = std::sqrt(tr + 1.0f) * 2.0f;
			q.w = 0.25f * s;
			q.x = (m21 - m12) / s;
			q.y = (m02 - m20) / s;
			q.z = (m10 - m01) / s;
		}
		else if (m00 > m11 && m00 > m22)
		{
			const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
			q.w = (m21 - m12) / s;
			q.x = 0.25f * s;
			q.y = (m01 + m10) / s;
			q.z = (m02 + m20) / s;
		}
		else if (m11 > m22)
		{
			const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
			q.w = (m02 - m20) / s;
			q.x = (m01 + m10) / s;
			q.y = 0.25f * s;
			q.z = (m12 + m21) / s;
		}
		else
		{
			const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
			q.w = (m10 - m01) / s;
			q.x = (m02 + m20) / s;
			q.y = (m12 + m21) / s;
			q.z = 0.25f * s;
		}
		return Normalized(q);
	}
}
