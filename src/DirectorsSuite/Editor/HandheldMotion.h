// Director's Suite - Handheld / phone camera motion.
//
// A procedural "operator holding the camera" motion layer for the video editor.
// Unlike the engine's SHAKE_CAM presets (jittery rumble), this is slow, organic,
// wandering movement - the sway, roll tilt, breathing bob and micro-tremor of a
// hand holding a phone while filming. It is pure math (layered, incommensurate
// sines so it never visibly loops) producing small position + rotation offsets
// that the camera director adds on top of each shot's base transform every frame.

#pragma once
#include "..\..\..\inc\types.h"

namespace HandheldMotion
{
	// Result of evaluating the motion at a moment in time.
	struct Offsets
	{
		Vector3 posLocal{}; // metres, camera-local: x = right, y = forward, z = up
		Vector3 rot{};      // degrees: x = pitch, y = roll, z = yaw
	};

	// style: index into StyleName() (0 = Off). intensity: 0..1 master scale.
	// t: time in seconds (any continuous source; the motion is stateless in t).
	Offsets Evaluate(int style, float intensity, float t);

	int StyleCount();
	const char* StyleName(int style);
}
