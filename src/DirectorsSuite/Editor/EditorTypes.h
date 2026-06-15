// Director's Suite - Core types and shared data tables
//
// RDR2 has no Rockstar Editor, so everything here is rebuilt from scripted
// cameras + Photo Mode natives. The data tables below (filters, shakes,
// weather, bones) were extracted from the official decompiled R* scripts
// (camera_photomode.c and friends) so every name is valid in-game.

#pragma once
#include <string>
#include <vector>
#include "..\..\..\inc\types.h"

constexpr int MAX_CAMERAS = 300;

// Camera behaviour flags. These are combinable, like the GTA V Rockstar
// Editor camera types they emulate:
//   Follow     - camera position tracks the target ped, keeping its offset
//   LookAt     - camera rotation always points at the target ped
//   SameAngle  - camera keeps its original rotation while moving with the target
//   FreeAngle  - camera angle is freely orbitable around the target (player cam)
enum eCamModeFlags : unsigned
{
	CAMMODE_STATIC     = 0,
	CAMMODE_FOLLOW     = 1 << 0,
	CAMMODE_LOOKAT     = 1 << 1,
	CAMMODE_SAME_ANGLE = 1 << 2,
	CAMMODE_FREE_ANGLE = 1 << 3,
};

// Maps to easeLocation/easeRotation of SET_CAM_ACTIVE_WITH_INTERP
enum eCamEase : int
{
	EASE_LINEAR     = 0,
	EASE_IN         = 1,
	EASE_OUT        = 2,
	EASE_IN_AND_OUT = 3,
};

enum eAimMode : int
{
	AIM_MODE_OFF = 0,
	AIM_MODE_PLAYER_HEADING,     // aim straight ahead of the player model
	AIM_MODE_CAMERA_DIRECTION_1, // native SET_CAM_AFFECTS_AIMING on the editor camera
	AIM_MODE_CAMERA_DIRECTION_2, // scripted TASK_AIM_GUN_AT_COORD along the camera forward vector
	AIM_MODE_GAMEPLAY_CAM,       // aim along the gameplay camera
	AIM_MODE_COUNT,
};

enum eAimAssistTargets : unsigned
{
	AA_TARGET_HUMANS_COMBAT  = 1 << 0, // humans currently in combat with the player
	AA_TARGET_HUMANS_ALL     = 1 << 1, // any human
	AA_TARGET_ANIMALS        = 1 << 2, // any animal
	AA_TARGET_MODELS         = 1 << 3, // models listed in the INI
	AA_TARGET_SPECIFIC_PED   = 1 << 4, // a single hand-picked ped
};

enum eAnimalPriority : int
{
	ANIMAL_PRIORITY_NONE = 0,
	ANIMAL_PRIORITY_ALIVE,
	ANIMAL_PRIORITY_DEAD,
};

enum eCutsceneUnlock : int
{
	CUTSCENE_UNLOCK_OFF = 0,
	CUTSCENE_UNLOCK_AUTO,   // keep editor cameras rendering automatically during anim scenes
	CUTSCENE_UNLOCK_MANUAL, // user keeps control; nothing is forced
};

// A single editor camera. We never hold 300 engine cam handles - the params
// are stored here and baked into one of two pooled script cams on activation.
struct EditorCamera
{
	int  id = 0;
	std::string name = "CAM 1";
	bool enabled = true;

	// World placement (rotationOrder 2, degrees)
	Vector3 pos{};
	Vector3 rot{};
	float fov = 50.0f;            // "zoom" - lower fov = more zoom

	// Behaviour
	unsigned modeFlags = CAMMODE_STATIC;
	int  targetPed = 0;           // script handle; 0 = player
	bool targetIsPlayer = true;
	Vector3 followOffset{};       // world-space offset captured on creation/edit
	bool followRelativeToHeading = true; // rotate the offset with the target's heading
	float orbitDistance = 4.5f;   // FreeAngle orbit radius
	float orbitHeading = 0.0f;    // FreeAngle current yaw around target
	float orbitPitch = -10.0f;    // FreeAngle current pitch

