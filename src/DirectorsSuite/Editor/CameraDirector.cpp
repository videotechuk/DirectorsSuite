#include "CameraDirector.h"
#include "CameraManager.h"
#include "EditorMath.h"
#include "HandheldMotion.h"
#include "Config.h"
#include "OBSClient.h"
#include "Overlays.h"
#include "..\script.h"
#include "..\UI\UIUtil.h"
#include <algorithm>
#include <cmath>

// Catmull-Rom position spline through 4 control points: interpolates p1 -> p2
// (t 0..1) with tangents implied by p0 and p3, so a chain of segments curves
// smoothly through every shot-list camera. Used by the Smooth camera path.
static Vector3 SplineCR(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t)
{
	float t2 = t * t, t3 = t2 * t;
	auto cr = [&](float a, float b, float c, float d) {
		return 0.5f * (2.0f * b + (-a + c) * t + (2.0f * a - 5.0f * b + 4.0f * c - d) * t2 + (-a + 3.0f * b - 3.0f * c + d) * t3);
	};
	Vector3 r{};
	r.x = cr(p0.x, p1.x, p2.x, p3.x);
	r.y = cr(p0.y, p1.y, p2.y, p3.y);
	r.z = cr(p0.z, p1.z, p2.z, p3.z);
	return r;
}

// Shortest-arc angle interpolation in degrees (avoids the long way round 180).
static float SplineLerpAngle(float a, float b, float t)
{
	float d = fmodf(b - a, 360.0f);
	if (d > 180.0f) d -= 360.0f;
	else if (d < -180.0f) d += 360.0f;
	return a + d * t;
}

// DOF parameter block for CAM::_0xE4B7945EF4F1BFB2 - the native Photo Mode
// itself uses (camera_photomode.c). Script structs are arrays of 8-byte
// slots, hence the ALIGN8 on every member. Photo Mode's "DOF" slider only
// raises the focal length: higher focal length = shallower depth of field.
struct CamDofParams
{
	ALIGN8 float focusDistance;   // f_0: 5.0 in Photo Mode
	ALIGN8 float aperture;        // f_1: always 2.0
	ALIGN8 float blurDiameter;    // f_2: always 128.0
	ALIGN8 float focalLength;     // f_3: 25..60 (Photo Mode), up to 90 (camera item)
	ALIGN8 float focalLength2;    // f_4: set equal to f_3
	ALIGN8 float focalLengthMax;  // f_5: 60 / 90
	ALIGN8 BOOL enabled;          // f_6: 1
	ALIGN8 BOOL flag7;            // f_7: 0
	ALIGN8 BOOL flag8;            // f_8: 1
	ALIGN8 BOOL flag9;            // f_9: 1
};

static void ApplyCamDof(Cam cam, float blurStrength, float focusDistance, bool focusPaused)
{
	if (blurStrength <= 0.0f && focusDistance < 0.0f) {
		return; // leave the camera's default DOF untouched
	}

	CamDofParams dof{};
	dof.focusDistance = (focusDistance >= 0.0f) ? focusDistance : 5.0f;
	dof.aperture = 2.0f;
	dof.blurDiameter = 128.0f;
	// blurStrength 0..1 maps onto Photo Mode's focal length range
	float focal = 25.0f + EMath::Clamp(blurStrength, 0.0f, 1.0f) * 65.0f;
	dof.focalLength = focal;
	dof.focalLength2 = focal;
	dof.focalLengthMax = 90.0f;
	dof.enabled = TRUE;
	dof.flag7 = FALSE;
	dof.flag8 = TRUE;
	dof.flag9 = TRUE;
	CAM::_0xE4B7945EF4F1BFB2(cam, (float*)&dof);

	if (focusDistance >= 0.0f) {
		CAM::_SET_CAM_FOCUS_DISTANCE(cam, focusDistance);
	}
	CAM::_PAUSE_CAMERA_FOCUS(cam, focusPaused);
}

