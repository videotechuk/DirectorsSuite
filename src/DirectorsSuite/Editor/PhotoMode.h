// Director's Suite - Photo Mode: a full cinematic capture suite that
// replaces RDR2's limited native Photo Mode.
//
// UI: ported onto the RDR2 Native Menu Base (the same submenu/option system the
// editor menus use). The old hand-drawn DRAW_RECT side panel overran the
// engine's per-frame script-draw budget; the native menu draws through
// scaleform + sprites and is budget-safe. Photo Mode hosts its own page tree
// (Photo Mode -> Camera / World / Character / Lighting / Post / Effects /
// Music) and, because it flies a free camera at the same time, it OWNS input:
// it disables all game controls and drives the menu with raw keys while the
// camera reads the disabled-control analog values. Only world-space overlays
// (aspect crop bars, light gizmos, composition grid, subject marker) are still
// drawn directly - those are cheap and part of the shot.
//
// Engine notes (honest limits):
//  - scripted lights are point lights (DRAW_LIGHT_WITH_RANGE); RDR2 exposes
//    no scripted spot/directional lights and no per-light shadow toggle
//  - one timecycle modifier slot exists, so the Color Grade choice includes
//    film grain / vignette / lens distortion looks; ANIMPOSTFX filters and
//    camera DOF stack on top of it independently
//  - the world freezes via the actual Photo Mode freeze natives; Frame
//    Forward pulses a defreeze for a couple of frames for precise timing

#pragma once
#include <string>
#include <vector>
#include <functional>
#include "..\..\..\inc\types.h"
#include "DirectorTypes.h"   // HeroLightSetup (Rockstar light rigs)

// A free-placed scene light (point light, drawn every frame)
struct PMLight
{
	bool  used = false;
	Vector3 pos{};
	float intensity = 4.0f;
	float range = 8.0f;
	float tempK = 5600.0f;        // colour temperature; drives r/g/b
	int   r = 255, g = 244, b = 229;
	bool  attached = false;       // follows an entity
	Entity attachEntity = 0;
	Vector3 attachOffset{};       // offset from the entity (heading-relative)

	// Physical bulb prop (p_lightbulb01x) that stands in for the old screen-space
	// gizmo: it marks the light in-world AND, via the engine's entity-light
	// system, glows as a real visible source. 0 = not yet created.
	Object bulb = 0;
};

constexpr int PM_MAX_LIGHTS = 8;
constexpr int PM_MAX_LAMPS = 4;

// A placed "Improved Artificial Lighting V2" prop (custom LML model). Each is a
// real world object with its own baked light, plus a full position/rotation
// editor in the menu.
struct PMArtLight
{
	bool    used = false;
	Object  obj = 0;
	int     modelIdx = 0;
	Vector3 pos{};
	Vector3 rot{};             // x pitch, y roll, z heading (degrees)
	float   intensity = 12.0f; // forced on the entity light each frame
	int     r = 255, g = 255, b = 255; // colour forced on the entity light + gizmo tint
};
constexpr int PM_MAX_ARTLIGHTS = 8;

class CPhotoMode
{
public:
	void Toggle();
	void Activate();
	void Deactivate();
	bool IsActive() const { return m_active; }

	// Target width:height for the next screenshot so the saved file is cropped to
	// the chosen aspect frame. 0 when Photo Mode is closed or the frame is "Off".
	float CurrentCropAspect() const;

	void Tick(); // input + camera + world upkeep + UI; every frame

private:
	// --- mode state ---
	bool m_active = false;
	bool m_uiHidden = false;     // H: hide the menu + overlays for a clean shot
	DWORD m_repeatTimer = 0;     // slider hold-to-repeat timing
	DWORD m_holdStart = 0;

	// Native control prompts (bottom-right, like the Creator Suite menu) shown
	// while Photo Mode is open: navigation + the free-cam fly controls. The
	// menu base already shows Select / Back, so we only add the rest.
	Prompt m_promptNav = 0, m_promptAdjust = 0, m_promptMove = 0, m_promptLook = 0, m_promptFast = 0;
	bool m_promptsRegistered = false;
	void RegisterPrompts();
	void UpdatePrompts(bool show);

