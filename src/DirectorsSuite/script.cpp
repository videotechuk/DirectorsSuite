// Director's Suite - an in-game video/cinematic editor for Red Dead Redemption 2
// inspired by GTA V's Rockstar Editor.
//
// RDR2 ships no Rockstar Editor, so this rebuilds the concept from scripted
// cameras + Photo Mode natives:
//   - up to 300 stored cameras with modes, transitions, filters and world overrides
//   - free camera, custom player camera, automatic camera switching
//   - photo-mode freeze, time scale, clock, weather and cloud control
//   - letterbox/grid/marker overlays, aim modes and aim assist
//
// UI is built on the RDR2 Native Menu Base by TuffyTown (Halen84), MIT.

#include "script.h"
#include "common.hpp"
#include "UI\Menu.hpp"
#include "UI\UIUtil.h"
#include "Submenus\EditorMenus.h"
#include "Editor\Config.h"
#include "Editor\CameraManager.h"
#include "Editor\CameraDirector.h"
#include "Editor\FreeCam.h"
#include "Editor\WorldControl.h"
#include "Editor\Overlays.h"
#include "Editor\Aiming.h"
#include "Editor\Gameplay.h"
#include "Editor\DirectorMode.h"
#include "Editor\OBSClient.h"
#include "Editor\PhotoMode.h"
#include "Editor\ScreenCapture.h"
#include "Editor\SceneData.h"

// Surface a finished screenshot capture as an on-screen message.
static void PollScreenshot()
{
	int rc; std::string path;
	if (ScreenCapture::Poll(rc, path)) {
		switch (rc) {
			case ScreenCapture::CAP_OK_PNG:
				UIUtil::PrintSubtitle("Saved lossless screenshot ~COLOR_BLUE~" + path + "~s~");
				break;
			case ScreenCapture::CAP_OK_HDR:
				UIUtil::PrintSubtitle("Saved ~COLOR_BLUE~HDR~s~ screenshot (.jxr) " + path);
				break;
			default:
				UIUtil::PrintSubtitle("~COLOR_RED~Screenshot failed:~s~ " + ScreenCapture::LastError());
				break;
		}
	}
}

// The OBS client runs network I/O on worker threads and reports back through
// an atomic status + message. Surface new messages as on-screen subtitles so
// the user gets feedback even when the menu is closed.
static void PollOBSStatus()
{
	static eOBSStatus lastStatus = eOBSStatus::Idle;
	static std::string lastMsg;

	eOBSStatus s = g_OBS.Status();
	std::string msg = g_OBS.LastMessage();
	if (s != lastStatus || msg != lastMsg) {
		lastStatus = s;
		lastMsg = msg;
		if (!msg.empty()) {
			UIUtil::PrintSubtitle(msg);
		}
	}
}