Cam CCameraDirector::EnsureCam(int slot)
{
	if (m_engineCam[slot] == 0 || !CAM::DOES_CAM_EXIST(m_engineCam[slot])) {
		m_engineCam[slot] = CAM::CREATE_CAM("DEFAULT_SCRIPTED_CAMERA", false);
	}
	return m_engineCam[slot];
}

Ped CCameraDirector::ResolveTarget(const EditorCamera& cam) const
{
	if (cam.targetIsPlayer || cam.targetPed == 0 || !ENTITY::DOES_ENTITY_EXIST(cam.targetPed)) {
		return PLAYER::PLAYER_PED_ID();
	}
	return cam.targetPed;
}

void CCameraDirector::BakeCamera(const EditorCamera& src, Cam dst)
{
	CAM::SET_CAM_COORD(dst, src.pos.x, src.pos.y, src.pos.z);
	CAM::SET_CAM_ROT(dst, src.rot.x, src.rot.y, src.rot.z, 2);
	CAM::SET_CAM_FOV(dst, EMath::Clamp(src.fov, 1.0f, 130.0f));
	CAM::SET_CAM_MOTION_BLUR_STRENGTH(dst, src.motionBlur);

	// Photo Mode style focus / blur. Blur reads much stronger in dark scenes,
	// so keep blurStrength low at night.
	ApplyCamDof(dst, src.blurStrength, src.focusDistance, src.focusPaused);

	CAM::STOP_CAM_SHAKING(dst, true);
	if (src.shakeIndex > 0 && src.shakeIndex < (int)CamShakeNames.size()) {
		CAM::SHAKE_CAM(dst, CamShakeNames[src.shakeIndex], src.shakeAmplitude);
	}

	Ped target = ResolveTarget(src);
	if (src.modeFlags & CAMMODE_LOOKAT) {
		CAM::POINT_CAM_AT_ENTITY(dst, target, 0.0f, 0.0f, 0.0f, true);
	}
	else {
		CAM::STOP_CAM_POINTING(dst);
	}
}

void CCameraDirector::ApplyWorldOverrides(const EditorCamera& src)
{
	if (src.weatherIndex > 0 && src.weatherIndex < (int)WeatherTypeNames.size()) {
		MISC::SET_WEATHER_TYPE(MISC::GET_HASH_KEY(WeatherTypeNames[src.weatherIndex]), true, true, false, 0.0f, false);
	}
	if (src.todHour >= 0) {
		CLOCK::SET_CLOCK_TIME(src.todHour, src.todMinute, 0);
	}
}

void CCameraDirector::ApplyFilter(int filterIndex)
{
	if (filterIndex == m_activeFilterIndex) return;

	if (m_activeFilterIndex > 0 && m_activeFilterIndex < (int)PhotoModeFilters.size()) {
		GRAPHICS::ANIMPOSTFX_STOP(PhotoModeFilters[m_activeFilterIndex]);
	}
	if (filterIndex > 0 && filterIndex < (int)PhotoModeFilters.size()) {
		GRAPHICS::ANIMPOSTFX_PLAY(PhotoModeFilters[filterIndex]);
	}
	m_activeFilterIndex = filterIndex;
}