	// Experimental tiled super-resolution capture (test): zoom + pan the camera
	// across an NxN grid, grab each tile and stitch into one large PNG.
	int  m_superResFactor = 2;   // 2 = 2x2 ... output = factor x screen size
	void CaptureSuperRes();

	// --- camera ---
	Cam m_cam = 0;
	Vector3 m_pos{};
	Vector3 m_rot{};   // x pitch, y roll, z yaw
	float m_fov = 50.0f;
	bool  m_orbit = false;
	float m_orbitDist = 4.0f;
	Ped   m_target = 0;          // selected character (0 = player)
	bool  m_trackTarget = false; // lock-on: keep pointing at the target

	// --- world ---
	bool m_frozen = false;
	int  m_freezeMethod = 0;     // 0 = timescale (aspect-safe), 1 = photo mode native
	int  m_stepFrames = 0;       // frame-forward pulse counter
	int  m_weatherIdx = 0;
	bool m_hideHud = true;
	bool m_cutsceneUnlock = false; // free the camera + controls during cutscenes
	bool m_invincible = true;      // keep the player unkillable while editing

	// Weather snapshot so "No Change" truly restores the scene's weather
	// instead of leaving the last forced type applied.
	Hash  m_savedWeather1 = 0, m_savedWeather2 = 0;
	float m_savedWeatherPct = 0.0f;
	bool  m_weatherSaved = false;

	// Sun control. RDR2 exposes no sun-direction native (verified against the
	// full native DB and the RedM source); the engine computes the sun from
	// clock time (position along the arc) and the calendar date (the arc's
	// height/declination - winter = low raking light, summer = high). These
	// two give real, native control. The original date is restored on exit.
	int  m_savedDay = -1, m_savedMonth = -1, m_savedYear = -1;
	float m_moonOverride = 0.0f; // ENABLE_MOON_CYCLE_OVERRIDE strength

	// Timecycle runtime access (ported RedM editor internals): discovered
	// sun-direction variable indices used by the Sun Azimuth/Elevation sliders.
	std::vector<int> m_sunVars;
	std::vector<float> m_sunVals;

	// Full sun-direction control. RDR3's timecycle exposes the sun as a 3-axis
	// direction vector (sun_direction_x/y/z); driving the axes raw only gives a
	// flat 2D sweep, so the missing third axis (elevation) is what kept the sun
	// from moving fully. Azimuth + Elevation compute a normalized x/y/z that we
	// write to all three vars at once.
	int   m_sunVarX = -1, m_sunVarY = -1, m_sunVarZ = -1;
	float m_sunAzimuth = 0.0f;    // compass bearing of the sun, degrees
	float m_sunElevation = 0.0f;  // height above the horizon, degrees
	bool  m_sunUserEdited = false;// once true, keep the user's values (don't re-seed)
	void  ApplySunDirection();    // normalize az/el -> sun_direction_x/y/z
	void  ComputeSunFromClock();  // seed Direction/Height from the current time of day

	// --- character ---
	int  m_poseIdx = 0;
	int  m_facialIdx = 0;
	bool m_targetHidden = false;
	bool m_hideOthers = false;
	std::vector<Ped> m_hiddenPeds;
	bool m_poseActive = false;   // a scenario is playing on the subject right now
	int  m_swapIdx = 0;          // character swapper: index into PMSwapModels (0 = default)

	// Character swap via decoy ped. Morphing the *player* model is futile: the
	// game's medium_update script re-applies the stored player model (Arthur /
	// John) within a frame, so a SET_PLAYER_MODEL swap flickers back instantly
	// and can leave the player ped invisible on exit. Instead we hide the real
	// player and stand a decoy ped of the chosen character in its place - it
	// persists for the whole session and is simply deleted on exit, with the
	// real player restored untouched.
	Ped  m_swapPed = 0;          // the decoy ped (0 = none)
	bool m_playerHidden = false; // real player hidden behind the decoy

	// Subject picker: nearby peds captured when the Character page is built, so
	// the "Subject" choice can map a menu index back to a ped handle.
	std::vector<Ped> m_subjectList;

