#include "Overlays.h"
#include <algorithm>
#include "CameraManager.h"
#include "CameraDirector.h"
#include "OBSClient.h"
#include "EditorMath.h"
#include "Config.h"
#include "..\script.h"
#include "..\UI\Drawing.h"

void COverlays::Tick()
{
	if (HideHud) {
		HUD::HIDE_HUD_AND_RADAR_THIS_FRAME();
		HUD::_HIDE_HUD_THIS_FRAME();
	}

	if (Letterbox) {
		DrawLetterbox();
	}

	// Screen aids are part of the game's framebuffer, so OBS captures them.
	// While a recording runs they all stay hidden unless explicitly allowed;
	// the letterbox is exempt because it is a cinematic effect meant to be
	// part of the video.
	bool hideForRecording = g_OBS.IsRecording() && !ShowOverlaysWhileRecording;

	if (ShowGrid && !hideForRecording) {
		DrawGrid();
	}

	if (ShowCameraMarkers) {
		SyncCameraProps();
		if (ShowMarkerLabels) {
			DrawCameraMarkers();
		}
	}
	else if (!m_props.empty()) {
		RemoveAllProps();
	}

	if (ShowProgressBar && !hideForRecording) {
		DrawTransitionBar();
	}

	DrawProjectPlaybackBar();
}

void COverlays::DrawProjectPlaybackBar()
{
	float progress = 0.0f;
	if (!g_Director.GetProjectProgress(progress)) return;

	// Everything drawn in-game ends up in OBS's capture, so while a recording
	// is running the bar and REC indicator stay off the screen by default -
	// OBS's own UI is the recording feedback. The bar still shows for
	// non-recorded playback.
	if (g_OBS.IsRecording() && !ShowOverlaysWhileRecording) return;

	const float barW = 0.5f;
	const float barH = 0.005f;
	const float y = 0.025f;

	GRAPHICS::DRAW_RECT(0.5f, y, barW, barH, 0, 0, 0, 170, false, false);
	float fillW = barW * progress;
	GRAPHICS::DRAW_RECT(0.5f - barW * 0.5f + fillW * 0.5f, y, fillW, barH, 220, 40, 40, 230, false, false);

	// Blinking REC dot + label while OBS is recording this playback
	if (g_OBS.IsRecording()) {
		bool blink = ((GetTickCount() / 500) % 2) == 0;
		if (blink) {
			GRAPHICS::DRAW_RECT(0.5f - barW * 0.5f - 0.02f, y, 0.008f, 0.014f, 230, 30, 30, 255, false, false);
		}
		Drawing::DrawFormattedText("REC", Font::Body, 230, 40, 40, 255, Alignment::Left, 18,
			(0.5f - barW * 0.5f - 0.005f) * SCREEN_WIDTH, (y - 0.012f) * SCREEN_HEIGHT);
	}
}

// ---------------------------------------------------------------------------
// Physical camera markers (p_camerabox01x)
// ---------------------------------------------------------------------------

namespace {
	// The p_camerabox01x model's lens faces along its local -Y axis, so applying
	// the camera rotation verbatim points the lens 180 degrees away from where
	// the camera is actually looking. Add this yaw offset so the lens faces the
	// camera's view direction.
	constexpr float kPropYawOffset = 180.0f;

	Vector3 PropRotationForCamera(const Vector3& camRot)
	{
		Vector3 r = camRot;
		r.z += kPropYawOffset;
		return r;
	}
}

void COverlays::SyncCameraProps()
{
	if (m_propModel == 0) {
		m_propModel = MISC::GET_HASH_KEY("p_camerabox01x");
	}

	if (!STREAMING::HAS_MODEL_LOADED(m_propModel)) {
		if (STREAMING::IS_MODEL_IN_CDIMAGE(m_propModel) && STREAMING::IS_MODEL_VALID(m_propModel)) {
			STREAMING::REQUEST_MODEL(m_propModel, false);
			m_propModelRequested = true;
		}
		return; // try again next frame once streamed in
	}

	// Hide every marker while an editor camera renders so the boxes never
	// appear in actual shots; they come back in gameplay / free cam.
	bool hideAll = g_Director.IsRendering();

	// Create / update one prop per camera
	for (int i = 0; i < g_CameraManager.Count(); i++) {
		EditorCamera* cam = g_CameraManager.Get(i);
		if (!cam) continue;

		MarkerProp& prop = m_props[cam->id];

		if (prop.obj == 0 || !ENTITY::DOES_ENTITY_EXIST(prop.obj)) {
			prop.obj = OBJECT::CREATE_OBJECT(m_propModel, cam->pos.x, cam->pos.y, cam->pos.z, false, false, false, false, false);
			if (prop.obj == 0) continue;
			ENTITY::SET_ENTITY_COLLISION(prop.obj, false, false);
			ENTITY::FREEZE_ENTITY_POSITION(prop.obj, true);
			prop.pos = cam->pos;
			prop.rot = cam->rot;
			Vector3 pr = PropRotationForCamera(cam->rot);
			ENTITY::SET_ENTITY_ROTATION(prop.obj, pr.x, pr.y, pr.z, 2, true);
			prop.visible = true;
		}

		// Only touch the entity when the camera actually moved
		if (prop.pos.x != cam->pos.x || prop.pos.y != cam->pos.y || prop.pos.z != cam->pos.z) {
			ENTITY::SET_ENTITY_COORDS_NO_OFFSET(prop.obj, cam->pos.x, cam->pos.y, cam->pos.z, false, false, false);
			prop.pos = cam->pos;
		}
		if (prop.rot.x != cam->rot.x || prop.rot.y != cam->rot.y || prop.rot.z != cam->rot.z) {
			Vector3 pr = PropRotationForCamera(cam->rot);
			ENTITY::SET_ENTITY_ROTATION(prop.obj, pr.x, pr.y, pr.z, 2, true);
			prop.rot = cam->rot;
		}

		bool shouldShow = !hideAll;
		if (prop.visible != shouldShow) {
			ENTITY::SET_ENTITY_VISIBLE(prop.obj, shouldShow);
			prop.visible = shouldShow;
		}
	}

	// Remove props whose camera was deleted
	for (auto it = m_props.begin(); it != m_props.end(); ) {
		bool exists = false;
		for (const auto& cam : g_CameraManager.Cameras) {
			if (cam.id == it->first) { exists = true; break; }
		}
		if (!exists) {
			if (it->second.obj != 0 && ENTITY::DOES_ENTITY_EXIST(it->second.obj)) {
				Object obj = it->second.obj;
				OBJECT::DELETE_OBJECT(&obj);
			}
			it = m_props.erase(it);
		}
		else {
			++it;
		}
	}
}

