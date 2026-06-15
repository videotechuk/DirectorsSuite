// Director's Suite - gameplay options: instant kill, invincible player/horse.

#pragma once

class CGameplay
{
public:
	bool InstantKill = false;
	bool InvinciblePlayer = false;
	bool InvincibleHorse = false;

	void Tick(); // must run every frame
};

inline CGameplay g_Gameplay;