	// Holding a drawn weapon under the script camera makes the player's upper-body
	// aim pose flail / glitch (the same reason native Photo Mode relaxes the
	// player). On entry we switch the player to unarmed and restore the weapon on
	// exit. Use the Character page's "Holster Player Weapon" toggle to override.
	Hash m_savedPlayerWeapon = 0;
	bool m_playerDisarmed = false;   // we put the player's weapon away this session
	bool m_holsterPlayer = true;     // user toggle (default on)
	void ApplyPlayerHolster();       // enforce/restore based on m_holsterPlayer

	// Weapon action for the subject so weapon-out shots are a deliberate, clean
	// pose (no idle-aim flail) and firing pics are possible: aim or fire on
	// command. 0 None, 1 Aim Forward, 2 Aim At Camera, 3 Fire Forward.
	int  m_weaponAction = 0;
	void ApplyWeaponAction();        // issue the aim/fire task on the subject

	// --- lighting ---
	PMLight m_lights[PM_MAX_LIGHTS];
	int  m_lightSel = 0;
	bool m_gizmos = true;

	// Shadow-casting layer: DRAW_LIGHT_WITH_RANGE never shadows (and RDR2
	// exposes no shadowed-light native), so real shadows come from
	// (a) lamp prop entities whose built-in lights shadow natively, and
	// (b) the Rockstar lightrig anim scenes pinned to the subject.
	Object m_lamps[PM_MAX_LAMPS] = {};
	int   m_lampCount = 0;
	int   m_lampModelIdx = 0;
	float m_lampIntensity = 4.0f;
	HeroLightSetup m_rigLight;

	void AddShadowLamp();
	void RemoveLastLamp();
	void RemoveAllLamps();
	void ApplyLampSettings();

	// --- artificial light props (Improved Artificial Lighting V2) ---
	PMArtLight m_artLights[PM_MAX_ARTLIGHTS];
	int   m_artSel = 0;          // which placed prop the editor edits
	int   m_artModelIdx = 0;     // model chosen for the next spawn
	float m_artStep = 0.5f;      // nudge step (metres)

	int   SpawnArtLight();                 // create the chosen model in front of the lens
	void  DestroyArtLight(int idx);
	void  RemoveAllArtLights();
	PMArtLight* SelectedArtLight();
	void  NudgeArtLight(float fwd, float right, float up); // move selected (camera-relative)
	void  ApplyArtLightTransform(int idx); // push pos/rot to the engine object
	void  UpdateArtLights();               // per-frame: keep lit + hide for clean shots

	// --- DOF ---
	float m_focusDist = 5.0f;
	float m_blurStrength = 0.0f;
	bool  m_focusLock = false;
	bool  m_autoFocus = false;   // keep the focal plane locked on the subject (Arthur/John by default)
	bool  m_dofDirty = false;
	bool  m_dofApplied = false;  // the DOF param block is currently active on the cam
	bool  m_dofLockActive = false;     // Composition page built with the lens locked (DoF on)
	bool  m_compRebuildPending = false;// re-skin the Composition page after a DoF on/off transition

	// --- post ---
	int   m_gradeIdx = 0;
	float m_gradeStrength = 1.0f;
	int   m_filterIdx = 0;
	float m_motionBlur = 0.0f;
	bool  m_letterbox = false;
	bool  m_grid = false;
	int   m_aspectIdx = 0;       // cinematic aspect frame (0 = off)
	float m_filterOpacity = 1.0f; // ANIMPOSTFX filter strength 0..1

	// --- enhanced render (R* photo-mode quality path) ---
	// The official Photo Mode holds GRAPHICS::_0xF5793BB386E1FF9C(1) every frame
	// while open - a dedicated render path that adds ambient occlusion / detail.
	// Plus an exposure/contrast trim driven by the engine's photo-mode natives
	// (_CHANGE_PHOTO_MODE_EXPOSURE / _CHANGE_PHOTO_MODE_CONTRAST), which are
	// relative-delta natives, so we commit only the change since last apply and
	// undo the accumulated trim on exit.
	bool  m_enhancedRender = false;   // R* photo render path (more AO / detail)
	bool  m_enhancedApplied = false;  // currently asserted in the engine
	bool  m_exposureLocked = true;    // hold exposure at baseline so the render path can't blow out
	float m_exposure = 0.0f;          // exposure trim target (relative)
	float m_contrast = 0.0f;          // contrast trim target (relative)
	float m_exposureApplied = 0.0f;   // accumulated exposure delta committed
	float m_contrastApplied = 0.0f;   // accumulated contrast delta committed
	void  ApplyEnhancedRender();      // assert/clear the render path (per frame)

