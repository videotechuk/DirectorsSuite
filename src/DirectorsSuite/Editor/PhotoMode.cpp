#include "PhotoMode.h"
#include "EditorMath.h"
#include "EditorTypes.h"
#include "DirectorTypes.h"
#include "Config.h"
#include "CameraDirector.h"
#include "FreeCam.h"
#include "Overlays.h"
#include "WorldControl.h"
#include "HeroLight.h"
#include "TimecycleRT.h"
#include "PhotoAudio.h"
#include "BlackBarsPatch.h"
#include "ScreenCapture.h"
#include "SceneEditor.h"
#include "SceneSteppers.h"
#include "..\script.h"
#include "..\keyboard.h"
#include "..\UI\UIUtil.h"
#include "..\UI\Drawing.h"
#include "..\UI\Menu.hpp"
#include <unordered_map>
#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------------------
// Data tables
// ---------------------------------------------------------------------------

// Color grades are timecycle modifiers (verified against the game's
// timecycles). Only one timecycle slot exists, so grain / vignette / lens
// distortion live in this list too; ANIMPOSTFX filters stack on top.
struct PMGradeDef { const char* label; const char* modifier; };
static const std::vector<PMGradeDef> PMGrades = {
	{ "None",               nullptr },
	{ "Cinematic",          "SPOTMETER_cinematicCam" },
	{ "Photo Studio",       "bla_photo_int" },
	{ "High Contrast",      "DeadEyeClarity" },
	{ "Desaturated",        "DeadEyeDark" },
	{ "Bright & Airy",      "DeadEyeLight" },
	{ "Warm Evening",       "wakeUp_guarma_evening" },
	{ "Cold Winter",        "winter1_adlerFog" },
	{ "Noir",               "EndCreditsDark" },
	{ "Sickly Green",       "PlayerSickGuarma" },
	{ "Dreamlike",          "dreams_coyote_final" },
	{ "Theater",            "bla_theater_int" },
	{ "Bloom",              "Bloom01" },
	{ "Film Grain",         "base_noise" },
	{ "Vignette",           "1P_MaskDark" },
	{ "Lens Distortion",    "LensDistDrunk" },
	{ "Heavy Motion Blur",  "GenMoBlur100" },
};

// Curated pose list: scenarios that read well as photo poses
struct PMPoseDef { const char* label; const char* scenario; };
static const std::vector<PMPoseDef> PMPoses = {
	{ "None",             nullptr },
	{ "Stoic Stare",      "WORLD_HUMAN_STARE_STOIC" },
	{ "Badass",           "WORLD_HUMAN_BADASS" },
	{ "Guard Stance",     "WORLD_HUMAN_GUARD_SCOUT" },
	{ "Military Guard",   "WORLD_HUMAN_GUARD_MILITARY" },
	{ "Smoking",          "WORLD_HUMAN_SMOKE" },
	{ "Cigar",            "WORLD_HUMAN_SMOKE_CIGAR" },
	{ "Drinking",         "WORLD_HUMAN_DRINKING" },
	{ "Drunk",            "WORLD_HUMAN_DRINKING_DRUNK" },
	{ "Coffee",           "WORLD_HUMAN_COFFEE_DRINK" },
	{ "Lean On Wall",     "WORLD_HUMAN_LEAN_BACK_WALL" },
	{ "Lean & Smoke",     "WORLD_HUMAN_LEAN_BACK_WALL_SMOKING" },
	{ "Sit On Ground",    "WORLD_HUMAN_SIT_GROUND" },
	{ "Sit & Read",       "WORLD_HUMAN_SIT_GROUND_READING" },
	{ "Exhausted",        "WORLD_HUMAN_SIT_BACK_EXHAUSTED" },
	{ "Sleep (Ground)",   "WORLD_HUMAN_SLEEP_GROUND_ARM" },
	{ "Inspect",          "WORLD_HUMAN_CROUCH_INSPECT" },
	{ "Write Notes",      "WORLD_HUMAN_WRITE_NOTEBOOK" },
	{ "Guitar",           "WORLD_HUMAN_SIT_GUITAR_MALE_A" },
	{ "Impatient",        "WORLD_HUMAN_WAITING_IMPATIENT" },
};

// Character swapper: become any of these models (the player ped is morphed,
// then restored on exit). Reuses the verified Story Mode cast.
static const std::vector<PedModelDef>& PMSwapModels = StoryPeds;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string PMFloatStr(float v, int decimals = 2)
{
	char buf[32];
	sprintf_s(buf, "%.*f", decimals, v);
	return buf;
}

// Several photo-mode natives are declared with an `Any` (uint64) parameter but
// actually take a float. Passing a float directly would numerically truncate it
// (0.05f -> 0); this packs the float's bit pattern into the low 32 bits so the
// native receives the real value (matching how float-typed natives push args).
static Any PMFloatArg(float f)
{
	Any a = 0;
	*reinterpret_cast<float*>(&a) = f;
	return a;
}

// Reverse map: ped model hash -> friendly name, built once from the verified
// Director Mode model tables so subjects read as "Dutch van der Linde" rather
// than "Ped #123".
static const char* PedNameForModel(Hash model)
{
	static std::unordered_map<Hash, const char*> s_names;
	if (s_names.empty()) {
		auto add = [](const std::vector<PedModelDef>& list) {
			for (const auto& p : list) {
				s_names[MISC::GET_HASH_KEY(p.model)] = p.label;
			}
		};
		add(StoryPeds);
		add(AmbientPeds);
		add(GangLawPeds);
		add(AnimalPeds);
	}
	auto it = s_names.find(model);
	return (it != s_names.end()) ? it->second : nullptr;
}

// Tanner Helland approximation: kelvin -> RGB
void CPhotoMode::TemperatureToRGB(float kelvin, int& r, int& g, int& b)
{
	float t = EMath::Clamp(kelvin, 1000.0f, 12000.0f) / 100.0f;
	float fr, fg, fb;

	if (t <= 66.0f) {
		fr = 255.0f;
		fg = 99.47f * logf(t) - 161.12f;
		fb = (t <= 19.0f) ? 0.0f : 138.52f * logf(t - 10.0f) - 305.04f;
	}
	else {
		fr = 329.7f * powf(t - 60.0f, -0.1332f);
		fg = 288.12f * powf(t - 60.0f, -0.0755f);
		fb = 255.0f;
	}

	r = (int)EMath::Clamp(fr, 0.0f, 255.0f);
	g = (int)EMath::Clamp(fg, 0.0f, 255.0f);
	b = (int)EMath::Clamp(fb, 0.0f, 255.0f);
}

Ped CPhotoMode::ResolveTarget() const
{
	if (m_target != 0 && ENTITY::DOES_ENTITY_EXIST(m_target)) return m_target;
	return PLAYER::PLAYER_PED_ID();
}

void CPhotoMode::CycleTarget(int dir)
{
	const int ARR_SIZE = 1024;
	int peds[ARR_SIZE];
	int found = worldGetAllPeds(peds, ARR_SIZE);

	Ped player = PLAYER::PLAYER_PED_ID();
	Vector3 center = ENTITY::GET_ENTITY_COORDS(player, true, true);

	struct Cand { Ped ped; float dist; };
	std::vector<Cand> list;
	for (int i = 0; i < found; i++) {
		if (peds[i] == player || !ENTITY::DOES_ENTITY_EXIST(peds[i])) continue;
		Vector3 pos = ENTITY::GET_ENTITY_COORDS(peds[i], true, true);
		float dist = EMath::Distance(center, pos);
		if (dist < 60.0f) list.push_back({ peds[i], dist });
	}
	std::sort(list.begin(), list.end(), [](const Cand& a, const Cand& b) { return a.dist < b.dist; });

	int current = 0; // 0 = player
	if (m_target != 0) {
		for (int i = 0; i < (int)list.size(); i++) {
			if (list[i].ped == m_target) { current = i + 1; break; }
		}
	}
	int total = 1 + (int)list.size();
	current = (current + dir) % total;
	if (current < 0) current += total;

	m_target = (current == 0) ? 0 : list[current - 1].ped;
	m_poseIdx = 0;
	m_facialIdx = 0;
	m_poseActive = false;
	m_targetHidden = false;
}

std::string CPhotoMode::DescribeTarget() const
{
	if (m_target == 0 || !ENTITY::DOES_ENTITY_EXIST(m_target)) {
		return "Player";
	}
	const char* name = PedNameForModel(ENTITY::GET_ENTITY_MODEL(m_target));
	if (name) return name;
	return PED::IS_PED_HUMAN(m_target) ? "Nearby Person" : "Animal";
}

// Floating marker over the selected subject so the user can see who they are
// editing. Hidden when the subject is the player or the UI is hidden.
void CPhotoMode::DrawSubjectMarker()
{
	if (m_uiHidden || ScreenCapture::IsCapturing()) return;
	if (m_target == 0 || !ENTITY::DOES_ENTITY_EXIST(m_target)) return;

	Vector3 pos = ENTITY::GET_ENTITY_COORDS(m_target, true, true);
	float sx = 0.0f, sy = 0.0f;
	if (!GRAPHICS::GET_SCREEN_COORD_FROM_WORLD_COORD(pos.x, pos.y, pos.z + 1.25f, &sx, &sy)) {
		return; // off screen
	}
	// downward chevron + label
	GRAPHICS::DRAW_RECT(sx, sy - 0.006f, 0.006f, 0.014f, 255, 255, 255, 235, false, false);
	GRAPHICS::DRAW_RECT(sx, sy, 0.013f, 0.0032f, 255, 255, 255, 235, false, false);
	Drawing::DrawFormattedText("SUBJECT", Font::Body, 255, 255, 255, 230, Alignment::Center, 16,
		sx * SCREEN_WIDTH, (sy - 0.03f) * SCREEN_HEIGHT);
}

// Re-plays the current filter at the chosen opacity. ANIMPOSTFX has no direct
// strength setter in the SDK, so opacity is delivered through the timecycle-
// independent _ANIMPOSTFX_SET_STRENGTH native used by the game itself.
void CPhotoMode::ApplyFilterWithOpacity()
{
	if (m_filterIdx > 0 && m_filterIdx < (int)PhotoModeFilters.size()) {
		GRAPHICS::_ANIMPOSTFX_SET_STRENGTH(PhotoModeFilters[m_filterIdx], EMath::Clamp(m_filterOpacity, 0.0f, 1.0f));
	}
}

// Stops any pose/look/facial cleanly. Order matters: we must NOT leave the
// ped frozen (the bug where the player locks up), so we explicitly unfreeze
// position and hand the ped back to its own AI.
void CPhotoMode::ClearSubjectAnim(Ped ped)
{
	if (!ENTITY::DOES_ENTITY_EXIST(ped)) return;
	TASK::CLEAR_PED_TASKS(ped, true, false);
	TASK::CLEAR_PED_SECONDARY_TASK(ped);
	PED::CLEAR_FACIAL_IDLE_ANIM_OVERRIDE(ped);
	ENTITY::FREEZE_ENTITY_POSITION(ped, false);
	PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped, false);
}

// Character swapper via a DECOY ped.
//
// Morphing the actual player model does not stick: the game's medium_update
// script holds a stored "desired" player model (Arthur = PLAYER_ZERO, John =
// PLAYER_THREE) and re-applies it with SET_PLAYER_MODEL the moment it sees the
// player ped's model differ - so a trainer-style morph reverts within a frame
// and the re-apply can leave the player invisible on exit. That global lives in
// script memory we don't touch from an ASI, so instead we never change the
// player model at all: we hide the real player and stand a decoy ped of the
// chosen character in its place. The decoy persists for the whole session and
// is simply deleted when leaving the swap, with the real player restored.
void CPhotoMode::ApplyCharacterSwap()
{
	Ped player = PLAYER::PLAYER_PED_ID();

	auto removeDecoy = [this]() {
		if (m_swapPed != 0 && ENTITY::DOES_ENTITY_EXIST(m_swapPed)) {
			Ped p = m_swapPed;
			ClearSubjectAnim(p);
			PED::DELETE_PED(&p);
		}
		if (m_target == m_swapPed) m_target = 0; // back to the real player as subject
		m_swapPed = 0;
	};

	// "Default" (or out of range): tear the decoy down and restore the player.
	if (m_swapIdx <= 0 || m_swapIdx > (int)PMSwapModels.size()) {
		removeDecoy();
		if (m_playerHidden) {
			ENTITY::FREEZE_ENTITY_POSITION(player, false);
			ENTITY::SET_ENTITY_VISIBLE(player, true);
			m_playerHidden = false;
		}
		return;
	}

	Hash model = MISC::GET_HASH_KEY(PMSwapModels[m_swapIdx - 1].model);
	if (!STREAMING::IS_MODEL_VALID(model)) return;
	STREAMING::REQUEST_MODEL(model, false);
	DWORD start = GetTickCount();
	while (!STREAMING::HAS_MODEL_LOADED(model) && GetTickCount() - start < 5000) WAIT(10);
	if (!STREAMING::HAS_MODEL_LOADED(model)) return;

	// Replace any previous decoy in place.
	removeDecoy();

	Vector3 pos = ENTITY::GET_ENTITY_COORDS(player, true, true);
	float heading = ENTITY::GET_ENTITY_HEADING(player);

	Ped decoy = PED::CREATE_PED(model, pos.x, pos.y, pos.z, heading, false, true, false, false);
	STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
	if (decoy == 0) return;

	// RDR2 quirk: fresh peds are invisible until an outfit variation is applied.
	PED::_SET_RANDOM_OUTFIT_VARIATION(decoy, true);
	PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(decoy, true);
	PED::SET_PED_KEEP_TASK(decoy, true);
	ENTITY::SET_ENTITY_INVINCIBLE(decoy, true);
	TASK::TASK_STAND_STILL(decoy, -1);

	// Drop the decoy straight onto the ground. A fresh ped spawns at the
	// player's root height and then falls under gravity - and with the world
	// frozen / time scaled down (the usual Photo Mode state) that fall is slow
	// or stalls entirely, so it hangs in the air. PLACE_ENTITY_ON_GROUND_PROPERLY
	// teleports it onto the surface at once, no physics required. Fall back to a
	// ground-Z probe if the prop placement reports failure.
	if (!ENTITY::PLACE_ENTITY_ON_GROUND_PROPERLY(decoy, true)) {
		float groundZ = pos.z;
		if (MISC::GET_GROUND_Z_FOR_3D_COORD(pos.x, pos.y, pos.z + 1.0f, &groundZ, false)) {
			ENTITY::SET_ENTITY_COORDS(decoy, pos.x, pos.y, groundZ, false, false, false, false);
		}
	}

	m_swapPed = decoy;

	// Hide the real player behind the decoy (kept frozen so it can't wander).
	ENTITY::SET_ENTITY_VISIBLE(player, false);
	ENTITY::FREEZE_ENTITY_POSITION(player, true);
	m_playerHidden = true;

	// Make the decoy the active photo subject so poses/lights/orbit target it.
	m_target = decoy;
	m_poseIdx = 0;
	m_facialIdx = 0;
	m_poseActive = false;
	m_targetHidden = false;
}

