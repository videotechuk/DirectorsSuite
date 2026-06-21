// Director's Suite - camera playback engine.
//
// Stored EditorCamera params are baked into one of two pooled engine cameras
// ("DEFAULT_SCRIPTED_CAMERA") on activation; the second handle exists so
// SET_CAM_ACTIVE_WITH_INTERP can blend between consecutive cameras like the
// Rockstar Editor camera transitions.

#pragma once
#include "EditorTypes.h"

class CCameraDirector
{
public:
	// --- activation ---
	void ActivateCamera(int index, bool withTransition = true);
	void ActivatePlayerCamera();              // Follow + LookAt + FreeAngle around player
	void Deactivate(bool smooth = true);

	bool IsRendering() const { return m_rendering; }
	bool IsPlayerCamActive() const { return m_playerCamActive; }
	int  ActiveIndex() const { return m_activeIndex; }
	Cam  ActiveEngineCam() const { return m_engineCam[m_activeSlot]; }

	// Switch to the next/previous enabled camera with its transition.
	// dir = +1 forward, -1 backward. Activates the first enabled camera when
	// none is rendering yet.
	void SwitchCamera(int dir);

	// --- auto switching ---
	void StartAutoSwitching();
	void StopAutoSwitching();
	void ToggleAutoSwitching();
	bool IsAutoSwitching() const { return m_autoSwitching; }

	// --- full-project playback ---
	// Plays every enabled camera once, in list order, start to finish, then
	// returns to the gameplay camera. When `record` is true and OBS is
	// configured, an OBS recording is started on the first frame and stopped
	// when the sequence ends or is cancelled.
	void PlayProject(bool record);
	void StopProject();                 // cancel an in-progress project playback
	bool IsPlayingProject() const { return m_projectPlayback; }
	// True while the current project playback is being recorded (set the instant
	// recorded playback starts, before OBS confirms over the websocket). Use this
	// rather than g_OBS.IsRecording() to keep overlays out of the recording from
	// the very first frame.
	bool IsRecordingProject() const { return m_projectRecording; }
	// Total project run time across all enabled cameras (durations + transitions)
	int  ProjectDurationMs() const;
	// 0..1 progress through the whole project; false when not playing
	bool GetProjectProgress(float& outProgress) const;

	// transition progress for the overlay bar; returns false when idle
	bool GetTransitionProgress(float& outProgress) const;

	// --- target picking (selectable ped per camera) ---
	// dir = +1/-1 cycles through Player -> nearby peds. Respects AnimalPriority.
	void CycleTarget(EditorCamera& cam, int dir);
	std::string DescribeTarget(const EditorCamera& cam);

	int AnimalPriority = ANIMAL_PRIORITY_NONE; // eAnimalPriority
	int CutsceneUnlock = CUTSCENE_UNLOCK_OFF;  // eCutsceneUnlock

	// must run every frame
	void Tick();

private:
	Cam  m_engineCam[2] = { 0, 0 };
	int  m_activeSlot = 0;
	int  m_activeIndex = -1;       // index into g_CameraManager.Cameras, -1 = none/player cam
	bool m_rendering = false;
	bool m_playerCamActive = false;
	EditorCamera m_playerCam;      // standalone runtime camera

	bool m_autoSwitching = false;
	DWORD m_camStartTime = 0;      // when current camera became active (for duration)
	DWORD m_interpStartTime = 0;
	int   m_interpDurationMs = 0;

	// Smooth (Catmull-Rom spline) transition: when g_Config.SmoothCameraPath is
	// on we drive the active engine cam manually along a curve through the
	// neighbouring shot-list cameras instead of the engine's two-point interp,
	// for a flowing dolly/crane move. Control points: P0=prev, P1=from, P2=to,
	// P3=next. Active only during the transition window.
	bool    m_splineActive = false;
	Vector3 m_splP0{}, m_splP1{}, m_splP2{}, m_splP3{};
	Vector3 m_splR1{}, m_splR2{};        // from / to rotation (eulers)
	float   m_splFov1 = 50.0f, m_splFov2 = 50.0f;

	// project playback
	bool  m_projectPlayback = false;
	bool  m_projectRecording = false;   // an OBS recording is tied to this playback
	std::vector<int> m_playOrder;       // enabled camera indices, in order
	int   m_playPos = 0;                // position within m_playOrder
	DWORD m_projectStartTime = 0;
	bool  m_prevHideHud = false;        // HUD state to restore after playback

	void UpdateProjectPlayback();
	void FinishProject(bool stoppedEarly);
	void CancelProjectState();          // stop recording + restore HUD + clear flags

	int m_activeFilterIndex = 0;   // currently playing ANIMPOSTFX filter
	float m_activationTargetHeading = 0.0f;

	Cam  EnsureCam(int slot);
	void BakeCamera(const EditorCamera& src, Cam dst);
	void ApplyWorldOverrides(const EditorCamera& src);
	void ApplyFilter(int filterIndex);
	void UpdateActiveCameraModes(EditorCamera& cam);
	void ApplyHandheld(EditorCamera& cam);   // organic handheld/phone motion on top of the base transform
	void UpdateAutoSwitching();
	Ped  ResolveTarget(const EditorCamera& cam) const;
	int  NextEnabledIndex(int from) const;
	int  PrevEnabledIndex(int from) const;
};

inline CCameraDirector g_Director;