void COverlays::RemoveAllProps()
{
	for (auto& [id, prop] : m_props) {
		if (prop.obj != 0 && ENTITY::DOES_ENTITY_EXIST(prop.obj)) {
			Object obj = prop.obj;
			OBJECT::DELETE_OBJECT(&obj);
		}
	}
	m_props.clear();

	if (m_propModelRequested && m_propModel != 0) {
		STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(m_propModel);
		m_propModelRequested = false;
	}
}

void COverlays::DrawLetterbox()
{
	if (UseNativeLetterbox) {
		CAM::_FORCE_LETTER_BOX_THIS_UPDATE();
		return;
	}

	float h = EMath::Clamp(g_Config.LetterboxRatio, 0.02f, 0.35f);
	// DRAW_RECT takes centered normalized coords
	GRAPHICS::DRAW_RECT(0.5f, h * 0.5f, 1.0f, h, 0, 0, 0, 255, false, false);
	GRAPHICS::DRAW_RECT(0.5f, 1.0f - h * 0.5f, 1.0f, h, 0, 0, 0, 255, false, false);
}

void COverlays::DrawGrid()
{
	const int rows = (std::max)(1, g_Config.GridRows);
	const int cols = (std::max)(1, g_Config.GridColumns);
	const float thickness = 0.0015f;
	const int alpha = 110;

	for (int c = 1; c < cols; c++) {
		float x = (float)c / (float)cols;
		GRAPHICS::DRAW_RECT(x, 0.5f, thickness, 1.0f, 255, 255, 255, alpha, false, false);
	}
	for (int r = 1; r < rows; r++) {
		float y = (float)r / (float)rows;
		GRAPHICS::DRAW_RECT(0.5f, y, 1.0f, thickness * (SCREEN_WIDTH / SCREEN_HEIGHT), 255, 255, 255, alpha, false, false);
	}
}

void COverlays::DrawCameraMarkers()
{
	// The p_camerabox01x prop is the in-world marker; this just floats the
	// camera name above it. Labels disappear with the props while an editor
	// camera is rendering.
	if (g_Director.IsRendering()) return;

	for (int i = 0; i < g_CameraManager.Count(); i++) {
		EditorCamera* cam = g_CameraManager.Get(i);
		if (!cam) continue;

		float sx = 0.0f, sy = 0.0f;
		if (!GRAPHICS::GET_SCREEN_COORD_FROM_WORLD_COORD(cam->pos.x, cam->pos.y, cam->pos.z + 0.35f, &sx, &sy)) {
			continue;
		}

		std::string label = cam->name;
		if (!cam->enabled) label += " (off)";
		Drawing::DrawFormattedText(label, Font::Body, 255, 255, 255, 210, Alignment::Center,
			18, sx * SCREEN_WIDTH, sy * SCREEN_HEIGHT - 20.0f);

		// Short direction tick showing where the camera points
		Vector3 dir = EMath::RotationToDirection(cam->rot);
		Vector3 tip = EMath::Add(cam->pos, EMath::Scale(dir, 0.75f));
		float tx = 0.0f, ty = 0.0f;
		if (GRAPHICS::GET_SCREEN_COORD_FROM_WORLD_COORD(tip.x, tip.y, tip.z, &tx, &ty)) {
			GRAPHICS::DRAW_RECT(tx, ty, 0.003f, 0.005f, 255, 200, 0, 170, false, false);
		}
	}
}

void COverlays::DrawTransitionBar()
{
	float progress = 0.0f;
	if (!g_Director.GetTransitionProgress(progress)) return;

	const float barW = 0.30f;
	const float barH = 0.006f;
	const float y = 0.93f;

	// Track + fill
	GRAPHICS::DRAW_RECT(0.5f, y, barW, barH, 0, 0, 0, 160, false, false);
	float fillW = barW * progress;
	GRAPHICS::DRAW_RECT(0.5f - barW * 0.5f + fillW * 0.5f, y, fillW, barH, 255, 255, 255, 220, false, false);
}
