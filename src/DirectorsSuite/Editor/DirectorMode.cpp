#include "DirectorMode.h"
#include "EditorMath.h"
#include "CameraDirector.h"
#include "HeroLight.h"
#include "SceneLights.h"
#include "OBSClient.h"
#include "..\script.h"
#include "..\UI\Drawing.h"
#include "..\UI\UIUtil.h"

// ---------------------------------------------------------------------------
// Spawning / lifetime
// ---------------------------------------------------------------------------

int CDirectorMode::SpawnNPC(const char* label, const char* modelName)
{
	Hash model = MISC::GET_HASH_KEY(modelName);
	if (!STREAMING::IS_MODEL_IN_CDIMAGE(model) || !STREAMING::IS_MODEL_VALID(model)) {
		return -1;
	}

	STREAMING::REQUEST_MODEL(model, false);
	DWORD start = GetTickCount();
	while (!STREAMING::HAS_MODEL_LOADED(model)) {
		if (GetTickCount() - start > 5000) {
			return -1; // model never streamed in
		}
		WAIT(10);
	}

	// Spawn on the ground, ~3m in front of whatever the user is looking
	// through (gameplay cam, free cam or an editor camera).
	Vector3 camPos = CAM::GET_FINAL_RENDERED_CAM_COORD();
	Vector3 camRot = CAM::GET_FINAL_RENDERED_CAM_ROT(2);
	Vector3 fwd = EMath::RotationToDirection(camRot);
	Vector3 pos = EMath::Add(camPos, EMath::Scale(fwd, 3.0f));

	float groundZ = pos.z;
	if (MISC::GET_GROUND_Z_FOR_3D_COORD(pos.x, pos.y, pos.z + 2.0f, &groundZ, false)) {
		pos.z = groundZ;
	}

	// Face the camera
	float heading = atan2f(-(camPos.x - pos.x), (camPos.y - pos.y)) * EMath::RAD2DEG;

	Ped ped = PED::CREATE_PED(model, pos.x, pos.y, pos.z, heading, false, true, false, false);
	STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
	if (ped == 0) {
		return -1;
	}

	// RDR2 quirk: freshly created peds have no outfit and are invisible
	// until a variation is applied.
	PED::_SET_RANDOM_OUTFIT_VARIATION(ped, true);

	PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped, true);
	PED::SET_PED_KEEP_TASK(ped, true);
	TASK::TASK_STAND_STILL(ped, -1);

	SceneNPC npc;
	npc.handle = ped;
	npc.model = model;
	npc.modelName = modelName;
	npc.name = std::string(label) + " #" + std::to_string((int)NPCs.size() + 1);
	NPCs.push_back(npc);
	return (int)NPCs.size() - 1;
}

void CDirectorMode::DeleteNPC(int index)
{
	SceneNPC* npc = Get(index);
	if (!npc) return;

	HeroLight::Shutdown(npc->light);
	if (npc->handle != 0 && ENTITY::DOES_ENTITY_EXIST(npc->handle)) {
		Ped ped = npc->handle;
		PED::DELETE_PED(&ped);
	}
	NPCs.erase(NPCs.begin() + index);
	if (SelectedNPC >= (int)NPCs.size()) {
		SelectedNPC = (int)NPCs.size() - 1;
	}
}

void CDirectorMode::DeleteNPCByHandle(Ped handle)
{
	if (handle == 0) return;
	for (int i = 0; i < (int)NPCs.size(); i++) {
		if (NPCs[i].handle == handle) {
			DeleteNPC(i);
			return;
		}
	}
}

void CDirectorMode::DeleteAllNPCs()
{
	while (!NPCs.empty()) {
		DeleteNPC((int)NPCs.size() - 1);
	}
	SelectedNPC = -1;
}

void CDirectorMode::ValidateNPCs()
{
	for (int i = (int)NPCs.size() - 1; i >= 0; i--) {
		if (NPCs[i].handle == 0 || !ENTITY::DOES_ENTITY_EXIST(NPCs[i].handle)) {
			HeroLight::Shutdown(NPCs[i].light);
			NPCs.erase(NPCs.begin() + i);
			if (SelectedNPC == i) SelectedNPC = -1;
		}
	}
}

