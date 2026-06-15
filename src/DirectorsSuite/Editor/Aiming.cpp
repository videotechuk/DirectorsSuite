#include "Aiming.h"
#include "EditorMath.h"
#include "Config.h"
#include "CameraDirector.h"
#include "..\script.h"
#include "..\keyboard.h"

void CAiming::PickSpecificPedFromAim()
{
	Entity aimed = 0;
	if (PLAYER::GET_ENTITY_PLAYER_IS_FREE_AIMING_AT(PLAYER::PLAYER_ID(), &aimed)) {
		if (ENTITY::DOES_ENTITY_EXIST(aimed) && ENTITY::IS_ENTITY_A_PED(aimed)) {
			SpecificPed = (Ped)aimed;
		}
	}
}

bool CAiming::IsValidTarget(Ped ped)
{
	Ped player = PLAYER::PLAYER_PED_ID();
	if (ped == player || !ENTITY::DOES_ENTITY_EXIST(ped)) return false;

	bool isHuman = PED::IS_PED_HUMAN(ped);
	bool isDead = ENTITY::IS_ENTITY_DEAD(ped);

	if ((TargetsMask & AA_TARGET_SPECIFIC_PED) && ped == SpecificPed) return true;

	if ((TargetsMask & AA_TARGET_MODELS) && !m_modelHashes.empty()) {
		Hash model = ENTITY::GET_ENTITY_MODEL(ped);
		for (Hash h : m_modelHashes) {
			if (h == model) return true;
		}
	}

	if (isHuman) {
		if (TargetsMask & AA_TARGET_HUMANS_ALL) return !isDead;
		if (TargetsMask & AA_TARGET_HUMANS_COMBAT) {
			return !isDead && PED::IS_PED_IN_COMBAT(ped, player);
		}
		return false;
	}

	// Animals
	if (!AnimalAimAssist) return false;
	if (TargetsMask & AA_TARGET_ANIMALS) {
		switch (g_Director.AnimalPriority) {
			case ANIMAL_PRIORITY_ALIVE: return !isDead;
			case ANIMAL_PRIORITY_DEAD:  return isDead;
			default:                    return true;
		}
	}
	return false;
}

Vector3 CAiming::GetTargetBonePosition(Ped ped)
{
	if (BodyPartIndex >= 0 && BodyPartIndex < (int)AimBodyParts.size()) {
		int boneIndex = ENTITY::GET_ENTITY_BONE_INDEX_BY_NAME(ped, AimBodyParts[BodyPartIndex].bone);
		if (boneIndex != -1) {
			return ENTITY::GET_WORLD_POSITION_OF_ENTITY_BONE(ped, boneIndex);
		}
	}
	// Animals or missing bones: fall back to the entity origin
	return ENTITY::GET_ENTITY_COORDS(ped, true, true);
}

Ped CAiming::FindBestTarget()
{
	const int ARR_SIZE = 1024;
	int peds[ARR_SIZE];
	int found = worldGetAllPeds(peds, ARR_SIZE);

	Vector3 camPos = CAM::GET_GAMEPLAY_CAM_COORD();
	Vector3 camRot = CAM::GET_GAMEPLAY_CAM_ROT(2);
	Vector3 camFwd = EMath::RotationToDirection(camRot);

	float bestScore = -1.0f;
	Ped best = 0;
	float maxDist = g_Config.AimAssistMaxDistance;
	float coneCos = cosf(g_Config.AimAssistConeDegrees * EMath::DEG2RAD);

	for (int i = 0; i < found; i++) {
		Ped ped = peds[i];
		if (!IsValidTarget(ped)) continue;

		Vector3 pos = ENTITY::GET_ENTITY_COORDS(ped, true, true);
		Vector3 toTarget = EMath::Sub(pos, camPos);
		float dist = EMath::Length(toTarget);
		if (dist < 1.0f || dist > maxDist) continue;

		float cosAngle = EMath::Dot(EMath::Normalize(toTarget), camFwd);
		if (cosAngle < coneCos) continue; // outside the assist cone

		// Prefer targets closest to the crosshair, slightly biased to nearby ones
		float score = cosAngle * 10.0f + (1.0f - dist / maxDist);
		if (score > bestScore) {
			bestScore = score;
			best = ped;
		}
	}
	return best;
}