// ---------------------------------------------------------------------------
// World
// ---------------------------------------------------------------------------

// Two freeze methods:
//  - Time Scale (default): SET_TIME_SCALE(0) halts the simulation without
//    going anywhere near the Photo Mode pipeline. The native photo freeze
//    engages the same framing the built-in Photo Mode uses, which crops
//    ultrawide displays to 16:9 - this method does not.
//  - Photo Mode Native: the engine's own freeze, kept as a fallback in case
//    something (animations, particles) behaves better with it.
void CPhotoMode::SetFrozen(bool frozen)
{
	if (m_frozen == frozen) return;
	m_frozen = frozen;

	if (m_freezeMethod == 0) {
		MISC::SET_TIME_SCALE(frozen ? 0.0f : g_World.TimeScale);
	}
	else {
		if (frozen) ANIMSCENE::_REQUEST_PHOTO_MODE_FREEZE();
		else ANIMSCENE::_REQUEST_PHOTO_MODE_DEFREEZE();
	}
}

// Holding a drawn weapon under the script camera makes the player's aim pose
// glitch, so (by default) put the weapon away for the session and restore it on
// exit. Driven by m_holsterPlayer; safe to call repeatedly.
void CPhotoMode::ApplyPlayerHolster()
{
	Ped player = PLAYER::PLAYER_PED_ID();
	if (!ENTITY::DOES_ENTITY_EXIST(player)) return;

	if (m_holsterPlayer) {
		if (m_playerDisarmed) return; // already put away
		Hash cur = 0;
		WEAPON::GET_CURRENT_PED_WEAPON(player, &cur, true, 0, false);
		m_savedPlayerWeapon = cur;
		WEAPON::SET_CURRENT_PED_WEAPON(player, WEAPON::_GET_DEFAULT_UNARMED_WEAPON_HASH(player), true, 0, false, false);
		m_playerDisarmed = true;
	}
	else if (m_playerDisarmed) {
		if (m_savedPlayerWeapon != 0)
			WEAPON::SET_CURRENT_PED_WEAPON(player, m_savedPlayerWeapon, true, 0, false, false);
		m_playerDisarmed = false;
	}
}

// Make the subject aim or fire on command. A real aim/shoot task gives a clean,
// intentional pose (unlike the flailing weapon-idle) and lets you capture a
// muzzle flash. The world must be live or slow for the firing animation / flash
// to play (Frame Forward also works); a frozen world holds a static aimed pose.
void CPhotoMode::ApplyWeaponAction()
{
	Ped ped = ResolveTarget();
	if (!ENTITY::DOES_ENTITY_EXIST(ped)) return;

	if (m_weaponAction == 0) {
		TASK::CLEAR_PED_TASKS(ped, true, false);
		return;
	}

	// The weapon has to be in hand for any of these, so cancel holstering for the
	// player (restores the saved weapon) when an action is requested.
	if (ped == PLAYER::PLAYER_PED_ID() && m_playerDisarmed) {
		m_holsterPlayer = false;
		ApplyPlayerHolster();
	}

	// Keep ammo topped up so a Fire action doesn't run dry mid-shoot.
	Hash wpn = 0;
	WEAPON::GET_CURRENT_PED_WEAPON(ped, &wpn, true, 0, false);
	if (wpn != 0) WEAPON::SET_PED_INFINITE_AMMO(ped, true, wpn);

	// Target point: the camera (aim at viewer) or ~60 m ahead of the ped's facing.
	Vector3 pos = ENTITY::GET_ENTITY_COORDS(ped, true, true);
	float h = ENTITY::GET_ENTITY_HEADING(ped) * EMath::DEG2RAD;
	Vector3 fwd{ -sinf(h), cosf(h), 0.0f };
	Vector3 ahead{ pos.x + fwd.x * 60.0f, pos.y + fwd.y * 60.0f, pos.z + 0.5f };
	Vector3 cam = m_pos;

	TASK::CLEAR_PED_TASKS(ped, true, false);
	switch (m_weaponAction) {
		case 1: // Aim Forward
			TASK::TASK_AIM_GUN_AT_COORD(ped, ahead.x, ahead.y, ahead.z, -1, false, false);
			break;
		case 2: // Aim At Camera
			TASK::TASK_AIM_GUN_AT_COORD(ped, cam.x, cam.y, cam.z, -1, false, false);
			break;
		case 3: // Fire Forward (continuous)
			TASK::TASK_SHOOT_AT_COORD(ped, ahead.x, ahead.y, ahead.z, -1,
				MISC::GET_HASH_KEY("FIRING_PATTERN_FULL_AUTO"), 0);
			break;
	}
}

void CPhotoMode::FrameForward()
{
	if (!m_frozen) return;
	if (m_freezeMethod == 0) {
		MISC::SET_TIME_SCALE(g_World.TimeScale);
	}
	else {
		ANIMSCENE::_REQUEST_PHOTO_MODE_DEFREEZE();
	}
	m_stepFrames = 2; // let the world advance for two ticks, then refreeze
}

// R* enhanced photo render path: GRAPHICS::_0xF5793BB386E1FF9C(1) is the render
// mode the official Photo Mode holds active every frame (more ambient occlusion
// / detail).
//
// The render path blows the exposure out to white on its own; the only thing
// that tames it is locking the photo-mode exposure to a captured baseline
// (exactly what R* does). But a locked exposure also makes the Exposure slider a
// no-op (the engine ignores _CHANGE_PHOTO_MODE_EXPOSURE while locked). So the
// lock is a user toggle (m_exposureLocked, default ON): locked = no blow-out,
// Exposure held; unlocked = Exposure slider works, user manages brightness.
void CPhotoMode::ApplyEnhancedRender()
{
	if (m_enhancedRender) {
		if (!m_enhancedApplied) {
			// Rising edge: lock the (still-good) exposure FIRST, enable the render
			// path, then snap exposure back to the locked baseline.
			if (m_exposureLocked) GRAPHICS::_0x5CD6A2CCE5087161(TRUE);
			GRAPHICS::_0xF5793BB386E1FF9C(1);
			if (m_exposureLocked) GRAPHICS::_0x9229ED770975BD9E();
			m_enhancedApplied = true;
		}
		else {
			GRAPHICS::_0xF5793BB386E1FF9C(1);    // hold the render path each frame
		}
	}
	else if (m_enhancedApplied) {
		GRAPHICS::_0xF5793BB386E1FF9C(0);
		GRAPHICS::_0x5CD6A2CCE5087161(FALSE); // unlock exposure (resume auto)
		m_enhancedApplied = false;
	}
}

void CPhotoMode::UpdateWorld()
{
	ApplyEnhancedRender();

	if (m_stepFrames > 0) {
		m_stepFrames--;
		if (m_stepFrames == 0 && m_frozen) {
			if (m_freezeMethod == 0) MISC::SET_TIME_SCALE(0.0f);
			else ANIMSCENE::_REQUEST_PHOTO_MODE_FREEZE();
		}
	}

	if (m_hideHud) {
		HUD::HIDE_HUD_AND_RADAR_THIS_FRAME();
		// _HIDE_HUD_THIS_FRAME() also wipes the button prompts (that's why the
		// control tips never showed). Only use that aggressive hide when we
		// genuinely want EVERYTHING gone - i.e. during a clean shot / capture.
		// While editing, the standard HUD+radar hide above keeps the view clean
		// but leaves our control prompts on screen.
		if (m_uiHidden || ScreenCapture::IsCapturing()) {
			HUD::_HIDE_HUD_THIS_FRAME();
		}
	}

	if (m_letterbox) {
		CAM::_FORCE_LETTER_BOX_THIS_UPDATE();
	}
}

// Full sun-direction control. The timecycle sun is a unit direction vector
// split across sun_direction_x/y/z; the previous build only surfaced two of
// the three axes, so the sun could swing around the horizon but never change
// height. Azimuth + Elevation rebuild the whole normalized vector so the sun
// moves in every direction. Default azimuth 0 / elevation 0 reproduces the
// engine default (0, -1, 0).
void CPhotoMode::ApplySunDirection()
{
	if (m_sunVarX < 0 || m_sunVarY < 0) return; // need the horizontal axes at least

	float az = m_sunAzimuth * EMath::DEG2RAD;
	float el = m_sunElevation * EMath::DEG2RAD;
	float ce = cosf(el);

	float dx = ce * sinf(az);
	float dy = -ce * cosf(az);
	float dz = sinf(el);

	TimecycleRT::ApplyVarToScene(m_sunVarX, dx);
	TimecycleRT::ApplyVarToScene(m_sunVarY, dy);
	if (m_sunVarZ >= 0) TimecycleRT::ApplyVarToScene(m_sunVarZ, dz);
}

// Seed Sun Direction / Sun Height from the current clock so the controls open
// showing roughly where the sun actually is (the engine drives the sun from
// time of day). Midnight = North & below the horizon, 06:00 = East at the
// horizon, noon = South & high, 18:00 = West at the horizon. An approximation,
// but it matches the visible sun closely and needs no fragile timecycle reads.
void CPhotoMode::ComputeSunFromClock()
{
	float h = (float)CLOCK::GET_CLOCK_HOURS() + (float)CLOCK::GET_CLOCK_MINUTES() / 60.0f;

	float az = ((h - 6.0f) / 24.0f) * 360.0f + 90.0f;
	while (az < 0.0f) az += 360.0f;
	while (az >= 360.0f) az -= 360.0f;

	float el = 75.0f * sinf((h - 6.0f) / 12.0f * 3.14159265f);

	m_sunAzimuth = az;
	m_sunElevation = el;
}

// [TEST] Tiled super-resolution capture. Keeps the camera POSITION fixed (no
// parallax) and pans a narrowed FOV across an NxN grid, grabbing one desktop
// frame per tile. Each tile is rendered slightly wider than its cell, then every
// output pixel is REPROJECTED into the tile that covers its ray (inverse mapping
// + bilinear sample) so the result is one seamless wide pinhole image at
// (factor x screen) resolution - not a naive grid paste. Synchronous on the
// script thread; the world should be frozen so the scene is static between tiles.
void CPhotoMode::CaptureSuperRes()
{
	int f = m_superResFactor; if (f < 2) f = 2; if (f > 3) f = 3;
	if (m_cam == 0 || !CAM::DOES_CAM_EXIST(m_cam)) {
		UIUtil::PrintSubtitle("~COLOR_RED~Super-res needs the Photo Mode camera~s~");
		return;
	}
	if (!ScreenCapture::BeginSequence()) {
		UIUtil::PrintSubtitle("~COLOR_RED~Super-res capture unavailable~s~");
		return;
	}
	UIUtil::PrintSubtitle("Capturing super-res (" + std::to_string(f) + "x)...");

	const float fov0 = m_fov;
	const Vector3 rot0 = m_rot;
	const float aspect = SCREEN_WIDTH / SCREEN_HEIGHT;

	auto cross = [](const Vector3& a, const Vector3& b) {
		return Vector3{ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
	};

	// Full-shot camera basis (roll applied around forward).
	const Vector3 fwd = EMath::RotationToDirection(rot0);
	const Vector3 worldUp{ 0.0f, 0.0f, 1.0f };
	const Vector3 r0 = EMath::Normalize(cross(fwd, worldUp));
	const Vector3 u0 = cross(r0, fwd);
	const float roll = rot0.y * EMath::DEG2RAD;
	const Vector3 right = EMath::Add(EMath::Scale(r0, cosf(roll)), EMath::Scale(u0, sinf(roll)));
	const Vector3 up    = EMath::Sub(EMath::Scale(u0, cosf(roll)), EMath::Scale(r0, sinf(roll)));

	// Full image-plane half-extents (tan units at distance 1).
	const float Ymax = tanf(fov0 * 0.5f * EMath::DEG2RAD);
	const float Xmax = Ymax * aspect;

	// Tiles are rendered a touch wider than their cell so reprojection always
	// finds coverage (overlap is harmless - it's the same scene).
	const float margin = 1.20f;
	const float tanHalfV = (Ymax / (float)f) * margin;
	const float tanHalfH = tanHalfV * aspect;
	const float tileFovDeg = 2.0f * atanf(tanHalfV) * EMath::RAD2DEG;

	// Re-apply rotation + FOV every frame: the frozen-cam tick re-derives the
	// camera, so a one-time set gets overwritten. Keep HUD + menu prompts hidden.
	auto renderTile = [this](const Vector3& rt, float fov, int frames) {
		for (int i = 0; i < frames; i++) {
			CAM::_0x3C8F74E8FE751614();
			CAM::SET_CAM_ROT(m_cam, rt.x, rt.y, rt.z, 2);
			CAM::SET_CAM_FOV(m_cam, fov);
			HUD::HIDE_HUD_AND_RADAR_THIS_FRAME();
			HUD::_HIDE_HUD_THIS_FRAME();
			UpdatePrompts(false);
			g_Menu->HidePrompts();
			WAIT(0);
		}
	};

	struct TileCam { Vector3 fwd, right, up; };
	std::vector<TileCam> cam(f * f);
	std::vector<std::vector<unsigned char>> tiles(f * f);
	int tw = 0, th = 0;
	bool ok = true;

	// Lock auto-exposure at the user's framing so every tile shares one exposure
	// - the main cause of tonal seams (a sky tile and a ground tile otherwise
	// adapt to different brightness). Restored after unless Lock Exposure is on.
	renderTile(rot0, fov0, 6);
	GRAPHICS::_0x5CD6A2CCE5087161(TRUE);
	GRAPHICS::_0x9229ED770975BD9E();

	for (int r = 0; r < f && ok; r++) {
		for (int c = 0; c < f && ok; c++) {
			int idx = r * f + c;
			float cellX = Xmax * ((2.0f * ((float)c + 0.5f) / (float)f) - 1.0f);
			float cellY = Ymax * (1.0f - (2.0f * ((float)r + 0.5f) / (float)f));
			Vector3 tFwd = EMath::Normalize(EMath::Add(EMath::Add(fwd, EMath::Scale(right, cellX)), EMath::Scale(up, cellY)));
			Vector3 euler = EMath::DirectionToRotation(tFwd); // pitch, 0, yaw
			Vector3 setRot{ euler.x, rot0.y, euler.z };

			// Tile basis, derived exactly as the game will orient this rotation.
			Vector3 tf = EMath::RotationToDirection(setRot);
			Vector3 tr0 = EMath::Normalize(cross(tf, worldUp));
			Vector3 tu0 = cross(tr0, tf);
			cam[idx].fwd = tf;
			cam[idx].right = EMath::Add(EMath::Scale(tr0, cosf(roll)), EMath::Scale(tu0, sinf(roll)));
			cam[idx].up    = EMath::Sub(EMath::Scale(tu0, cosf(roll)), EMath::Scale(tr0, sinf(roll)));

			renderTile(setRot, tileFovDeg, 10);

			int gw = 0, gh = 0;
			if (!ScreenCapture::GrabFrame(tiles[idx], gw, gh) || gw <= 0 || gh <= 0) { ok = false; break; }
			if (tw == 0) { tw = gw; th = gh; }
			if (gw != tw || gh != th) { ok = false; break; }
		}
	}

	CAM::SET_CAM_ROT(m_cam, rot0.x, rot0.y, rot0.z, 2);
	CAM::SET_CAM_FOV(m_cam, fov0);
	if (!m_exposureLocked) GRAPHICS::_0x5CD6A2CCE5087161(FALSE); // restore auto-exposure
	ScreenCapture::EndSequence();

	if (!ok || tw == 0) { UIUtil::PrintSubtitle("~COLOR_RED~Super-res capture failed~s~"); return; }

	// --- Reproject every output pixel into the tile that covers its ray ---
	const int Wout = tw * f, Hout = th * f;
	std::vector<unsigned char> out((size_t)Wout * Hout * 4, 0);

	const float ovl = 1.0f / margin; // tile-NDC at which a tile's own cell ends
	for (int oy = 0; oy < Hout; oy++) {
		float ndcy = 1.0f - 2.0f * ((float)oy + 0.5f) / (float)Hout; // +1 top
		for (int ox = 0; ox < Wout; ox++) {
			float ndcx = 2.0f * ((float)ox + 0.5f) / (float)Wout - 1.0f; // +1 right
			Vector3 dir = EMath::Add(EMath::Add(fwd, EMath::Scale(right, ndcx * Xmax)), EMath::Scale(up, ndcy * Ymax));

			// Accumulate every tile that covers this ray, weighted so the overlap
			// bands cross-fade (feather) - hides any residual tonal/AA difference.
			float acc[4] = { 0, 0, 0, 0 };
			float wsum = 0.0f;
			for (int t = 0; t < f * f; t++) {
				const TileCam& tc = cam[t];
				float fz = EMath::Dot(dir, tc.fwd);
				if (fz <= 1e-4f) continue;
				float tx = (EMath::Dot(dir, tc.right) / fz) / tanHalfH;
				float ty = (EMath::Dot(dir, tc.up)    / fz) / tanHalfV;
				float ax = fabsf(tx), ay = fabsf(ty);
				if (ax >= 1.0f || ay >= 1.0f) continue; // ray not inside this tile
				float wx = (ax <= ovl) ? 1.0f : (1.0f - ax) / (1.0f - ovl);
				float wy = (ay <= ovl) ? 1.0f : (1.0f - ay) / (1.0f - ovl);
				float w = wx * wy;
				if (w <= 0.0f) continue;

				const std::vector<unsigned char>& src = tiles[t];
				float sx = (tx + 1.0f) * 0.5f * tw - 0.5f;
				float sy = (1.0f - ty) * 0.5f * th - 0.5f;
				if (sx < 0) sx = 0; else if (sx > tw - 1) sx = (float)(tw - 1);
				if (sy < 0) sy = 0; else if (sy > th - 1) sy = (float)(th - 1);
				int x0 = (int)sx, y0 = (int)sy;
				int x1 = (x0 + 1 < tw) ? x0 + 1 : x0;
				int y1 = (y0 + 1 < th) ? y0 + 1 : y0;
				float fx = sx - x0, fy = sy - y0;
				for (int ch = 0; ch < 4; ch++) {
					float p00 = src[((size_t)y0 * tw + x0) * 4 + ch];
					float p10 = src[((size_t)y0 * tw + x1) * 4 + ch];
					float p01 = src[((size_t)y1 * tw + x0) * 4 + ch];
					float p11 = src[((size_t)y1 * tw + x1) * 4 + ch];
					float top = p00 + (p10 - p00) * fx;
					float bot = p01 + (p11 - p01) * fx;
					acc[ch] += (top + (bot - top) * fy) * w;
				}
				wsum += w;
			}

			unsigned char* d = &out[((size_t)oy * Wout + ox) * 4];
			if (wsum > 1e-4f) {
				for (int ch = 0; ch < 4; ch++) {
					float v = acc[ch] / wsum + 0.5f;
					d[ch] = (unsigned char)(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v));
				}
			}
		}
	}

	std::string path;
	if (ScreenCapture::SaveBGRAImage(out, Wout, Hout, path))
		UIUtil::PrintSubtitle("Saved " + std::to_string(Wout) + "x" + std::to_string(Hout) + " to Captured Screenshots");
	else
		UIUtil::PrintSubtitle("~COLOR_RED~Super-res stitch/save failed~s~");
}

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------