// ---------------------------------------------------------------------------
// Placement & composition
// ---------------------------------------------------------------------------

void CDirectorMode::PlaceAtCurrentView(SceneNPC& npc)
{
	if (!ENTITY::DOES_ENTITY_EXIST(npc.handle)) return;

	Vector3 camPos = CAM::GET_FINAL_RENDERED_CAM_COORD();
	Vector3 fwd = EMath::RotationToDirection(CAM::GET_FINAL_RENDERED_CAM_ROT(2));
	Vector3 pos = EMath::Add(camPos, EMath::Scale(fwd, 3.0f));

	float groundZ = pos.z;
	if (MISC::GET_GROUND_Z_FOR_3D_COORD(pos.x, pos.y, pos.z + 2.0f, &groundZ, false)) {
		pos.z = groundZ;
	}

	ENTITY::SET_ENTITY_COORDS(npc.handle, pos.x, pos.y, pos.z, false, false, false, false);
	FaceCamera(npc);
}

void CDirectorMode::NudgeNPC(SceneNPC& npc, float forward, float right, float up)
{
	if (!ENTITY::DOES_ENTITY_EXIST(npc.handle)) return;
	Vector3 pos = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(npc.handle, right, forward, up);
	ENTITY::SET_ENTITY_COORDS(npc.handle, pos.x, pos.y, pos.z, false, false, false, false);
}

void CDirectorMode::SetNPCHeading(SceneNPC& npc, float heading)
{
	if (!ENTITY::DOES_ENTITY_EXIST(npc.handle)) return;
	ENTITY::SET_ENTITY_HEADING(npc.handle, heading);
}

void CDirectorMode::FaceCamera(SceneNPC& npc)
{
	if (!ENTITY::DOES_ENTITY_EXIST(npc.handle)) return;
	Vector3 camPos = CAM::GET_FINAL_RENDERED_CAM_COORD();
	Vector3 pos = ENTITY::GET_ENTITY_COORDS(npc.handle, true, true);
	float heading = atan2f(-(camPos.x - pos.x), (camPos.y - pos.y)) * EMath::RAD2DEG;
	ENTITY::SET_ENTITY_HEADING(npc.handle, heading);
}

void CDirectorMode::FacePlayer(SceneNPC& npc)
{
	if (!ENTITY::DOES_ENTITY_EXIST(npc.handle)) return;
	TASK::TASK_TURN_PED_TO_FACE_ENTITY(npc.handle, PLAYER::PLAYER_PED_ID(), 1500, 0.0f, 0.0f, 0.0f);
}

void CDirectorMode::SnapToGround(SceneNPC& npc)
{
	if (!ENTITY::DOES_ENTITY_EXIST(npc.handle)) return;
	Vector3 pos = ENTITY::GET_ENTITY_COORDS(npc.handle, true, true);
	float groundZ = pos.z;
	if (MISC::GET_GROUND_Z_FOR_3D_COORD(pos.x, pos.y, pos.z + 2.0f, &groundZ, false)) {
		ENTITY::SET_ENTITY_COORDS(npc.handle, pos.x, pos.y, groundZ, false, false, false, false);
	}
}

void CDirectorMode::SetFrozen(SceneNPC& npc, bool frozen)
{
	npc.frozen = frozen;
	if (ENTITY::DOES_ENTITY_EXIST(npc.handle)) {
		ENTITY::FREEZE_ENTITY_POSITION(npc.handle, frozen);
	}
}

// ---------------------------------------------------------------------------
// Properties & behaviour
// ---------------------------------------------------------------------------

