#include "SceneLights.h"
#include "EditorMath.h"
#include "HeroLight.h"      // DaylightBoost (shared with the hero rig)
#include "TimecycleRT.h"
#include "..\script.h"
#include "..\UI\UIUtil.h"

namespace
{
	// label, spawn model, and the RGB we FORCE on the entity light each frame (so
	// the colour is driven by the mod, not the baked .ytyp - reliable regardless
	// of how the asset turned out).
	struct ArtLightDef { const char* label; const char* model; int r, g, b; };
	const std::vector<ArtLightDef> kModels = {
		{ "White",      "csk_light_white",  255, 255, 255 },
		{ "Warm White", "csk_light_warm",   255, 214, 170 },
		{ "Blue",       "csk_light_blue",    70, 130, 255 },
		{ "Cyan",       "csk_light_cyan",    60, 220, 255 },
		{ "Green",      "csk_light_green",   80, 255, 120 },
		{ "Yellow",     "csk_light_yellow", 255, 230,  90 },
		{ "Orange",     "csk_light_orange", 255, 137,  56 },
		{ "Red",        "csk_light_red",    255,  55,  45 },
		{ "Pink",       "csk_light_pink",   255,  90, 170 },
		{ "Purple",     "csk_light_purple", 200,  90, 255 },
	};

	// The "lens": whatever camera is currently rendering (gameplay, free or editor
	// cam), so a placed light lands in front of the view in every mode.
	Vector3 LensPos() { return CAM::GET_FINAL_RENDERED_CAM_COORD(); }
	Vector3 LensRot() { return CAM::GET_FINAL_RENDERED_CAM_ROT(2); }
}

namespace SceneLights
{
	int         ModelCount()        { return (int)kModels.size(); }
	const char* ModelLabel(int i)   { return (i >= 0 && i < (int)kModels.size()) ? kModels[i].label : ""; }

	Light* Get(State& s, int slot)
	{
		if (slot < 0 || slot >= MAX) return nullptr;
		return s.lights[slot].used ? &s.lights[slot] : nullptr;
	}

	Light* Selected(State& s) { return Get(s, s.sel); }

	void ApplyTransform(State& s, int slot)
	{
		Light* a = Get(s, slot);
		if (!a) return;
		if (a->obj == 0 || !ENTITY::DOES_ENTITY_EXIST(a->obj)) return;
		ENTITY::SET_ENTITY_COORDS(a->obj, a->pos.x, a->pos.y, a->pos.z, false, false, false, false);
		ENTITY::SET_ENTITY_ROTATION(a->obj, a->rot.x, a->rot.y, a->rot.z, 2, true);
	}

	int Spawn(State& s)
	{
		int slot = -1;
		for (int i = 0; i < MAX; i++) {
			if (!s.lights[i].used) { slot = i; break; }
		}
		if (slot < 0) {
			UIUtil::PrintSubtitle("~COLOR_RED~All " + std::to_string(MAX) + " light slots in use~s~");
			return -1;
		}

		int mi = (s.modelIdx >= 0 && s.modelIdx < (int)kModels.size()) ? s.modelIdx : 0;
		Hash model = MISC::GET_HASH_KEY(kModels[mi].model);

		// Custom models are NOT in the base cdimage, so we don't gate on
		// IS_MODEL_IN_CDIMAGE - we just request and wait, and report if it never
		// loads (which means the csk_lights asset pack isn't installed/enabled).
		STREAMING::REQUEST_MODEL(model, false);
		DWORD start = GetTickCount();
		while (!STREAMING::HAS_MODEL_LOADED(model)) {
			if (GetTickCount() - start > 4000) {
				UIUtil::PrintSubtitle("~COLOR_RED~'" + std::string(kModels[mi].model) + "' failed to load~s~ - is the csk_lights asset pack installed in lml?");
				return -1;
			}
			WAIT(10);
		}

		// Drop it a couple of metres ahead of the lens.
		Vector3 camRot = LensRot();
		Vector3 fwd = EMath::RotationToDirection(camRot);
		Vector3 pos = EMath::Add(LensPos(), EMath::Scale(fwd, 2.0f));

		Object obj = OBJECT::CREATE_OBJECT(model, pos.x, pos.y, pos.z, false, false, false, false, false);
		STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
		if (obj == 0) {
			UIUtil::PrintSubtitle("~COLOR_RED~Could not create the light prop~s~");
			return -1;
		}

		ENTITY::SET_ENTITY_COLLISION(obj, false, false);
		ENTITY::FREEZE_ENTITY_POSITION(obj, true);
		ENTITY::_SET_ENTITY_LIGHTS_ENABLED(obj, true);
		// Force colour + intensity from code rather than trusting the baked .ytyp.
		GRAPHICS::_SET_LIGHTS_COLOR_FOR_ENTITY(obj, kModels[mi].r, kModels[mi].g, kModels[mi].b);
		GRAPHICS::_SET_LIGHTS_INTENSITY_FOR_ENTITY(obj, 12.0f);
		GRAPHICS::UPDATE_LIGHTS_ON_ENTITY(obj);

		Light& a = s.lights[slot];
		a = Light{};
		a.used = true;
		a.obj = obj;
		a.modelIdx = mi;
		a.r = kModels[mi].r;
		a.g = kModels[mi].g;
		a.b = kModels[mi].b;
		a.pos = pos;
		a.rot = Vector3{ 0.0f, 0.0f, camRot.z }; // face away from the lens by default
		ApplyTransform(s, slot);
		s.sel = slot;
		return slot;
	}