void CPhotoMode::UpdateCamera()
{
	// [EXPERIMENT] Per-frame scripted/photo-camera "tick" the official Photo Mode
	// (camera_photomode.c case 6) and every other free-camera-while-paused scene
	// (camera_item.c, the theater shows, barber, bathing...) calls every frame
	// while their camera is movable. We have never called it - and it is the one
	// per-frame CAM native the official movable-camera state runs that we don't.
	// Hypothesis: this is what keeps the scripted camera responsive while the
	// world is frozen by _REQUEST_PHOTO_MODE_FREEZE. Harmless in Time Scale mode.
	CAM::_0x3C8F74E8FE751614();

	// Photo mode is modal: block EVERY game control so editor keys never
	// leak into gameplay (TAB used to holster/draw the weapon, etc.).
	// Our own reads all use the DISABLED control variants, so the camera
	// keeps working.
	PAD::DISABLE_ALL_CONTROL_ACTIONS(0);

	float lookLR = PAD::GET_DISABLED_CONTROL_NORMAL(0, INPUT_LOOK_LR);
	float lookUD = PAD::GET_DISABLED_CONTROL_NORMAL(0, INPUT_LOOK_UD);
	float moveLR = PAD::GET_DISABLED_CONTROL_NORMAL(0, INPUT_MOVE_LR);
	float moveUD = PAD::GET_DISABLED_CONTROL_NORMAL(0, INPUT_MOVE_UD);

	float speed = g_Config.FreeCamSpeed;
	if (IsKeyDown(VK_SHIFT)) speed *= g_Config.FreeCamFastMultiplier;
	if (IsKeyDown(VK_CONTROL)) speed *= g_Config.FreeCamSlowMultiplier;

	if (m_orbit) {
		// Orbit/pivot around the target: mouse orbits, W/S dollies
		Ped target = ResolveTarget();
		Vector3 tpos = ENTITY::GET_ENTITY_COORDS(target, true, true);
		tpos.z += 0.6f;

		m_rot.z -= lookLR * g_Config.FreeCamMouseSensitivity;
		m_rot.x = EMath::Clamp(m_rot.x - lookUD * g_Config.FreeCamMouseSensitivity, -85.0f, 85.0f);
		m_orbitDist = EMath::Clamp(m_orbitDist + moveUD * speed, 0.8f, 50.0f);

		float yawRad = m_rot.z * EMath::DEG2RAD;
		float pitchRad = m_rot.x * EMath::DEG2RAD;
		m_pos.x = tpos.x + sinf(yawRad) * cosf(pitchRad) * m_orbitDist;
		m_pos.y = tpos.y - cosf(yawRad) * cosf(pitchRad) * m_orbitDist;
		m_pos.z = tpos.z - sinf(pitchRad) * m_orbitDist;
	}
	else {
		// Free fly
		m_rot.z -= lookLR * g_Config.FreeCamMouseSensitivity;
		m_rot.x = EMath::Clamp(m_rot.x - lookUD * g_Config.FreeCamMouseSensitivity, -89.0f, 89.0f);

		Vector3 fwdRot{};
		fwdRot.x = m_rot.x; fwdRot.z = m_rot.z;
		Vector3 forward = EMath::RotationToDirection(fwdRot);
		Vector3 flatRot{};
		flatRot.z = m_rot.z - 90.0f;
		Vector3 right = EMath::RotationToDirection(flatRot);

		m_pos = EMath::Add(m_pos, EMath::Scale(forward, -moveUD * speed));
		m_pos = EMath::Add(m_pos, EMath::Scale(right, moveLR * speed));

		if (IsKeyDown(VK_SPACE)) m_pos.z += speed;
		if (IsKeyDown('Z')) m_pos.z -= speed;
	}

	// Roll on Q/E, reset with R
	if (IsKeyDown('Q')) m_rot.y = EMath::Clamp(m_rot.y + 0.4f, -180.0f, 180.0f);
	if (IsKeyDown('E')) m_rot.y = EMath::Clamp(m_rot.y - 0.4f, -180.0f, 180.0f);
	if (IsKeyJustUp('R')) m_rot.y = 0.0f;

	// Lock-on tracking: keep pointing at the target (free-fly only)
	if (m_trackTarget && !m_orbit) {
		Ped target = ResolveTarget();
		Vector3 tpos = ENTITY::GET_ENTITY_COORDS(target, true, true);
		tpos.z += 0.6f;
		Vector3 dir = EMath::Normalize(EMath::Sub(tpos, m_pos));
		Vector3 aim = EMath::DirectionToRotation(dir);
		m_rot.x = aim.x;
		m_rot.z = aim.z;
	}

	CAM::SET_CAM_COORD(m_cam, m_pos.x, m_pos.y, m_pos.z);
	CAM::SET_CAM_ROT(m_cam, m_rot.x, m_rot.y, m_rot.z, 2);
	CAM::SET_CAM_FOV(m_cam, EMath::Clamp(m_fov, 5.0f, 120.0f));
	CAM::SET_CAM_MOTION_BLUR_STRENGTH(m_cam, m_motionBlur);

	// Streaming follows the camera so far-away compositions stay sharp
	Vector3 playerPos = ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true, true);
	if (EMath::Distance(m_pos, playerPos) > 100.0f) {
		STREAMING::SET_FOCUS_POS_AND_VEL(m_pos.x, m_pos.y, m_pos.z, 0.0f, 0.0f, 0.0f);
		STREAMING::REQUEST_COLLISION_AT_COORD(m_pos.x, m_pos.y, m_pos.z);
	}
	else {
		STREAMING::CLEAR_FOCUS();
	}
}

// Photo Mode's own DOF (the same 10-slot native parameter block R* uses)
struct PMDofParams
{
	ALIGN8 float focusDistance;
	ALIGN8 float aperture;
	ALIGN8 float blurDiameter;
	ALIGN8 float focalLength;
	ALIGN8 float focalLength2;
	ALIGN8 float focalLengthMax;
	ALIGN8 BOOL enabled;
	ALIGN8 BOOL flag7;
	ALIGN8 BOOL flag8;
	ALIGN8 BOOL flag9;
};

void CPhotoMode::ApplyDof()
{
	if (m_cam == 0) return;

	// The DOF parameter block carries its own focal length, which competes
	// with SET_CAM_FOV - applying it while blur is off silently locked the
	// Field Of View slider. Only touch the block when blur is actually
	// wanted (or when switching it back off after use).
	bool wantDof = (m_blurStrength > 0.0f);

	if (wantDof || m_dofApplied) {
		PMDofParams dof{};
		dof.focusDistance = m_focusDist;
		dof.aperture = 2.0f;
		dof.blurDiameter = 128.0f;
		float focal = 25.0f + EMath::Clamp(m_blurStrength, 0.0f, 1.0f) * 65.0f;
		dof.focalLength = focal;
		dof.focalLength2 = focal;
		dof.focalLengthMax = 90.0f;
		dof.enabled = wantDof ? TRUE : FALSE;
		dof.flag7 = FALSE;
		dof.flag8 = TRUE;
		dof.flag9 = TRUE;
		CAM::_0xE4B7945EF4F1BFB2(m_cam, (float*)&dof);
		m_dofApplied = wantDof;
	}

	CAM::_SET_CAM_FOCUS_DISTANCE(m_cam, m_focusDist);
	CAM::_PAUSE_CAMERA_FOCUS(m_cam, m_focusLock);
}

// ---------------------------------------------------------------------------
// Post
// ---------------------------------------------------------------------------

void CPhotoMode::ApplyGrade()
{
	if (m_gradeIdx > 0 && m_gradeIdx < (int)PMGrades.size() && PMGrades[m_gradeIdx].modifier) {
		GRAPHICS::SET_TIMECYCLE_MODIFIER(PMGrades[m_gradeIdx].modifier);
		GRAPHICS::SET_TIMECYCLE_MODIFIER_STRENGTH(m_gradeStrength);
	}
	else {
		GRAPHICS::CLEAR_TIMECYCLE_MODIFIER();
	}
}

void CPhotoMode::ApplyFilter(int newIdx)
{
	if (newIdx == m_activeFilterIdx) return;
	if (m_activeFilterIdx > 0 && m_activeFilterIdx < (int)PhotoModeFilters.size()) {
		GRAPHICS::ANIMPOSTFX_STOP(PhotoModeFilters[m_activeFilterIdx]);
	}
	if (newIdx > 0 && newIdx < (int)PhotoModeFilters.size()) {
		GRAPHICS::ANIMPOSTFX_PLAY(PhotoModeFilters[newIdx]);
	}
	m_activeFilterIdx = newIdx;
}

// ---------------------------------------------------------------------------
// Lighting
// ---------------------------------------------------------------------------

PMLight* CPhotoMode::SelectedLight()
{
	if (m_lightSel < 0 || m_lightSel >= PM_MAX_LIGHTS) return nullptr;
	if (!m_lights[m_lightSel].used) return nullptr;
	return &m_lights[m_lightSel];
}

// The placement marker is now a real prop: p_lightbulb01x. As well as showing
// the user exactly where the light sits in the world (replacing the old screen-
// space cross gizmo), its built-in entity light is switched on so the bulb
// actually glows - an active source in the scene, not just a marker. The
// scripted DRAW_LIGHT_WITH_RANGE in UpdateLights still does the heavy lifting
// for illumination; the bulb adds the visible filament/glow at the same spot.
static const char* kLightBulbModel = "p_lightbulb01x";

void CPhotoMode::SpawnLightBulb(PMLight& l)
{
	if (l.bulb != 0 && ENTITY::DOES_ENTITY_EXIST(l.bulb)) return;

	Hash model = MISC::GET_HASH_KEY(kLightBulbModel);
	if (!STREAMING::IS_MODEL_VALID(model)) return;
	STREAMING::REQUEST_MODEL(model, false);
	DWORD start = GetTickCount();
	while (!STREAMING::HAS_MODEL_LOADED(model)) {
		if (GetTickCount() - start > 3000) return;
		WAIT(10);
	}

	Vector3 p = LightWorldPos(l);
	Object bulb = OBJECT::CREATE_OBJECT(model, p.x, p.y, p.z, false, false, false, false, false);
	STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
	if (bulb == 0) return;

	ENTITY::SET_ENTITY_COLLISION(bulb, false, false);
	ENTITY::FREEZE_ENTITY_POSITION(bulb, true);

	// Turn the prop into a live light source matching the light's colour.
	ENTITY::_SET_ENTITY_LIGHTS_ENABLED(bulb, true);
	GRAPHICS::_SET_LIGHTS_COLOR_FOR_ENTITY(bulb, l.r, l.g, l.b);
	GRAPHICS::_SET_LIGHTS_INTENSITY_FOR_ENTITY(bulb, l.intensity);
	GRAPHICS::UPDATE_LIGHTS_ON_ENTITY(bulb);

	l.bulb = bulb;
}