void CDirectorMode::EnsureRelationshipGroups()
{
	if (m_relGroupsCreated) return;
	PED::ADD_RELATIONSHIP_GROUP("RDRE_NEUTRAL", &m_relGroups[HOSTILITY_NEUTRAL]);
	PED::ADD_RELATIONSHIP_GROUP("RDRE_FRIENDLY", &m_relGroups[HOSTILITY_FRIENDLY]);
	PED::ADD_RELATIONSHIP_GROUP("RDRE_HOSTILE", &m_relGroups[HOSTILITY_HOSTILE]);
	PED::ADD_RELATIONSHIP_GROUP("RDRE_SCARED", &m_relGroups[HOSTILITY_SCARED]);

	Hash player = MISC::GET_HASH_KEY("PLAYER");
	// relation type ids: 2=LIKE, 3=IGNORE, 4=DISLIKE, 6=HATE
	PED::SET_RELATIONSHIP_BETWEEN_GROUPS(3, m_relGroups[HOSTILITY_NEUTRAL], player);
	PED::SET_RELATIONSHIP_BETWEEN_GROUPS(3, player, m_relGroups[HOSTILITY_NEUTRAL]);
	PED::SET_RELATIONSHIP_BETWEEN_GROUPS(2, m_relGroups[HOSTILITY_FRIENDLY], player);
	PED::SET_RELATIONSHIP_BETWEEN_GROUPS(2, player, m_relGroups[HOSTILITY_FRIENDLY]);
	PED::SET_RELATIONSHIP_BETWEEN_GROUPS(6, m_relGroups[HOSTILITY_HOSTILE], player);
	PED::SET_RELATIONSHIP_BETWEEN_GROUPS(6, player, m_relGroups[HOSTILITY_HOSTILE]);
	PED::SET_RELATIONSHIP_BETWEEN_GROUPS(4, m_relGroups[HOSTILITY_SCARED], player);
	PED::SET_RELATIONSHIP_BETWEEN_GROUPS(4, player, m_relGroups[HOSTILITY_SCARED]);
	m_relGroupsCreated = true;
}

void CDirectorMode::ApplyHealth(SceneNPC& npc)
{
	if (!ENTITY::DOES_ENTITY_EXIST(npc.handle)) return;
	ENTITY::SET_ENTITY_MAX_HEALTH(npc.handle, npc.health);
	ENTITY::SET_ENTITY_HEALTH(npc.handle, npc.health, 0);
}

void CDirectorMode::ApplyInvincible(SceneNPC& npc)
{
	if (!ENTITY::DOES_ENTITY_EXIST(npc.handle)) return;
	ENTITY::SET_ENTITY_INVINCIBLE(npc.handle, npc.invincible);
}

void CDirectorMode::ApplyHostility(SceneNPC& npc)
{
	if (!ENTITY::DOES_ENTITY_EXIST(npc.handle)) return;
	EnsureRelationshipGroups();

	PED::SET_PED_RELATIONSHIP_GROUP_HASH(npc.handle, m_relGroups[npc.hostility]);

	// combat attribute ids from rdr3_discoveries: 5=ALWAYS_FIGHT, 17=ALWAYS_FLEE, 0=USE_COVER
	switch (npc.hostility) {
		case HOSTILITY_HOSTILE:
			PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(npc.handle, false);
			PED::SET_PED_COMBAT_ATTRIBUTES(npc.handle, 5, true);   // always fight
			PED::SET_PED_COMBAT_ATTRIBUTES(npc.handle, 17, false);
			PED::SET_PED_COMBAT_ATTRIBUTES(npc.handle, 0, true);   // use cover
			break;
		case HOSTILITY_SCARED:
			PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(npc.handle, false);
			PED::SET_PED_COMBAT_ATTRIBUTES(npc.handle, 5, false);
			PED::SET_PED_COMBAT_ATTRIBUTES(npc.handle, 17, true);  // always flee
			break;
		default:
			PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(npc.handle, true);
			PED::SET_PED_COMBAT_ATTRIBUTES(npc.handle, 5, false);
			PED::SET_PED_COMBAT_ATTRIBUTES(npc.handle, 17, false);
			break;
	}
}

void CDirectorMode::ApplyAccuracy(SceneNPC& npc)
{
	if (!ENTITY::DOES_ENTITY_EXIST(npc.handle)) return;
	PED::SET_PED_ACCURACY(npc.handle, npc.accuracy);
}

