#include "Config.h"
#include <sstream>

static std::string GetIniPath(const char* fileName)
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH); // game exe directory
	std::string dir(path);
	size_t slash = dir.find_last_of("\\/");
	dir = (slash == std::string::npos) ? "." : dir.substr(0, slash);
	return dir + "\\" + fileName;
}

int CConfig::ReadInt(const char* section, const char* key, int def)
{
	return (int)GetPrivateProfileIntA(section, key, def, m_iniPath.c_str());
}

float CConfig::ReadFloat(const char* section, const char* key, float def)
{
	char buf[64];
	char defStr[64];
	sprintf_s(defStr, "%f", def);
	GetPrivateProfileStringA(section, key, defStr, buf, sizeof(buf), m_iniPath.c_str());
	return (float)atof(buf);
}

bool CConfig::ReadBool(const char* section, const char* key, bool def)
{
	return ReadInt(section, key, def ? 1 : 0) != 0;
}

std::string CConfig::ReadString(const char* section, const char* key, const char* def)
{
	char buf[512];
	GetPrivateProfileStringA(section, key, def, buf, sizeof(buf), m_iniPath.c_str());
	return std::string(buf);
}

void CConfig::WriteInt(const char* section, const char* key, int value)
{
	char buf[32];
	sprintf_s(buf, "%d", value);
	WritePrivateProfileStringA(section, key, buf, m_iniPath.c_str());
}

void CConfig::WriteFloat(const char* section, const char* key, float value)
{
	char buf[64];
	sprintf_s(buf, "%f", value);
	WritePrivateProfileStringA(section, key, buf, m_iniPath.c_str());
}

void CConfig::WriteString(const char* section, const char* key, const char* value)
{
	WritePrivateProfileStringA(section, key, value, m_iniPath.c_str());
}