void CPhotoMode::DestroyLightBulb(PMLight& l)
{
	if (l.bulb != 0 && ENTITY::DOES_ENTITY_EXIST(l.bulb)) {
		Object b = l.bulb;
		OBJECT::DELETE_OBJECT(&b);
	}
	l.bulb = 0;
}

void CPhotoMode::FreeLight(PMLight& l)
{
	DestroyLightBulb(l);
	l.used = false;
	l.attached = false;
	l.attachEntity = 0;
}

int CPhotoMode::AddLightAtCamera()
{
	for (int i = 0; i < PM_MAX_LIGHTS; i++) {
		if (!m_lights[i].used) {
			PMLight& l = m_lights[i];
			DestroyLightBulb(l); // in case a stale prop lingered on the slot
			l = PMLight{};
			l.used = true;
			// drop the light slightly ahead of the lens so it lights what you see
			Vector3 fwd = EMath::RotationToDirection(m_rot);
			l.pos = EMath::Add(m_pos, EMath::Scale(fwd, 1.5f));
			TemperatureToRGB(l.tempK, l.r, l.g, l.b);
			m_lightSel = i;
			SpawnLightBulb(l);
			return i;
		}
	}
	return -1;
}

void CPhotoMode::AttachSelectedToTarget()
{
	PMLight* l = SelectedLight();
	if (!l) return;

	Ped target = ResolveTarget();
	if (!ENTITY::DOES_ENTITY_EXIST(target)) return;

	if (l->attached) {
		// detach in place
		l->pos = LightWorldPos(*l);
		l->attached = false;
		l->attachEntity = 0;
		return;
	}

	Vector3 tpos = ENTITY::GET_ENTITY_COORDS(target, true, true);
	l->attachEntity = target;
	l->attachOffset = EMath::Sub(l->pos, tpos);
	// store heading-relative so the light keeps its side when the ped turns
	l->attachOffset = EMath::RotateZ(l->attachOffset, -ENTITY::GET_ENTITY_HEADING(target));
	l->attached = true;
}

Vector3 CPhotoMode::LightWorldPos(const PMLight& l) const
{
	if (l.attached && ENTITY::DOES_ENTITY_EXIST(l.attachEntity)) {
		Vector3 base = ENTITY::GET_ENTITY_COORDS(l.attachEntity, true, true);
		Vector3 off = EMath::RotateZ(l.attachOffset, ENTITY::GET_ENTITY_HEADING(l.attachEntity));
		return EMath::Add(base, off);
	}
	return l.pos;
}

// Classic key/fill/rim setup around the current target, attached so it tracks
void CPhotoMode::Apply3PointPreset()
{
	Ped target = ResolveTarget();
	if (!ENTITY::DOES_ENTITY_EXIST(target)) return;

	Vector3 tpos = ENTITY::GET_ENTITY_COORDS(target, true, true);
	float heading = ENTITY::GET_ENTITY_HEADING(target);

	struct P { float angle, dist, height, intensity, range, kelvin; };
	const P preset[3] = {
		{  45.0f, 2.2f, 1.7f, 5.0f, 7.0f, 4800.0f },  // key: warm, bright
		{ -60.0f, 2.6f, 1.4f, 2.0f, 7.0f, 7500.0f },  // fill: cool, soft
		{ 180.0f, 2.0f, 2.1f, 4.0f, 6.0f, 6500.0f },  // rim: neutral, behind
	};

	for (int i = 0; i < 3; i++) {
		int slot = AddLightAtCamera();
		if (slot < 0) return;
		PMLight& l = m_lights[slot];
		float a = (heading + preset[i].angle) * EMath::DEG2RAD;
		Vector3 off{};
		off.x = -sinf(a) * preset[i].dist;
		off.y = cosf(a) * preset[i].dist;
		off.z = preset[i].height;
		l.pos = EMath::Add(tpos, off);
		l.intensity = preset[i].intensity;
		l.range = preset[i].range;
		l.tempK = preset[i].kelvin;
		TemperatureToRGB(l.tempK, l.r, l.g, l.b);
		l.attachEntity = target;
		l.attachOffset = EMath::RotateZ(EMath::Sub(l.pos, tpos), -heading);
		l.attached = true;
	}
}

void CPhotoMode::UpdateLights()
{
	// The old DRAW_LIGHT_WITH_RANGE point lights / bulb gizmos and shadow lamps
	// have been replaced by the glowing light-prop system (UpdateArtLights). All
	// that remains here is the Rockstar light rig pinned to the subject
	// (cutscene-grade lighting with real shadows).
	HeroLight::Update(ResolveTarget(), m_rigLight);
}

// ---------------------------------------------------------------------------
// Shadow lamps: real lamp prop entities whose built-in light sources cast
// proper engine shadows - the only scripted path to shadowed point light.
// The physical lamp is visible, so place it just out of frame.
// ---------------------------------------------------------------------------

struct PMLampDef { const char* label; const char* model; };
static const std::vector<PMLampDef> PMLampModels = {
	{ "Lantern",       "p_lantern05x" },
	{ "Kerosene Lamp", "p_lampkerosene01x" },
	{ "Exterior Lamp", "p_lampexterior04x" },
};

void CPhotoMode::AddShadowLamp()
{
	if (m_lampCount >= PM_MAX_LAMPS) {
		UIUtil::PrintSubtitle("~COLOR_RED~All " + std::to_string(PM_MAX_LAMPS) + " lamp slots in use~s~");
		return;
	}

	Hash model = MISC::GET_HASH_KEY(PMLampModels[m_lampModelIdx].model);
	if (!STREAMING::IS_MODEL_VALID(model)) return;

	STREAMING::REQUEST_MODEL(model, false);
	DWORD start = GetTickCount();
	while (!STREAMING::HAS_MODEL_LOADED(model)) {
		if (GetTickCount() - start > 3000) return;
		WAIT(10);
	}

	Vector3 fwd = EMath::RotationToDirection(m_rot);
	Vector3 pos = EMath::Add(m_pos, EMath::Scale(fwd, 1.6f));

	Object lamp = OBJECT::CREATE_OBJECT(model, pos.x, pos.y, pos.z, false, false, false, false, false);
	STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
	if (lamp == 0) return;

	ENTITY::SET_ENTITY_COLLISION(lamp, false, false);
	ENTITY::FREEZE_ENTITY_POSITION(lamp, true);
	ENTITY::_SET_ENTITY_LIGHTS_ENABLED(lamp, true);
	GRAPHICS::_SET_LIGHTS_INTENSITY_FOR_ENTITY(lamp, m_lampIntensity);
	GRAPHICS::UPDATE_LIGHTS_ON_ENTITY(lamp);

	m_lamps[m_lampCount++] = lamp;
}

void CPhotoMode::RemoveLastLamp()
{
	if (m_lampCount <= 0) return;
	Object lamp = m_lamps[--m_lampCount];
	if (lamp != 0 && ENTITY::DOES_ENTITY_EXIST(lamp)) {
		OBJECT::DELETE_OBJECT(&lamp);
	}
	m_lamps[m_lampCount] = 0;
}

void CPhotoMode::RemoveAllLamps()
{
	while (m_lampCount > 0) {
		RemoveLastLamp();
	}
}

void CPhotoMode::ApplyLampSettings()
{
	for (int i = 0; i < m_lampCount; i++) {
		if (m_lamps[i] != 0 && ENTITY::DOES_ENTITY_EXIST(m_lamps[i])) {
			GRAPHICS::_SET_LIGHTS_INTENSITY_FOR_ENTITY(m_lamps[i], m_lampIntensity);
			GRAPHICS::UPDATE_LIGHTS_ON_ENTITY(m_lamps[i]);
		}
	}
}

// ---------------------------------------------------------------------------
// Artificial light props (csk_lights - our own LML asset pack, built from the
// stock p_lightbulb01x fixture with recolored, always-on light extensions).
// Each .ydr drawable (registered by csk_lights.ytyp via DLC_ITYP_REQUEST)
// carries its own baked light, so spawning one and enabling its entity light
// makes it glow. Each placed prop has a full position (camera-relative nudge) +
// rotation editor. Custom models are NOT in the base cdimage, so we don't gate
// on IS_MODEL_IN_CDIMAGE - we just request and wait, and report if it never
// loads (which means the csk_lights asset pack isn't installed/enabled).
// ---------------------------------------------------------------------------

// label, spawn model, and the RGB we FORCE on the entity light each frame (so
// the colour is driven by the mod, not the baked .ytyp - reliable regardless of
// how the asset turned out, and all 10 models could even be identical copies).
struct PMArtLightDef { const char* label; const char* model; int r, g, b; };
static const std::vector<PMArtLightDef> PMArtLightModels = {
	{ "White",      "csk_light_white",  255, 255, 255 },
	{ "Warm White", "csk_light_warm",   255, 214, 170 },
	{ "Blue",       "csk_light_blue",    70, 130, 255 },
	{ "Cyan",       "csk_light_cyan",    60, 220, 255 },
	{ "Green",      "csk_light_green",   80, 255, 120 },
	{ "Yellow",     "csk_light_yellow", 255, 230,  90 },
	{ "Orange",     "csk_light_orange", 255, 137,  56 },
	{ "Red",        "csk_light_red",    255,  55,  45 },
	{ "Pink",       "csk_light_pink",   255,  90, 170 },
	{ "Purple",     "csk_light_purple", 200,  90, 255 },
};

PMArtLight* CPhotoMode::SelectedArtLight()
{
	if (m_artSel < 0 || m_artSel >= PM_MAX_ARTLIGHTS) return nullptr;
	if (!m_artLights[m_artSel].used) return nullptr;
	return &m_artLights[m_artSel];
}

void CPhotoMode::ApplyArtLightTransform(int idx)
{
	if (idx < 0 || idx >= PM_MAX_ARTLIGHTS) return;
	PMArtLight& a = m_artLights[idx];
	if (a.obj == 0 || !ENTITY::DOES_ENTITY_EXIST(a.obj)) return;
	ENTITY::SET_ENTITY_COORDS(a.obj, a.pos.x, a.pos.y, a.pos.z, false, false, false, false);
	ENTITY::SET_ENTITY_ROTATION(a.obj, a.rot.x, a.rot.y, a.rot.z, 2, true);
}

int CPhotoMode::SpawnArtLight()
{
	int slot = -1;
	for (int i = 0; i < PM_MAX_ARTLIGHTS; i++) {
		if (!m_artLights[i].used) { slot = i; break; }
	}
	if (slot < 0) {
		UIUtil::PrintSubtitle("~COLOR_RED~All " + std::to_string(PM_MAX_ARTLIGHTS) + " light-prop slots in use~s~");
		return -1;
	}

	int mi = (m_artModelIdx >= 0 && m_artModelIdx < (int)PMArtLightModels.size()) ? m_artModelIdx : 0;
	Hash model = MISC::GET_HASH_KEY(PMArtLightModels[mi].model);

	STREAMING::REQUEST_MODEL(model, false);
	DWORD start = GetTickCount();
	while (!STREAMING::HAS_MODEL_LOADED(model)) {
		if (GetTickCount() - start > 4000) {
			UIUtil::PrintSubtitle("~COLOR_RED~'" + std::string(PMArtLightModels[mi].model) + "' failed to load~s~ - is the csk_lights asset pack installed in lml?");
			return -1;
		}
		WAIT(10);
	}

	// Drop it a couple of metres ahead of the lens, on the ground if close.
	Vector3 fwd = EMath::RotationToDirection(m_rot);
	Vector3 pos = EMath::Add(m_pos, EMath::Scale(fwd, 2.0f));

	Object obj = OBJECT::CREATE_OBJECT(model, pos.x, pos.y, pos.z, false, false, false, false, false);
	STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
	if (obj == 0) {
		UIUtil::PrintSubtitle("~COLOR_RED~Could not create the light prop~s~");
		return -1;
	}

	ENTITY::SET_ENTITY_COLLISION(obj, false, false);
	ENTITY::FREEZE_ENTITY_POSITION(obj, true);
	ENTITY::_SET_ENTITY_LIGHTS_ENABLED(obj, true);
	// Force colour + intensity from code rather than trusting the baked .ytyp.
	GRAPHICS::_SET_LIGHTS_COLOR_FOR_ENTITY(obj, PMArtLightModels[mi].r, PMArtLightModels[mi].g, PMArtLightModels[mi].b);
	GRAPHICS::_SET_LIGHTS_INTENSITY_FOR_ENTITY(obj, 12.0f);
	GRAPHICS::UPDATE_LIGHTS_ON_ENTITY(obj);

	PMArtLight& a = m_artLights[slot];
	a = PMArtLight{};
	a.used = true;
	a.obj = obj;
	a.modelIdx = mi;
	a.r = PMArtLightModels[mi].r;
	a.g = PMArtLightModels[mi].g;
	a.b = PMArtLightModels[mi].b;
	a.pos = pos;
	a.rot = Vector3{ 0.0f, 0.0f, m_rot.z }; // face away from the lens by default
	ApplyArtLightTransform(slot);
	m_artSel = slot;
	return slot;
}

void CPhotoMode::DestroyArtLight(int idx)
{
	if (idx < 0 || idx >= PM_MAX_ARTLIGHTS) return;
	PMArtLight& a = m_artLights[idx];
	if (a.obj != 0 && ENTITY::DOES_ENTITY_EXIST(a.obj)) {
		Object o = a.obj;
		OBJECT::DELETE_OBJECT(&o);
	}
	a = PMArtLight{};
}

void CPhotoMode::RemoveAllArtLights()
{
	for (int i = 0; i < PM_MAX_ARTLIGHTS; i++) DestroyArtLight(i);
}

void CPhotoMode::NudgeArtLight(float fwd, float right, float up)
{
	PMArtLight* a = SelectedArtLight();
	if (!a) return;
	// Move relative to the prop's own heading so "forward" is where it points.
	float h = a->rot.z * EMath::DEG2RAD;
	Vector3 f{ -sinf(h), cosf(h), 0.0f };
	Vector3 r{ cosf(h), sinf(h), 0.0f };
	a->pos = EMath::Add(a->pos, EMath::Scale(f, fwd));
	a->pos = EMath::Add(a->pos, EMath::Scale(r, right));
	a->pos.z += up;
	ApplyArtLightTransform(m_artSel);
}