void CDirectorMode::ApplyWeapon(SceneNPC& npc)
{
	if (!ENTITY::DOES_ENTITY_EXIST(npc.handle)) return;
	if (npc.weaponIndex <= 0) return; // keep default loadout

	const char* weapon = NPCWeapons[npc.weaponIndex];
	if (strcmp(weapon, "Unarmed (remove all)") == 0) {
		WEAPON::REMOVE_ALL_PED_WEAPONS(npc.handle, true, true);
		return;
	}

	WEAPON::GIVE_WEAPON_TO_PED(npc.handle, MISC::GET_HASH_KEY(weapon), 200, true, false, 0, false, 0.5f, 1.0f, 752097756 /*ADD_REASON_DEFAULT*/, false, 0.0f, false);
}

void CDirectorMode::ApplyOutfit(SceneNPC& npc)
{
	if (!ENTITY::DOES_ENTITY_EXIST(npc.handle)) return;
	PED::_EQUIP_META_PED_OUTFIT_PRESET(npc.handle, npc.outfit, false);
}

void CDirectorMode::ApplyTask(SceneNPC& npc)
{
	if (!ENTITY::DOES_ENTITY_EXIST(npc.handle)) return;

	Ped player = PLAYER::PLAYER_PED_ID();
	TASK::CLEAR_PED_TASKS(npc.handle, true, false);

	switch (npc.taskIndex) {
		case NPCTASK_STAND_STILL:
			TASK::TASK_STAND_STILL(npc.handle, -1);
			break;
		case NPCTASK_WANDER:
		{
			Vector3 pos = ENTITY::GET_ENTITY_COORDS(npc.handle, true, true);
			TASK::TASK_WANDER_IN_AREA(npc.handle, pos.x, pos.y, pos.z, 15.0f, 1.0f, 1.0f, 0);
			break;
		}
		case NPCTASK_COMBAT_PLAYER:
			PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(npc.handle, false);
			TASK::TASK_COMBAT_PED(npc.handle, player, 0, 16);
			break;
		case NPCTASK_FLEE_PLAYER:
			TASK::TASK_FLEE_PED(npc.handle, player, 0, 0, -1.0f, -1, 0);
			break;
		case NPCTASK_FOLLOW_PLAYER:
			TASK::TASK_FOLLOW_TO_OFFSET_OF_ENTITY(npc.handle, player, 0.0f, -1.5f, 0.0f, 1.0f, -1, 1.5f, true, false, false, false, false, false);
			break;
		case NPCTASK_AMBIENT_AI:
			PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(npc.handle, false);
			PED::SET_PED_KEEP_TASK(npc.handle, false);
			break;
	}
}

// ---------------------------------------------------------------------------
// Preview gating: facial expressions + scenarios
// ---------------------------------------------------------------------------

bool CDirectorMode::IsSceneLive() const
{
	return ManualPreview || g_Director.IsRendering() || g_OBS.IsRecording();
}

void CDirectorMode::ApplyFacial(Ped ped, int facialIndex)
{
	if (!ENTITY::DOES_ENTITY_EXIST(ped)) return;
	if (facialIndex > 0 && facialIndex < (int)FacialMoods.size()) {
		PED::SET_FACIAL_IDLE_ANIM_OVERRIDE(ped, FacialMoods[facialIndex], 0);
	}
	else {
		PED::CLEAR_FACIAL_IDLE_ANIM_OVERRIDE(ped);
	}
}

void CDirectorMode::ApplyScenario(Ped ped, int scenarioIndex)
{
	if (!ENTITY::DOES_ENTITY_EXIST(ped)) return;
	if (scenarioIndex > 0 && scenarioIndex < (int)ScenarioNames.size()) {
		float heading = ENTITY::GET_ENTITY_HEADING(ped);
		TASK::TASK_START_SCENARIO_IN_PLACE_HASH(ped, MISC::GET_HASH_KEY(ScenarioNames[scenarioIndex]), -1, true, 0, heading, false);
	}
}