void CConfig::Load()
{
	m_iniPath = GetIniPath("DirectorsSuite.ini");

	KeyOpenMenu                       = ReadInt("Keys", "KeyOpenMenu", KeyOpenMenu);
	KeyCameraAutoSwitchingStartStop   = ReadInt("Keys", "KeyCameraAutoSwitchingStartStop", KeyCameraAutoSwitchingStartStop);
	KeyAimAssist                      = ReadInt("Keys", "KeyAimAssist", KeyAimAssist);
	KeyAddCamera                      = ReadInt("Keys", "KeyAddCamera", KeyAddCamera);
	KeyPlayerCamToggle                = ReadInt("Keys", "KeyPlayerCamToggle", KeyPlayerCamToggle);
	KeyNextCamera                     = ReadInt("Keys", "KeyNextCamera", KeyNextCamera);
	KeyPrevCamera                     = ReadInt("Keys", "KeyPrevCamera", KeyPrevCamera);
	KeyPhotoMode                      = ReadInt("Keys", "KeyPhotoMode", KeyPhotoMode);
	KeyScreenshot                     = ReadInt("Keys", "KeyScreenshot", KeyScreenshot);

	FreeCamSpeed            = ReadFloat("FreeCam", "Speed", FreeCamSpeed);
	FreeCamFastMultiplier   = ReadFloat("FreeCam", "FastMultiplier", FreeCamFastMultiplier);
	FreeCamSlowMultiplier   = ReadFloat("FreeCam", "SlowMultiplier", FreeCamSlowMultiplier);
	FreeCamMouseSensitivity = ReadFloat("FreeCam", "MouseSensitivity", FreeCamMouseSensitivity);
	FreeCamHighDetail       = ReadBool("FreeCam", "HighDetail", FreeCamHighDetail);

	TipsSeenMask            = ReadInt("Tips", "SeenMask", TipsSeenMask);
	WelcomeShown            = ReadBool("Tips", "WelcomeShown", WelcomeShown);

	NamingTemplate      = ReadString("Cameras", "NamingTemplate", NamingTemplate.c_str());
	DefaultDurationMs   = ReadInt("Cameras", "DefaultDurationMs", DefaultDurationMs);
	DefaultTransitionMs = ReadInt("Cameras", "DefaultTransitionMs", DefaultTransitionMs);
	SmoothCameraPath    = ReadBool("Cameras", "SmoothPath", SmoothCameraPath);

	CloudSpeedX     = ReadFloat("Clouds", "SpeedX", CloudSpeedX);
	CloudSpeedY     = ReadFloat("Clouds", "SpeedY", CloudSpeedY);
	CloudHeightStep = ReadFloat("Clouds", "HeightStep", CloudHeightStep);
	CloudNoiseX     = ReadFloat("Clouds", "NoiseX", CloudNoiseX);
	CloudNoiseY     = ReadFloat("Clouds", "NoiseY", CloudNoiseY);
	CloudNoiseZ     = ReadFloat("Clouds", "NoiseZ", CloudNoiseZ);

	AimAssistMaxDistance = ReadFloat("AimAssist", "MaxDistance", AimAssistMaxDistance);
	AimAssistConeDegrees = ReadFloat("AimAssist", "ConeDegrees", AimAssistConeDegrees);
	AimAssistModels.clear();
	std::string models = ReadString("AimAssist", "Models", "");
	std::stringstream ss(models);
	std::string item;
	while (std::getline(ss, item, ',')) {
		// trim whitespace
		size_t a = item.find_first_not_of(" \t");
		size_t b = item.find_last_not_of(" \t");
		if (a != std::string::npos) {
			AimAssistModels.push_back(item.substr(a, b - a + 1));
		}
	}

	LetterboxRatio = ReadFloat("Interface", "LetterboxRatio", LetterboxRatio);
	GridRows       = ReadInt("Interface", "GridRows", GridRows);
	GridColumns    = ReadInt("Interface", "GridColumns", GridColumns);

	OBSEnabled               = ReadBool("OBS", "Enabled", OBSEnabled);
	OBSAutoRecord            = ReadBool("OBS", "AutoRecord", OBSAutoRecord);
	OBSHost                  = ReadString("OBS", "Host", OBSHost.c_str());
	OBSPort                  = ReadInt("OBS", "Port", OBSPort);
	OBSPassword              = ReadString("OBS", "Password", OBSPassword.c_str());
	OBSHideHudDuringPlayback = ReadBool("OBS", "HideHudDuringPlayback", OBSHideHudDuringPlayback);

	DirectorCustomPeds.clear();
	std::string customPeds = ReadString("Director", "CustomPeds", "");
	std::stringstream ssPeds(customPeds);
	std::string pedItem;
	while (std::getline(ssPeds, pedItem, ',')) {
		size_t a = pedItem.find_first_not_of(" \t");
		size_t b = pedItem.find_last_not_of(" \t");
		if (a != std::string::npos) {
			DirectorCustomPeds.push_back(pedItem.substr(a, b - a + 1));
		}
	}

	// First run: persist defaults so users can discover all settings
	if (GetFileAttributesA(m_iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
		Save();
	}
}

void CConfig::Save()
{
	WriteInt("Keys", "KeyOpenMenu", KeyOpenMenu);
	WriteInt("Keys", "KeyCameraAutoSwitchingStartStop", KeyCameraAutoSwitchingStartStop);
	WriteInt("Keys", "KeyAimAssist", KeyAimAssist);
	WriteInt("Keys", "KeyAddCamera", KeyAddCamera);
	WriteInt("Keys", "KeyPlayerCamToggle", KeyPlayerCamToggle);
	WriteInt("Keys", "KeyNextCamera", KeyNextCamera);
	WriteInt("Keys", "KeyPrevCamera", KeyPrevCamera);
	WriteInt("Keys", "KeyPhotoMode", KeyPhotoMode);
	WriteInt("Keys", "KeyScreenshot", KeyScreenshot);

	WriteFloat("FreeCam", "Speed", FreeCamSpeed);
	WriteFloat("FreeCam", "FastMultiplier", FreeCamFastMultiplier);
	WriteFloat("FreeCam", "SlowMultiplier", FreeCamSlowMultiplier);
	WriteFloat("FreeCam", "MouseSensitivity", FreeCamMouseSensitivity);
	WriteInt("FreeCam", "HighDetail", FreeCamHighDetail ? 1 : 0);

	WriteInt("Tips", "SeenMask", TipsSeenMask);
	WriteInt("Tips", "WelcomeShown", WelcomeShown ? 1 : 0);

	WriteString("Cameras", "NamingTemplate", NamingTemplate.c_str());
	WriteInt("Cameras", "DefaultDurationMs", DefaultDurationMs);
	WriteInt("Cameras", "DefaultTransitionMs", DefaultTransitionMs);
	WriteInt("Cameras", "SmoothPath", SmoothCameraPath ? 1 : 0);

	WriteFloat("Clouds", "SpeedX", CloudSpeedX);
	WriteFloat("Clouds", "SpeedY", CloudSpeedY);
	WriteFloat("Clouds", "HeightStep", CloudHeightStep);
	WriteFloat("Clouds", "NoiseX", CloudNoiseX);
	WriteFloat("Clouds", "NoiseY", CloudNoiseY);
	WriteFloat("Clouds", "NoiseZ", CloudNoiseZ);

	WriteFloat("AimAssist", "MaxDistance", AimAssistMaxDistance);
	WriteFloat("AimAssist", "ConeDegrees", AimAssistConeDegrees);
	std::string models;
	for (size_t i = 0; i < AimAssistModels.size(); i++) {
		if (i) models += ",";
		models += AimAssistModels[i];
	}
	WriteString("AimAssist", "Models", models.c_str());

	WriteFloat("Interface", "LetterboxRatio", LetterboxRatio);
	WriteInt("Interface", "GridRows", GridRows);
	WriteInt("Interface", "GridColumns", GridColumns);

	WriteInt("OBS", "Enabled", OBSEnabled ? 1 : 0);
	WriteInt("OBS", "AutoRecord", OBSAutoRecord ? 1 : 0);
	WriteString("OBS", "Host", OBSHost.c_str());
	WriteInt("OBS", "Port", OBSPort);
	WriteString("OBS", "Password", OBSPassword.c_str());
	WriteInt("OBS", "HideHudDuringPlayback", OBSHideHudDuringPlayback ? 1 : 0);

	std::string customPeds;
	for (size_t i = 0; i < DirectorCustomPeds.size(); i++) {
		if (i) customPeds += ",";
		customPeds += DirectorCustomPeds[i];
	}
	WriteString("Director", "CustomPeds", customPeds.c_str());
}
