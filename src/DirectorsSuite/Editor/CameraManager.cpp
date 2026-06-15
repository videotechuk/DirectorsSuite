#include "CameraManager.h"
#include "Config.h"
#include "..\script.h"

static std::string ReplaceAll(std::string str, const std::string& from, const std::string& to)
{
	size_t pos = 0;
	while ((pos = str.find(from, pos)) != std::string::npos) {
		str.replace(pos, from.length(), to);
		pos += to.length();
	}
	return str;
}

std::string CCameraManager::CamerasFilePath() const
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	std::string dir(path);
	size_t slash = dir.find_last_of("\\/");
	dir = (slash == std::string::npos) ? "." : dir.substr(0, slash);
	return dir + "\\DirectorsSuite_Cameras.ini";
}

std::string CCameraManager::PreviewName(const std::string& tpl, int index) const
{
	int total = (int)Cameras.size();
	if (total < index + 1) total = index + 1;

	std::string name = tpl;
	name = ReplaceAll(name, "{index}", std::to_string(index + 1));
	name = ReplaceAll(name, "{total}", std::to_string(total));
	name = ReplaceAll(name, "{hour}", std::to_string(CLOCK::GET_CLOCK_HOURS()));
	name = ReplaceAll(name, "{minute}", std::to_string(CLOCK::GET_CLOCK_MINUTES()));
	return name;
}

std::string CCameraManager::BuildName(int index) const
{
	return PreviewName(g_Config.NamingTemplate, index);
}

void CCameraManager::RenameAll()
{
	for (int i = 0; i < (int)Cameras.size(); i++) {
		Cameras[i].name = BuildName(i);
	}
}

int CCameraManager::InsertCamera()
{
	if ((int)Cameras.size() >= MAX_CAMERAS) {
		return -1;
	}

	EditorCamera cam;
	cam.id = m_nextId++;
	cam.pos = CAM::GET_FINAL_RENDERED_CAM_COORD();
	cam.rot = CAM::GET_FINAL_RENDERED_CAM_ROT(2);
	cam.fov = CAM::GET_FINAL_RENDERED_CAM_FOV();
	cam.durationMs = g_Config.DefaultDurationMs;
	cam.transitionMs = g_Config.DefaultTransitionMs;

	// Capture the offset to the player so Follow/SameAngle modes work immediately
	Ped player = PLAYER::PLAYER_PED_ID();
	Vector3 playerPos = ENTITY::GET_ENTITY_COORDS(player, true, true);
	cam.followOffset.x = cam.pos.x - playerPos.x;
	cam.followOffset.y = cam.pos.y - playerPos.y;
	cam.followOffset.z = cam.pos.z - playerPos.z;

	if (NewCameraPresetIndex > 0 && NewCameraPresetIndex <= (int)Presets.size()) {
		ApplyPreset(Presets[NewCameraPresetIndex - 1], cam);
	}

	Cameras.push_back(cam);
	Cameras.back().name = BuildName((int)Cameras.size() - 1);
	return (int)Cameras.size() - 1;
}

void CCameraManager::DeleteCamera(int index)
{
	if (index < 0 || index >= (int)Cameras.size()) return;
	Cameras.erase(Cameras.begin() + index);
	if (SelectedIndex >= (int)Cameras.size()) {
		SelectedIndex = (int)Cameras.size() - 1;
	}
	// Keep the automatic numbering consistent after removals so the list
	// never shows gaps like CAM 1, CAM 3
	RenameAll();
}

void CCameraManager::DeleteAllCameras()
{
	Cameras.clear();
	SelectedIndex = -1;
}

int CCameraManager::EnabledCount() const
{
	int n = 0;
	for (const auto& c : Cameras) {
		if (c.enabled) n++;
	}
	return n;
}

void CCameraManager::UpdateCameraFromCurrentView(int index)
{
	EditorCamera* cam = Get(index);
	if (!cam) return;
	cam->pos = CAM::GET_FINAL_RENDERED_CAM_COORD();
	cam->rot = CAM::GET_FINAL_RENDERED_CAM_ROT(2);
	cam->fov = CAM::GET_FINAL_RENDERED_CAM_FOV();

	Ped player = PLAYER::PLAYER_PED_ID();
	Vector3 playerPos = ENTITY::GET_ENTITY_COORDS(player, true, true);
	cam->followOffset.x = cam->pos.x - playerPos.x;
	cam->followOffset.y = cam->pos.y - playerPos.y;
	cam->followOffset.z = cam->pos.z - playerPos.z;
}