void CDirectorMode::ApplyLiveState(bool live)
{
	Ped player = PLAYER::PLAYER_PED_ID();

	if (live) {
		// Scene goes live: push expressions and scenarios onto everyone
		ApplyFacial(player, PlayerFacialIndex);
		if (PlayerScenarioIndex > 0) {
			ApplyScenario(player, PlayerScenarioIndex);
		}
		for (auto& npc : NPCs) {
			ApplyFacial(npc.handle, npc.facialIndex);
			if (npc.scenarioIndex > 0) {
				ApplyScenario(npc.handle, npc.scenarioIndex);
			}
		}
	}
	else {
		// Scene over: clear expressions, stop scenarios, restore base tasks
		PED::CLEAR_FACIAL_IDLE_ANIM_OVERRIDE(player);
		if (PlayerScenarioIndex > 0 && ENTITY::DOES_ENTITY_EXIST(player)) {
			TASK::CLEAR_PED_TASKS(player, true, false);
		}
		for (auto& npc : NPCs) {
			if (!ENTITY::DOES_ENTITY_EXIST(npc.handle)) continue;
			PED::CLEAR_FACIAL_IDLE_ANIM_OVERRIDE(npc.handle);
			if (npc.scenarioIndex > 0) {
				ApplyTask(npc); // back to the configured base task
			}
		}
	}
}

// ---------------------------------------------------------------------------
// World controls
// ---------------------------------------------------------------------------

