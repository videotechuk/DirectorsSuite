// Director's Suite - Hero Lighting (authentic per-character light rigs).
//
// Rockstar light rigs - the exact system the game's photo studio, catalogue
// and gambling minigames use to light characters: an anim scene of the
// "lightrig@*" family whose origin is pinned to the character every frame.
// Authentic R* cinematic lighting with real shadows, zero custom parameters
// (everything is baked into the rig asset). Runs every frame in every mode
// (edit/preview/record).
//
// Free-placed scene lights (the "Improved Artificial Lighting" props) and the
// sun live in the shared SceneLights system instead.

#pragma once
#include "DirectorTypes.h"

namespace HeroLight
{
	// Drives the rig state machine for one character. Call every frame.
	void Update(Ped target, HeroLightSetup& setup);

	// Tears down the anim scene.
	void Shutdown(HeroLightSetup& setup);

	// Intensity multiplier that keeps scripted lights visible in daylight (~1x at
	// night, up to ~6x at solar noon). Shared by the hero rig and the SceneLights
	// art-light props so both read at any time of day. See HeroLight.cpp for the
	// curve.
	float DaylightBoost();
}