void CCameraDirector::ActivateCamera(int index, bool withTransition)
{
	EditorCamera* cam = g_CameraManager.Get(index);
	if (!cam) return;

	m_playerCamActive = false;

	int newSlot = m_rendering ? (1 - m_activeSlot) : m_activeSlot;
	Cam newCam = EnsureCam(newSlot);
	Cam oldCam = EnsureCam(m_activeSlot);

	BakeCamera(*cam, newCam);
	ApplyWorldOverrides(*cam);
	ApplyFilter(cam->filterIndex);

	Ped target = ResolveTarget(*cam);
	m_activationTargetHeading = ENTITY::GET_ENTITY_HEADING(target);

	// Effective blend time: a Hard Cut shot snaps in with no interpolation.
	int trans = cam->EffectiveTransitionMs();

	// Smooth path: drive the active engine cam manually along a Catmull-Rom
	// curve through the neighbouring shot-list cameras (flowing dolly/crane),
	// instead of the engine's straight two-point interp.
	if (g_Config.SmoothCameraPath && m_rendering && withTransition && trans > 0 && m_activeIndex >= 0) {
		int fromIndex = m_activeIndex;
		EditorCamera* fromCam = g_CameraManager.Get(fromIndex);
		EditorCamera* prevCam = g_CameraManager.Get(PrevEnabledIndex(fromIndex));
		EditorCamera* nextCam = g_CameraManager.Get(NextEnabledIndex(index));

		m_splP1 = fromCam ? fromCam->pos : cam->pos;
		m_splP2 = cam->pos;
		m_splP0 = prevCam ? prevCam->pos : m_splP1;
		m_splP3 = nextCam ? nextCam->pos : m_splP2;
		m_splR1 = fromCam ? fromCam->rot : cam->rot;
		m_splR2 = cam->rot;
		m_splFov1 = fromCam ? fromCam->fov : cam->fov;
		m_splFov2 = cam->fov;

		// Bake the destination's full look (DOF, clips, shake) onto the active
		// cam, then start it at the FROM pose so the destination never flashes.
		Cam driveCam = EnsureCam(m_activeSlot);
		BakeCamera(*cam, driveCam);
		CAM::SET_CAM_COORD(driveCam, m_splP1.x, m_splP1.y, m_splP1.z);
		CAM::SET_CAM_ROT(driveCam, m_splR1.x, m_splR1.y, m_splR1.z, 2);
		CAM::SET_CAM_FOV(driveCam, m_splFov1);
		CAM::SET_CAM_ACTIVE(driveCam, true);

		m_interpStartTime = GetTickCount();
		m_interpDurationMs = trans;
		m_splineActive = true;
		m_activeIndex = index;
		m_camStartTime = GetTickCount();
		return;
	}

	if (m_rendering && withTransition && trans > 0 && newSlot != m_activeSlot) {
		CAM::SET_CAM_ACTIVE_WITH_INTERP(newCam, oldCam, trans, cam->easeLocation, cam->easeRotation);
		m_interpStartTime = GetTickCount();
		m_interpDurationMs = trans;
	}
	else {
		CAM::SET_CAM_ACTIVE(newCam, true);
		if (m_rendering && newSlot != m_activeSlot) {
			CAM::SET_CAM_ACTIVE(oldCam, false);
		}
		m_interpDurationMs = 0;
	}

	if (!m_rendering) {
		CAM::RENDER_SCRIPT_CAMS(true, withTransition && trans > 0, trans, true, false, 0);
		m_rendering = true;
	}

	m_activeSlot = newSlot;
	m_activeIndex = index;
	m_camStartTime = GetTickCount();
}

void CCameraDirector::ActivatePlayerCamera()
{
	// Build the special player camera: Follow + LookAt + FreeAngle around
	m_playerCam.modeFlags = CAMMODE_FOLLOW | CAMMODE_LOOKAT | CAMMODE_FREE_ANGLE;
	m_playerCam.targetIsPlayer = true;
	m_playerCam.transitionMs = 0;
	m_playerCam.filterIndex = 0;
	m_playerCam.weatherIndex = 0;
	m_playerCam.todHour = -1;

	Ped player = PLAYER::PLAYER_PED_ID();
	Vector3 playerPos = ENTITY::GET_ENTITY_COORDS(player, true, true);
	Vector3 camPos = CAM::GET_FINAL_RENDERED_CAM_COORD();
	m_playerCam.orbitDistance = (std::max)(1.5f, EMath::Distance(playerPos, camPos));

	// Start the orbit from the current view direction
	Vector3 toCam = EMath::Sub(camPos, playerPos);
	m_playerCam.orbitHeading = atan2f(-toCam.x, toCam.y) * EMath::RAD2DEG;
	m_playerCam.orbitPitch = EMath::Clamp(asinf(toCam.z / (std::max)(0.01f, EMath::Length(toCam))) * EMath::RAD2DEG, -85.0f, 85.0f);
	m_playerCam.pos = camPos;
	m_playerCam.fov = CAM::GET_FINAL_RENDERED_CAM_FOV();

	Cam cam = EnsureCam(m_activeSlot);
	BakeCamera(m_playerCam, cam);
	CAM::SET_CAM_ACTIVE(cam, true);
	if (!m_rendering) {
		CAM::RENDER_SCRIPT_CAMS(true, false, 0, true, false, 0);
		m_rendering = true;
	}

	m_playerCamActive = true;
	m_activeIndex = -1;
	m_autoSwitching = false;
	m_camStartTime = GetTickCount();
}