void CPhotoMode::UpdateArtLights()
{
	bool hide = m_uiHidden || ScreenCapture::IsCapturing();
	// Daylight compensation: entity lights (like all scripted lights) are washed
	// out by the sun, so they "stop working" in daytime. Scale the intensity up
	// while the sun is high - the same curve the hero lights use (~1x night, ~6x
	// noon) - so the props stay visible at any time of day.
	float boost = HeroLight::DaylightBoost();
	for (int i = 0; i < PM_MAX_ARTLIGHTS; i++) {
		PMArtLight& a = m_artLights[i];
		if (!a.used) continue;
		if (a.obj == 0 || !ENTITY::DOES_ENTITY_EXIST(a.obj)) { a = PMArtLight{}; continue; }
		// Hide only the fixture MESH for a clean shot, never the entity: hiding
		// the whole entity (SET_ENTITY_VISIBLE false) also kills its baked light.
		// Fading the mesh to alpha 0 leaves the entity - and its light - active,
		// so the glow stays but the bulb prop vanishes.
		ENTITY::SET_ENTITY_VISIBLE(a.obj, true);
		ENTITY::SET_ENTITY_ALPHA(a.obj, hide ? 0 : 255, false);
		ENTITY::_SET_ENTITY_LIGHTS_ENABLED(a.obj, true);
		GRAPHICS::_SET_LIGHTS_COLOR_FOR_ENTITY(a.obj, a.r, a.g, a.b);
		GRAPHICS::_SET_LIGHTS_INTENSITY_FOR_ENTITY(a.obj, a.intensity * boost);
		GRAPHICS::UPDATE_LIGHTS_ON_ENTITY(a.obj);
	}
}

// ---------------------------------------------------------------------------
// Character visibility
// ---------------------------------------------------------------------------

void CPhotoMode::UpdateCharacterVisibility()
{
	Ped target = ResolveTarget();

	if (ENTITY::DOES_ENTITY_EXIST(target)) {
		ENTITY::SET_ENTITY_VISIBLE(target, !m_targetHidden);
	}

	if (m_hideOthers) {
		const int ARR_SIZE = 1024;
		int peds[ARR_SIZE];
		int found = worldGetAllPeds(peds, ARR_SIZE);
		Ped player = PLAYER::PLAYER_PED_ID();
		for (int i = 0; i < found; i++) {
			Ped ped = peds[i];
			if (ped == target || ped == player || !ENTITY::DOES_ENTITY_EXIST(ped)) continue;
			if (std::find(m_hiddenPeds.begin(), m_hiddenPeds.end(), ped) == m_hiddenPeds.end()) {
				ENTITY::SET_ENTITY_VISIBLE(ped, false);
				m_hiddenPeds.push_back(ped);
			}
		}
	}
	else if (!m_hiddenPeds.empty()) {
		for (Ped ped : m_hiddenPeds) {
			if (ENTITY::DOES_ENTITY_EXIST(ped)) ENTITY::SET_ENTITY_VISIBLE(ped, true);
		}
		m_hiddenPeds.clear();
	}
}

// ---------------------------------------------------------------------------
// Native menu pages (RDR2 Native Menu Base)
//
// Each Photo Mode tab is a submenu. Sliders follow the editor-menu convention
// (a VectorOption over a generated value list + FloatRange/FloatIndex). Pages
// are rebuilt on entry so they open showing the current state. Photo Mode owns
// input, so navigation/sounds are driven from HandleInput, not the menu itself.
// ---------------------------------------------------------------------------

namespace
{
	std::vector<std::string> PMRange(float start, float end, float step, int decimals)
	{
		std::vector<std::string> out;
		char buf[32];
		for (float v = start; v <= end + step * 0.5f; v += step) {
			sprintf_s(buf, "%.*f", decimals, v);
			out.emplace_back(buf);
		}
		return out;
	}
	int PMIdx(float value, float start, float step, int count)
	{
		int idx = (int)((value - start) / step + 0.5f);
		if (idx < 0) idx = 0;
		if (idx > count - 1) idx = count - 1;
		return idx;
	}
	float PMFromIdx(int idx, float start, float step) { return start + (float)idx * step; }

	// Current selected VectorOption's index (used inside option funcs).
	int CurIdx()
	{
		Option* o = g_Menu->GetSelectedOption();
		return (o && o->IsVectorOption) ? o->As<VectorOption*>()->GetVectorIndex() : 0;
	}

	// Mirror bools for toggles whose setter has a guard / indirection.
	bool sPMFreeze = false;
	bool sPMMute = true;
	bool sPMMusic = false;
	bool sPMNoBars = false;
}

// aspect-frame names live in PhotoModeUI.cpp
extern const int PMAspectCount;
extern const char* PMAspectName(int idx);

void CPhotoMode::ShowTabTip(int bit, const char* text)
{
	const int mask = 1 << bit;
	if (g_Config.TipsSeenMask & mask) return; // already shown once
	UIUtil::Notify::ShowTip(text);
	g_Config.TipsSeenMask |= mask;
	g_Config.Save(); // persist so the tip never nags again
}

void CPhotoMode::BuildPhotoMenus()
{
	// Entry page: links to each tab + quick actions. The first time each tab is
	// opened, a one-time tip card explains the feature and how to use it for shots.
	g_Menu->AddSubmenu("PHOTO MODE", "Director's Suite Photo Mode", Submenu_PhotoMode, 13, [this](Submenu* sub)
	{
		sub->AddRegularOption("Camera", "Lens, framing, depth of field and auto-focus", [this] { RebuildCameraPage();    g_Menu->GoToSubmenu(Submenu_PhotoMode_Camera); ShowTabTip(0, "Camera: fly the lens freely, orbit a subject, and dial in FOV, roll and depth of field. Frame the exact angle and focus for your shot."); });
		sub->AddRegularOption("World", "Freeze time, set time of day, weather and the moon", [this] { RebuildWorldPage();    g_Menu->GoToSubmenu(Submenu_PhotoMode_World); ShowTabTip(1, "World: freeze time, and set the time of day, weather and moon. Lock the exact moment you want to capture."); });
		sub->AddRegularOption("Character", "Pick a subject, pose, expressions, become a character", [this] { RebuildCharacterPage(); g_Menu->GoToSubmenu(Submenu_PhotoMode_Character); ShowTabTip(2, "Character: pick any nearby person or animal as your subject, then pose them, set an expression, look at camera, or become a character. Direct who is in frame."); });
		sub->AddRegularOption("Lighting", "Scene lights (X/Y/Z), the sun (azimuth/elevation), Rockstar & 3-point hero rigs", [this] { RebuildLightingPage(); g_Menu->GoToSubmenu(Submenu_PhotoMode_Lighting); ShowTabTip(3, "Lighting: position scene lights by X/Y/Z, aim the sun with azimuth/elevation, and add the Rockstar or 3-point hero rig. Sculpt mood, highlights and shadows on your subject."); });
		sub->AddRegularOption("Post", "Color grades, motion blur, aspect frames, grid", [this] { RebuildPostPage();     g_Menu->GoToSubmenu(Submenu_PhotoMode_Post); ShowTabTip(4, "Post: apply color grades, motion blur, aspect-ratio bars and a grid. Give the shot a cinematic look and clean composition."); });
		sub->AddRegularOption("Effects", "Photo filters and lossless / HDR screenshots", [this] { RebuildEffectsPage();  g_Menu->GoToSubmenu(Submenu_PhotoMode_Effects); ShowTabTip(5, "Effects: stack authentic photo filters and take a lossless or HDR screenshot - a final stylized pass right before you capture."); });
		sub->AddRegularOption("Scene Editor (Beta)", "Place props, direct actors with scenarios, and make temporary world edits", [this] { RebuildScenePage();     g_Menu->GoToSubmenu(Submenu_PhotoMode_Scene); ShowTabTip(6, "Scene Editor: stage a whole scene - place props, spawn and direct actors with scenarios, and make temporary world edits. Build the moment your shot needs."); });
		sub->AddRegularOption("Music", "Background track and world-audio mute", [this] { RebuildMusicPage();    g_Menu->GoToSubmenu(Submenu_PhotoMode_Music); ShowTabTip(7, "Music: choose a background track and mute world audio. Set the mood while you compose and record."); });
		sub->AddRegularOption("Take Screenshot (Lossless/HDR)", "Saves exactly what is on screen. Also bound to F3", [] {
			if (!ScreenCapture::IsCapturing()) { ScreenCapture::RequestCapture(); PhotoAudio::Play(PhotoAudio::PM_SHUTTER); }
		});
		sub->AddEmptyOption("- Settings & Credits -");
		sub->AddRegularOption("Settings", "Free-cam speed and look sensitivity (shared with the main menu)", [this] { RebuildSettingsPage(); g_Menu->GoToSubmenu(Submenu_PhotoMode_Settings); ShowTabTip(8, "Settings: tune free-cam speed and look sensitivity for smooth, precise camera moves."); });
		sub->AddRegularOption("Credits", "The people behind Director's Suite", [this] { RebuildCreditsPage(); g_Menu->GoToSubmenu(Submenu_PhotoMode_Credits); });
		sub->AddRegularOption("Exit Photo Mode", "Close the editor and restore the scene", [this] { Deactivate(); });
	});

	// Build each tab page once so it exists; they are rebuilt on entry.
	RebuildCameraPage();
	RebuildWorldPage();
	RebuildCharacterPage();
	RebuildLightingPage();
	RebuildPostPage();
	RebuildEffectsPage();
	RebuildMusicPage();
	RebuildScenePage();
	RebuildSettingsPage();
	RebuildCreditsPage();
}

void CPhotoMode::RebuildSettingsPage()
{
	g_Menu->AddSubmenu("PHOTO MODE", "Settings", Submenu_PhotoMode_Settings, 10, [this](Submenu* sub)
	{
		// Free-cam movement settings - the same g_Config values the main Director's
		// Suite Settings page uses, so changes carry over both ways. Photo Mode's
		// fly reads these live (see UpdateCamera).
		auto speeds = PMRange(0.05f, 2.0f, 0.05f, 2);
		sub->AddVectorOption("Free Cam Speed", "Base movement speed of the Photo Mode camera", speeds, [] {
			g_Config.FreeCamSpeed = PMFromIdx(CurIdx(), 0.05f, 0.05f);
		})->SetVectorIndex(PMIdx(g_Config.FreeCamSpeed, 0.05f, 0.05f, (int)speeds.size()));

		auto fastMult = PMRange(1.0f, 12.0f, 0.5f, 1);
		sub->AddVectorOption("Fast Speed (Shift)", "Speed multiplier while holding Shift to accelerate", fastMult, [] {
			g_Config.FreeCamFastMultiplier = PMFromIdx(CurIdx(), 1.0f, 0.5f);
		})->SetVectorIndex(PMIdx(g_Config.FreeCamFastMultiplier, 1.0f, 0.5f, (int)fastMult.size()));

		auto slowMult = PMRange(0.05f, 1.0f, 0.05f, 2);
		sub->AddVectorOption("Slow Speed (Ctrl)", "Speed multiplier while holding Ctrl for precise moves", slowMult, [] {
			g_Config.FreeCamSlowMultiplier = PMFromIdx(CurIdx(), 0.05f, 0.05f);
		})->SetVectorIndex(PMIdx(g_Config.FreeCamSlowMultiplier, 0.05f, 0.05f, (int)slowMult.size()));

		auto sens = PMRange(0.5f, 10.0f, 0.5f, 1);
		sub->AddVectorOption("Look Sensitivity", "Mouse / stick sensitivity for the camera", sens, [] {
			g_Config.FreeCamMouseSensitivity = PMFromIdx(CurIdx(), 0.5f, 0.5f);
		})->SetVectorIndex(PMIdx(g_Config.FreeCamMouseSensitivity, 0.5f, 0.5f, (int)sens.size()));

		sub->AddBoolOption("High Detail Streaming", "Stream the world around the camera for full detail anywhere", &g_Config.FreeCamHighDetail);

		sub->AddRegularOption("Save Settings To INI", "Persist these values to DirectorsSuite.ini", [] {
			g_Config.Save();
			UIUtil::PrintSubtitle("Settings saved");
		});
	});
}

void CPhotoMode::RebuildCreditsPage()
{
	g_Menu->AddSubmenu("PHOTO MODE", "Credits", Submenu_PhotoMode_Credits, 12, [this](Submenu* sub)
	{
		struct Credit { const char* name; const char* role; };
		const Credit credits[] = {
			{ "videotech",        "Mod Creator" },
			{ "Alexander Blade",  "ScriptHookRDR SDK" },
			{ "Disquse",          "Timecycle Editor" },
			{ "Cfx.re",           "Timecycle Editor" },
			{ "Halen84",          "Script Research & UI Base" },
			{ "kepmehz",          "Script Research" },
			{ "femga",            "Game Research" },
			{ "TheNathanNS",      "QA" },
			{ "WesternSpace",     "Patcher for black bars in Photo Mode for ultra-wide displays" },
			{ "Claude (Fable 5)", "Research, Code, Bug Fixes" },
		};
		for (const auto& c : credits) {
			sub->AddRegularOption(c.name, c.role);
		}
		sub->AddEmptyOption(" ");
		sub->AddEmptyOption("Thank you to everyone who made");
		sub->AddEmptyOption("Director's Suite possible.");
	});
}

void CPhotoMode::RebuildCameraPage()
{
	g_Menu->AddSubmenu("PHOTO MODE", "Camera", Submenu_PhotoMode_Camera, 10, [this](Submenu* sub)
	{
		auto fovs = PMRange(5.0f, 120.0f, 1.0f, 0);
		sub->AddVectorOption("Field Of View", "Lower = more zoom. Updates the lens live", fovs, [this] {
			m_fov = PMFromIdx(CurIdx(), 5.0f, 1.0f);
			if (m_cam != 0) CAM::SET_CAM_FOV(m_cam, m_fov);
		})->SetVectorIndex(PMIdx(m_fov, 5.0f, 1.0f, (int)fovs.size()));

		auto rolls = PMRange(-180.0f, 180.0f, 1.0f, 0);
		sub->AddVectorOption("Roll", "Dutch angle (also Q/E while flying; R resets)", rolls, [this] {
			m_rot.y = PMFromIdx(CurIdx(), -180.0f, 1.0f);
		})->SetVectorIndex(PMIdx(m_rot.y, -180.0f, 1.0f, (int)rolls.size()));

		auto sens = PMRange(0.5f, 10.0f, 0.5f, 1);
		sub->AddVectorOption("Look Sensitivity", "Mouse / stick speed (saved on exit)", sens, [] {
			g_Config.FreeCamMouseSensitivity = PMFromIdx(CurIdx(), 0.5f, 0.5f);
		})->SetVectorIndex(PMIdx(g_Config.FreeCamMouseSensitivity, 0.5f, 0.5f, (int)sens.size()));

		sub->AddBoolOption("Orbit Target", "Pivot around the subject. Mouse orbits, W/S dollies", &m_orbit, [this] {
			if (m_orbit) {
				Vector3 tpos = ENTITY::GET_ENTITY_COORDS(ResolveTarget(), true, true);
				m_orbitDist = (std::max)(1.5f, EMath::Distance(m_pos, tpos));
			}
		});
		sub->AddBoolOption("Track Target", "Lock-on: the camera keeps pointing at the subject", &m_trackTarget);
		sub->AddBoolOption("Auto Focus Subject", "Keep the focal plane on the subject. Raise Blur to see it", &m_autoFocus, [this] { m_dofDirty = true; });

		auto focus = PMRange(0.3f, 150.0f, 0.3f, 1);
		sub->AddVectorOption("Focus Distance", "Focal-plane distance (auto while Auto Focus is on)", focus, [this] {
			m_focusDist = PMFromIdx(CurIdx(), 0.3f, 0.3f); m_dofDirty = true;
		})->SetVectorIndex(PMIdx(m_focusDist, 0.3f, 0.3f, (int)focus.size()));

		auto blur = PMRange(0.0f, 1.0f, 0.05f, 2);
		sub->AddVectorOption("Blur Intensity", "Depth of field strength. Reads stronger at night", blur, [this] {
			m_blurStrength = PMFromIdx(CurIdx(), 0.0f, 0.05f); m_dofDirty = true;
		})->SetVectorIndex(PMIdx(m_blurStrength, 0.0f, 0.05f, (int)blur.size()));

		sub->AddBoolOption("Focus Lock", "Freeze autofocus so the focal plane stays put", &m_focusLock, [this] { m_dofDirty = true; });
	});
}

