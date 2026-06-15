// Director's Suite - Director Mode: scene NPCs, character controls, world
// controls and hero lighting (see DirectorTypes.h for the data tables).
//
// Facial expressions and scenarios are only applied while a scene is
// "live": previewing through an editor camera, recording through OBS, or
// when the user enables Preview Scene manually. Hero lighting is always
// active (editing, previewing and recording).

#pragma once
#include <vector>
#include "DirectorTypes.h"

class CDirectorMode
{
public:
	// --- scene NPCs ---
	std::vector<SceneNPC> NPCs;
	int SelectedNPC = -1;

	SceneNPC* Get(int index)
	{
		if (index < 0 || index >= (int)NPCs.size()) return nullptr;
		return &NPCs[index];
	}
	SceneNPC* GetSelected() { return Get(SelectedNPC); }

	// Spawns in front of the current view, ground-snapped, facing the camera.
	// Blocks (bounded) while the model streams in. Returns index or -1.
	int  SpawnNPC(const char* label, const char* modelName);
	void DeleteNPC(int index);
	void DeleteNPCByHandle(Ped handle); // remove the NPC with this ped handle, if any
	void DeleteAllNPCs();

	// --- placement / composition ---
	void PlaceAtCurrentView(SceneNPC& npc); // move to the spot the camera looks at
	void NudgeNPC(SceneNPC& npc, float forward, float right, float up);
	void SetNPCHeading(SceneNPC& npc, float heading);
	void FaceCamera(SceneNPC& npc);
	void FacePlayer(SceneNPC& npc);
	void SnapToGround(SceneNPC& npc);
	void SetFrozen(SceneNPC& npc, bool frozen);

	// --- properties / behaviour ---
	void ApplyHealth(SceneNPC& npc);
	void ApplyInvincible(SceneNPC& npc);
	void ApplyHostility(SceneNPC& npc);   // relationship groups + combat attributes
	void ApplyAccuracy(SceneNPC& npc);
	void ApplyWeapon(SceneNPC& npc);
	void ApplyOutfit(SceneNPC& npc);
	void ApplyTask(SceneNPC& npc);        // immediate tasks (stand/wander/combat/...)

	// --- player character controls ---
	int PlayerFacialIndex = 0;
	int PlayerScenarioIndex = 0;
	HeroLightSetup PlayerLight;

	// --- preview gating ---
	// Facial expressions + scenarios run only while the scene is live.
	bool ManualPreview = false; // "Preview Scene" menu toggle
	bool IsSceneLive() const;

	// --- world controls ---
	bool  OverrideDensity = false;
	float PedDensity = 1.0f;     // 0 = empty streets
	bool  KeepAreaClear = false;
	float ClearRadius = 60.0f;
	void  ClearAreaNow();        // remove ambient peds/animals around the player

	// --- hostile scene trigger ---
	// A countdown gives the player time to frame the shot; at zero every
	// Hostile-configured NPC engages and the player becomes invincible so
	// they can capture the action without dying.
	void StartHostileCountdown(int seconds);
	void CancelHostileCountdown();
	bool HostileArmed = false;       // invincibility + combat currently active

	void Tick(); // must run every frame

private:
	bool m_prevLive = false;
	DWORD m_lastClearSweep = 0;
	DWORD m_hostileFireTime = 0;     // GetTickCount when combat should begin (0 = idle)
	void EngageHostileNPCs();
	void DrawHostileCountdown();
	Hash m_relGroups[4] = { 0, 0, 0, 0 }; // per eHostility custom rel groups
	bool m_relGroupsCreated = false;

	void EnsureRelationshipGroups();
	void ApplyLiveState(bool live);
	void ApplyFacial(Ped ped, int facialIndex);
	void ApplyScenario(Ped ped, int scenarioIndex);
	void ValidateNPCs(); // drop entries whose entity vanished
};

inline CDirectorMode g_DirectorMode;
