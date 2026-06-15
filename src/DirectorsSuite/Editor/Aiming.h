// Director's Suite - aiming features.
//
// Aim modes decide which direction the player shoots while an editor camera
// is rendering (player heading, camera direction, gameplay camera, ...).
// Aim Assist softly (or hard) snaps the gameplay camera onto a chosen body
// part of the best target while the assist key (default C) is held during
// free aim.

#pragma once
#include <vector>
#include <string>
#include "EditorTypes.h"
#include "..\..\..\inc\types.h"

class CAiming
{
public:
	// --- Aim modes ---
	int AimMode = AIM_MODE_OFF;             // eAimMode
	bool HighlightAimTarget = false;

	// --- Aim Assist ---
	bool AimAssistEnabled = false;
	bool AnimalAimAssist = true;            // include animals when enabled
	unsigned TargetsMask = AA_TARGET_HUMANS_COMBAT;
	int BodyPartIndex = 0;                  // index into AimBodyParts
	int SnapStrengthIndex = 1;              // index into AimSnapStrengths (Medium)
	Ped SpecificPed = 0;

	void PickSpecificPedFromAim();          // capture the ped currently aimed at

	void Tick();                            // must run every frame

private:
	std::vector<Hash> m_modelHashes;        // resolved from INI on first tick
	bool m_modelsResolved = false;

	Ped FindBestTarget();
	bool IsValidTarget(Ped ped);
	Vector3 GetTargetBonePosition(Ped ped);
	void UpdateAimMode();
	void UpdateAimAssist();
	void DrawTargetMarker(const Vector3& worldPos, int r, int g, int b);
};

inline CAiming g_Aiming;