void CPhotoMode::RebuildWorldPage()
{
	sPMFreeze = m_frozen;
	g_Menu->AddSubmenu("PHOTO MODE", "World", Submenu_PhotoMode_World, 12, [this](Submenu* sub)
	{
		sub->AddBoolOption("Freeze World", "Halt the whole world; the camera stays free", &sPMFreeze, [this] { SetFrozen(sPMFreeze); });

		sub->AddVectorOption("Freeze Method", "Time Scale keeps your full screen ratio; native freeze crops ultrawide", std::vector<const char*>{ "Time Scale", "Photo Mode Native" }, [this] {
			bool wasFrozen = m_frozen;
			if (wasFrozen) SetFrozen(false);
			m_freezeMethod = CurIdx() % 2;
			if (wasFrozen) { sPMFreeze = true; SetFrozen(true); }
		})->SetVectorIndex(m_freezeMethod);

		sub->AddRegularOption("Frame Forward", "Advance the frozen world by a single step", [this] { FrameForward(); });

		// Engine-level removal of the native photo-mode 16:9 crop (the black bars
		// on ultrawide). This patches the live game image in memory - global and
		// build-dependent - so it reports back whether it actually took.
		sPMNoBars = BlackBarsPatch::IsApplied();
		sub->AddBoolOption("Remove Black Bars (engine patch)", "Patches RDR2 in memory to drop the photo-mode 16:9 crop on ultrawide. Global; depends on game build", &sPMNoBars, [] {
			bool ok = sPMNoBars ? BlackBarsPatch::Apply() : BlackBarsPatch::Revert();
			sPMNoBars = BlackBarsPatch::IsApplied();
			UIUtil::PrintSubtitle((ok || !sPMNoBars ? "~COLOR_GREEN~" : "~COLOR_RED~") + std::string(BlackBarsPatch::LastResult()) + "~s~");
		});

		float curTod = (float)CLOCK::GET_CLOCK_HOURS() + (float)CLOCK::GET_CLOCK_MINUTES() / 60.0f;
		auto tod = PMRange(0.0f, 23.75f, 0.25f, 2);
		sub->AddVectorOption("Time Of Day", "Drag through the whole day. Golden hour is your friend", tod, [] {
			float v = PMFromIdx(CurIdx(), 0.0f, 0.25f);
			CLOCK::SET_CLOCK_TIME((int)v, (int)((v - (int)v) * 60.0f + 0.5f), 0);
		})->SetVectorIndex(PMIdx(curTod, 0.0f, 0.25f, (int)tod.size()));

		auto moon = PMRange(0.0f, 1.0f, 0.05f, 2);
		sub->AddVectorOption("Moon Override", "Force the moon cycle for night shots (0 = default)", moon, [this] {
			m_moonOverride = PMFromIdx(CurIdx(), 0.0f, 0.05f);
			GRAPHICS::ENABLE_MOON_CYCLE_OVERRIDE(m_moonOverride);
		})->SetVectorIndex(PMIdx(m_moonOverride, 0.0f, 0.05f, (int)moon.size()));

		// (Sun Azimuth / Sun Elevation moved to the Lighting page.)

		sub->AddVectorOption("Weather", "'No Change' restores the original; pick a type to force it", WeatherTypeNames, [this] {
			m_weatherIdx = CurIdx();
			if (m_weatherIdx > 0) {
				MISC::SET_WEATHER_TYPE(MISC::GET_HASH_KEY(WeatherTypeNames[m_weatherIdx]), true, true, false, 0.0f, false);
			}
			else if (m_weatherSaved) {
				MISC::CLEAR_OVERRIDE_WEATHER();
				MISC::SET_CURR_WEATHER_STATE(m_savedWeather1, m_savedWeather2, m_savedWeatherPct, true);
			}
		})->SetVectorIndex(m_weatherIdx);

		sub->AddBoolOption("Hide HUD", "Hide the game HUD while composing", &m_hideHud);
		sub->AddBoolOption("Invincible Player", "Keep the player unkillable while editing", &m_invincible);
		sub->AddBoolOption("Cutscene Unlock", "Keep the photo camera free during cutscenes", &m_cutsceneUnlock);
	});
}

void CPhotoMode::RebuildCharacterPage()
{
	// Capture nearby peds so the Subject choice can map index -> ped.
	m_subjectList.clear();
	m_subjectList.push_back(0); // index 0 = player
	std::vector<std::string> subjectNames; subjectNames.push_back("Player");
	{
		const int ARR = 1024; int peds[ARR];
		int found = worldGetAllPeds(peds, ARR);
		Ped player = PLAYER::PLAYER_PED_ID();
		Vector3 c = ENTITY::GET_ENTITY_COORDS(player, true, true);
		struct Cand { Ped ped; float d; };
		std::vector<Cand> list;
		for (int i = 0; i < found; i++) {
			if (peds[i] == player || !ENTITY::DOES_ENTITY_EXIST(peds[i])) continue;
			float d = EMath::Distance(c, ENTITY::GET_ENTITY_COORDS(peds[i], true, true));
			if (d < 60.0f) list.push_back({ peds[i], d });
		}
		std::sort(list.begin(), list.end(), [](const Cand& a, const Cand& b) { return a.d < b.d; });
		for (auto& cd : list) {
			m_subjectList.push_back(cd.ped);
			subjectNames.push_back(PedNameForModel(ENTITY::GET_ENTITY_MODEL(cd.ped)) ? PedNameForModel(ENTITY::GET_ENTITY_MODEL(cd.ped))
				: (PED::IS_PED_HUMAN(cd.ped) ? "Nearby Person" : "Animal"));
		}
	}
	int curSubject = 0;
	for (int i = 0; i < (int)m_subjectList.size(); i++) if (m_subjectList[i] == m_target) { curSubject = i; break; }

	g_Menu->AddSubmenu("PHOTO MODE", "Character", Submenu_PhotoMode_Character, 11, [this, subjectNames, curSubject](Submenu* sub)
	{
		sub->AddVectorOption("Subject", "Cycle nearby characters (a marker floats over them)", subjectNames, [this] {
			int i = CurIdx();
			m_target = (i >= 0 && i < (int)m_subjectList.size()) ? m_subjectList[i] : 0;
			m_poseIdx = 0; m_facialIdx = 0; m_poseActive = false; m_targetHidden = false;
		})->SetVectorIndex(curSubject);

		std::vector<std::string> poseNames; for (const auto& p : PMPoses) poseNames.push_back(p.label);
		sub->AddVectorOption("Pose", "Poses play while UNFROZEN; freeze once it looks right", poseNames, [this] {
			m_poseIdx = CurIdx();
			Ped tg = ResolveTarget();
			TASK::CLEAR_PED_TASKS(tg, true, false);
			if (m_poseIdx > 0 && PMPoses[m_poseIdx].scenario) {
				TASK::TASK_START_SCENARIO_IN_PLACE_HASH(tg, MISC::GET_HASH_KEY(PMPoses[m_poseIdx].scenario), -1, true, 0, ENTITY::GET_ENTITY_HEADING(tg), false);
				m_poseActive = true;
			}
			else m_poseActive = false;
		})->SetVectorIndex(m_poseIdx);

		sub->AddVectorOption("Expression", "Facial mood for the subject (needs an unfrozen world)", FacialMoods, [this] {
			m_facialIdx = CurIdx();
			Ped tg = ResolveTarget();
			if (m_facialIdx > 0) PED::SET_FACIAL_IDLE_ANIM_OVERRIDE(tg, FacialMoods[m_facialIdx], 0);
			else PED::CLEAR_FACIAL_IDLE_ANIM_OVERRIDE(tg);
		})->SetVectorIndex(m_facialIdx);

		sub->AddRegularOption("Look At Camera", "The subject turns to the lens for ~8 seconds", [this] {
			Ped tg = ResolveTarget();
			TASK::TASK_LOOK_AT_COORD(tg, m_pos.x, m_pos.y, m_pos.z, 8000, 0, 0, 0);
		});
		sub->AddRegularOption("Clear Pose & Look", "Reset the subject to natural behaviour", [this] {
			ClearSubjectAnim(ResolveTarget()); m_poseIdx = 0; m_facialIdx = 0; m_poseActive = false;
		});

		sub->AddBoolOption("Holster Player Weapon", "ON puts the player's weapon away so the body doesn't glitch under the camera. Turn OFF to show the weapon in hand", &m_holsterPlayer, [this] {
			ApplyPlayerHolster();
		});

		sub->AddVectorOption("Weapon Action", "Pose the subject with its weapon: aim or fire. Firing needs a live or slow world for the muzzle flash (Frame Forward works)", std::vector<const char*>{ "None", "Aim Forward", "Aim At Camera", "Fire Forward" }, [this] {
			m_weaponAction = CurIdx();
			ApplyWeaponAction();
		})->SetVectorIndex(m_weaponAction);

		std::vector<std::string> become; become.push_back("Default (Arthur/John)");
		for (const auto& p : PMSwapModels) become.push_back(p.label);
		sub->AddVectorOption("Become Character", "Stand in as any story character (decoy; restored on exit)", become, [this] {
			m_swapIdx = CurIdx();
			ApplyCharacterSwap();
		})->SetVectorIndex(m_swapIdx);

		sub->AddBoolOption("Hide Subject", "Make the selected character invisible", &m_targetHidden);
		sub->AddBoolOption("Hide Everyone Else", "Isolate the subject: every other ped invisible", &m_hideOthers);
	});
}

// Lighting page - rebuilt around the new glowing light-prop system. The old
// DRAW_LIGHT_WITH_RANGE point lights, 3-point preset and shadow lamps are gone;
// only the Rockstar light rig is kept alongside the new props.
void CPhotoMode::RebuildLightingPage()
{
	g_Menu->AddSubmenu("PHOTO MODE", "Lighting", Submenu_PhotoMode_Lighting, 10, [this](Submenu* sub)
	{
		// === Section 1: Rockstar light rig (pinned to the subject, real shadows) ===
		// (Empty-option text starting with '-' renders as a centered title.)
		sub->AddEmptyOption("- LIGHT RIG -");
		std::vector<std::string> rigs; rigs.push_back("Off");
		for (const auto& r : HeroLightRigs) rigs.push_back(r);
		sub->AddVectorOption("Rockstar Light Rig", "The game's cutscene light rig pinned to the subject - real shadows", rigs, [this] {
			int idx = CurIdx();
			m_rigLight.rigEnabled = (idx > 0);
			if (idx > 0) m_rigLight.rigIndex = idx - 1;
		})->SetVectorIndex(m_rigLight.rigEnabled ? m_rigLight.rigIndex + 1 : 0);

		// === Section 2: Sun (the scene's key light). Only when the timecycle
		// engine resolved the sun vars; 1-degree steps, reverted on exit. ===
		if (TimecycleRT::VarsAvailable() && m_sunVarX >= 0 && m_sunVarY >= 0) {
			// Until the user moves the sun themselves, keep the controls showing the
			// real time-of-day position (so changing time on the World tab is
			// reflected here too).
			if (!m_sunUserEdited) ComputeSunFromClock();

			sub->AddEmptyOption("- SUN -");

			auto az = PMRange(0.0f, 360.0f, 1.0f, 0);
			sub->AddVectorOption("Sun Direction", "Turn the sun around you - which way it shines from. Starts at the current time-of-day position. Reverted on exit", az, [this] {
				m_sunAzimuth = PMFromIdx(CurIdx(), 0.0f, 1.0f); m_sunUserEdited = true; ApplySunDirection();
			})->SetVectorIndex(PMIdx(m_sunAzimuth, 0.0f, 1.0f, (int)az.size()));

			auto el = PMRange(-90.0f, 90.0f, 1.0f, 0);
			sub->AddVectorOption("Sun Height", "Raise or lower the sun. Below 0 drops it under the horizon. Reverted on exit", el, [this] {
				m_sunElevation = PMFromIdx(CurIdx(), -90.0f, 1.0f); m_sunUserEdited = true; ApplySunDirection();
			})->SetVectorIndex(PMIdx(m_sunElevation, -90.0f, 1.0f, (int)el.size()));
		}

		// === Section 3: Scene lights (the new glowing light-prop system) ===
		sub->AddEmptyOption("- SCENE LIGHTS -");
		std::vector<std::string> models; for (const auto& m : PMArtLightModels) models.push_back(m.label);
		sub->AddVectorOption("Light Colour", "Colour spawned by 'Spawn Light' (always-on; works day or night)", models, [this] {
			m_artModelIdx = CurIdx();
		})->SetVectorIndex(m_artModelIdx);

		sub->AddRegularOption("Spawn Light", "Place a glowing light in front of the lens (8 max)", [this] { SpawnArtLight(); });

		{
			RegularOption* del = sub->AddRegularOption("Delete Selected Light", "Remove the selected light", [this] { if (SelectedArtLight()) DestroyArtLight(m_artSel); });
			del->TextR = 204; del->TextG = 40; del->TextB = 40;
		}
		sub->AddRegularOption("Remove All Lights", "Delete every placed light", [this] { RemoveAllArtLights(); });

		std::vector<std::string> slots; for (int i = 0; i < PM_MAX_ARTLIGHTS; i++) slots.push_back("Light " + std::to_string(i + 1));
		sub->AddVectorOption("Selected Light", "Which placed light the editor targets (its gizmo is highlighted in-world)", slots, [this] { m_artSel = CurIdx(); })->SetVectorIndex(m_artSel);

		sub->AddRegularOption("Move To Camera", "Reposition the selected light to the lens", [this] {
			if (PMArtLight* a = SelectedArtLight()) {
				Vector3 fwd = EMath::RotationToDirection(m_rot);
				a->pos = EMath::Add(m_pos, EMath::Scale(fwd, 2.0f));
				ApplyArtLightTransform(m_artSel);
			}
		});

		PMArtLight* selI = SelectedArtLight();
		auto intens = PMRange(0.0f, 40.0f, 0.5f, 1);
		sub->AddVectorOption("Intensity", "Brightness of the selected light (0 = off)", intens, [this] {
			if (PMArtLight* a = SelectedArtLight()) {
				a->intensity = PMFromIdx(CurIdx(), 0.0f, 0.5f);
				GRAPHICS::_SET_LIGHTS_INTENSITY_FOR_ENTITY(a->obj, a->intensity);
				GRAPHICS::UPDATE_LIGHTS_ON_ENTITY(a->obj);
			}
		})->SetVectorIndex(PMIdx(selI ? selI->intensity : 12.0f, 0.0f, 0.5f, (int)intens.size()));

		// X/Y/Z value positioning for the selected light (shared with the Scene
		// Editor's object/actor placement). Step sizes come from Move/Rotate Step.
		PMArtLight* sel = SelectedArtLight();
		Vector3 lp = sel ? sel->pos : Vector3{};
		Vector3 lr = sel ? sel->rot : Vector3{};

		SceneStep::AddStepSelectors(sub);

		SceneStep::AddStepper(sub, "Position X", "World X (left/right adjusts by Move Step)", lp.x, 3, [this](int d) -> float {
			PMArtLight* a = SelectedArtLight(); if (!a) return 0.0f;
			a->pos.x += SceneStep::MoveStep() * d; ApplyArtLightTransform(m_artSel); return a->pos.x;
		});
		SceneStep::AddStepper(sub, "Position Y", "World Y", lp.y, 3, [this](int d) -> float {
			PMArtLight* a = SelectedArtLight(); if (!a) return 0.0f;
			a->pos.y += SceneStep::MoveStep() * d; ApplyArtLightTransform(m_artSel); return a->pos.y;
		});
		SceneStep::AddStepper(sub, "Position Z", "World Z (height)", lp.z, 3, [this](int d) -> float {
			PMArtLight* a = SelectedArtLight(); if (!a) return 0.0f;
			a->pos.z += SceneStep::MoveStep() * d; ApplyArtLightTransform(m_artSel); return a->pos.z;
		});

		SceneStep::AddStepper(sub, "Rotation X (Pitch)", "Tilt up/down", lr.x, 1, [this](int d) -> float {
			PMArtLight* a = SelectedArtLight(); if (!a) return 0.0f;
			a->rot.x += SceneStep::RotStep() * d; ApplyArtLightTransform(m_artSel); return a->rot.x;
		});
		SceneStep::AddStepper(sub, "Rotation Y (Roll)", "Bank left/right", lr.y, 1, [this](int d) -> float {
			PMArtLight* a = SelectedArtLight(); if (!a) return 0.0f;
			a->rot.y += SceneStep::RotStep() * d; ApplyArtLightTransform(m_artSel); return a->rot.y;
		});
		SceneStep::AddStepper(sub, "Rotation Z (Yaw)", "Rotate around vertical", lr.z, 1, [this](int d) -> float {
			PMArtLight* a = SelectedArtLight(); if (!a) return 0.0f;
			a->rot.z += SceneStep::RotStep() * d; ApplyArtLightTransform(m_artSel); return a->rot.z;
		});
	});
}

