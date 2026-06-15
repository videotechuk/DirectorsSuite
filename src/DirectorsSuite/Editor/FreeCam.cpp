#include "FreeCam.h"
#include "EditorMath.h"
#include "Config.h"
#include "..\script.h"
#include "..\keyboard.h"
#include "..\UI\UIUtil.h"

void CFreeCam::Toggle()
{
	if (m_active) Deactivate();
	else Activate();
}

// Keeps the placement instructions on screen for the whole flight
// (subtitles fade after a few seconds, so they are re-issued periodically).
void CFreeCam::ShowPlacementPrompt()
{
	if (GetTickCount() - m_lastPrompt < 3500) return;
	m_lastPrompt = GetTickCount();

	std::string key = (g_Config.KeyAddCamera == VK_INSERT) ? "INSERT" : "the Add Camera key";
	if (EditingCameraIndex >= 0) {
		UIUtil::PrintSubtitle("~COLOR_OBJECTIVE~PLACEMENT CAMERA~s~  Reposition the camera, then press ~COLOR_BLUE~" + key + "~s~ to save its new position");
	}
	else {
		UIUtil::PrintSubtitle("~COLOR_OBJECTIVE~PLACEMENT CAMERA~s~  Press ~COLOR_BLUE~" + key + "~s~ to place a camera here");
	}
}

// While the camera is far away the world streams around IT, not the player.
// An unfrozen player would lose collision and fall through the map, so they
// are frozen in place for the duration of the flight.
void CFreeCam::FreezePlayer()
{
	m_frozenPlayer = PLAYER::PLAYER_PED_ID();
	ENTITY::FREEZE_ENTITY_POSITION(m_frozenPlayer, true);

	if (PED::IS_PED_ON_MOUNT(m_frozenPlayer)) {
		m_frozenMount = PED::GET_MOUNT(m_frozenPlayer);
		if (ENTITY::DOES_ENTITY_EXIST(m_frozenMount)) {
			ENTITY::FREEZE_ENTITY_POSITION(m_frozenMount, true);
		}
	}
}

void CFreeCam::UnfreezePlayer()
{
	if (m_frozenPlayer != 0 && ENTITY::DOES_ENTITY_EXIST(m_frozenPlayer)) {
		ENTITY::FREEZE_ENTITY_POSITION(m_frozenPlayer, false);
	}
	if (m_frozenMount != 0 && ENTITY::DOES_ENTITY_EXIST(m_frozenMount)) {
		ENTITY::FREEZE_ENTITY_POSITION(m_frozenMount, false);
	}
	m_frozenPlayer = 0;
	m_frozenMount = 0;
}

void CFreeCam::Activate()
{
	ActivateAt(CAM::GET_FINAL_RENDERED_CAM_COORD(), CAM::GET_FINAL_RENDERED_CAM_ROT(2), CAM::GET_FINAL_RENDERED_CAM_FOV());
}

void CFreeCam::ActivateAt(const Vector3& pos, const Vector3& rot, float fov)
{
	if (m_active) {
		// Already flying: just jump to the requested pose
		m_pos = pos;
		m_rot = rot;
		m_fov = fov;
		return;
	}

	EditingCameraIndex = -1;
	m_lastPrompt = 0; // show the instructions immediately
	m_pos = pos;
	m_rot = rot;
	m_fov = fov;

	if (m_cam == 0 || !CAM::DOES_CAM_EXIST(m_cam)) {
		m_cam = CAM::CREATE_CAM("DEFAULT_SCRIPTED_CAMERA", false);
	}
	CAM::SET_CAM_COORD(m_cam, m_pos.x, m_pos.y, m_pos.z);
	CAM::SET_CAM_ROT(m_cam, m_rot.x, m_rot.y, m_rot.z, 2);
	CAM::SET_CAM_FOV(m_cam, m_fov);
	CAM::SET_CAM_ACTIVE(m_cam, true);
	CAM::RENDER_SCRIPT_CAMS(true, false, 0, true, false, 0);

	FreezePlayer();
	m_active = true;
}

void CFreeCam::Deactivate()
{
	if (!m_active) return;

	CAM::RENDER_SCRIPT_CAMS(false, false, 0, true, false, 0);
	if (m_cam != 0 && CAM::DOES_CAM_EXIST(m_cam)) {
		CAM::SET_CAM_ACTIVE(m_cam, false);
		CAM::DESTROY_CAM(m_cam, false);
		m_cam = 0;
	}
	if (m_focusSet) {
		STREAMING::CLEAR_FOCUS();
		m_focusSet = false;
	}
	UnfreezePlayer();
	m_active = false;
	EditingCameraIndex = -1;
}