void CDirectorMode::ClearAreaNow()
{
	Ped player = PLAYER::PLAYER_PED_ID();
	Vector3 center = ENTITY::GET_ENTITY_COORDS(player, true, true);

	const int ARR_SIZE = 1024;
	int peds[ARR_SIZE];
	int found = worldGetAllPeds(peds, ARR_SIZE);

	Ped playerMount = PED::IS_PED_ON_MOUNT(player) ? PED::GET_MOUNT(player) : 0;

	for (int i = 0; i < found; i++) {
		Ped ped = peds[i];
		if (ped == player || ped == playerMount) continue;
		if (!ENTITY::DOES_ENTITY_EXIST(ped)) continue;

		// Never remove scene NPCs
		bool isSceneNPC = false;
		for (const auto& npc : NPCs) {
			if (npc.handle == ped) { isSceneNPC = true; break; }
		}
		if (isSceneNPC) continue;

		Vector3 pos = ENTITY::GET_ENTITY_COORDS(ped, true, true);
		if (EMath::Distance(center, pos) <= ClearRadius) {
			Ped toDelete = ped;
			PED::DELETE_PED(&toDelete);
		}
	}

	MISC::CLEAR_AREA(center.x, center.y, center.z, ClearRadius, 0);
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void CDirectorMode::Tick()
{
	ValidateNPCs();

	// Facial/scenario preview gating (edge-triggered)
	bool live = IsSceneLive();
	if (live != m_prevLive) {
		ApplyLiveState(live);
		m_prevLive = live;
	}

	// Ped density override (per-frame natives)
	if (OverrideDensity) {
		PED::_SET_AMBIENT_PED_DENSITY_MULTIPLIER_THIS_FRAME(PedDensity);
		PED::SET_SCENARIO_PED_DENSITY_MULTIPLIER_THIS_FRAME(PedDensity);
	}

	// Keep-clear sweep every couple of seconds
	if (KeepAreaClear && GetTickCount() - m_lastClearSweep > 2000) {
		ClearAreaNow();
		m_lastClearSweep = GetTickCount();
	}

	// Hostile countdown -> engage + invincibility
	if (m_hostileFireTime != 0) {
		DrawHostileCountdown();
		if (GetTickCount() >= m_hostileFireTime) {
			EngageHostileNPCs();
			m_hostileFireTime = 0;
			HostileArmed = true;
		}
	}
	if (HostileArmed) {
		// Auto stand-down once the firefight is over - i.e. no hostile NPC is
		// still alive - so the player can't be left invincible indefinitely
		// (and gets the "no longer invincible" confirmation). Otherwise keep
		// the player unkillable while capturing the action.
		bool anyHostileAlive = false;
		for (const auto& npc : NPCs) {
			if (npc.hostility == HOSTILITY_HOSTILE
				&& ENTITY::DOES_ENTITY_EXIST(npc.handle)
				&& !ENTITY::IS_ENTITY_DEAD(npc.handle)) {
				anyHostileAlive = true;
				break;
			}
		}
		if (!anyHostileAlive) {
			CancelHostileCountdown(); // drops invincibility + restores base tasks
		}
		else {
			ENTITY::SET_ENTITY_INVINCIBLE(PLAYER::PLAYER_PED_ID(), true);
			PLAYER::SET_PLAYER_INVINCIBLE(PLAYER::PLAYER_ID(), true);
		}
	}

	// Hero lighting runs in every mode: editing, previewing and recording
	Ped player = PLAYER::PLAYER_PED_ID();
	HeroLight::Update(player, PlayerLight);
	for (auto& npc : NPCs) {
		HeroLight::Update(npc.handle, npc.light);
	}

	// Scene lights too. Hide the bulb fixtures (keeping their glow) once the
	// scene goes live, so the placed light props don't show up in the filmed
	// output the way they do while you're composing the shot.
	SceneLights::Update(SceneLighting, IsSceneLive());
}

void CDirectorMode::ClearSceneLighting()
{
	SceneLights::RemoveAll(SceneLighting);
	SceneLights::RestoreSun();
	SceneLighting.sunUserEdited = false;
}

void CDirectorMode::StartHostileCountdown(int seconds)
{
	if (NPCs.empty()) {
		UIUtil::PrintSubtitle("~COLOR_RED~No scene NPCs to make hostile~s~");
		return;
	}
	m_hostileFireTime = GetTickCount() + (DWORD)(seconds * 1000);
	UIUtil::PrintSubtitle("Hostile behaviour in " + std::to_string(seconds) + "s - position your camera");
}

void CDirectorMode::CancelHostileCountdown()
{
	m_hostileFireTime = 0;
	HostileArmed = false;
	PLAYER::SET_PLAYER_INVINCIBLE(PLAYER::PLAYER_ID(), false);
	ENTITY::SET_ENTITY_INVINCIBLE(PLAYER::PLAYER_PED_ID(), false);
	// stand the NPCs back down
	for (auto& npc : NPCs) {
		if (ENTITY::DOES_ENTITY_EXIST(npc.handle)) {
			TASK::CLEAR_PED_TASKS(npc.handle, true, false);
			ApplyTask(npc);
		}
	}
	UIUtil::PrintSubtitle("Hostile behaviour stood down; player no longer invincible");
}

void CDirectorMode::EngageHostileNPCs()
{
	Ped player = PLAYER::PLAYER_PED_ID();
	int engaged = 0;
	for (auto& npc : NPCs) {
		if (!ENTITY::DOES_ENTITY_EXIST(npc.handle)) continue;
		if (npc.hostility != HOSTILITY_HOSTILE) continue;
		PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(npc.handle, false);
		TASK::TASK_COMBAT_PED(npc.handle, player, 0, 16);
		engaged++;
	}
	UIUtil::PrintSubtitle(engaged > 0
		? "~COLOR_RED~Hostile!~s~ " + std::to_string(engaged) + " attacking - you are invincible"
		: "No NPCs are set to Hostile (set it under each NPC's Properties)");
}

void CDirectorMode::DrawHostileCountdown()
{
	int remainMs = (int)(m_hostileFireTime - GetTickCount());
	if (remainMs < 0) remainMs = 0;
	int secs = remainMs / 1000 + 1;

	// darkened pill + big number, centered upper-third
	GRAPHICS::DRAW_RECT(0.5f, 0.30f, 0.13f, 0.13f, 0, 0, 0, 150, false, false);
	Drawing::DrawFormattedText(std::to_string(secs), Font::Title, 255, 70, 60, 255, Alignment::Center, 90,
		SCREEN_WIDTH * 0.5f, SCREEN_HEIGHT * 0.255f);
	Drawing::DrawFormattedText("HOSTILE INCOMING", Font::Body, 255, 255, 255, 230, Alignment::Center, 24,
		SCREEN_WIDTH * 0.5f, SCREEN_HEIGHT * 0.36f);
}