void CCameraDirector::Deactivate(bool smooth)
{
	// A manual deactivate during project playback also tears down its state
	// (stops the OBS recording, restores the HUD). CancelProjectState clears
	// m_projectPlayback so FinishProject -> Deactivate never recurses here.
	if (m_projectPlayback) {
		CancelProjectState();
	}

	if (m_rendering) {
		CAM::RENDER_SCRIPT_CAMS(false, smooth, 1000, true, false, 0);
		m_rendering = false;
	}
	for (int i = 0; i < 2; i++) {
		if (m_engineCam[i] != 0 && CAM::DOES_CAM_EXIST(m_engineCam[i])) {
			CAM::SET_CAM_ACTIVE(m_engineCam[i], false);
		}
	}
	ApplyFilter(0);
	m_activeIndex = -1;
	m_playerCamActive = false;
	m_autoSwitching = false;
	m_interpDurationMs = 0;
	m_splineActive = false;
}

void CCameraDirector::StartAutoSwitching()
{
	if (m_projectPlayback) StopProject(); // the two playback modes are exclusive
	if (g_CameraManager.EnabledCount() == 0) return;
	if (!m_rendering || m_activeIndex < 0) {
		ActivateCamera(NextEnabledIndex(-1), true);
	}
	m_autoSwitching = true;
}

void CCameraDirector::StopAutoSwitching()
{
	m_autoSwitching = false;
}

void CCameraDirector::ToggleAutoSwitching()
{
	if (m_autoSwitching) StopAutoSwitching();
	else StartAutoSwitching();
}

int CCameraDirector::NextEnabledIndex(int from) const
{
	int count = g_CameraManager.Count();
	if (count == 0) return -1;
	for (int step = 1; step <= count; step++) {
		int i = (from + step) % count;
		if (i < 0) i += count;
		const EditorCamera* cam = const_cast<CCameraManager&>(g_CameraManager).Get(i);
		if (cam && cam->enabled) return i;
	}
	return -1;
}

int CCameraDirector::PrevEnabledIndex(int from) const
{
	int count = g_CameraManager.Count();
	if (count == 0) return -1;
	if (from < 0) from = 0; // so the first backward step lands on the last camera
	for (int step = 1; step <= count; step++) {
		int i = (from - step) % count;
		if (i < 0) i += count;
		const EditorCamera* cam = const_cast<CCameraManager&>(g_CameraManager).Get(i);
		if (cam && cam->enabled) return i;
	}
	return -1;
}

void CCameraDirector::SwitchCamera(int dir)
{
	if (m_projectPlayback) StopProject(); // manual switching ends project playback
	if (g_CameraManager.EnabledCount() == 0) return;

	int next = (dir >= 0) ? NextEnabledIndex(m_activeIndex) : PrevEnabledIndex(m_activeIndex);
	if (next >= 0) {
		ActivateCamera(next, true);
	}
}

// ---------------------------------------------------------------------------
// Full-project playback
// ---------------------------------------------------------------------------