void CFreeCam::Tick()
{
	if (!m_active || m_cam == 0) return;

	ShowPlacementPrompt();

	// Take over the controls while flying
	PAD::DISABLE_CONTROL_ACTION(0, INPUT_LOOK_LR, false);
	PAD::DISABLE_CONTROL_ACTION(0, INPUT_LOOK_UD, false);
	PAD::DISABLE_CONTROL_ACTION(0, INPUT_MOVE_LR, false);
	PAD::DISABLE_CONTROL_ACTION(0, INPUT_MOVE_UD, false);
	PAD::DISABLE_CONTROL_ACTION(0, INPUT_ATTACK, false);
	PAD::DISABLE_CONTROL_ACTION(0, INPUT_AIM, false);
	PAD::DISABLE_CONTROL_ACTION(0, INPUT_JUMP, false);
	PAD::DISABLE_CONTROL_ACTION(0, INPUT_SPRINT, false);

	// --- look ---
	float lookLR = PAD::GET_DISABLED_CONTROL_NORMAL(0, INPUT_LOOK_LR);
	float lookUD = PAD::GET_DISABLED_CONTROL_NORMAL(0, INPUT_LOOK_UD);
	m_rot.z -= lookLR * g_Config.FreeCamMouseSensitivity;
	m_rot.x = EMath::Clamp(m_rot.x - lookUD * g_Config.FreeCamMouseSensitivity, -89.0f, 89.0f);

	// --- move ---
	float speed = g_Config.FreeCamSpeed;
	if (IsKeyDown(VK_SHIFT)) speed *= g_Config.FreeCamFastMultiplier;
	if (IsKeyDown(VK_CONTROL)) speed *= g_Config.FreeCamSlowMultiplier;

	float moveLR = PAD::GET_DISABLED_CONTROL_NORMAL(0, INPUT_MOVE_LR);
	float moveUD = PAD::GET_DISABLED_CONTROL_NORMAL(0, INPUT_MOVE_UD);

	Vector3 forward = EMath::RotationToDirection(m_rot);
	Vector3 flatRot{};
	flatRot.z = m_rot.z - 90.0f;
	Vector3 right = EMath::RotationToDirection(flatRot);

	m_pos = EMath::Add(m_pos, EMath::Scale(forward, -moveUD * speed));
	m_pos = EMath::Add(m_pos, EMath::Scale(right, moveLR * speed));

	// vertical: Space up, Z down (also gamepad jump/sprint-style controls)
	if (IsKeyDown(VK_SPACE) || PAD::IS_DISABLED_CONTROL_PRESSED(0, INPUT_JUMP)) m_pos.z += speed;
	if (IsKeyDown('Z')) m_pos.z -= speed;

	// FOV: Q/E for zoom out/in
	if (IsKeyDown('Q')) m_fov = EMath::Clamp(m_fov + 0.5f, 5.0f, 120.0f);
	if (IsKeyDown('E')) m_fov = EMath::Clamp(m_fov - 0.5f, 5.0f, 120.0f);

	CAM::SET_CAM_COORD(m_cam, m_pos.x, m_pos.y, m_pos.z);
	CAM::SET_CAM_ROT(m_cam, m_rot.x, m_rot.y, m_rot.z, 2);
	CAM::SET_CAM_FOV(m_cam, m_fov);

	// High-detail streaming: keep the world loaded around the camera instead
	// of the player, so distant places render fully. Moving the streaming
	// focus near the player starves it of nothing, but doing it constantly
	// causes LOD pop and collision gaps - so the focus only moves once the
	// camera is genuinely far away, and collision is explicitly requested at
	// the camera every frame.
	Vector3 playerPos = ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true, true);
	float distFromPlayer = EMath::Distance(m_pos, playerPos);

	if (g_Config.FreeCamHighDetail && distFromPlayer > 100.0f) {
		STREAMING::SET_FOCUS_POS_AND_VEL(m_pos.x, m_pos.y, m_pos.z, 0.0f, 0.0f, 0.0f);
		STREAMING::REQUEST_COLLISION_AT_COORD(m_pos.x, m_pos.y, m_pos.z);
		m_focusSet = true;
	}
	else if (m_focusSet) {
		STREAMING::CLEAR_FOCUS();
		m_focusSet = false;
	}
}
