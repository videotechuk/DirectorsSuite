// Director's Suite - time & world control: freeze game (Photo Mode style), time
// scale, freeze time of day, weather, and advanced cloud controls.

#pragma once

class CWorldControl
{
public:
	// Freeze game - uses the Photo Mode freeze natives so animations, physics
	// and particles halt exactly like the built-in Photo Mode.
	bool FreezeGame = false;
	void SetFreezeGame(bool freeze);

	// Time scale (1.0 = normal)
	float TimeScale = 1.0f;
	void ApplyTimeScale();

	// Time of day
	bool FreezeTimeOfDay = false;
	void SetFreezeTimeOfDay(bool freeze);

	// Weather
	int CurrentWeatherIndex = 0; // index into WeatherTypeNames (0 = no change)
	bool WeatherFrozen = false;
	void SetWeather(int index);
	void SetWeatherFrozen(bool frozen);

	// Clouds (precision step values come from the INI [Clouds] section)
	bool  FreezeClouds = false;
	bool  MoveClouds = false;
	float CloudPosX = 0.0f;
	float CloudPosY = 0.0f;
	float CloudHeight = 500.0f;
	bool  CloudHeightOverride = false;

	void Tick(); // per-frame upkeep (cloud movement/freeze, ToD freeze)

private:
	bool m_cloudStateApplied = false;
};

inline CWorldControl g_World;