int CCameraDirector::ProjectDurationMs() const
{
	int total = 0;
	for (int i = 0; i < g_CameraManager.Count(); i++) {
		const EditorCamera* cam = const_cast<CCameraManager&>(g_CameraManager).Get(i);
		if (cam && cam->enabled) total += cam->durationMs;
	}
	return total;
}

bool CCameraDirector::GetProjectProgress(float& outProgress) const
{
	if (!m_projectPlayback) return false;
	int total = ProjectDurationMs();
	if (total <= 0) { outProgress = 0.0f; return true; }
	float p = (float)(GetTickCount() - m_projectStartTime) / (float)total;
	outProgress = EMath::Clamp(p, 0.0f, 1.0f);
	return true;
}

void CCameraDirector::PlayProject(bool record)
{
	// Build the ordered list of enabled cameras
	m_playOrder.clear();
	for (int i = 0; i < g_CameraManager.Count(); i++) {
		EditorCamera* cam = g_CameraManager.Get(i);
		if (cam && cam->enabled) m_playOrder.push_back(i);
	}
	if (m_playOrder.empty()) {
		UIUtil::PrintSubtitle("~COLOR_RED~No enabled cameras to play~s~");
		return;
	}

	m_autoSwitching = false;
	m_projectPlayback = true;
	m_playPos = 0;
	m_projectStartTime = GetTickCount();

	// Clean the screen for the recording
	if (g_Config.OBSHideHudDuringPlayback) {
		m_prevHideHud = g_Overlays.HideHud;
		g_Overlays.HideHud = true;
	}

	// Kick off the OBS recording before the first camera goes live
	m_projectRecording = false;
	if (record && g_Config.OBSEnabled && g_Config.OBSAutoRecord) {
		g_OBS.Host = g_Config.OBSHost;
		g_OBS.Port = g_Config.OBSPort;
		g_OBS.Password = g_Config.OBSPassword;
		g_OBS.StartRecording();
		m_projectRecording = true;
	}

	ActivateCamera(m_playOrder[0], true);

	// While recording, stay silent: a subtitle printed here would be captured
	// in the opening seconds of the clip. OBS's own UI shows recording state.
	if (!m_projectRecording) {
		UIUtil::PrintSubtitle("Playing project - " + std::to_string((int)m_playOrder.size()) + " cameras");
	}
}

void CCameraDirector::CancelProjectState()
{
	if (m_projectRecording) {
		g_OBS.StopRecording();
		m_projectRecording = false;
	}
	if (g_Config.OBSHideHudDuringPlayback) {
		g_Overlays.HideHud = m_prevHideHud;
	}
	m_projectPlayback = false;
}

void CCameraDirector::FinishProject(bool stoppedEarly)
{
	bool wasRecording = m_projectRecording;
	CancelProjectState();
	Deactivate(true);
	UIUtil::PrintSubtitle(stoppedEarly
		? (wasRecording ? "Project playback stopped - ~COLOR_RED~recording saved~s~" : "Project playback stopped")
		: (wasRecording ? "Project finished - ~COLOR_RED~recording saved~s~" : "Project finished"));
}

void CCameraDirector::StopProject()
{
	if (m_projectPlayback) {
		FinishProject(true);
	}
}

void CCameraDirector::UpdateProjectPlayback()
{
	if (!m_projectPlayback) return;

	if (m_playPos < 0 || m_playPos >= (int)m_playOrder.size()) { FinishProject(false); return; }

	int currentIndex = m_playOrder[m_playPos];
	EditorCamera* cam = g_CameraManager.Get(currentIndex);
	if (!cam) { FinishProject(true); return; } // a camera was deleted mid-playback

	DWORD elapsed = GetTickCount() - m_camStartTime;
	if ((int)elapsed >= cam->durationMs) {
		m_playPos++;
		if (m_playPos >= (int)m_playOrder.size()) {
			FinishProject(false); // reached the end naturally
			return;
		}
		ActivateCamera(m_playOrder[m_playPos], true);
	}
}

