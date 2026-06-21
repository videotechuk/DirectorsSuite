// Director's Suite - INI configuration (DirectorsSuite.ini next to the game executable)

#pragma once
#include <windows.h>
#include <string>
#include <vector>

class CConfig
{
public:
	// [Keys] - virtual key codes
	int KeyOpenMenu = VK_F2;
	int KeyCameraAutoSwitchingStartStop = 0; // unassigned by default; set in INI
	int KeyAimAssist = 'C';                  // hold while aiming
	int KeyAddCamera = VK_INSERT;            // add camera at current view
	int KeyPlayerCamToggle = 0;
	int KeyNextCamera = 'N';                 // switch to the next enabled camera
	int KeyPrevCamera = 'B';                 // switch to the previous enabled camera
	int KeyPhotoMode = VK_F1;                // toggle the Photo Mode editor
	int KeyScreenshot = VK_F3;               // lossless / HDR screenshot

	// [FreeCam]
	float FreeCamSpeed = 0.35f;
	float FreeCamFastMultiplier = 4.0f;
	float FreeCamSlowMultiplier = 0.2f;
	float FreeCamMouseSensitivity = 4.0f;
	bool  FreeCamHighDetail = true;          // stream world around the free cam

	// [Tips] - bitmask of Photo Mode tabs whose first-time tip has been shown
	int   TipsSeenMask = 0;
	// One-time welcome message shown the first time the player loads in-game
	bool  WelcomeShown = false;

	// [Cameras]
	std::string NamingTemplate = "CAM {index}"; // {index} {total} {hour} {minute}
	int  DefaultDurationMs = 5000;
	int  DefaultTransitionMs = 1500;
	bool SmoothCameraPath = false; // Catmull-Rom spline transitions (flowing dolly/crane move through the shot list)

	// [Clouds] - precision settings
	float CloudSpeedX = 0.0005f;
	float CloudSpeedY = 0.0002f;
	float CloudHeightStep = 5.0f;
	float CloudNoiseX = 0.0f;
	float CloudNoiseY = 0.0f;
	float CloudNoiseZ = 0.0f;

	// [AimAssist]
	float AimAssistMaxDistance = 120.0f;
	float AimAssistConeDegrees = 25.0f;
	std::vector<std::string> AimAssistModels; // comma separated model names in INI

	// [Director]
	std::vector<std::string> DirectorCustomPeds; // extra ped models for Director Mode

	// [Interface]
	float LetterboxRatio = 0.12f;            // bar height as fraction of screen height
	int   GridRows = 3;
	int   GridColumns = 3;


	// [OBS] - automatic recording over the obs-websocket 5.x protocol
	bool        OBSEnabled = false;          // master switch for OBS integration
	bool        OBSAutoRecord = true;        // record automatically during project playback
	std::string OBSHost = "127.0.0.1";
	int         OBSPort = 4455;
	std::string OBSPassword = "";            // obs-websocket server password (blank if disabled)
	bool        OBSHideHudDuringPlayback = true; // clean screen while the project records

	void Load();
	void Save(); // writes the default-commented file if missing

	const std::string& Path() const { return m_iniPath; }

private:
	std::string m_iniPath;

	int   ReadInt(const char* section, const char* key, int def);
	float ReadFloat(const char* section, const char* key, float def);
	bool  ReadBool(const char* section, const char* key, bool def);
	std::string ReadString(const char* section, const char* key, const char* def);
	void  WriteInt(const char* section, const char* key, int value);
	void  WriteFloat(const char* section, const char* key, float value);
	void  WriteString(const char* section, const char* key, const char* value);
};

inline CConfig g_Config;