	// First-time tab tip: shows a one-time, script-drawn hint card the first time
	// each tab is opened (persisted via g_Config.TipsSeenMask). `bit` is the tab's
	// bit index.
	void ShowTabTip(int bit, const char* text);

	// --- native menu pages (PhotoMenus.cpp) ---
	void BuildPhotoMenus();        // entry page + all tab pages (called on Activate)
	void RebuildCameraPage();
	void RebuildWorldPage();
	void RebuildCharacterPage();
	void RebuildLightingPage();
	void RebuildPostPage();
	void RebuildEffectsPage();
	void RebuildCapturesPage();
	void RebuildSettingsPage();    // free-cam speed / sensitivity (shared g_Config)
	void RebuildCreditsPage();     // credits (mirrors the main menu's credits)
	bool m_menusBuilt = false;

	// --- Scene Editor tab (ScenePhotoMenus.cpp) ---
	// A lightweight in-Photo-Mode staging tool: place props, direct actors with
	// validated scenarios, and make temporary (reverted-on-exit) world edits.
	void RebuildScenePage();
	void RebuildSceneObjectsPage();
	void RebuildSceneObjectList();   // dynamic: current search/bucket result set
	void RebuildScenePlacedList();   // dynamic: placed objects
	void RebuildSceneObjectEdit();   // dynamic: selected placed object
	void RebuildSceneActorsPage();   // dynamic: scene cast
	void RebuildSceneAddActor();     // dynamic: model picker for the chosen group
	void RebuildSceneActorEdit();    // dynamic: selected actor
	void RebuildSceneActorScenario();// dynamic: validated scenario list
	void RebuildSceneWorldPage();    // dynamic: temporary YMAP edits
	void RebuildScenePedBrowse();    // dynamic: browse all peds (humans / animals)
	void RebuildScenePedList();      // dynamic: ped search / bucket result set
	// Rebuilding a submenu from inside its own option would destroy the running
	// callback, so same-page refreshes are deferred and applied here, after the
	// menu's input handling has returned. Called from Tick().
	void PumpSceneRebuild();

	// --- internals ---
	void HandleInput();            // drives the native menu from raw keys
	void UpdateCamera();
	void UpdateWorld();
	void UpdateLights();
	void UpdateCharacterVisibility();
	void ApplyDof();
	void ApplyGrade();
	void ApplyFilter(int newIdx);
	int  m_activeFilterIdx = 0;

	Ped  ResolveTarget() const;   // selected ped or the player
	void CycleTarget(int dir);
	void SetFrozen(bool frozen);
	void FrameForward();
	std::string DescribeTarget() const;   // readable name for the subject
	void ApplyFilterWithOpacity();        // (re)play the filter at m_filterOpacity
	void ClearSubjectAnim(Ped ped);       // stop pose/look/facial cleanly
	void ApplyCharacterSwap();            // morph the player into PMSwapModels[m_swapIdx]
	void DrawSubjectMarker();             // on-screen indicator over the selected ped

	// lighting helpers
	int  AddLightAtCamera();
	PMLight* SelectedLight();
	void AttachSelectedToTarget();
	Vector3 LightWorldPos(const PMLight& l) const;
	static void TemperatureToRGB(float kelvin, int& r, int& g, int& b);
	void Apply3PointPreset();
	void SpawnLightBulb(PMLight& l);      // create the p_lightbulb01x prop for a light
	void DestroyLightBulb(PMLight& l);    // delete a light's bulb prop
	void FreeLight(PMLight& l);           // release a light slot (and its bulb)

	// World-space overlays (PhotoModeUI.cpp) - cheap, part of the shot.
	void DrawOverlays();         // grid + light gizmos + aspect bars
	void DrawLightGizmos();
	void DrawAspectFrame();      // ratio crop bars; survive Hide UI (part of the shot)
	void DrawControlHints();     // self-drawn control tip bar (native prompts are unreliable under Hide HUD)
	void DrawSunCompass();       // top-right sky map showing sun azimuth/elevation (Lighting tab)
};

inline CPhotoMode g_PhotoMode;