void CCameraDirector::UpdateAutoSwitching()
{
	if (!m_autoSwitching || m_activeIndex < 0) return;

	EditorCamera* cam = g_CameraManager.Get(m_activeIndex);
	if (!cam) { StopAutoSwitching(); return; }

	DWORD elapsed = GetTickCount() - m_camStartTime;
	if ((int)elapsed >= cam->durationMs) {
		int next = NextEnabledIndex(m_activeIndex);
		if (next < 0) { StopAutoSwitching(); return; }
		ActivateCamera(next, true);
	}
}

bool CCameraDirector::GetTransitionProgress(float& outProgress) const
{
	if (m_interpDurationMs <= 0) return false;
	DWORD elapsed = GetTickCount() - m_interpStartTime;
	if ((int)elapsed >= m_interpDurationMs) return false;
	outProgress = (float)elapsed / (float)m_interpDurationMs;
	return true;
}

void CCameraDirector::UpdateActiveCameraModes(EditorCamera& cam)
{
	Cam engineCam = m_engineCam[m_activeSlot];
	if (engineCam == 0 || !CAM::DOES_CAM_EXIST(engineCam)) return;

	Ped target = ResolveTarget(cam);
	if (!ENTITY::DOES_ENTITY_EXIST(target)) return;
	Vector3 targetPos = ENTITY::GET_ENTITY_COORDS(target, true, true);

	if (cam.modeFlags & CAMMODE_FREE_ANGLE) {
		// Orbit the target with the look input (right stick / mouse)
		PAD::DISABLE_CONTROL_ACTION(0, INPUT_LOOK_LR, false);
		PAD::DISABLE_CONTROL_ACTION(0, INPUT_LOOK_UD, false);
		float lookLR = PAD::GET_DISABLED_CONTROL_NORMAL(0, INPUT_LOOK_LR);
		float lookUD = PAD::GET_DISABLED_CONTROL_NORMAL(0, INPUT_LOOK_UD);

		cam.orbitHeading -= lookLR * g_Config.FreeCamMouseSensitivity;
		cam.orbitPitch = EMath::Clamp(cam.orbitPitch - lookUD * g_Config.FreeCamMouseSensitivity, -85.0f, 85.0f);

		float yawRad = cam.orbitHeading * EMath::DEG2RAD;
		float pitchRad = cam.orbitPitch * EMath::DEG2RAD;
		Vector3 offset{};
		offset.x = -sinf(yawRad) * cosf(pitchRad) * cam.orbitDistance;
		offset.y = cosf(yawRad) * cosf(pitchRad) * cam.orbitDistance;
		offset.z = sinf(pitchRad) * cam.orbitDistance + 0.6f; // aim roughly at the chest

		Vector3 newPos = EMath::Add(targetPos, offset);
		CAM::SET_CAM_COORD(engineCam, newPos.x, newPos.y, newPos.z);
		cam.pos = newPos;

		if (!(cam.modeFlags & CAMMODE_LOOKAT)) {
			// orbiting always faces the target unless LookAt handles it already
			Vector3 dir = EMath::Normalize(EMath::Sub(targetPos, newPos));
			Vector3 rot = EMath::DirectionToRotation(dir);
			CAM::SET_CAM_ROT(engineCam, rot.x, rot.y, rot.z, 2);
		}
		return;
	}

	if (cam.modeFlags & CAMMODE_FOLLOW) {
		Vector3 offset = cam.followOffset;
		if (cam.followRelativeToHeading) {
			float headingDelta = ENTITY::GET_ENTITY_HEADING(target) - m_activationTargetHeading;
			offset = EMath::RotateZ(offset, headingDelta);
		}
		Vector3 newPos = EMath::Add(targetPos, offset);
		CAM::SET_CAM_COORD(engineCam, newPos.x, newPos.y, newPos.z);
		cam.pos = newPos;
		return;
	}

	if (cam.modeFlags & CAMMODE_SAME_ANGLE) {
		// Keep the original world angle: translate with the target, never rotate
		Vector3 newPos = EMath::Add(targetPos, cam.followOffset);
		CAM::SET_CAM_COORD(engineCam, newPos.x, newPos.y, newPos.z);
		cam.pos = newPos;
		CAM::SET_CAM_ROT(engineCam, cam.rot.x, cam.rot.y, cam.rot.z, 2);
		return;
	}
}