void CPhotoMode::RebuildPostPage()
{
	g_Menu->AddSubmenu("PHOTO MODE", "Post", Submenu_PhotoMode_Post, 10, [this](Submenu* sub)
	{
		std::vector<std::string> grades; for (const auto& g : PMGrades) grades.push_back(g.label);
		sub->AddVectorOption("Color Grade", "Real in-game grading looks; includes grain, vignette, lens", grades, [this] {
			m_gradeIdx = CurIdx(); ApplyGrade();
		})->SetVectorIndex(m_gradeIdx);

		auto strength = PMRange(0.05f, 1.0f, 0.05f, 2);
		sub->AddVectorOption("Grade Strength", "Blend of the selected grade", strength, [this] {
			m_gradeStrength = PMFromIdx(CurIdx(), 0.05f, 0.05f); ApplyGrade();
		})->SetVectorIndex(PMIdx(m_gradeStrength, 0.05f, 0.05f, (int)strength.size()));

		auto mblur = PMRange(0.0f, 1.0f, 0.05f, 2);
		sub->AddVectorOption("Motion Blur", "Camera motion blur for movement shots", mblur, [this] {
			m_motionBlur = PMFromIdx(CurIdx(), 0.0f, 0.05f);
		})->SetVectorIndex(PMIdx(m_motionBlur, 0.0f, 0.05f, (int)mblur.size()));

		std::vector<std::string> aspects; for (int i = 0; i < PMAspectCount; i++) aspects.push_back(PMAspectName(i));
		sub->AddVectorOption("Aspect Frame", "Crop bars for any format; drawn by the mod (full ratio respected)", aspects, [this] {
			m_aspectIdx = CurIdx();
		})->SetVectorIndex(m_aspectIdx);

		// R*'s photo-mode render path + exposure/contrast trims (see ApplyEnhancedRender).
		// It only engages under the native Photo Mode freeze, so enabling it
		// switches the freeze method to it (and freezes) automatically.
		sub->AddBoolOption("Enhanced Render (R* Quality)", "Extra ambient occlusion + detail. Auto-switches to the Photo Mode freeze it needs. May show 16:9 bars on ultrawide unless the black-bars patch is on", &m_enhancedRender, [this] {
			if (!m_enhancedRender) return; // turning off: leave the freeze as-is
			bool wasFrozen = m_frozen;
			if (wasFrozen) SetFrozen(false);   // release the old freeze method first
			m_freezeMethod = 1;                // Photo Mode Native
			sPMFreeze = true;
			SetFrozen(true);
			UIUtil::PrintSubtitle("~COLOR_GREEN~Photo Mode freeze engaged for Enhanced Render~s~");
		});

		// Exposure lock (default ON) holds the photo-mode exposure at its baseline
		// so Enhanced Render can't blow out. Turn OFF to drive Exposure manually.
		sub->AddBoolOption("Lock Exposure", "ON keeps Enhanced Render from blowing out (Exposure slider held). OFF lets you set Exposure by hand", &m_exposureLocked, [this] {
			if (m_exposureLocked) { GRAPHICS::_0x5CD6A2CCE5087161(TRUE); GRAPHICS::_0x9229ED770975BD9E(); }
			else GRAPHICS::_0x5CD6A2CCE5087161(FALSE);
		});

		auto expo = PMRange(-1.0f, 1.0f, 0.05f, 2);
		sub->AddVectorOption("Exposure", "Photo-mode exposure trim (R* native). 0 = scene default", expo, [this] {
			float v = PMFromIdx(CurIdx(), -1.0f, 0.05f);
			float d = v - m_exposureApplied;
			if (d > 0.0001f || d < -0.0001f) { GRAPHICS::_0xC8D0611D9A0CF5D3(PMFloatArg(d)); m_exposureApplied = v; }
			m_exposure = v;
		})->SetVectorIndex(PMIdx(m_exposure, -1.0f, 0.05f, (int)expo.size()));

		auto cont = PMRange(-1.0f, 1.0f, 0.05f, 2);
		sub->AddVectorOption("Contrast", "Photo-mode contrast trim (R* native). 0 = scene default", cont, [this] {
			float v = PMFromIdx(CurIdx(), -1.0f, 0.05f);
			float d = v - m_contrastApplied;
			if (d > 0.0001f || d < -0.0001f) { GRAPHICS::_0x62B9F9A1272AED80(PMFloatArg(d)); m_contrastApplied = v; }
			m_contrast = v;
		})->SetVectorIndex(PMIdx(m_contrast, -1.0f, 0.05f, (int)cont.size()));

		sub->AddBoolOption("Engine Letterbox", "The game's own 16:9 cinematic bars (prefer Aspect Frame)", &m_letterbox);
		sub->AddBoolOption("Composition Grid", "Rule-of-thirds overlay (hidden with the UI)", &m_grid);
	});
}

void CPhotoMode::RebuildEffectsPage()
{
	g_Menu->AddSubmenu("PHOTO MODE", "Effects", Submenu_PhotoMode_Effects, 10, [this](Submenu* sub)
	{
		std::vector<std::string> filters;
		for (int i = 0; i < (int)PhotoModeFilters.size(); i++) filters.push_back(i == 0 ? "None" : PhotoModeFilters[i]);
		sub->AddVectorOption("Photo Filter", "All native Photo Mode filters. Stacks with the color grade", filters, [this] {
			m_filterIdx = CurIdx(); ApplyFilter(m_filterIdx); ApplyFilterWithOpacity();
		})->SetVectorIndex(m_filterIdx);

		auto opacity = PMRange(0.0f, 1.0f, 0.05f, 2);
		sub->AddVectorOption("Filter Opacity", "Blend the filter from subtle to full strength", opacity, [this] {
			m_filterOpacity = PMFromIdx(CurIdx(), 0.0f, 0.05f); ApplyFilterWithOpacity();
		})->SetVectorIndex(PMIdx(m_filterOpacity, 0.0f, 0.05f, (int)opacity.size()));

		sub->AddRegularOption("Take Screenshot (Lossless/HDR)", "Saves an uncompressed PNG (or .jxr in HDR). Also F3", [] {
			if (!ScreenCapture::IsCapturing()) { ScreenCapture::RequestCapture(); PhotoAudio::Play(PhotoAudio::PM_SHUTTER); UIUtil::PrintSubtitle("Capturing... saved to Captured Screenshots"); }
		});

		sub->AddVectorOption("Super-Res Factor", "Tile grid for the experimental high-resolution capture (output = factor x your screen)", std::vector<const char*>{ "2x", "3x" }, [this] {
			m_superResFactor = CurIdx() + 2;
		})->SetVectorIndex(m_superResFactor - 2);
		sub->AddRegularOption("~COLOR_YELLOW~Super-Resolution Capture (Test)~s~", "Experimental: pans a zoomed camera over a grid and reprojects the tiles into one large seamless PNG. Freeze the world first; takes a moment", [this] {
			CaptureSuperRes();
		});

		sub->AddRegularOption("Clear Filter", "Clear the photo filter", [this] { ApplyFilter(0); m_filterIdx = 0; });
		sub->AddRegularOption("Reset Everything", "Back to a clean slate: no grade, filter, DOF, roll or lights", [this] {
			ApplyFilter(0); m_filterIdx = 0; m_gradeIdx = 0; ApplyGrade();
			m_motionBlur = 0.0f; m_letterbox = false; m_grid = false;
			m_blurStrength = 0.0f; m_focusLock = false; m_dofDirty = true;
			m_rot.y = 0.0f; m_fov = 50.0f;
			for (int i = 0; i < PM_MAX_LIGHTS; i++) FreeLight(m_lights[i]);
		});
	});
}

void CPhotoMode::RebuildMusicPage()
{
	sPMMute = PhotoAudio::MuteWorld();
	sPMMusic = PhotoAudio::MusicEnabled();
	g_Menu->AddSubmenu("PHOTO MODE", "Music", Submenu_PhotoMode_Music, 8, [](Submenu* sub)
	{
		sub->AddBoolOption("Enable Music", "Off by default. Turn on to play a calm exploration bed over your shot", &sPMMusic, [] { PhotoAudio::SetMusicEnabled(sPMMusic); });

		std::vector<std::string> tracks; for (int i = 0; i < PhotoAudio::TrackCount(); i++) tracks.push_back(PhotoAudio::TrackLabel(i));
		sub->AddVectorOption("Music Track", "Ambient free-roam exploration beds. Switches live (also enables music). Some may not play from every location", tracks, [] {
			PhotoAudio::SelectTrack(CurIdx());
			sPMMusic = PhotoAudio::MusicEnabled();
		})->SetVectorIndex(PhotoAudio::CurrentTrack());

		sub->AddBoolOption("Mute Game Audio", "Duck the live world so only your chosen music plays", &sPMMute, [] { PhotoAudio::SetMuteWorld(sPMMute); });
	});
}

// ---------------------------------------------------------------------------
// Input - Photo Mode owns input and drives the native menu with raw keys
// ---------------------------------------------------------------------------

void CPhotoMode::HandleInput()
{
	// Keep the editor hotkeys / main-menu key from leaking in
	ResetKeyState((DWORD)g_Config.KeyOpenMenu);
	ResetKeyState((DWORD)g_Config.KeyAddCamera);
	ResetKeyState((DWORD)g_Config.KeyFreeCamToggle);
	ResetKeyState((DWORD)g_Config.KeyNextCamera);
	ResetKeyState((DWORD)g_Config.KeyPrevCamera);

	// Nav SFX use the native Creator Suite menu's own sounds (played inside the
	// menu's Handle* methods). We only silence them on slider auto-repeat so a
	// held adjustment doesn't machine-gun the click.

	// Hide the whole UI for a clean shot
	if (IsKeyJustUp('H')) {
		m_uiHidden = !m_uiHidden;
	}
	if (m_uiHidden) return; // fly only; no menu nav while hidden

	if (IsKeyJustUp(VK_UP))   { g_Menu->SetNavSoundSuppressed(false); g_Menu->ExtNavUp(); }
	if (IsKeyJustUp(VK_DOWN)) { g_Menu->SetNavSoundSuppressed(false); g_Menu->ExtNavDown(); }

	// Left/Right adjust with hold-to-repeat (sliders)
	int dir = 0;
	bool leftDown = IsKeyDown(VK_LEFT), rightDown = IsKeyDown(VK_RIGHT);
	if (IsKeyJustUp(VK_LEFT)) dir = -1;
	if (IsKeyJustUp(VK_RIGHT)) dir = 1;
	bool fresh = (dir != 0);
	if (leftDown || rightDown) {
		DWORD now = GetTickCount();
		if (m_holdStart == 0) m_holdStart = now;
		if (now - m_holdStart > 350 && now - m_repeatTimer > 45) {
			m_repeatTimer = now;
			dir = rightDown ? 1 : -1;
		}
	}
	else {
		m_holdStart = 0;
	}
	if (dir != 0) {
		g_Menu->SetNavSoundSuppressed(!fresh); // silent on auto-repeat
		if (dir < 0) g_Menu->ExtAdjustLeft();
		else         g_Menu->ExtAdjustRight();
	}

	// Enter: action / toggle / enter a tab page
	if (IsKeyJustUp(VK_RETURN)) {
		g_Menu->SetNavSoundSuppressed(false);
		g_Menu->ExtEnter();
		if (!m_active) return; // an option (Exit Photo Mode) closed us
	}

	// Back (Backspace): up one page, or exit Photo Mode at the root
	if (IsKeyJustUp(VK_BACK)) {
		g_Menu->SetNavSoundSuppressed(false);
		if (!g_Menu->ExtBack(Submenu_PhotoMode)) {
			Deactivate();
			return;
		}
	}
}

// ---------------------------------------------------------------------------
// Control prompts (native, bottom-right - matches the Creator Suite menu).
// The menu base already shows Select / Back; we add navigation + the free-cam
// fly controls so the full control set is visible while composing.
// ---------------------------------------------------------------------------

