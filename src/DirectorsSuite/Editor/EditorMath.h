// Director's Suite - small math helpers shared by the camera systems

#pragma once
#define _USE_MATH_DEFINES
#include <cmath>
#include "..\..\..\inc\types.h"

namespace EMath
{
	constexpr float DEG2RAD = 0.01745329252f;
	constexpr float RAD2DEG = 57.2957795131f;

	// rot.x = pitch, rot.z = yaw (rotation order 2, degrees) -> unit forward vector
	inline Vector3 RotationToDirection(const Vector3& rot)
	{
		float pitch = rot.x * DEG2RAD;
		float yaw = rot.z * DEG2RAD;
		Vector3 dir{};
		dir.x = -sinf(yaw) * cosf(pitch);
		dir.y = cosf(yaw) * cosf(pitch);
		dir.z = sinf(pitch);
		return dir;
	}

	// forward unit vector -> rotation (pitch around x, yaw around z, degrees)
	inline Vector3 DirectionToRotation(const Vector3& dir)
	{
		Vector3 rot{};
		rot.x = asinf(dir.z) * RAD2DEG;
		rot.y = 0.0f;
		rot.z = atan2f(-dir.x, dir.y) * RAD2DEG;
		return rot;
	}

	inline float Distance(const Vector3& a, const Vector3& b)
	{
		float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
		return sqrtf(dx * dx + dy * dy + dz * dz);
	}

	inline float Length(const Vector3& v)
	{
		return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
	}

	inline Vector3 Normalize(const Vector3& v)
	{
		float len = Length(v);
		Vector3 out{};
		if (len > 0.0001f) {
			out.x = v.x / len; out.y = v.y / len; out.z = v.z / len;
		}
		return out;
	}

	inline Vector3 Sub(const Vector3& a, const Vector3& b)
	{
		Vector3 out{};
		out.x = a.x - b.x; out.y = a.y - b.y; out.z = a.z - b.z;
		return out;
	}

	inline Vector3 Add(const Vector3& a, const Vector3& b)
	{
		Vector3 out{};
		out.x = a.x + b.x; out.y = a.y + b.y; out.z = a.z + b.z;
		return out;
	}

	inline Vector3 Scale(const Vector3& v, float s)
	{
		Vector3 out{};
		out.x = v.x * s; out.y = v.y * s; out.z = v.z * s;
		return out;
	}

	inline float Dot(const Vector3& a, const Vector3& b)
	{
		return a.x * b.x + a.y * b.y + a.z * b.z;
	}

	// Rotate a vector around the Z axis by `degrees`
	inline Vector3 RotateZ(const Vector3& v, float degrees)
	{
		float r = degrees * DEG2RAD;
		float c = cosf(r), s = sinf(r);
		Vector3 out{};
		out.x = v.x * c - v.y * s;
		out.y = v.x * s + v.y * c;
		out.z = v.z;
		return out;
	}

	// Wrap an angle delta into [-180, 180]
	inline float NormalizeAngle(float deg)
	{
		while (deg > 180.0f) deg -= 360.0f;
		while (deg < -180.0f) deg += 360.0f;
		return deg;
	}

	inline float Lerp(float a, float b, float t)
	{
		return a + (b - a) * t;
	}

	inline float Clamp(float v, float lo, float hi)
	{
		return v < lo ? lo : (v > hi ? hi : v);
	}
}