	void Destroy(State& s, int slot)
	{
		if (slot < 0 || slot >= MAX) return;
		Light& a = s.lights[slot];
		if (a.obj != 0 && ENTITY::DOES_ENTITY_EXIST(a.obj)) {
			Object o = a.obj;
			OBJECT::DELETE_OBJECT(&o);
		}
		a = Light{};
	}

	void RemoveAll(State& s)
	{
		for (int i = 0; i < MAX; i++) Destroy(s, i);
	}

	void MoveSelectedToCamera(State& s)
	{
		Light* a = Selected(s);
		if (!a) return;
		Vector3 fwd = EMath::RotationToDirection(LensRot());
		a->pos = EMath::Add(LensPos(), EMath::Scale(fwd, 2.0f));
		ApplyTransform(s, s.sel);
	}

	void Update(State& s, bool hideForShot)
	{
		// Daylight compensation: entity lights (like all scripted lights) are
		// washed out by the sun, so they "stop working" in daytime. Scale the
		// intensity up while the sun is high - the same curve the hero rig uses
		// (~1x night, ~6x noon) - so the props stay visible at any time of day.
		float boost = HeroLight::DaylightBoost();
		for (int i = 0; i < MAX; i++) {
			Light& a = s.lights[i];
			if (!a.used) continue;
			if (a.obj == 0 || !ENTITY::DOES_ENTITY_EXIST(a.obj)) { a = Light{}; continue; }
			// Hide only the fixture MESH for a clean shot, never the entity: hiding
			// the whole entity (SET_ENTITY_VISIBLE false) also kills its baked
			// light. Fading the mesh to alpha 0 leaves the entity - and its light -
			// active, so the glow stays but the bulb prop vanishes.
			ENTITY::SET_ENTITY_VISIBLE(a.obj, true);
			ENTITY::SET_ENTITY_ALPHA(a.obj, hideForShot ? 0 : 255, false);
			ENTITY::_SET_ENTITY_LIGHTS_ENABLED(a.obj, true);
			GRAPHICS::_SET_LIGHTS_COLOR_FOR_ENTITY(a.obj, a.r, a.g, a.b);
			GRAPHICS::_SET_LIGHTS_INTENSITY_FOR_ENTITY(a.obj, a.intensity * boost);
			GRAPHICS::UPDATE_LIGHTS_ON_ENTITY(a.obj);
		}
	}

	// ---------------------------------------------------------------------------
	// Sun
	// ---------------------------------------------------------------------------

	void InitSun(State& s)
	{
		// Graceful no-op on unsupported game builds; safe to call repeatedly.
		TimecycleRT::Init();
		if (s.sunVarX < 0) s.sunVarX = TimecycleRT::FindVarByName("sun_direction_x");
		if (s.sunVarY < 0) s.sunVarY = TimecycleRT::FindVarByName("sun_direction_y");
		if (s.sunVarZ < 0) s.sunVarZ = TimecycleRT::FindVarByName("sun_direction_z");
		if (!s.sunUserEdited) SeedSunFromClock(s);
	}

	bool SunAvailable(const State& s)
	{
		return TimecycleRT::VarsAvailable() && s.sunVarX >= 0 && s.sunVarY >= 0;
	}

	// Seed Sun Direction / Sun Height from the current clock so the controls open
	// showing roughly where the sun actually is (the engine drives the sun from
	// time of day). Midnight = North & below the horizon, 06:00 = East at the
	// horizon, noon = South & high, 18:00 = West at the horizon. An approximation,
	// but it matches the visible sun closely and needs no fragile timecycle reads.
	void SeedSunFromClock(State& s)
	{
		float h = (float)CLOCK::GET_CLOCK_HOURS() + (float)CLOCK::GET_CLOCK_MINUTES() / 60.0f;

		float az = ((h - 6.0f) / 24.0f) * 360.0f + 90.0f;
		while (az < 0.0f) az += 360.0f;
		while (az >= 360.0f) az -= 360.0f;

		float el = 75.0f * sinf((h - 6.0f) / 12.0f * 3.14159265f);

		s.sunAz = az;
		s.sunEl = el;
	}

	// The timecycle sun is a unit direction vector split across
	// sun_direction_x/y/z. Azimuth + Elevation rebuild the whole normalized
	// vector so the sun moves in every direction. Azimuth is a true compass
	// bearing measured clockwise from North (0 = N/+Y, 90 = E/+X); elevation 0
	// sits on the horizon, 90 straight overhead.
	void ApplySun(const State& s)
	{
		if (s.sunVarX < 0 || s.sunVarY < 0) return; // need the horizontal axes at least

		float az = s.sunAz * EMath::DEG2RAD;
		float el = s.sunEl * EMath::DEG2RAD;
		float ce = cosf(el);

		float dx = ce * sinf(az);
		float dy = ce * cosf(az);
		float dz = sinf(el);

		TimecycleRT::ApplyVarToScene(s.sunVarX, dx);
		TimecycleRT::ApplyVarToScene(s.sunVarY, dy);
		if (s.sunVarZ >= 0) TimecycleRT::ApplyVarToScene(s.sunVarZ, dz);
	}

	void RestoreSun()
	{
		TimecycleRT::RestoreAllEdits();
	}
}
