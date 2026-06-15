// Director's Suite - Placement Camera Mode: a free-flying camera for
// composing shots and placing cameras anywhere in the world.
// WASD + mouse (or sticks), Shift = fast, Ctrl = slow, Space up, Z down,
// Q/E zoom. Streaming focus follows the camera so the world stays fully
// detailed far away from the player.

#pragma once
#include "..\..\..\inc\types.h"

class CFreeCam
{
public:
	// When >= 0, Placement Camera Mode is repositioning this existing camera
	// (index into g_CameraManager.Cameras): the Add Camera key saves the new
	// position into it instead of creating a new camera.
	int EditingCameraIndex = -1;

	void Toggle();
	void Activate();
	// Open Placement Camera Mode at a specific pose (Edit Camera Placement)
	void ActivateAt(const Vector3& pos, const Vector3& rot, float fov);
	void Deactivate();
	bool IsActive() const { return m_active; }

	Vector3 Position() const { return m_pos; }
	Vector3 Rotation() const { return m_rot; }
	float Fov() const { return m_fov; }

	void Tick(); // must run every frame while active

private:
	bool m_active = false;
	Cam m_cam = 0;
	Vector3 m_pos{};
	Vector3 m_rot{};
	float m_fov = 50.0f;
	bool m_focusSet = false;
	Ped m_frozenPlayer = 0;   // player ped frozen while flying
	Ped m_frozenMount = 0;    // their mount, if any
	DWORD m_lastPrompt = 0;   // throttles the on-screen INSERT hint

	void ShowPlacementPrompt();
	void FreezePlayer();
	void UnfreezePlayer();
};

inline CFreeCam g_FreeCam;
