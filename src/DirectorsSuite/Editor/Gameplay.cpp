#include "Gameplay.h"
#include "..\script.h"

void CGameplay::Tick()
{
	Ped player = PLAYER::PLAYER_PED_ID();

	ENTITY::SET_ENTITY_INVINCIBLE(player, InvinciblePlayer);

	if (PED::IS_PED_ON_MOUNT(player)) {
		Ped horse = PED::GET_MOUNT(player);
		if (ENTITY::DOES_ENTITY_EXIST(horse)) {
			ENTITY::SET_ENTITY_INVINCIBLE(horse, InvincibleHorse);
		}
	}

	// Instant kill: any ped freshly damaged by the player dies outright
	if (InstantKill) {
		const int ARR_SIZE = 1024;
		int peds[ARR_SIZE];
		int found = worldGetAllPeds(peds, ARR_SIZE);
		for (int i = 0; i < found; i++) {
			Ped ped = peds[i];
			if (ped == player || !ENTITY::DOES_ENTITY_EXIST(ped)) continue;
			if (ENTITY::IS_ENTITY_DEAD(ped)) continue;
			if (ENTITY::HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY(ped, player, true, true)) {
				ENTITY::SET_ENTITY_HEALTH(ped, 0, player);
			}
		}
	}
}