// Adds organic handheld/phone operator motion on top of whatever base transform
// the mode logic just produced. Position sway is applied for every mode (base
// position is always kept current in cam.pos). Rotation drift is only applied
// where WE own the camera angle - LookAt (engine POINT_CAM_AT_ENTITY) and the
// orbitable FreeAngle drive their own rotation, so we leave those to sway in
// position only, which keeps a handheld lock-on framing.
void CCameraDirector::ApplyHandheld(EditorCamera& cam)
{
	if (cam.handheldStyle <= 0) return;
	Cam engineCam = m_engineCam[m_activeSlot];
	if (engineCam == 0 || !CAM::DOES_CAM_EXIST(engineCam)) return;

	float t = (float)GetTickCount() * 0.001f; // continuous seconds; motion is stateless
	HandheldMotion::Offsets off = HandheldMotion::Evaluate(cam.handheldStyle, cam.handheldIntensity, t);

	// Local sway -> world, using the camera's facing as the reference frame.
	Vector3 forward = EMath::RotationToDirection(cam.rot);
	Vector3 flatRot{}; flatRot.z = cam.rot.z - 90.0f;
	Vector3 right = EMath::RotationToDirection(flatRot);
	Vector3 worldOff = EMath::Add(
		EMath::Add(EMath::Scale(right, off.posLocal.x), EMath::Scale(forward, off.posLocal.y)),
		Vector3{ 0.0f, 0.0f, off.posLocal.z });

	Vector3 p = EMath::Add(cam.pos, worldOff);
	CAM::SET_CAM_COORD(engineCam, p.x, p.y, p.z);

	bool engineOwnsRot = (cam.modeFlags & CAMMODE_LOOKAT) || (cam.modeFlags & CAMMODE_FREE_ANGLE);
	if (!engineOwnsRot) {
		CAM::SET_CAM_ROT(engineCam,
			cam.rot.x + off.rot.x, cam.rot.y + off.rot.y, cam.rot.z + off.rot.z, 2);
	}
}

void CCameraDirector::CycleTarget(EditorCamera& cam, int dir)
{
	// Build candidate list: player first, then nearby peds sorted by distance,
	// optionally bubbling dead or alive animals to the front.
	const int ARR_SIZE = 1024;
	int peds[ARR_SIZE];
	int found = worldGetAllPeds(peds, ARR_SIZE);

	Ped player = PLAYER::PLAYER_PED_ID();
	Vector3 playerPos = ENTITY::GET_ENTITY_COORDS(player, true, true);

	struct Candidate { Ped ped; float dist; bool isAnimal; bool isDead; };
	std::vector<Candidate> list;
	for (int i = 0; i < found; i++) {
		if (peds[i] == player || !ENTITY::DOES_ENTITY_EXIST(peds[i])) continue;
		Vector3 pos = ENTITY::GET_ENTITY_COORDS(peds[i], true, true);
		float dist = EMath::Distance(playerPos, pos);
		if (dist > 200.0f) continue;
		Candidate c;
		c.ped = peds[i];
		c.dist = dist;
		c.isAnimal = !PED::IS_PED_HUMAN(peds[i]);
		c.isDead = ENTITY::IS_ENTITY_DEAD(peds[i]);
		list.push_back(c);
	}

	int priority = AnimalPriority;
	std::sort(list.begin(), list.end(), [priority](const Candidate& a, const Candidate& b) {
		if (priority == ANIMAL_PRIORITY_ALIVE) {
			bool pa = a.isAnimal && !a.isDead, pb = b.isAnimal && !b.isDead;
			if (pa != pb) return pa;
		}
		else if (priority == ANIMAL_PRIORITY_DEAD) {
			bool pa = a.isAnimal && a.isDead, pb = b.isAnimal && b.isDead;
			if (pa != pb) return pa;
		}
		return a.dist < b.dist;
	});

	// Find current position in [player, list...]
	int current = 0; // 0 = player
	if (!cam.targetIsPlayer) {
		for (int i = 0; i < (int)list.size(); i++) {
			if (list[i].ped == cam.targetPed) { current = i + 1; break; }
		}
	}

	int total = 1 + (int)list.size();
	current = (current + dir) % total;
	if (current < 0) current += total;

	if (current == 0) {
		cam.targetIsPlayer = true;
		cam.targetPed = 0;
	}
	else {
		cam.targetIsPlayer = false;
		cam.targetPed = list[current - 1].ped;
	}
}

