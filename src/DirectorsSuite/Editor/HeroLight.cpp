#include "HeroLight.h"
#include "EditorMath.h"
#include "..\script.h"

namespace
{
	// Rig lifecycle mirrors shop_photo_studio.c:
	// _CREATE_ANIM_SCENE -> metadata loaded -> LOAD_ANIM_SCENE -> loaded ->
	// SET_ANIM_SCENE_ORIGIN + START_ANIM_SCENE -> ... -> _DELETE_ANIM_SCENE
	void UpdateRig(Ped target, HeroLightSetup& setup)
	{
		bool targetValid = ENTITY::DOES_ENTITY_EXIST(target);

		// Tear down when disabled, the rig style changed, or the target died
		if (!setup.rigEnabled || !targetValid || (setup.rigState != RIG_OFF && setup.activeRigIndex != setup.rigIndex)) {
			if (setup.rigScene != 0 && ANIMSCENE::DOES_ANIM_SCENE_EXIST(setup.rigScene)) {
				ANIMSCENE::_DELETE_ANIM_SCENE(setup.rigScene);
			}
			setup.rigScene = 0;
			setup.rigState = RIG_OFF;
			setup.activeRigIndex = -1;
			if (!setup.rigEnabled || !targetValid) return;
		}

		Vector3 pos = ENTITY::GET_ENTITY_COORDS(target, true, true);
		float heading = ENTITY::GET_ENTITY_HEADING(target);

		switch (setup.rigState) {
			case RIG_OFF:
			{
				int idx = setup.rigIndex;
				if (idx < 0 || idx >= (int)HeroLightRigs.size()) idx = 0;
				setup.rigScene = ANIMSCENE::_CREATE_ANIM_SCENE(HeroLightRigs[idx], 0, 0, false, true);
				setup.activeRigIndex = setup.rigIndex;
				setup.rigState = RIG_CREATING;
				break;
			}
			case RIG_CREATING:
			{
				if (!ANIMSCENE::DOES_ANIM_SCENE_EXIST(setup.rigScene)) { setup.rigState = RIG_OFF; break; }
				if (ANIMSCENE::IS_ANIM_SCENE_METADATA_LOADED(setup.rigScene, false)) {
					ANIMSCENE::LOAD_ANIM_SCENE(setup.rigScene);
					setup.rigState = RIG_LOADING;
				}
				break;
			}
			case RIG_LOADING:
			{
				if (!ANIMSCENE::DOES_ANIM_SCENE_EXIST(setup.rigScene)) { setup.rigState = RIG_OFF; break; }
				if (ANIMSCENE::IS_ANIM_SCENE_LOADED(setup.rigScene, true, false)) {
					ANIMSCENE::SET_ANIM_SCENE_ORIGIN(setup.rigScene, pos.x, pos.y, pos.z, 0.0f, 0.0f, heading, 2);
					ANIMSCENE::START_ANIM_SCENE(setup.rigScene);
					setup.rigState = RIG_RUNNING;
				}
				break;
			}
			case RIG_RUNNING:
			{
				if (!ANIMSCENE::DOES_ANIM_SCENE_EXIST(setup.rigScene)) { setup.rigState = RIG_OFF; break; }
				// Keep the rig pinned to the character so the lighting
				// follows them through the scene.
				ANIMSCENE::SET_ANIM_SCENE_ORIGIN(setup.rigScene, pos.x, pos.y, pos.z, 0.0f, 0.0f, heading, 2);
				break;
			}
		}
	}

	// Daylight compensation.
	//
	// DRAW_LIGHT_WITH_RANGE intensities tuned to read at night are simply
	// swamped by the sun's exposure in daytime - that is why the hero lights
	// "stopped working" once the clock moved past dawn. The engine exposes no
	// "render in daylight" flag for scripted lights, so we compensate the only
	// way available: scale intensity up while the sun is high so the lights stay
	// visible regardless of time of day. ~1x through the night, ramping to ~6x
	// at solar noon, with smooth dawn/dusk shoulders.
	float DaylightBoostImpl()
	{
		int h = CLOCK::GET_CLOCK_HOURS();
		int m = CLOCK::GET_CLOCK_MINUTES();
		float t = (float)h + (float)m / 60.0f;

		// 0 = full night, 1 = full midday. Night < 5, day 8..17, dusk by 21.
		float day;
		if (t < 5.0f || t >= 21.0f) day = 0.0f;
		else if (t < 8.0f)  day = (t - 5.0f) / 3.0f;          // dawn ramp
		else if (t <= 17.0f) day = 1.0f;                       // full day
		else day = 1.0f - (t - 17.0f) / 4.0f;                  // dusk ramp

		day = EMath::Clamp(day, 0.0f, 1.0f);
		return 1.0f + day * 5.0f; // 1x night -> 6x midday
	}

	void DrawPoint(const Vector3& pedPos, float pedHeading, const HeroLightPoint& light)
	{
		if (!light.enabled) return;

		// Orbit position around the character, relative to their heading so
		// "key at 45 degrees" stays on the same cheek when they turn.
		float angleRad = (pedHeading + light.orbitDeg) * EMath::DEG2RAD;
		Vector3 offset{};
		offset.x = -sinf(angleRad) * light.distance;
		offset.y = cosf(angleRad) * light.distance;
		offset.z = light.height;

		Vector3 lightPos = EMath::Add(pedPos, offset);
		GRAPHICS::DRAW_LIGHT_WITH_RANGE(lightPos.x, lightPos.y, lightPos.z,
			light.r, light.g, light.b, light.range, light.intensity * DaylightBoostImpl());
	}
}

float HeroLight::DaylightBoost() { return DaylightBoostImpl(); }

namespace HeroLight
{
	void Update(Ped target, HeroLightSetup& setup)
	{
		UpdateRig(target, setup);

		if (!ENTITY::DOES_ENTITY_EXIST(target)) return;
		if (!setup.key.enabled && !setup.fill.enabled && !setup.back.enabled) return;

		Vector3 pos = ENTITY::GET_ENTITY_COORDS(target, true, true);
		float heading = ENTITY::GET_ENTITY_HEADING(target);

		DrawPoint(pos, heading, setup.key);
		DrawPoint(pos, heading, setup.fill);
		DrawPoint(pos, heading, setup.back);
	}

	void Shutdown(HeroLightSetup& setup)
	{
		if (setup.rigScene != 0 && ANIMSCENE::DOES_ANIM_SCENE_EXIST(setup.rigScene)) {
			ANIMSCENE::_DELETE_ANIM_SCENE(setup.rigScene);
		}
		setup.rigScene = 0;
		setup.rigState = RIG_OFF;
		setup.activeRigIndex = -1;
		setup.rigEnabled = false;
	}
}