static void HandleHotkeys()
{
	if (g_Config.KeyPhotoMode && IsKeyJustUp((DWORD)g_Config.KeyPhotoMode)) {
		if (!g_PhotoMode.IsActive()) {
			g_Menu->SetEnabled(false, false);
		}
		g_PhotoMode.Toggle();
	}

	// Screenshot works everywhere (gameplay, free cam, photo mode)
	if (g_Config.KeyScreenshot && IsKeyJustUp((DWORD)g_Config.KeyScreenshot)) {
		if (!ScreenCapture::IsCapturing()) {
			ScreenCapture::RequestCapture();
		}
	}

	// Photo mode owns all input while it is open
	if (g_PhotoMode.IsActive()) {
		return;
	}

	if (g_Config.KeyFreeCamToggle && IsKeyJustUp((DWORD)g_Config.KeyFreeCamToggle)) {
		if (!g_FreeCam.IsActive()) {
			g_Director.Deactivate(false);
		}
		g_FreeCam.Toggle();
		if (!g_FreeCam.IsActive()) {
			UIUtil::PrintSubtitle("Placement Camera Mode ~COLOR_BLUE~OFF~s~");
		}
		// ON gets no extra subtitle - placement mode shows its own instructions
	}

	if (g_Config.KeyAddCamera && IsKeyJustUp((DWORD)g_Config.KeyAddCamera)) {
		// Repositioning an existing camera saves into it instead of creating
		if (g_FreeCam.IsActive() && g_FreeCam.EditingCameraIndex >= 0) {
			int editIdx = g_FreeCam.EditingCameraIndex;
			g_CameraManager.UpdateCameraFromCurrentView(editIdx);
			EditorCamera* cam = g_CameraManager.Get(editIdx);
			UIUtil::PrintSubtitle("Saved new position for ~COLOR_BLUE~" + (cam ? cam->name : "camera") + "~s~");
			g_FreeCam.Deactivate();
		}
		else {
			int idx = g_CameraManager.InsertCamera();
			if (idx < 0) {
				UIUtil::PrintSubtitle("~COLOR_RED~Camera limit reached (300)~s~");
			}
			else {
				UIUtil::PrintSubtitle("Created ~COLOR_BLUE~" + g_CameraManager.Cameras[idx].name + "~s~");
			}
		}
	}

	if (g_Config.KeyNextCamera && IsKeyJustUp((DWORD)g_Config.KeyNextCamera)) {
		g_FreeCam.Deactivate();
		g_Director.SwitchCamera(1);
	}

	if (g_Config.KeyPrevCamera && IsKeyJustUp((DWORD)g_Config.KeyPrevCamera)) {
		g_FreeCam.Deactivate();
		g_Director.SwitchCamera(-1);
	}

	if (g_Config.KeyCameraAutoSwitchingStartStop && IsKeyJustUp((DWORD)g_Config.KeyCameraAutoSwitchingStartStop)) {
		g_Director.ToggleAutoSwitching();
		UIUtil::PrintSubtitle(g_Director.IsAutoSwitching() ? "Auto camera switching ~COLOR_BLUE~started~s~" : "Auto camera switching ~COLOR_BLUE~stopped~s~");
	}

	if (g_Config.KeyPlayerCamToggle && IsKeyJustUp((DWORD)g_Config.KeyPlayerCamToggle)) {
		if (g_Director.IsPlayerCamActive()) {
			g_Director.Deactivate(true);
		}
		else {
			g_FreeCam.Deactivate();
			g_Director.ActivatePlayerCamera();
		}
	}
}

void main()
{
	g_Config.Load();

	// Parse the Scene Editor catalogues (objects + scenarios) shipped next to
	// the game exe. Falls back to built-in lists if the files are missing.
	g_SceneData.Load();

	// Reserve up front so EditorCamera pointers held by open menu pages are
	// never invalidated by vector growth.
	g_CameraManager.Cameras.reserve(MAX_CAMERAS);

	g_Menu = std::make_unique<CNativeMenu>();

	g_EditorMenus = new CEditorMenus();
	g_EditorMenus->Init();
	g_Menu->GoToSubmenu(Submenu_EntryMenu);

	if (!UIUtil::GetScreenDimensions()) {
		PRINT_WARN("Failed to get the RDR2 window dimensions. The UI may be sized incorrectly.");
	}

	while (true)
	{
		// Photo mode runs first so it can consume hotkeys (menu open, camera
		// placement...) before anything else reacts to them.
		g_PhotoMode.Tick();

		g_Menu->Update();
		CEditorMenus::Tick();

		HandleHotkeys();
		PollOBSStatus();
		PollScreenshot();

		g_FreeCam.Tick();
		g_Director.Tick();
		g_World.Tick();
		g_Overlays.Tick();
		g_Aiming.Tick();
		g_Gameplay.Tick();
		g_DirectorMode.Tick();

		WAIT(0);
	}
}

#pragma warning(disable:28159)
void WaitAndDraw(unsigned ms)
{
	DWORD time = GetTickCount() + ms;
	bool waited = false;
	while (GetTickCount() < time || !waited)
	{
		WAIT(0);
		waited = true;
		if (g_Menu) {
			g_Menu->Update();
		}
	}
}
#pragma warning(default:28159)

void ScriptMain()
{
	main();
}
