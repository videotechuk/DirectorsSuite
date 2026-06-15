// Director's Suite - Hero Lighting (artificial character lighting).
//
// Two layers, both running every frame in every mode (edit/preview/record):
//
// 1. Rockstar light rigs - the exact system the game's photo studio,
//    catalogue and gambling minigames use to light characters: an anim
//    scene of the "lightrig@*" family whose origin is pinned to the
//    character every frame. Authentic R* cinematic lighting, zero custom
//    parameters (everything is baked into the rig asset).
//
// 2. A custom three-point rig (key / fill / back) built on per-frame
//    DRAW_LIGHT_WITH_RANGE point lights, exposing intensity, range,
//    colour, orbit angle, distance and height per light. RDR2 has no
//    scripted spot light, so point lights are the full extent of what the
//    engine exposes to scripts.

#pragma once
#include "DirectorTypes.h"

namespace HeroLight
{
	// Drives the rig state machine and draws the custom lights for one
	// character. Call every frame.
	void Update(Ped target, HeroLightSetup& setup);

	// Tears down the anim scene (custom lights stop by not being drawn).
	void Shutdown(HeroLightSetup& setup);

	// Intensity multiplier that keeps scripted DRAW_LIGHT_WITH_RANGE lights
	// visible in daylight (~1x at night, up to ~6x at solar noon). Shared by the
	// hero rig and Photo Mode's placed scene lights so both work at any time of
	// day. See HeroLight.cpp for the curve.
	float DaylightBoost();
}