std::string CCameraDirector::DescribeTarget(const EditorCamera& cam)
{
	if (cam.targetIsPlayer || cam.targetPed == 0) return "Player";
	if (!ENTITY::DOES_ENTITY_EXIST(cam.targetPed)) return "Player (target lost)";

	std::string desc = PED::IS_PED_HUMAN(cam.targetPed) ? "Ped" : "Animal";
	if (ENTITY::IS_ENTITY_DEAD(cam.targetPed)) desc += " (dead)";
	desc += " #" + std::to_string(cam.targetPed);
	return desc;
}

void CCameraDirector::Tick()
{
	if (!m_rendering) return;

	// Cutscene unlock: anim scenes (RDR2 cutscenes) steal the render cam;
	// in Auto mode we keep forcing our script cam back on every frame.
	if (CutsceneUnlock == CUTSCENE_UNLOCK_AUTO && CAM::_IS_ANIM_SCENE_CAM_ACTIVE()) {
		CAM::RENDER_SCRIPT_CAMS(true, false, 0, true, false, 0);
	}

	if (m_splineActive) {
		// Manually drive the active cam along the Catmull-Rom curve for the
		// duration of the transition; normal mode updates resume once done.
		Cam driveCam = m_engineCam[m_activeSlot];
		float dur = (float)(m_interpDurationMs > 0 ? m_interpDurationMs : 1);
		float t = EMath::Clamp((float)(GetTickCount() - m_interpStartTime) / dur, 0.0f, 1.0f);
		float te = t * t * (3.0f - 2.0f * t); // smoothstep ease-in-out
		Vector3 pos = SplineCR(m_splP0, m_splP1, m_splP2, m_splP3, te);
		if (driveCam != 0 && CAM::DOES_CAM_EXIST(driveCam)) {
			CAM::SET_CAM_COORD(driveCam, pos.x, pos.y, pos.z);
			CAM::SET_CAM_ROT(driveCam,
				SplineLerpAngle(m_splR1.x, m_splR2.x, te),
				SplineLerpAngle(m_splR1.y, m_splR2.y, te),
				SplineLerpAngle(m_splR1.z, m_splR2.z, te), 2);
			CAM::SET_CAM_FOV(driveCam, m_splFov1 + (m_splFov2 - m_splFov1) * te);
		}
		if (t >= 1.0f) m_splineActive = false;
	}
	else if (m_playerCamActive) {
		UpdateActiveCameraModes(m_playerCam);
	}
	else if (m_activeIndex >= 0) {
		EditorCamera* cam = g_CameraManager.Get(m_activeIndex);
		if (cam) {
			UpdateActiveCameraModes(*cam);
			ApplyHandheld(*cam); // organic handheld motion on top of the base transform
		}
	}

	UpdateProjectPlayback();
	UpdateAutoSwitching();
}
