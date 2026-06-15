// Director's Suite - interface & visual tools: HUD/prompt hiding, cinematic
// letterbox, composition grid, 3D camera markers and the transition
// progress bar. RDR2 has no debug line/box natives, so 3D markers are
// projected to screen space with GET_SCREEN_COORD_FROM_WORLD_COORD.

#pragma once
#include <unordered_map>
#include "..\..\..\inc\types.h"

class COverlays
{
public:
	bool HideHud = false;          // hide HUD and prompts
	bool Letterbox = false;        // cinematic black bars
	bool UseNativeLetterbox = true;// engine letterbox vs drawn bars
	bool ShowGrid = false;         // rule-of-thirds style overlay
	bool ShowCameraMarkers = true; // p_camerabox01x prop + label at every camera
	bool ShowMarkerLabels = true;  // 2D name labels above the props
	bool ShowProgressBar = true;   // camera transition progress bar
	bool ShowOverlaysWhileRecording = false; // draw screen aids (bars, REC, grid) even while OBS records (they WILL appear in the video)

	void Tick(); // draws everything; must run every frame

private:
	void DrawLetterbox();
	void DrawGrid();
	void DrawCameraMarkers();
	void DrawTransitionBar();
	void DrawProjectPlaybackBar();

	// Physical camera markers: one p_camerabox01x prop per stored camera so
	// placed cameras are visible from gameplay, free cam and other cameras.
	struct MarkerProp
	{
		Object obj = 0;
		Vector3 pos{};
		Vector3 rot{};
		bool visible = true;
	};
	std::unordered_map<int, MarkerProp> m_props; // EditorCamera.id -> prop
	Hash m_propModel = 0;
	bool m_propModelRequested = false;

	void SyncCameraProps();
	void RemoveAllProps();
};

inline COverlays g_Overlays;