void CAiming::UpdateAimAssist()
{
	if (!AimAssistEnabled) return;
	if (!PLAYER::IS_PLAYER_FREE_AIMING(PLAYER::PLAYER_ID())) return;
	if (!IsKeyDown((DWORD)g_Config.KeyAimAssist)) return;

	Ped target = FindBestTarget();
	if (target == 0) return;

	Vector3 bonePos = GetTargetBonePosition(target);
	Vector3 camPos = CAM::GET_GAMEPLAY_CAM_COORD();
	Vector3 camRot = CAM::GET_GAMEPLAY_CAM_ROT(2);

	Vector3 desired = EMath::DirectionToRotation(EMath::Normalize(EMath::Sub(bonePos, camPos)));
	float deltaYaw = EMath::NormalizeAngle(desired.z - camRot.z);
	float deltaPitch = EMath::NormalizeAngle(desired.x - camRot.x);

	int snapIdx = SnapStrengthIndex;
	if (snapIdx < 0) snapIdx = 0;
	if (snapIdx >= (int)AimSnapStrengths.size()) snapIdx = (int)AimSnapStrengths.size() - 1;
	float strength = AimSnapStrengths[snapIdx].lerpPerFrame;

	CAM::SET_GAMEPLAY_CAM_RELATIVE_HEADING(CAM::GET_GAMEPLAY_CAM_RELATIVE_HEADING() + deltaYaw * strength, 1.0f);
	CAM::SET_GAMEPLAY_CAM_RELATIVE_PITCH(CAM::GET_GAMEPLAY_CAM_RELATIVE_PITCH() + deltaPitch * strength, 1.0f);

	if (HighlightAimTarget) {
		DrawTargetMarker(bonePos, 255, 60, 60);
	}
}

void CAiming::UpdateAimMode()
{
	if (AimMode == AIM_MODE_OFF) return;

	Ped player = PLAYER::PLAYER_PED_ID();
	bool editorCamActive = g_Director.IsRendering();

	// Camera Direction I uses the engine's own native: bullets follow the
	// rendering script camera.
	if (AimMode == AIM_MODE_CAMERA_DIRECTION_1) {
		if (editorCamActive) {
			Cam cam = g_Director.ActiveEngineCam();
			if (cam != 0 && CAM::DOES_CAM_EXIST(cam)) {
				CAM::SET_CAM_AFFECTS_AIMING(cam, true);
			}
		}
		return;
	}

	// The scripted modes only matter while the player is holding aim
	if (!PAD::IS_CONTROL_PRESSED(0, INPUT_AIM) && !PAD::IS_DISABLED_CONTROL_PRESSED(0, INPUT_AIM)) {
		return;
	}

	Vector3 from{}, dir{};
	switch (AimMode) {
		case AIM_MODE_PLAYER_HEADING:
		{
			from = ENTITY::GET_ENTITY_COORDS(player, true, true);
			dir = ENTITY::GET_ENTITY_FORWARD_VECTOR(player);
			break;
		}
		case AIM_MODE_CAMERA_DIRECTION_2:
		{
			if (!editorCamActive) return;
			Cam cam = g_Director.ActiveEngineCam();
			if (cam == 0 || !CAM::DOES_CAM_EXIST(cam)) return;
			from = CAM::GET_CAM_COORD(cam);
			dir = EMath::RotationToDirection(CAM::GET_CAM_ROT(cam, 2));
			break;
		}
		case AIM_MODE_GAMEPLAY_CAM:
		{
			from = CAM::GET_GAMEPLAY_CAM_COORD();
			dir = EMath::RotationToDirection(CAM::GET_GAMEPLAY_CAM_ROT(2));
			break;
		}
		default:
			return;
	}

	Vector3 aimAt = EMath::Add(from, EMath::Scale(dir, 60.0f));
	TASK::TASK_AIM_GUN_AT_COORD(player, aimAt.x, aimAt.y, aimAt.z, 250, false, false);

	if (HighlightAimTarget) {
		DrawTargetMarker(aimAt, 255, 200, 0);
	}
}

void CAiming::DrawTargetMarker(const Vector3& worldPos, int r, int g, int b)
{
	float sx = 0.0f, sy = 0.0f;
	if (GRAPHICS::GET_SCREEN_COORD_FROM_WORLD_COORD(worldPos.x, worldPos.y, worldPos.z, &sx, &sy)) {
		GRAPHICS::DRAW_RECT(sx, sy, 0.006f, 0.0022f, r, g, b, 230, false, false);
		GRAPHICS::DRAW_RECT(sx, sy, 0.0028f, 0.0048f, r, g, b, 230, false, false);
	}
}

void CAiming::Tick()
{
	if (!m_modelsResolved) {
		m_modelHashes.clear();
		for (const auto& name : g_Config.AimAssistModels) {
			m_modelHashes.push_back(MISC::GET_HASH_KEY(name.c_str()));
		}
		m_modelsResolved = true;
	}

	UpdateAimMode();
	UpdateAimAssist();

	// Highlight whatever the player is naturally aiming at
	if (HighlightAimTarget && PLAYER::IS_PLAYER_FREE_AIMING(PLAYER::PLAYER_ID())) {
		Entity aimed = 0;
		if (PLAYER::GET_ENTITY_PLAYER_IS_FREE_AIMING_AT(PLAYER::PLAYER_ID(), &aimed) && ENTITY::DOES_ENTITY_EXIST(aimed)) {
			Vector3 pos = ENTITY::GET_ENTITY_COORDS(aimed, true, true);
			DrawTargetMarker(pos, 120, 255, 120);
		}
	}
}