void CCameraManager::ApplyPreset(const CameraPreset& preset, EditorCamera& cam)
{
	cam.fov = preset.fov;
	cam.modeFlags = preset.modeFlags;
	cam.durationMs = preset.durationMs;
	cam.transitionMs = preset.transitionMs;
	cam.easeLocation = preset.easeLocation;
	cam.easeRotation = preset.easeRotation;
	cam.shakeIndex = preset.shakeIndex;
	cam.shakeAmplitude = preset.shakeAmplitude;
	cam.handheldStyle = preset.handheldStyle;
	cam.handheldIntensity = preset.handheldIntensity;
	cam.motionBlur = preset.motionBlur;
	cam.blurStrength = preset.blurStrength;
	cam.focusDistance = preset.focusDistance;
	cam.filterIndex = preset.filterIndex;
}

void CCameraManager::SavePresetFromCamera(const EditorCamera& cam, const std::string& name)
{
	CameraPreset preset;
	preset.name = name;
	preset.fov = cam.fov;
	preset.modeFlags = cam.modeFlags;
	preset.durationMs = cam.durationMs;
	preset.transitionMs = cam.transitionMs;
	preset.easeLocation = cam.easeLocation;
	preset.easeRotation = cam.easeRotation;
	preset.shakeIndex = cam.shakeIndex;
	preset.shakeAmplitude = cam.shakeAmplitude;
	preset.handheldStyle = cam.handheldStyle;
	preset.handheldIntensity = cam.handheldIntensity;
	preset.motionBlur = cam.motionBlur;
	preset.blurStrength = cam.blurStrength;
	preset.focusDistance = cam.focusDistance;
	preset.filterIndex = cam.filterIndex;
	Presets.push_back(preset);
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

static void WriteIniFloat(const std::string& file, const char* sec, const char* key, float v)
{
	char buf[64];
	sprintf_s(buf, "%f", v);
	WritePrivateProfileStringA(sec, key, buf, file.c_str());
}

static void WriteIniInt(const std::string& file, const char* sec, const char* key, int v)
{
	char buf[32];
	sprintf_s(buf, "%d", v);
	WritePrivateProfileStringA(sec, key, buf, file.c_str());
}

static float ReadIniFloat(const std::string& file, const char* sec, const char* key, float def)
{
	char buf[64], defStr[64];
	sprintf_s(defStr, "%f", def);
	GetPrivateProfileStringA(sec, key, defStr, buf, sizeof(buf), file.c_str());
	return (float)atof(buf);
}

static int ReadIniInt(const std::string& file, const char* sec, const char* key, int def)
{
	return (int)GetPrivateProfileIntA(sec, key, def, file.c_str());
}

void CCameraManager::SaveToFile()
{
	std::string file = CamerasFilePath();
	DeleteFileA(file.c_str()); // start fresh so removed cameras don't linger

	WriteIniInt(file, "General", "Count", (int)Cameras.size());
	WriteIniInt(file, "General", "PresetCount", (int)Presets.size());

	for (int i = 0; i < (int)Cameras.size(); i++) {
		const EditorCamera& c = Cameras[i];
		std::string sec = "Camera" + std::to_string(i);
		const char* s = sec.c_str();
		WritePrivateProfileStringA(s, "Name", c.name.c_str(), file.c_str());
		WriteIniInt(file, s, "Enabled", c.enabled ? 1 : 0);
		WriteIniFloat(file, s, "PosX", c.pos.x);
		WriteIniFloat(file, s, "PosY", c.pos.y);
		WriteIniFloat(file, s, "PosZ", c.pos.z);
		WriteIniFloat(file, s, "RotX", c.rot.x);
		WriteIniFloat(file, s, "RotY", c.rot.y);
		WriteIniFloat(file, s, "RotZ", c.rot.z);
		WriteIniFloat(file, s, "Fov", c.fov);
		WriteIniInt(file, s, "ModeFlags", (int)c.modeFlags);
		WriteIniFloat(file, s, "OffsetX", c.followOffset.x);
		WriteIniFloat(file, s, "OffsetY", c.followOffset.y);
		WriteIniFloat(file, s, "OffsetZ", c.followOffset.z);
		WriteIniInt(file, s, "OffsetRelative", c.followRelativeToHeading ? 1 : 0);
		WriteIniFloat(file, s, "OrbitDistance", c.orbitDistance);
		WriteIniFloat(file, s, "OrbitHeading", c.orbitHeading);
		WriteIniFloat(file, s, "OrbitPitch", c.orbitPitch);
		WriteIniInt(file, s, "DurationMs", c.durationMs);
		WriteIniInt(file, s, "TransitionMs", c.transitionMs);
		WriteIniInt(file, s, "EaseLocation", c.easeLocation);
		WriteIniInt(file, s, "EaseRotation", c.easeRotation);
		WriteIniInt(file, s, "ShakeIndex", c.shakeIndex);
		WriteIniFloat(file, s, "ShakeAmplitude", c.shakeAmplitude);
		WriteIniInt(file, s, "HandheldStyle", c.handheldStyle);
		WriteIniFloat(file, s, "HandheldIntensity", c.handheldIntensity);
		WriteIniFloat(file, s, "MotionBlur", c.motionBlur);
		WriteIniFloat(file, s, "BlurStrength", c.blurStrength);
		WriteIniFloat(file, s, "FocusDistance", c.focusDistance);
		WriteIniInt(file, s, "FocusPaused", c.focusPaused ? 1 : 0);
		WriteIniInt(file, s, "FilterIndex", c.filterIndex);
		WriteIniInt(file, s, "WeatherIndex", c.weatherIndex);
		WriteIniInt(file, s, "TodHour", c.todHour);
		WriteIniInt(file, s, "TodMinute", c.todMinute);
	}

	for (int i = 0; i < (int)Presets.size(); i++) {
		const CameraPreset& p = Presets[i];
		std::string sec = "Preset" + std::to_string(i);
		const char* s = sec.c_str();
		WritePrivateProfileStringA(s, "Name", p.name.c_str(), file.c_str());
		WriteIniFloat(file, s, "Fov", p.fov);
		WriteIniInt(file, s, "ModeFlags", (int)p.modeFlags);
		WriteIniInt(file, s, "DurationMs", p.durationMs);
		WriteIniInt(file, s, "TransitionMs", p.transitionMs);
		WriteIniInt(file, s, "EaseLocation", p.easeLocation);
		WriteIniInt(file, s, "EaseRotation", p.easeRotation);
		WriteIniInt(file, s, "ShakeIndex", p.shakeIndex);
		WriteIniFloat(file, s, "ShakeAmplitude", p.shakeAmplitude);
		WriteIniInt(file, s, "HandheldStyle", p.handheldStyle);
		WriteIniFloat(file, s, "HandheldIntensity", p.handheldIntensity);
		WriteIniFloat(file, s, "MotionBlur", p.motionBlur);
		WriteIniFloat(file, s, "BlurStrength", p.blurStrength);
		WriteIniFloat(file, s, "FocusDistance", p.focusDistance);
		WriteIniInt(file, s, "FilterIndex", p.filterIndex);
	}
}

void CCameraManager::LoadFromFile()
{
	std::string file = CamerasFilePath();
	int count = ReadIniInt(file, "General", "Count", 0);
	int presetCount = ReadIniInt(file, "General", "PresetCount", 0);

	Cameras.clear();
	for (int i = 0; i < count && i < MAX_CAMERAS; i++) {
		std::string sec = "Camera" + std::to_string(i);
		const char* s = sec.c_str();
		EditorCamera c;
		c.id = m_nextId++;
		char nameBuf[128];
		GetPrivateProfileStringA(s, "Name", ("CAM " + std::to_string(i + 1)).c_str(), nameBuf, sizeof(nameBuf), file.c_str());
		c.name = nameBuf;
		c.enabled = ReadIniInt(file, s, "Enabled", 1) != 0;
		c.pos.x = ReadIniFloat(file, s, "PosX", 0);
		c.pos.y = ReadIniFloat(file, s, "PosY", 0);
		c.pos.z = ReadIniFloat(file, s, "PosZ", 0);
		c.rot.x = ReadIniFloat(file, s, "RotX", 0);
		c.rot.y = ReadIniFloat(file, s, "RotY", 0);
		c.rot.z = ReadIniFloat(file, s, "RotZ", 0);
		c.fov = ReadIniFloat(file, s, "Fov", 50.0f);
		c.modeFlags = (unsigned)ReadIniInt(file, s, "ModeFlags", 0);
		c.followOffset.x = ReadIniFloat(file, s, "OffsetX", 0);
		c.followOffset.y = ReadIniFloat(file, s, "OffsetY", 0);
		c.followOffset.z = ReadIniFloat(file, s, "OffsetZ", 0);
		c.followRelativeToHeading = ReadIniInt(file, s, "OffsetRelative", 1) != 0;
		c.orbitDistance = ReadIniFloat(file, s, "OrbitDistance", 4.5f);
		c.orbitHeading = ReadIniFloat(file, s, "OrbitHeading", 0.0f);
		c.orbitPitch = ReadIniFloat(file, s, "OrbitPitch", -10.0f);
		c.durationMs = ReadIniInt(file, s, "DurationMs", 5000);
		c.transitionMs = ReadIniInt(file, s, "TransitionMs", 1500);
		c.easeLocation = ReadIniInt(file, s, "EaseLocation", EASE_IN_AND_OUT);
		c.easeRotation = ReadIniInt(file, s, "EaseRotation", EASE_IN_AND_OUT);
		c.shakeIndex = ReadIniInt(file, s, "ShakeIndex", 0);
		c.shakeAmplitude = ReadIniFloat(file, s, "ShakeAmplitude", 0.25f);
		c.handheldStyle = ReadIniInt(file, s, "HandheldStyle", 0);
		c.handheldIntensity = ReadIniFloat(file, s, "HandheldIntensity", 0.6f);
		c.motionBlur = ReadIniFloat(file, s, "MotionBlur", 0);
		c.blurStrength = ReadIniFloat(file, s, "BlurStrength", 0);
		c.focusDistance = ReadIniFloat(file, s, "FocusDistance", -1.0f);
		c.focusPaused = ReadIniInt(file, s, "FocusPaused", 0) != 0;
		c.filterIndex = ReadIniInt(file, s, "FilterIndex", 0);
		c.weatherIndex = ReadIniInt(file, s, "WeatherIndex", 0);
		c.todHour = ReadIniInt(file, s, "TodHour", -1);
		c.todMinute = ReadIniInt(file, s, "TodMinute", 0);
		Cameras.push_back(c);
	}

	Presets.clear();
	for (int i = 0; i < presetCount; i++) {
		std::string sec = "Preset" + std::to_string(i);
		const char* s = sec.c_str();
		CameraPreset p;
		char nameBuf[128];
		GetPrivateProfileStringA(s, "Name", ("Preset " + std::to_string(i + 1)).c_str(), nameBuf, sizeof(nameBuf), file.c_str());
		p.name = nameBuf;
		p.fov = ReadIniFloat(file, s, "Fov", 50.0f);
		p.modeFlags = (unsigned)ReadIniInt(file, s, "ModeFlags", 0);
		p.durationMs = ReadIniInt(file, s, "DurationMs", 5000);
		p.transitionMs = ReadIniInt(file, s, "TransitionMs", 1500);
		p.easeLocation = ReadIniInt(file, s, "EaseLocation", EASE_IN_AND_OUT);
		p.easeRotation = ReadIniInt(file, s, "EaseRotation", EASE_IN_AND_OUT);
		p.shakeIndex = ReadIniInt(file, s, "ShakeIndex", 0);
		p.shakeAmplitude = ReadIniFloat(file, s, "ShakeAmplitude", 0.25f);
		p.handheldStyle = ReadIniInt(file, s, "HandheldStyle", 0);
		p.handheldIntensity = ReadIniFloat(file, s, "HandheldIntensity", 0.6f);
		p.motionBlur = ReadIniFloat(file, s, "MotionBlur", 0);
		p.blurStrength = ReadIniFloat(file, s, "BlurStrength", 0);
		p.focusDistance = ReadIniFloat(file, s, "FocusDistance", -1.0f);
		p.filterIndex = ReadIniInt(file, s, "FilterIndex", 0);
		Presets.push_back(p);
	}
}
