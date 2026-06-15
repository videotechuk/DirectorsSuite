// Director's Suite - all menu pages.
//
// Static pages are created once in Init(). Pages whose content depends on
// runtime data (the camera list, the per-camera edit pages, presets) are
// rebuilt right before navigation; CNativeMenu::AddSubmenu() overwrites the
// submenu in place, which is safe because options are recreated wholesale.

#pragma once
#include "SubmenuInclude.h"

class CEditorMenus
{
public:
	void Init();

	// Dynamic page rebuilders
	static void RebuildCameraList();
	static void RebuildCameraEdit();
	static void RebuildCameraEditModes();
	static void RebuildCameraEditProperties();
	static void RebuildCameraEditFilter();
	static void RebuildCameraEditWorld();
	static void RebuildCameraEditTarget();
	static void RebuildPresetList();
	static void RebuildPresetEdit();
	static void RebuildOBSMenu();
	static void BuildNamingMenu();      // rebuilt on entry (live template preview)
	static void RebuildCinematicMenu(); // rebuilt on entry so the Camera Mode row reflects the live state

	// Director Mode pages (implemented in DirectorMenus.cpp)
	static void BuildDirectorMenus();         // static pages, called from Init()
	static void RebuildDirectorNPCList();
	static void RebuildDirectorAddNPCList();
	static void RebuildDirectorNPCEdit();
	static void RebuildDirectorNPCPlace();
	static void RebuildDirectorNPCProps();
	static void RebuildDirectorNPCBehaviour();
	static void RebuildDirectorPlayer();
	static void RebuildDirectorHeroLight(bool forPlayer);
	static void RebuildDirectorHeroLightPoint(int pointIndex);

	// Per-frame upkeep for pages with live values (OBS status line, record
	// button label). Call once per frame from the script loop.
	static void Tick();

private:
	void BuildEntryMenu();
	void BuildCamerasMenu();
	void BuildInterfaceMenu();
	void BuildWorldMenus();
	void BuildAimingMenus();
	void BuildGameplayMenu();
	void BuildSettingsMenu();
	void BuildHelpMenus();
	void BuildCreditsMenu();
};

inline CEditorMenus* g_EditorMenus{};
