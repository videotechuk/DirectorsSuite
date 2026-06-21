// Director's Suite - Scene Lighting (the "Improved Artificial Lighting V2"
// system, shared so both Photo Mode and the video editor light a scene the
// same way).
//
// Two layers, both scene-global (placed in the world, not pinned to a
// character - that is what the per-character Rockstar rig in HeroLight is for):
//
// 1. Art-light props (csk_lights - our own LML asset pack, built from the stock
//    p_lightbulb01x fixture with recolored, always-on light extensions). Each
//    .ydr drawable carries its own baked light, so spawning one and enabling
//    its entity light makes it glow as a real, shadow-free source. Each placed
//    prop has a full position + rotation + intensity + colour editor and is
//    daylight-compensated so it reads at any time of day.
//
// 2. The sun: RDR3's timecycle exposes the sun as a 3-axis direction vector
//    (sun_direction_x/y/z). Azimuth + Elevation rebuild that normalized vector
//    so the scene's key light can be aimed anywhere. Driven through TimecycleRT
//    and reverted with TimecycleRT::RestoreAllEdits().
//
// All state lives in a State struct so each mode owns its own instance (no
// cross-mode coupling); the functions below operate on a State by reference.

#pragma once
#include "..\..\..\inc\types.h"
#include <vector>
#include <string>

namespace SceneLights
{
	// A placed art-light prop: a real world object with its own baked light.
	struct Light
	{
		bool    used = false;
		Object  obj = 0;
		int     modelIdx = 0;
		Vector3 pos{};
		Vector3 rot{};                     // x pitch, y roll, z heading (degrees)
		float   intensity = 12.0f;         // forced on the entity light each frame
		int     r = 255, g = 255, b = 255; // colour forced on the entity light
	};

	constexpr int MAX = 8;

	struct State
	{
		Light lights[MAX];
		int   sel = 0;          // which placed light the editor targets
		int   modelIdx = 0;     // colour/model chosen for the next spawn

		// Sun direction control (timecycle). Resolved by InitSun().
		int   sunVarX = -1, sunVarY = -1, sunVarZ = -1;
		float sunAz = 0.0f;     // compass bearing of the sun, degrees
		float sunEl = 0.0f;     // height above the horizon, degrees
		bool  sunUserEdited = false; // once true, keep the user's values
	};

	// --- model table (the csk_lights colours) ---
	int         ModelCount();
	const char* ModelLabel(int i);

	// --- art lights (camera = GET_FINAL_RENDERED_CAM, so it follows whatever
	// the user is looking through: gameplay cam, free cam or an editor cam) ---
	int    Spawn(State& s);                 // place the chosen colour ahead of the lens; returns slot or -1
	void   Destroy(State& s, int slot);
	void   RemoveAll(State& s);
	Light* Get(State& s, int slot);
	Light* Selected(State& s);              // the slot the editor targets, or nullptr
	void   ApplyTransform(State& s, int slot); // push pos/rot to the engine object
	void   MoveSelectedToCamera(State& s);
	void   Update(State& s, bool hideForShot); // per frame: keep lit + daylight boost (+ hide mesh for a clean shot)

	// --- sun ---
	void  InitSun(State& s);   // resolve the timecycle sun vars + seed from clock (safe to call repeatedly)
	bool  SunAvailable(const State& s);
	void  SeedSunFromClock(State& s); // set az/el to roughly the current time-of-day position
	void  ApplySun(const State& s);   // write az/el -> sun_direction_x/y/z
	void  RestoreSun();               // undo every timecycle edit (TimecycleRT::RestoreAllEdits)
}