	// Timing
	int durationMs = 5000;        // how long this camera holds during auto-switching
	int transitionMs = 1500;      // interpolation time when switching TO this camera (0 = cut)
	int easeLocation = EASE_IN_AND_OUT; // smoothness
	int easeRotation = EASE_IN_AND_OUT;

	// Effects
	int   shakeIndex = 0;         // index into CamShakeNames, 0 = none
	float shakeAmplitude = 0.25f;
	int   handheldStyle = 0;      // index into HandheldStyleNames, 0 = off (organic operator motion)
	float handheldIntensity = 0.6f; // 0..1 master scale for the handheld motion
	float motionBlur = 0.0f;      // 0..1, SET_CAM_MOTION_BLUR_STRENGTH
	float blurStrength = 0.0f;    // 0..1, SET_HIDOF_OVERRIDE blend (stronger at night!)
	float focusDistance = -1.0f;  // meters, <0 = autofocus off (_SET_CAM_FOCUS_DISTANCE)
	bool  focusPaused = false;    // _PAUSE_CAMERA_FOCUS

	// Photo Mode filter (ANIMPOSTFX), index into PhotoModeFilters, 0 = none
	int filterIndex = 0;

	// Per-camera world overrides (applied when the camera becomes active)
	int weatherIndex = 0;         // index into WeatherTypeNames, 0 = "No Change"
	int todHour = -1;             // -1 = no time-of-day override
	int todMinute = 0;
};

// A reusable property bundle that can be applied to new or existing cameras.
struct CameraPreset
{
	std::string name = "Preset";
	float fov = 50.0f;
	unsigned modeFlags = CAMMODE_STATIC;
	int durationMs = 5000;
	int transitionMs = 1500;
	int easeLocation = EASE_IN_AND_OUT;
	int easeRotation = EASE_IN_AND_OUT;
	int shakeIndex = 0;
	float shakeAmplitude = 0.25f;
	int   handheldStyle = 0;
	float handheldIntensity = 0.6f;
	float motionBlur = 0.0f;
	float blurStrength = 0.0f;
	float focusDistance = -1.0f;
	int filterIndex = 0;
};

// ---------------------------------------------------------------------------
// Data tables (verified against decompiled RDR2 scripts)
// ---------------------------------------------------------------------------

// Photo Mode filters are ANIMPOSTFX effects (see camera_photomode.c func_24)
inline const std::vector<const char*> PhotoModeFilters = {
	"None",
	"PhotoMode_FilterVintage01", "PhotoMode_FilterVintage02", "PhotoMode_FilterVintage03",
	"PhotoMode_FilterVintage04", "PhotoMode_FilterVintage05", "PhotoMode_FilterVintage06",
	"PhotoMode_FilterVintage07", "PhotoMode_FilterVintage08", "PhotoMode_FilterVintage09",
	"PhotoMode_FilterVintage10",
	"PhotoMode_FilterModern01", "PhotoMode_FilterModern02", "PhotoMode_FilterModern03",
	"PhotoMode_FilterModern04", "PhotoMode_FilterModern05", "PhotoMode_FilterModern06",
	"PhotoMode_FilterModern07", "PhotoMode_FilterModern08", "PhotoMode_FilterModern09",
	"PhotoMode_FilterModern10",
	"PhotoMode_FilterGame01", "PhotoMode_FilterGame02", "PhotoMode_FilterGame03",
	"PhotoMode_FilterGame04", "PhotoMode_FilterGame05", "PhotoMode_FilterGame06",
	"PhotoMode_FilterGame07", "PhotoMode_FilterGame08", "PhotoMode_FilterGame09",
	"PhotoMode_FilterGame10", "PhotoMode_FilterGame11", "PhotoMode_FilterGame12",
	"PhotoMode_FilterGame13", "PhotoMode_FilterGame14", "PhotoMode_FilterGame15",
	"PhotoMode_FilterGame16", "PhotoMode_FilterGame17", "PhotoMode_FilterGame18",
};