void CPhotoMode::RegisterPrompts()
{
	if (m_promptsRegistered) return;
	m_promptsRegistered = true;

	auto reg = [](Prompt& p, const char* text, Hash control) {
		p = HUD::_UI_PROMPT_REGISTER_BEGIN();
		HUD::_UI_PROMPT_SET_CONTROL_ACTION(p, control);
		HUD::_UI_PROMPT_SET_PRIORITY(p, 2);
		HUD::_UI_PROMPT_SET_TEXT(p, MISC::VAR_STRING(10, "LITERAL_STRING", text));
		HUD::_UI_PROMPT_SET_STANDARD_MODE(p, true);
		HUD::_UI_PROMPT_SET_ATTRIBUTE(p, 34, true); // allow duplicate control glyphs
		HUD::_UI_PROMPT_REGISTER_END(p);
		HUD::_UI_PROMPT_SET_VISIBLE(p, false);
		HUD::_UI_PROMPT_SET_ENABLED(p, false);
	};

	reg(m_promptNav,    "Navigate", INPUT_GAME_MENU_UP);
	reg(m_promptAdjust, "Adjust",   INPUT_GAME_MENU_LEFT);
	reg(m_promptMove,   "Move",     INPUT_MOVE_UD);
	reg(m_promptLook,   "Look",     INPUT_LOOK_LR);
	reg(m_promptFast,   "Fast",     INPUT_SPRINT);
}

void CPhotoMode::UpdatePrompts(bool show)
{
	if (!m_promptsRegistered) return;
	const Prompt all[] = { m_promptNav, m_promptAdjust, m_promptMove, m_promptLook, m_promptFast };
	for (Prompt p : all) {
		HUD::_UI_PROMPT_SET_VISIBLE(p, show);
		HUD::_UI_PROMPT_SET_ENABLED(p, show);
	}
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void CPhotoMode::Toggle()
{
	if (m_active) Deactivate();
	else Activate();
}

void CPhotoMode::Activate()
{
	if (m_active) return;

	// Native subtitles don't render under the Photo Mode freeze / slow timescale,
	// so route all PrintSubtitle messages through our script-drawn notification.
	UIUtil::Notify::SetScriptMode(true);

	// Smooth fade in: hide the camera cut while we set Photo Mode up.
	CAM::DO_SCREEN_FADE_OUT(250);
	for (DWORD t = GetTickCount(); !CAM::IS_SCREEN_FADED_OUT() && GetTickCount() - t < 500; ) WAIT(0);

	// Photo mode owns the screen: park the other camera systems
	g_FreeCam.Deactivate();
	g_Director.Deactivate(false);

	m_pos = CAM::GET_FINAL_RENDERED_CAM_COORD();
	m_rot = CAM::GET_FINAL_RENDERED_CAM_ROT(2);
	m_rot.y = 0.0f;
	m_fov = CAM::GET_FINAL_RENDERED_CAM_FOV();

	if (m_cam == 0 || !CAM::DOES_CAM_EXIST(m_cam)) {
		m_cam = CAM::CREATE_CAM("DEFAULT_SCRIPTED_CAMERA", false);
	}
	CAM::SET_CAM_COORD(m_cam, m_pos.x, m_pos.y, m_pos.z);
	CAM::SET_CAM_ROT(m_cam, m_rot.x, m_rot.y, m_rot.z, 2);
	CAM::SET_CAM_FOV(m_cam, m_fov);
	CAM::SET_CAM_ACTIVE(m_cam, true);
	CAM::RENDER_SCRIPT_CAMS(true, false, 0, true, false, 0);

	m_uiHidden = false;
	m_target = 0;
	m_dofDirty = true;
	m_dofApplied = false; // fresh camera, no DOF block on it yet
	m_weatherIdx = 0;

	// Sun Height drives the calendar; remember the real date to put back
	m_savedDay = CLOCK::GET_CLOCK_DAY_OF_MONTH();
	m_savedMonth = CLOCK::GET_CLOCK_MONTH();
	m_savedYear = CLOCK::GET_CLOCK_YEAR();
	m_moonOverride = 0.0f;

	// Timecycle runtime MUST be probed before the World page is built - the Sun
	// Azimuth/Elevation rows only appear when the sun vars resolved. (Graceful
	// no-op on unsupported game builds.)
	TimecycleRT::Init();
	m_sunVars = TimecycleRT::FindSunCandidates();
	m_sunVals.assign(m_sunVars.size(), 0.0f);

	// Resolve the three sun-direction axes by name so Azimuth/Elevation can
	// drive them together (this build's dump confirms sun_direction_x/y/z).
	m_sunVarX = TimecycleRT::FindVarByName("sun_direction_x");
	m_sunVarY = TimecycleRT::FindVarByName("sun_direction_y");
	m_sunVarZ = TimecycleRT::FindVarByName("sun_direction_z");
	m_sunUserEdited = false;
	ComputeSunFromClock(); // start the sun controls at the real time-of-day position

	// Snapshot the weather so "No Change" can restore it later
	MISC::GET_CURR_WEATHER_STATE(&m_savedWeather1, &m_savedWeather2, &m_savedWeatherPct);
	m_weatherSaved = true;

	m_swapIdx = 0;
	m_swapPed = 0;
	m_playerHidden = false;
	m_poseActive = false;
	m_filterOpacity = 1.0f;

	// Put the player's weapon away so the body doesn't glitch under the script
	// camera (world is still live here, so the holster animates cleanly).
	m_weaponAction = 0;
	m_playerDisarmed = false;
	m_savedPlayerWeapon = 0;
	ApplyPlayerHolster();

	// Enhanced render + exposure/contrast trims start clean each session.
	m_enhancedRender = false;
	m_enhancedApplied = false;
	m_exposureLocked = true;
	m_exposure = m_contrast = 0.0f;
	m_exposureApplied = m_contrastApplied = 0.0f;

	BuildPhotoMenus();

	// Take over the native menu: open it at the Photo Mode page and suspend its
	// own input (we drive it from raw keys while flying the camera). Nav sounds
	// stay the menu's own Creator Suite sounds (toggled per-key in HandleInput).
	g_Menu->SetNavSoundSuppressed(false);
	g_Menu->SetInputSuspended(true);
	g_Menu->SetForceHidden(false);
	g_Menu->OpenAt(Submenu_PhotoMode);

	RegisterPrompts();

	m_active = true;

	// Swap the world's audio for the editor ambience (duck score + walla, start
	// the soundtrack bed). Restored in Deactivate().
	PhotoAudio::Enter();

	// Auto-freeze on entry: lets the user snap action-packed gunfight scenes
	// the instant they open the editor, and (with Invincible) keeps the
	// player from dying while they compose. Default method = time scale.
	m_freezeMethod = 0;
	SetFrozen(true);

	// Reveal Photo Mode with a smooth fade in.
	CAM::DO_SCREEN_FADE_IN(350);
}

void CPhotoMode::Deactivate()
{
	if (!m_active) return;

	// Smooth fade out while we tear Photo Mode down and hand the camera back.
	CAM::DO_SCREEN_FADE_OUT(250);
	for (DWORD t = GetTickCount(); !CAM::IS_SCREEN_FADED_OUT() && GetTickCount() - t < 500; ) WAIT(0);

	// Hide our control prompts.
	UpdatePrompts(false);

	// Restore the world's audio.
	PhotoAudio::Exit();

	// Unfreeze FIRST, then stop any pose - clearing tasks while frozen is what
	// left the player locked up. With the world live again, the ped resumes
	// normal behaviour cleanly.
	SetFrozen(false);
	m_stepFrames = 0;

	// stop any pose/look/facial on player AND the selected subject
	ClearSubjectAnim(PLAYER::PLAYER_PED_ID());
	if (m_target != 0 && ENTITY::DOES_ENTITY_EXIST(m_target)) {
		ClearSubjectAnim(m_target);
	}
	m_poseIdx = 0;
	m_facialIdx = 0;
	m_poseActive = false;

	// Re-draw the weapon we put away on entry (world is live again here).
	if (m_playerDisarmed) {
		Ped player = PLAYER::PLAYER_PED_ID();
		if (m_savedPlayerWeapon != 0 && ENTITY::DOES_ENTITY_EXIST(player))
			WEAPON::SET_CURRENT_PED_WEAPON(player, m_savedPlayerWeapon, true, 0, false, false);
		m_playerDisarmed = false;
	}

	// drop the editor's invincibility and undo a character swap (delete the
	// decoy, un-hide and un-freeze the real player)
	PLAYER::SET_PLAYER_INVINCIBLE(PLAYER::PLAYER_ID(), false);
	if (m_swapIdx != 0 || m_swapPed != 0 || m_playerHidden) {
		m_swapIdx = 0;
		ApplyCharacterSwap();
	}

	// restore weather to the scene's original state
	if (m_weatherSaved) {
		MISC::CLEAR_OVERRIDE_WEATHER();
		MISC::SET_CURR_WEATHER_STATE(m_savedWeather1, m_savedWeather2, m_savedWeatherPct, true);
		m_weatherSaved = false;
	}

	// restore visibility
	m_hideOthers = false;
	UpdateCharacterVisibility();
	Ped target = ResolveTarget();
	if (ENTITY::DOES_ENTITY_EXIST(target)) {
		ENTITY::SET_ENTITY_VISIBLE(target, true);
	}
	m_targetHidden = false;

	// clear post effects
	ApplyFilter(0);
	m_filterIdx = 0;
	GRAPHICS::CLEAR_TIMECYCLE_MODIFIER();
	m_gradeIdx = 0;

	// turn off the R* enhanced render path and undo exposure/contrast trims
	// (the change natives are relative, so apply the inverse of what we committed)
	m_enhancedRender = false;
	if (m_enhancedApplied) {
		GRAPHICS::_0xF5793BB386E1FF9C(0);
		GRAPHICS::_0x5CD6A2CCE5087161(FALSE); // unlock exposure
		m_enhancedApplied = false;
	}
	if (m_exposureApplied > 0.0001f || m_exposureApplied < -0.0001f) {
		GRAPHICS::_0xC8D0611D9A0CF5D3(PMFloatArg(-m_exposureApplied));
		m_exposureApplied = 0.0f;
	}
	if (m_contrastApplied > 0.0001f || m_contrastApplied < -0.0001f) {
		GRAPHICS::_0x62B9F9A1272AED80(PMFloatArg(-m_contrastApplied));
		m_contrastApplied = 0.0f;
	}
	m_exposure = m_contrast = 0.0f;

	STREAMING::CLEAR_FOCUS();

	// undo all live timecycle edits before clearing the modifier slot
	TimecycleRT::RestoreAllEdits();

	// put the calendar and moon back the way we found them
	if (m_savedDay > 0) {
		CLOCK::SET_CLOCK_DATE(m_savedDay, m_savedMonth, m_savedYear);
		m_savedDay = -1;
	}
	if (m_moonOverride > 0.0f) {
		GRAPHICS::ENABLE_MOON_CYCLE_OVERRIDE(0.0f);
		m_moonOverride = 0.0f;
	}

	// Scene Editor: delete every placed object and undo all temporary YMAP
	// edits so the world is left exactly as it was on entry.
	g_SceneEditor.RevertAll();

	// shadow-casting layer down
	RemoveAllLamps();
	RemoveAllArtLights();
	HeroLight::Shutdown(m_rigLight);

	// remove every placed light's bulb prop
	for (int i = 0; i < PM_MAX_LIGHTS; i++) FreeLight(m_lights[i]);

	CAM::RENDER_SCRIPT_CAMS(false, true, 800, true, false, 0);
	if (m_cam != 0 && CAM::DOES_CAM_EXIST(m_cam)) {
		CAM::SET_CAM_ACTIVE(m_cam, false);
		CAM::DESTROY_CAM(m_cam, false);
		m_cam = 0;
	}

	// Release the native menu back to normal operation: hand input back, point
	// it at the editor entry page (so reopening with the menu key doesn't land
	// on a Photo Mode page) and close it.
	g_Menu->SetInputSuspended(false);
	g_Menu->SetNavSoundSuppressed(false);
	g_Menu->SetForceHidden(false);
	g_Menu->OpenAt(Submenu_EntryMenu);
	g_Menu->SetEnabled(false, false);

	g_Config.Save(); // persist tweaks made in here (look sensitivity, ...)
	m_active = false;

	// Native subtitles work again outside Photo Mode.
	UIUtil::Notify::SetScriptMode(false);

	// Reveal gameplay again with a smooth fade in.
	CAM::DO_SCREEN_FADE_IN(350);
}

void CPhotoMode::Tick()
{
	if (!m_active) return;

	// Keep the player safe while the editor is open
	PLAYER::SET_PLAYER_INVINCIBLE(PLAYER::PLAYER_ID(), m_invincible);

	// Hold the audience ambience down (walla density resets every frame)
	PhotoAudio::Tick();

	// Hide the native menu render with the H toggle, and also while a screenshot
	// is being captured so the UI never appears in the saved image.
	bool hideUI = m_uiHidden || ScreenCapture::IsCapturing();
	g_Menu->SetForceHidden(hideUI);
	// Native prompts get wiped by the HUD hide (and flash on unpause), so keep
	// them off and use our self-drawn control hint bar (DrawControlHints) instead.
	UpdatePrompts(false);

	// Cutscene unlock: re-assert our script camera so cutscenes / anim scenes
	// can't yank control away from the photo editor.
	if (m_cutsceneUnlock) {
		CAM::_DISABLE_CINEMATIC_MODE_THIS_FRAME();
		if (m_cam != 0 && CAM::DOES_CAM_EXIST(m_cam) && !CAM::IS_CAM_RENDERING(m_cam)) {
			CAM::SET_CAM_ACTIVE(m_cam, true);
			CAM::RENDER_SCRIPT_CAMS(true, false, 0, true, false, 0);
		}
	}

	HandleInput();
	if (!m_active) return; // an option (Exit Photo Mode / back at root) closed us

	// Apply any Scene Editor same-page refresh requested by an option callback
	// (deferred so we never rebuild a submenu while its own callback is running).
	PumpSceneRebuild();

	UpdateCamera();
	UpdateWorld();
	UpdateLights();
	UpdateArtLights();
	UpdateCharacterVisibility();
	g_SceneEditor.Tick(); // drop placed objects whose entity streamed out

	// Auto-focus: pull the focal plane onto the subject (the player - Arthur or
	// John - by default) every frame, so flying the camera around keeps them
	// tack-sharp without touching the Focus Distance slider. Aim at upper-body
	// / face height (the same offset orbit + track use). Needs Blur Intensity
	// above 0 for the defocus to be visible.
	if (m_autoFocus) {
		Ped t = ResolveTarget();
		if (ENTITY::DOES_ENTITY_EXIST(t)) {
			Vector3 fp = ENTITY::GET_ENTITY_COORDS(t, true, true);
			fp.z += 0.6f;
			m_focusDist = EMath::Clamp(EMath::Distance(m_pos, fp), 0.3f, 150.0f);
			m_dofDirty = true;
		}
	}

	if (m_dofDirty) {
		ApplyDof();
		m_dofDirty = false;
	}

	DrawOverlays();
	DrawSubjectMarker();
}
