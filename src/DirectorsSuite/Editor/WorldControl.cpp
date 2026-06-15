#include "WorldControl.h"
#include "EditorTypes.h"
#include "Config.h"
#include "..\script.h"

void CWorldControl::SetFreezeGame(bool freeze)
{
	FreezeGame = freeze;
	if (freeze) {
		ANIMSCENE::_REQUEST_PHOTO_MODE_FREEZE();
	}
	else {
		ANIMSCENE::_REQUEST_PHOTO_MODE_DEFREEZE();
	}
}

void CWorldControl::ApplyTimeScale()
{
	MISC::SET_TIME_SCALE(TimeScale);
}

void CWorldControl::SetFreezeTimeOfDay(bool freeze)
{
	FreezeTimeOfDay = freeze;
	CLOCK::PAUSE_CLOCK(freeze, 0);
}

void CWorldControl::SetWeather(int index)
{
	CurrentWeatherIndex = index;
	if (index > 0 && index < (int)WeatherTypeNames.size()) {
		MISC::SET_WEATHER_TYPE(MISC::GET_HASH_KEY(WeatherTypeNames[index]), true, true, true, 3.0f, false);
	}
}

void CWorldControl::SetWeatherFrozen(bool frozen)
{
	WeatherFrozen = frozen;
	MISC::_SET_WEATHER_TYPE_FROZEN(frozen);
}

void CWorldControl::Tick()
{
	// Clouds: the engine has no cloud position getter, so we own the position
	// once freeze or move is enabled and re-apply it every frame.
	if (FreezeClouds || MoveClouds) {
		if (MoveClouds) {
			CloudPosX += g_Config.CloudSpeedX;
			CloudPosY += g_Config.CloudSpeedY;
		}
		GRAPHICS::_SET_CLOUD_POSITION(CloudPosX, CloudPosY, 0.0f);
		if (g_Config.CloudNoiseX != 0.0f || g_Config.CloudNoiseY != 0.0f || g_Config.CloudNoiseZ != 0.0f) {
			GRAPHICS::_SET_CLOUD_NOISE(g_Config.CloudNoiseX, g_Config.CloudNoiseY, g_Config.CloudNoiseZ);
		}
		m_cloudStateApplied = true;
	}

	if (CloudHeightOverride) {
		GRAPHICS::_SET_CLOUD_HEIGHT(CloudHeight);
	}
}