// Handheld / phone-operator motion styles (see HandheldMotion.cpp). Distinct
// from the jittery SHAKE_CAM presets below: this is slow organic sway/roll/
// breathing, like someone holding a phone while filming.
inline const std::vector<const char*> HandheldStyleNames = {
	"Off", "Phone (Steady)", "Phone (Vlog)", "Handheld Walk", "Run & Gun",
};

// Camera shake types used by R* mission scripts
inline const std::vector<const char*> CamShakeNames = {
	"None",
	"HAND_SHAKE",
	"SMALL_EXPLOSION_SHAKE",
	"MEDIUM_EXPLOSION_SHAKE",
	"LARGE_EXPLOSION_SHAKE",
	"KILL_SHOT_SHAKE",
	"JOLT_SHAKE",
	"VIBRATE_SHAKE",
	"ROAD_VIBRATION_SHAKE",
	"DRUNK_SHAKE",
	"SKY_DIVING_SHAKE",
	"FAMILY5_DRUG_TRIP_SHAKE",
	"DEATH_FAIL_IN_EFFECT_SHAKE",
	"WOBBLY_SHAKE",
};

// Weather types (MISC::SET_WEATHER_TYPE via GET_HASH_KEY). Index 0 = no per-cam change.
inline const std::vector<const char*> WeatherTypeNames = {
	"No Change",
	"HIGHPRESSURE", "RAIN", "SNOW", "MISTY", "FOG", "SUNNY", "CLOUDS", "OVERCAST",
	"THUNDERSTORM", "HURRICANE", "THUNDER", "SHOWER", "BLIZZARD", "SNOWLIGHT",
	"WHITEOUT", "HAIL", "SLEET", "DRIZZLE", "SANDSTORM", "OVERCASTDARK", "GROUNDBLIZZARD",
};

// Aim assist body parts -> RDR2 skeleton bone names (joaat is case-insensitive)
struct BodyPartDef { const char* label; const char* bone; };
inline const std::vector<BodyPartDef> AimBodyParts = {
	{ "Head",          "SKEL_HEAD"        },
	{ "Neck",          "SKEL_NECK0"       },
	{ "Chest",         "SKEL_SPINE3"      },
	{ "Pelvis",        "SKEL_PELVIS"      },
	{ "Left Arm",      "SKEL_L_UPPERARM"  },
	{ "Right Arm",     "SKEL_R_UPPERARM"  },
	{ "Left Forearm",  "SKEL_L_FOREARM"   },
	{ "Right Forearm", "SKEL_R_FOREARM"   },
	{ "Left Hand",     "SKEL_L_HAND"      },
	{ "Right Hand",    "SKEL_R_HAND"      },
	{ "Left Thigh",    "SKEL_L_THIGH"     },
	{ "Right Thigh",   "SKEL_R_THIGH"     },
	{ "Left Leg",      "SKEL_L_CALF"      },
	{ "Right Leg",     "SKEL_R_CALF"      },
	{ "Left Foot",     "SKEL_L_FOOT"      },
	{ "Right Foot",    "SKEL_R_FOOT"      },
};

struct SnapStrengthDef { const char* label; float lerpPerFrame; };
inline const std::vector<SnapStrengthDef> AimSnapStrengths = {
	{ "Soft",       0.06f },
	{ "Medium",     0.14f },
	{ "Hard",       0.30f },
	{ "Ultra Hard", 0.65f },
};

inline const std::vector<const char*> AimModeNames = {
	"Off", "Player Heading", "Camera Direction I", "Camera Direction II", "Gameplay Camera",
};

inline const std::vector<const char*> EaseNames = {
	"Linear", "Ease In", "Ease Out", "Ease In & Out",
};

inline const std::vector<const char*> CutsceneUnlockNames = {
	"Off", "Auto", "Manual",
};
