#include "EditorMenus.h"
#include "..\Editor\KeyBind.h"
#include <sstream>

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static std::vector<std::string> FloatRange(float start, float end, float step, int decimals = 2)
{
	std::vector<std::string> out;
	char buf[32];
	for (float v = start; v <= end + step * 0.5f; v += step) {
		sprintf_s(buf, "%.*f", decimals, v);
		out.emplace_back(buf);
	}
	return out;
}

static int FloatIndex(float value, float start, float step, int count)
{
	int idx = (int)((value - start) / step + 0.5f);
	if (idx < 0) idx = 0;
	if (idx > count - 1) idx = count - 1;
	return idx;
}

static float FloatFromIndex(int idx, float start, float step)
{
	return start + (float)idx * step;
}

// Re-bake the selected camera into the engine if we are currently looking
// through it, so property edits are visible immediately.
static void LiveRefresh()
{
	int idx = g_CameraManager.SelectedIndex;
	if (idx >= 0 && g_Director.IsRendering() && g_Director.ActiveIndex() == idx) {
		g_Director.ActivateCamera(idx, false);
	}
}

static EditorCamera* SelCam()
{
	return g_CameraManager.GetSelected();
}

// Mirror state for bitmask-backed bool options (BoolOption needs a bool*)
static bool sModeFollow = false;
static bool sModeLookAt = false;
static bool sModeSameAngle = false;
static bool sModeFreeAngle = false;
static bool sFollowRelative = true;
static bool sCamEnabled = true;
static bool sFocusPaused = false;
static bool sHardCut = false;

// Set when a Camera Properties option needs the page re-skinned (e.g. Hard Cut
// greys/ungreys the Transition row). Rebuilding the active page from inside an
// option's own callback would tear down the running callback, so the rebuild is
// deferred to CEditorMenus::Tick().
static bool sPropsRebuildPending = false;

static bool sTargetHumansCombat = true;
static bool sTargetHumansAll = false;
static bool sTargetAnimals = false;
static bool sTargetModels = false;
static bool sTargetSpecific = false;

static bool sFreeCamActive = false;
static bool sAutoSwitching = false;
static bool sFreezeGame = false;
static bool sFreezeTimeOfDay = false;
static bool sWeatherFrozen = false;

static void SyncModeFlagsFromStatics()
{
	EditorCamera* cam = SelCam();
	if (!cam) return;
	unsigned flags = 0;
	if (sModeFollow)    flags |= CAMMODE_FOLLOW;
	if (sModeLookAt)    flags |= CAMMODE_LOOKAT;
	if (sModeSameAngle) flags |= CAMMODE_SAME_ANGLE;
	if (sModeFreeAngle) flags |= CAMMODE_FREE_ANGLE;
	cam->modeFlags = flags;
	LiveRefresh();
}

static void SyncTargetMaskFromStatics()
{
	unsigned mask = 0;
	if (sTargetHumansCombat) mask |= AA_TARGET_HUMANS_COMBAT;
	if (sTargetHumansAll)    mask |= AA_TARGET_HUMANS_ALL;
	if (sTargetAnimals)      mask |= AA_TARGET_ANIMALS;
	if (sTargetModels)       mask |= AA_TARGET_MODELS;
	if (sTargetSpecific)     mask |= AA_TARGET_SPECIFIC_PED;
	g_Aiming.TargetsMask = mask;
}

// ---------------------------------------------------------------------------
// Dynamic pages
// ---------------------------------------------------------------------------

void CEditorMenus::RebuildCameraList()
{
	g_Menu->AddSubmenu("DIRECTOR'S SUITE", "Cameras (" + std::to_string(g_CameraManager.Count()) + "/" + std::to_string(MAX_CAMERAS) + ")", Submenu_Cameras_List, 10, [](Submenu* sub)
	{
		if (g_CameraManager.Count() == 0) {
			sub->AddRegularOption("No cameras yet", "Use Insert Camera, or press the Add Camera key while playing");
			return;
		}

		for (int i = 0; i < g_CameraManager.Count(); i++) {
			EditorCamera* cam = g_CameraManager.Get(i);
			std::string label = cam->name;
			if (g_Director.ActiveIndex() == i) label += " [ACTIVE]";
			else if (!cam->enabled) label += " [off]";

			sub->AddRegularOption(label, "Edit this camera", [i] {
				g_CameraManager.SelectedIndex = i;
				CEditorMenus::RebuildCameraEdit();
				g_Menu->GoToSubmenu(Submenu_Camera_Edit);
			});
		}
	});
}

void CEditorMenus::RebuildCameraEdit()
{
	EditorCamera* cam = SelCam();
	if (!cam) return;

	sCamEnabled = cam->enabled;

	g_Menu->AddSubmenu("DIRECTOR'S SUITE", cam->name, Submenu_Camera_Edit, 12, [](Submenu* sub)
	{
		sub->AddRegularOption("Camera Properties", "Zoom, shake, duration, smoothness, blur, focus and more", [] {
			CEditorMenus::RebuildCameraEditProperties();
			g_Menu->GoToSubmenu(Submenu_Camera_Edit_Properties);
		});

		sub->AddRegularOption("Edit Camera Placement", "Flies this camera with its settings panel open. Frame the new spot (controls stay active), then Finish Placing to save the new position", [] {
			if (EditorCamera* c = SelCam()) {
				g_Director.Deactivate(false);
				g_FreeCam.ActivateAt(c->pos, c->rot, c->fov);
				g_FreeCam.EditingCameraIndex = g_CameraManager.SelectedIndex;
				// Open the settings panel and keep the menu up while flying.
				CEditorMenus::RebuildCameraEditProperties();
				g_Menu->GoToSubmenu(Submenu_Camera_Edit_Properties);
			}
		});

		sub->AddRegularOption("Activate (View Through)", "Switch to this camera using its transition settings", [] {
			g_Director.ActivateCamera(g_CameraManager.SelectedIndex, true);
		});

		sub->AddBoolOption("Enabled", "Disabled cameras are skipped by auto-switching and project playback", &sCamEnabled, [] {
			if (EditorCamera* c = SelCam()) c->enabled = sCamEnabled;
		});

		sub->AddRegularOption("Update From Current View", "Re-capture position, rotation and zoom from whatever the camera currently sees", [] {
			g_CameraManager.UpdateCameraFromCurrentView(g_CameraManager.SelectedIndex);
			UIUtil::PrintSubtitle("Camera updated from current view");
		});

		sub->AddRegularOption("Camera Modes", "Follow, Look At, Same Angle, Free Angle - combine as desired", [] {
			CEditorMenus::RebuildCameraEditModes();
			g_Menu->GoToSubmenu(Submenu_Camera_Edit_Modes);
		});

		sub->AddRegularOption("Photo Mode Filter", "Apply one of the Photo Mode filters to this camera", [] {
			CEditorMenus::RebuildCameraEditFilter();
			g_Menu->GoToSubmenu(Submenu_Camera_Edit_Filter);
		});

		sub->AddRegularOption("Weather & Time", "Weather and time-of-day overrides applied while this camera is active", [] {
			CEditorMenus::RebuildCameraEditWorld();
			g_Menu->GoToSubmenu(Submenu_Camera_Edit_World);
		});

		sub->AddRegularOption("Target Ped", "Choose which ped this camera follows / looks at", [] {
			CEditorMenus::RebuildCameraEditTarget();
			g_Menu->GoToSubmenu(Submenu_Camera_Edit_Target);
		});

		sub->AddRegularOption("Save As Preset", "Store this camera's properties as a reusable preset", [] {
			if (EditorCamera* c = SelCam()) {
				g_CameraManager.SavePresetFromCamera(*c, "Preset " + std::to_string((int)g_CameraManager.Presets.size() + 1));
				UIUtil::PrintSubtitle("Preset saved");
			}
		});

		sub->AddRegularOption("~COLOR_RED~Delete Camera~s~", "Remove this camera", [] {
			g_CameraManager.DeleteCamera(g_CameraManager.SelectedIndex);
			CEditorMenus::RebuildCameraList();
			g_Menu->GoToSubmenu(Submenu_Cameras_List);
			g_Menu->SetSelectionIndex(g_Menu->GetSelectionIndex());
			UIUtil::PrintSubtitle("Camera deleted");
		});
	});
}

void CEditorMenus::RebuildCameraEditModes()
{
	EditorCamera* cam = SelCam();
	if (!cam) return;

	sModeFollow = (cam->modeFlags & CAMMODE_FOLLOW) != 0;
	sModeLookAt = (cam->modeFlags & CAMMODE_LOOKAT) != 0;
	sModeSameAngle = (cam->modeFlags & CAMMODE_SAME_ANGLE) != 0;
	sModeFreeAngle = (cam->modeFlags & CAMMODE_FREE_ANGLE) != 0;
	sFollowRelative = cam->followRelativeToHeading;

	g_Menu->AddSubmenu("DIRECTOR'S SUITE", "Camera Modes", Submenu_Camera_Edit_Modes, 8, [](Submenu* sub)
	{
		sub->AddBoolOption("Follow", "Camera moves with the target, keeping its current offset", &sModeFollow, [] { SyncModeFlagsFromStatics(); });
		sub->AddBoolOption("Look At", "Camera rotation always tracks the target", &sModeLookAt, [] { SyncModeFlagsFromStatics(); });
		sub->AddBoolOption("Same Angle", "Camera translates with the target while keeping the exact same world angle", &sModeSameAngle, [] { SyncModeFlagsFromStatics(); });
		sub->AddBoolOption("Free Angle (orbit)", "Orbit the target freely with the look controls. Follow + Look At + Free Angle = custom player camera", &sModeFreeAngle, [] { SyncModeFlagsFromStatics(); });
		sub->AddBoolOption("Offset Rotates With Target", "Follow mode: rotate the camera offset with the target's heading", &sFollowRelative, [] {
			if (EditorCamera* c = SelCam()) c->followRelativeToHeading = sFollowRelative;
		});

		auto orbit = FloatRange(1.0f, 30.0f, 0.5f, 1);
		int orbitCount = (int)orbit.size();
		sub->AddVectorOption("Orbit Distance", "Free Angle orbit radius", orbit, [] {
			if (EditorCamera* c = SelCam()) {
				c->orbitDistance = FloatFromIndex(g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex(), 1.0f, 0.5f);
			}
		})->SetVectorIndex(FloatIndex(SelCam()->orbitDistance, 1.0f, 0.5f, orbitCount));
	});
}

void CEditorMenus::RebuildCameraEditProperties()
{
	EditorCamera* cam = SelCam();
	if (!cam) return;

	sFocusPaused = cam->focusPaused;
	sHardCut = cam->hardCut;

	g_Menu->AddSubmenu("DIRECTOR'S SUITE", "Camera Properties", Submenu_Camera_Edit_Properties, 12, [](Submenu* sub)
	{
		EditorCamera* cam = SelCam();

		// While placing this camera, a clear "done" action that bakes in the
		// current framed view and leaves Placement Camera Mode.
		if (g_FreeCam.IsActive()) {
			RegularOption* fin = sub->AddRegularOption("Finish Placing This Camera", "Save this shot at the current view, leave Placement Camera Mode and return to the Camera Shots menu", [] {
				g_FreeCam.Deactivate();              // bakes the live view into the camera
				// Return to Camera Shots (clean back-stack) instead of closing the
				// whole menu, so you can keep working / place another shot.
				g_Menu->OpenAt(Submenu_Cameras);
				UIUtil::PrintSubtitle("Camera placed");
			});
			fin->TextR = 120; fin->TextG = 230; fin->TextB = 140;
		}

		// Zoom (FOV)
		auto fovs = FloatRange(5.0f, 130.0f, 1.0f, 0);
		sub->AddVectorOption("Zoom (FOV)", "Lower FOV = stronger zoom", fovs, [] {
			if (EditorCamera* c = SelCam()) {
				c->fov = FloatFromIndex(g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex(), 5.0f, 1.0f);
				// While composing in Placement Camera Mode, push the lens to the live
				// camera so the framing updates as you drag the slider.
				if (g_FreeCam.IsActive()) g_FreeCam.SetFov(c->fov);
				LiveRefresh();
			}
		})->SetVectorIndex(FloatIndex(cam->fov, 5.0f, 1.0f, (int)fovs.size()));

		// Duration (auto switching hold time), in seconds. Up to 10 minutes.
		auto durations = FloatRange(0.5f, 600.0f, 0.5f, 1);
		sub->AddVectorOption("Duration (seconds)", "How long this camera holds during automatic switching / playback (up to 10 min)", durations, [] {
			if (EditorCamera* c = SelCam()) {
				float sec = FloatFromIndex(g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex(), 0.5f, 0.5f);
				c->durationMs = (int)(sec * 1000.0f + 0.5f);
			}
		})->SetVectorIndex(FloatIndex(cam->durationMs / 1000.0f, 0.5f, 0.5f, (int)durations.size()));

		// Hard cut: snap straight to this shot with no blend. Greys out Transition.
		sub->AddBoolOption("Hard Cut", "Snap straight to this shot with no blend. Disables the transition time below", &sHardCut, [] {
			if (EditorCamera* c = SelCam()) {
				c->hardCut = sHardCut;
				sPropsRebuildPending = true; // re-skin so Transition greys/ungreys
			}
		});

		// Transition time, in seconds (up to 10 min). Locked out while Hard Cut is
		// on - shown greyed so the row keeps its place when it re-skins on toggle.
		if (cam->hardCut) {
			Option* o = sub->AddRegularOption("Transition (seconds)   [ Hard Cut ]",
				"This shot is a hard cut - turn off Hard Cut to set a transition time", [] {
					UIUtil::PrintSubtitle("~COLOR_YELLOW~Hard Cut is on~s~ - turn it off to set a transition time.");
				});
			o->TextR = 120; o->TextG = 120; o->TextB = 120; // greyed = disabled
		}
		else {
			auto transitions = FloatRange(0.5f, 600.0f, 0.5f, 1);
			sub->AddVectorOption("Transition (seconds)", "Interpolation time when switching to this camera (up to 10 min)", transitions, [] {
				if (EditorCamera* c = SelCam()) {
					float sec = FloatFromIndex(g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex(), 0.5f, 0.5f);
					c->transitionMs = (int)(sec * 1000.0f + 0.5f);
				}
			})->SetVectorIndex(FloatIndex(cam->transitionMs / 1000.0f, 0.5f, 0.5f, (int)transitions.size()));
		}

		// Smoothness
		sub->AddVectorOption("Smoothness (Position)", "Easing applied to the camera position during transitions", EaseNames, [] {
			if (EditorCamera* c = SelCam()) c->easeLocation = g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex();
		})->SetVectorIndex(cam->easeLocation);

		sub->AddVectorOption("Smoothness (Rotation)", "Easing applied to the camera rotation during transitions", EaseNames, [] {
			if (EditorCamera* c = SelCam()) c->easeRotation = g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex();
		})->SetVectorIndex(cam->easeRotation);

		// Shake
		sub->AddVectorOption("Shake", "Camera shake type", CamShakeNames, [] {
			if (EditorCamera* c = SelCam()) {
				c->shakeIndex = g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex();
				LiveRefresh();
			}
		})->SetVectorIndex(cam->shakeIndex);

		auto amplitudes = FloatRange(0.05f, 2.0f, 0.05f, 2);
		sub->AddVectorOption("Shake Amplitude", "Shake intensity", amplitudes, [] {
			if (EditorCamera* c = SelCam()) {
				c->shakeAmplitude = FloatFromIndex(g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex(), 0.05f, 0.05f);
				LiveRefresh();
			}
		})->SetVectorIndex(FloatIndex(cam->shakeAmplitude, 0.05f, 0.05f, (int)amplitudes.size()));

		// Handheld / phone operator motion (organic sway, distinct from Shake)
		sub->AddVectorOption("Handheld Motion", "Organic 'holding a phone' sway/roll/breathing - applied during playback", HandheldStyleNames, [] {
			if (EditorCamera* c = SelCam()) {
				c->handheldStyle = g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex();
				LiveRefresh();
			}
		})->SetVectorIndex(cam->handheldStyle);

		auto hhIntensity = FloatRange(0.05f, 1.0f, 0.05f, 2);
		sub->AddVectorOption("Handheld Intensity", "How strong the handheld motion is (0.05 subtle - 1.0 full)", hhIntensity, [] {
			if (EditorCamera* c = SelCam()) {
				c->handheldIntensity = FloatFromIndex(g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex(), 0.05f, 0.05f);
				LiveRefresh();
			}
		})->SetVectorIndex(FloatIndex(cam->handheldIntensity, 0.05f, 0.05f, (int)hhIntensity.size()));

		// Motion blur
		auto blur01 = FloatRange(0.0f, 1.0f, 0.05f, 2);
		sub->AddVectorOption("Motion Blur", "Per-camera motion blur strength", blur01, [] {
			if (EditorCamera* c = SelCam()) {
				c->motionBlur = FloatFromIndex(g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex(), 0.0f, 0.05f);
				LiveRefresh();
			}
		})->SetVectorIndex(FloatIndex(cam->motionBlur, 0.0f, 0.05f, (int)blur01.size()));

		// DOF blur strength
		sub->AddVectorOption("Blur Strength (DOF)", "Depth-of-field blur. Appears much stronger in dark scenes - use low values at night", blur01, [] {
			if (EditorCamera* c = SelCam()) {
				c->blurStrength = FloatFromIndex(g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex(), 0.0f, 0.05f);
				LiveRefresh();
			}
		})->SetVectorIndex(FloatIndex(cam->blurStrength, 0.0f, 0.05f, (int)blur01.size()));

		// Focus distance
		std::vector<std::string> focus;
		focus.push_back("Auto");
		auto focusRange = FloatRange(0.5f, 150.0f, 0.5f, 1);
		focus.insert(focus.end(), focusRange.begin(), focusRange.end());
		int focusIdx = cam->focusDistance < 0.0f ? 0 : 1 + FloatIndex(cam->focusDistance, 0.5f, 0.5f, (int)focusRange.size());
		sub->AddVectorOption("Focus Distance", "Photo Mode style focus distance in meters", focus, [] {
			if (EditorCamera* c = SelCam()) {
				int idx = g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex();
				c->focusDistance = (idx == 0) ? -1.0f : FloatFromIndex(idx - 1, 0.5f, 0.5f);
				LiveRefresh();
			}
		})->SetVectorIndex(focusIdx);

		sub->AddBoolOption("Pause Focus", "Lock the focus so it does not track", &sFocusPaused, [] {
			if (EditorCamera* c = SelCam()) {
				c->focusPaused = sFocusPaused;
				LiveRefresh();
			}
		});
	});
}

void CEditorMenus::RebuildCameraEditFilter()
{
	EditorCamera* cam = SelCam();
	if (!cam) return;

	g_Menu->AddSubmenu("DIRECTOR'S SUITE", "Photo Mode Filter", Submenu_Camera_Edit_Filter, 12, [](Submenu* sub)
	{
		for (int i = 0; i < (int)PhotoModeFilters.size(); i++) {
			std::string label = (i == 0) ? "None" : PhotoModeFilters[i];
			EditorCamera* c = SelCam();
			if (c && c->filterIndex == i) label += " ~COLOR_BLUE~(current)~s~";
			sub->AddRegularOption(label, "Apply this Photo Mode filter to the camera", [i] {
				if (EditorCamera* c2 = SelCam()) {
					c2->filterIndex = i;
					LiveRefresh();
					UIUtil::PrintSubtitle(i == 0 ? "Filter removed" : ("Filter: " + std::string(PhotoModeFilters[i])));
				}
			});
		}
	});
}

void CEditorMenus::RebuildCameraEditWorld()
{
	EditorCamera* cam = SelCam();
	if (!cam) return;

	g_Menu->AddSubmenu("DIRECTOR'S SUITE", "Weather & Time", Submenu_Camera_Edit_World, 8, [](Submenu* sub)
	{
		EditorCamera* cam = SelCam();

		sub->AddVectorOption("Weather", "Weather forced while this camera is active", WeatherTypeNames, [] {
			if (EditorCamera* c = SelCam()) {
				c->weatherIndex = g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex();
				LiveRefresh();
			}
		})->SetVectorIndex(cam->weatherIndex);

		std::vector<std::string> hours = { "No Change" };
		for (int h = 0; h < 24; h++) hours.push_back(std::to_string(h) + ":00");
		sub->AddVectorOption("Time Of Day (Hour)", "Clock hour forced while this camera is active", hours, [] {
			if (EditorCamera* c = SelCam()) {
				int idx = g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex();
				c->todHour = (idx == 0) ? -1 : idx - 1;
				LiveRefresh();
			}
		})->SetVectorIndex(cam->todHour < 0 ? 0 : cam->todHour + 1);

		std::vector<std::string> minutes;
		for (int m = 0; m < 60; m += 5) minutes.push_back(std::to_string(m));
		sub->AddVectorOption("Time Of Day (Minute)", "", minutes, [] {
			if (EditorCamera* c = SelCam()) {
				c->todMinute = g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex() * 5;
				LiveRefresh();
			}
		})->SetVectorIndex(cam->todMinute / 5);
	});
}

void CEditorMenus::RebuildCameraEditTarget()
{
	EditorCamera* cam = SelCam();
	if (!cam) return;

	g_Menu->AddSubmenu("DIRECTOR'S SUITE", "Target: " + g_Director.DescribeTarget(*cam), Submenu_Camera_Edit_Target, 8, [](Submenu* sub)
	{
		sub->AddRegularOption("Next Target", "Cycle forward through nearby peds (Player first)", [] {
			if (EditorCamera* c = SelCam()) {
				g_Director.CycleTarget(*c, 1);
				UIUtil::PrintSubtitle("Target: " + g_Director.DescribeTarget(*c));
			}
		});

		sub->AddRegularOption("Previous Target", "Cycle backward through nearby peds", [] {
			if (EditorCamera* c = SelCam()) {
				g_Director.CycleTarget(*c, -1);
				UIUtil::PrintSubtitle("Target: " + g_Director.DescribeTarget(*c));
			}
		});

		sub->AddRegularOption("Reset To Player", "", [] {
			if (EditorCamera* c = SelCam()) {
				c->targetIsPlayer = true;
				c->targetPed = 0;
				UIUtil::PrintSubtitle("Target: Player");
			}
		});

		sub->AddVectorOption("Animal Priority", "Prioritise dead or alive animals when cycling targets", std::vector<const char*>{ "None", "Alive Animals First", "Dead Animals First" }, [] {
			g_Director.AnimalPriority = g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex();
		})->SetVectorIndex(g_Director.AnimalPriority);
	});
}

void CEditorMenus::RebuildPresetList()
{
	g_Menu->AddSubmenu("DIRECTOR'S SUITE", "Camera Presets", Submenu_Camera_Presets, 10, [](Submenu* sub)
	{
		// Auto-apply preset to newly created cameras
		std::vector<std::string> names = { "None" };
		for (const auto& p : g_CameraManager.Presets) names.push_back(p.name);
		int idx = g_CameraManager.NewCameraPresetIndex;
		if (idx > (int)g_CameraManager.Presets.size()) idx = 0;
		sub->AddVectorOption("Apply To New Cameras", "Preset automatically applied to every inserted camera", names, [] {
			g_CameraManager.NewCameraPresetIndex = g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex();
		})->SetVectorIndex(idx);

		if (g_CameraManager.Presets.empty()) {
			sub->AddRegularOption("No presets yet", "Open a camera and choose Save As Preset");
			return;
		}

		for (int i = 0; i < (int)g_CameraManager.Presets.size(); i++) {
			sub->AddRegularOption(g_CameraManager.Presets[i].name, "Edit / apply this preset", [i] {
				g_CameraManager.SelectedPresetIndex = i;
				CEditorMenus::RebuildPresetEdit();
				g_Menu->GoToSubmenu(Submenu_Camera_Preset_Edit);
			});
		}
	});
}

void CEditorMenus::RebuildPresetEdit()
{
	int pidx = g_CameraManager.SelectedPresetIndex;
	if (pidx < 0 || pidx >= (int)g_CameraManager.Presets.size()) return;

	g_Menu->AddSubmenu("DIRECTOR'S SUITE", g_CameraManager.Presets[pidx].name, Submenu_Camera_Preset_Edit, 8, [](Submenu* sub)
	{
		sub->AddRegularOption("Apply To Selected Camera", "", [] {
			int p = g_CameraManager.SelectedPresetIndex;
			EditorCamera* c = SelCam();
			if (c && p >= 0 && p < (int)g_CameraManager.Presets.size()) {
				g_CameraManager.ApplyPreset(g_CameraManager.Presets[p], *c);
				LiveRefresh();
				UIUtil::PrintSubtitle("Preset applied to " + c->name);
			}
		});

		sub->AddRegularOption("Apply To All Cameras", "", [] {
			int p = g_CameraManager.SelectedPresetIndex;
			if (p >= 0 && p < (int)g_CameraManager.Presets.size()) {
				for (auto& cam : g_CameraManager.Cameras) {
					g_CameraManager.ApplyPreset(g_CameraManager.Presets[p], cam);
				}
				UIUtil::PrintSubtitle("Preset applied to all cameras");
			}
		});

		sub->AddRegularOption("~COLOR_RED~Delete Preset~s~", "", [] {
			int p = g_CameraManager.SelectedPresetIndex;
			if (p >= 0 && p < (int)g_CameraManager.Presets.size()) {
				g_CameraManager.Presets.erase(g_CameraManager.Presets.begin() + p);
				if (g_CameraManager.NewCameraPresetIndex == p + 1) g_CameraManager.NewCameraPresetIndex = 0;
				g_CameraManager.SelectedPresetIndex = -1;
				CEditorMenus::RebuildPresetList();
				g_Menu->GoToSubmenu(Submenu_Camera_Presets);
				UIUtil::PrintSubtitle("Preset deleted");
			}
		});
	});
}

static std::string OBSStatusText()
{
	switch (g_OBS.Status()) {
		case eOBSStatus::Working:   return "~COLOR_YELLOW~Working...~s~";
		case eOBSStatus::Recording: return "~COLOR_RED~Recording~s~";
		case eOBSStatus::Stopped:   return "~COLOR_GREEN~Connected / idle~s~";
		case eOBSStatus::Error:     return "~COLOR_RED~Error~s~";
		default:                    return "Not connected";
	}
}

// Option indexes that CEditorMenus::Tick() refreshes live while the page is open
static constexpr int OBS_OPT_STATUS = 0;
static constexpr int OBS_OPT_RECORD = 7;

void CEditorMenus::RebuildOBSMenu()
{
	g_Menu->AddSubmenu("DIRECTOR'S SUITE", "OBS Recording Setup", Submenu_Settings_OBS, 8, [](Submenu* sub)
	{
		// 0: live status line (text + footer refreshed every frame in Tick())
		sub->AddRegularOption("Status: " + OBSStatusText(),
			g_OBS.LastMessage().empty() ? "Live connection state. Use Test Connection below to check the link" : g_OBS.LastMessage());

		// 1-4: toggles, persisted to the INI the moment they change
		sub->AddBoolOption("Enable OBS Integration", "Master switch. When on, Play Full Project starts and stops an OBS recording for you", &g_Config.OBSEnabled, [] { g_Config.Save(); });
		sub->AddBoolOption("Auto-Record Project Playback", "Start recording when Play Full Project begins, stop when it ends", &g_Config.OBSAutoRecord, [] { g_Config.Save(); });
		sub->AddBoolOption("Hide HUD During Playback", "Hide the HUD and prompts while the project plays for a clean capture", &g_Config.OBSHideHudDuringPlayback, [] { g_Config.Save(); });
		sub->AddBoolOption("Show Overlays In Recording", "Draw the progress bars, REC indicator and grid even while recording - they WILL appear in the video. Leave off for clean clips", &g_Overlays.ShowOverlaysWhileRecording);

		// 5: connection summary
		sub->AddRegularOption("Server: " + g_Config.OBSHost + ":" + std::to_string(g_Config.OBSPort)
			+ (g_Config.OBSPassword.empty() ? "  -  no password" : "  -  password set"),
			"Edit Host/Port/Password under [OBS] in DirectorsSuite.ini, then Settings > Reload INI. The password must match OBS Tools > WebSocket Server Settings > Show Connect Info");

		// 6: test
		sub->AddRegularOption("Test Connection", "Connect to OBS and authenticate without recording. The status line above shows the result", [] {
			g_OBS.Host = g_Config.OBSHost;
			g_OBS.Port = g_Config.OBSPort;
			g_OBS.Password = g_Config.OBSPassword;
			g_OBS.TestConnection();
		});

		// 7: manual record toggle (label flips live in Tick())
		sub->AddRegularOption("Start Recording Now", "Manually start or stop an OBS recording, outside of project playback", [] {
			if (g_OBS.IsRecording()) {
				g_OBS.StopRecording();
			}
			else {
				g_OBS.Host = g_Config.OBSHost;
				g_OBS.Port = g_Config.OBSPort;
				g_OBS.Password = g_Config.OBSPassword;
				g_OBS.StartRecording();
			}
		});
	});
}

// Keeps the OBS page's status line and record button current while it is open.
// Mutating option text in place is safe - only AddSubmenu rebuilds destroy options.
void CEditorMenus::Tick()
{
	if (!g_Menu || !g_Menu->IsOpen()) return;
	Submenu* sub = g_Menu->GetCurrentSubmenu();
	if (!sub) return;

	// Backing out of the placement settings page leaves Placement Camera Mode too.
	// The settings panel auto-opens during placement, so a plain menu "back" would
	// otherwise keep navigating out (and eventually close the whole menu) while the
	// camera is still flying. Catching it here means one Backspace exits placement
	// and lands on the Camera Shots menu (saving the camera). Only for placement
	// (EditingCameraIndex >= 0), not the standalone free camera.
	if (g_FreeCam.IsActive() && g_FreeCam.EditingCameraIndex >= 0 && sub->ID != Submenu_Camera_Edit_Properties) {
		g_FreeCam.Deactivate();
		UIUtil::PrintSubtitle("Placement Camera Mode is now off");
	}

	// Deferred re-skin of the Camera Properties page (Hard Cut greys/ungreys the
	// Transition row). Done here, outside the option's own callback, so rebuilding
	// the active page can't tear down the running callback. The row count is
	// unchanged, so the cursor keeps its place.
	if (sPropsRebuildPending) {
		sPropsRebuildPending = false;
		if (sub->ID == Submenu_Camera_Edit_Properties) {
			int sel = g_Menu->GetSelectionIndex();
			RebuildCameraEditProperties();
			g_Menu->SetSelectionIndex(sel);
		}
	}

	if (sub->ID != Submenu_Settings_OBS) return;

	if (Option* status = sub->GetOption(OBS_OPT_STATUS)) {
		status->Text = "Status: " + OBSStatusText();
		if (!g_OBS.LastMessage().empty()) {
			status->Footer = g_OBS.LastMessage();
		}
	}
	if (Option* record = sub->GetOption(OBS_OPT_RECORD)) {
		record->Text = g_OBS.IsRecording() ? "Stop Recording Now" : "Start Recording Now";
	}
}

// ---------------------------------------------------------------------------
// Static pages
// ---------------------------------------------------------------------------

void CEditorMenus::BuildEntryMenu()
{
	// Tidied top level: the two creative hubs first (Cameras + Director, which
	// now owns world/time/weather and gameplay), then the support menus. Photo
	// Mode is intentionally NOT listed here - it opens with its F1 hotkey.
	g_Menu->AddSubmenu("DIRECTOR'S SUITE", "Direct and edit video content", Submenu_EntryMenu, 8, [](Submenu* sub)
	{
		sub->AddSubmenuOption("Camera Shots", "Create, edit and play back your saved camera angles (up to 300)", Submenu_Cameras);
		sub->AddSubmenuOption("Scene & World", "Place NPCs and direct your character; control time, weather, world and gameplay", Submenu_Director);
		sub->AddRegularOption("Free Cam & Playback", "Free-fly camera, follow-player camera, cutscene unlock and auto-switch playback", [] {
			CEditorMenus::RebuildCinematicMenu(); // refresh so Camera Mode shows the live state
			g_Menu->GoToSubmenu(Submenu_Cinematic);
		});
		sub->AddSubmenuOption("Screen Overlays", "HUD, letterbox bars, composition grid, camera markers, progress bar", Submenu_Interface);
		sub->AddSubmenuOption("Aim Assist", "Aim modes and target lock-on assist", Submenu_Aiming);
		sub->AddSubmenuOption("Settings", "Keybinds, free-cam tuning, INI, OBS recording", Submenu_Settings);
		sub->AddSubmenuOption("Help", "Guides for getting started, camera modes, recording and troubleshooting", Submenu_Help);
		sub->AddSubmenuOption("Credits", "The people behind Director's Suite", Submenu_Credits);
	});
}

void CEditorMenus::BuildCamerasMenu()
{
	g_Menu->AddSubmenu("DIRECTOR'S SUITE", "Camera Shots", Submenu_Cameras, 10, [](Submenu* sub)
	{
		sub->AddRegularOption("Place a Camera (Placement Mode)", "Drops a camera here and opens its settings panel. Fly to frame the shot (controls stay active) and tweak its settings, then Finish Placing. Select again to exit", [] {
			if (g_FreeCam.IsActive()) {
				g_FreeCam.Deactivate();
				UIUtil::PrintSubtitle("Placement Camera Mode is now off");
				return;
			}
			g_Director.Deactivate(false);
			int idx = g_CameraManager.InsertCamera();
			if (idx < 0) { UIUtil::PrintSubtitle("~COLOR_RED~Camera limit reached (300)~s~"); return; }
			EditorCamera* c = g_CameraManager.Get(idx);
			g_CameraManager.SelectedIndex = idx;
			g_FreeCam.ActivateAt(c->pos, c->rot, c->fov);
			g_FreeCam.EditingCameraIndex = idx;       // ActivateAt resets this, so set it after
			// Open the shot's settings automatically; the menu stays up and you can
			// fly at the same time.
			CEditorMenus::RebuildCameraEditProperties();
			g_Menu->GoToSubmenu(Submenu_Camera_Edit_Properties);
		});

		sub->AddRegularOption("Adjust Camera Settings", "While in Placement Camera Mode: tweak this shot's zoom, duration, transition, shake, blur and more. Zoom updates the live view; fly on to fine-tune", [] {
			if (!g_FreeCam.IsActive()) {
				UIUtil::PrintSubtitle("~COLOR_YELLOW~Enter Placement Camera Mode first~s~ (Place a Camera)");
				return;
			}
			int idx = g_FreeCam.EditingCameraIndex;
			if (idx < 0) {
				// Placing a new camera: drop it at the current view so there is a real
				// camera to edit, then keep flying to fine-tune (INSERT re-saves it).
				idx = g_CameraManager.InsertCamera();
				if (idx < 0) { UIUtil::PrintSubtitle("~COLOR_RED~Camera limit reached (300)~s~"); return; }
				g_FreeCam.EditingCameraIndex = idx;
				UIUtil::PrintSubtitle("Placed ~COLOR_BLUE~" + g_CameraManager.Cameras[idx].name + "~s~ - adjust its settings, then fly to fine-tune");
			}
			g_CameraManager.SelectedIndex = idx;
			CEditorMenus::RebuildCameraEditProperties();
			g_Menu->GoToSubmenu(Submenu_Camera_Edit_Properties);
		});

		sub->AddRegularOption("Insert Camera Here", "Create a camera at the current view (placement camera supported). Also bound to the Add Camera key", [] {
			int idx = g_CameraManager.InsertCamera();
			if (idx < 0) {
				UIUtil::PrintSubtitle("~COLOR_RED~Camera limit reached (300)~s~");
			}
			else {
				UIUtil::PrintSubtitle("Created ~COLOR_BLUE~" + g_CameraManager.Cameras[idx].name + "~s~");
			}
		});

		sub->AddRegularOption("Camera List", "Browse, edit, activate and delete cameras", [] {
			CEditorMenus::RebuildCameraList();
			g_Menu->GoToSubmenu(Submenu_Cameras_List);
		});

		sub->AddEmptyOption("--- Playback ---");

		sub->AddRegularOption("Play Full Project", "Watch the entire sequence from start to finish. Records automatically through OBS when OBS recording is enabled below", [] {
			bool record = g_Config.OBSEnabled && g_Config.OBSAutoRecord;
			g_Director.PlayProject(record);
			g_Menu->SetEnabled(false); // close the menu so it stays out of the shot
		});

		sub->AddRegularOption("Play Full Project (No Recording)", "Preview the whole sequence without starting an OBS recording", [] {
			g_Director.PlayProject(false);
			g_Menu->SetEnabled(false);
		});

		sub->AddRegularOption("Stop Project Playback", "Stop the sequence early (also stops the OBS recording)", [] {
			if (g_Director.IsPlayingProject()) {
				g_Director.StopProject();
			}
			else {
				UIUtil::PrintSubtitle("No project is playing");
			}
		});

		sub->AddBoolOption("Smooth Camera Path (Spline)", "Glide through your cameras on a Catmull-Rom curve (flowing dolly/crane move) instead of straight cuts between angles. Affects playback and switching", &g_Config.SmoothCameraPath);

		sub->AddRegularOption("OBS Recording Setup", "Connect Director's Suite to OBS so full-project playback exports a video automatically", [] {
			CEditorMenus::RebuildOBSMenu();
			g_Menu->GoToSubmenu(Submenu_Settings_OBS);
		});

		sub->AddEmptyOption("--- Switching ---");

		sub->AddBoolOption("Automatic Camera Switching", "Cycle enabled cameras using each camera's duration and transition. Assign KeyCameraAutoSwitchingStartStop in the INI for a hotkey", &sAutoSwitching, [] {
			if (sAutoSwitching) g_Director.StartAutoSwitching();
			else g_Director.StopAutoSwitching();
			sAutoSwitching = g_Director.IsAutoSwitching();
		});

		sub->AddRegularOption("Deactivate Camera", "Return to the normal gameplay camera", [] {
			g_Director.Deactivate(true);
			sAutoSwitching = false;
		});

		sub->AddRegularOption("Camera Naming", "Automatic camera naming templates", [] {
			CEditorMenus::BuildNamingMenu(); // refresh examples + current selection
			g_Menu->GoToSubmenu(Submenu_Camera_Naming);
		});

		sub->AddRegularOption("Camera Presets", "Reusable property bundles for frequent setups", [] {
			CEditorMenus::RebuildPresetList();
			g_Menu->GoToSubmenu(Submenu_Camera_Presets);
		});

		sub->AddEmptyOption("--- Storage ---");

		sub->AddRegularOption("Save Cameras To File", "Write all cameras and presets to DirectorsSuite_Cameras.ini", [] {
			g_CameraManager.SaveToFile();
			UIUtil::PrintSubtitle("Saved " + std::to_string(g_CameraManager.Count()) + " cameras");
		});

		sub->AddRegularOption("Load Cameras From File", "Load cameras and presets from DirectorsSuite_Cameras.ini", [] {
			g_CameraManager.LoadFromFile();
			UIUtil::PrintSubtitle("Loaded " + std::to_string(g_CameraManager.Count()) + " cameras");
		});

		sub->AddRegularOption("~COLOR_RED~Delete All Cameras~s~", "", [] {
			g_Director.Deactivate(true);
			g_CameraManager.DeleteAllCameras();
			UIUtil::PrintSubtitle("All cameras deleted");
		});
	});
}

// The selectable naming templates. Shown to the user as live examples
// ("CAM 1", "Shot 1", ...) instead of raw placeholder strings.
static const std::vector<const char*> NamingTemplates = {
	"CAM {index}",
	"Camera {index}",
	"Shot {index}",
	"CAM {index}/{total}",
	"CAM {index} ({hour}:{minute})",
};

void CEditorMenus::BuildNamingMenu()
{
	g_Menu->AddSubmenu("DIRECTOR'S SUITE", "Camera Naming", Submenu_Camera_Naming, 8, [](Submenu* sub)
	{
		// Display entries are example names produced by each template
		std::vector<std::string> examples;
		int currentIdx = -1;
		for (int i = 0; i < (int)NamingTemplates.size(); i++) {
			examples.push_back(g_CameraManager.PreviewName(NamingTemplates[i], 0));
			if (g_Config.NamingTemplate == NamingTemplates[i]) currentIdx = i;
		}
		// A template hand-edited in the INI that matches no preset
		if (currentIdx < 0) {
			examples.push_back(g_CameraManager.PreviewName(g_Config.NamingTemplate, 0) + " (INI)");
			currentIdx = (int)examples.size() - 1;
		}

		sub->AddVectorOption("Naming Template", "Changing the template renames every camera immediately. Placeholders {index} {total} {hour} {minute}; custom templates go in the INI", examples, [] {
			int idx = g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex();
			if (idx < (int)NamingTemplates.size()) {
				g_Config.NamingTemplate = NamingTemplates[idx];
				g_Config.Save();
				g_CameraManager.RenameAll();
				UIUtil::PrintSubtitle("Cameras renamed using ~COLOR_BLUE~" + g_CameraManager.PreviewName(g_Config.NamingTemplate, 0) + "~s~ style");
			}
		})->SetVectorIndex(currentIdx);

		sub->AddRegularOption("Rename All Cameras Now", "Re-apply the current template to every camera (also happens automatically when cameras are deleted)", [] {
			g_CameraManager.RenameAll();
			UIUtil::PrintSubtitle("All cameras renamed");
		});
	});
}

// Current live camera mode: 0 = Off (gameplay), 1 = Free Camera, 2 = Player Camera.
static int CurrentCameraMode()
{
	if (g_FreeCam.IsActive()) return 1;
	if (g_Director.IsPlayerCamActive()) return 2;
	return 0;
}

void CEditorMenus::RebuildCinematicMenu()
{
	g_Menu->AddSubmenu("DIRECTOR'S SUITE", "Free Cam & Playback", Submenu_Cinematic, 8, [](Submenu* sub)
	{
		// One clear selector for what the camera is doing right now (inspired by
		// SimpleCamera). Engages immediately and stays active when the menu
		// closes; Off returns to the normal gameplay camera.
		sub->AddVectorOption("Camera Mode", "Off = gameplay camera. Free Camera = fly anywhere (INSERT places a camera). Player Camera = custom follow camera",
			std::vector<const char*>{ "Off", "Free Camera", "Player Camera" }, [] {
				int m = g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex();
				if (m == 1) {            // Free Camera
					g_Director.Deactivate(false);
					g_FreeCam.Activate();
					sFreeCamActive = true;
				}
				else if (m == 2) {       // Player Camera
					g_FreeCam.Deactivate();
					sFreeCamActive = false;
					g_Director.ActivatePlayerCamera();
				}
				else {                   // Off
					g_FreeCam.Deactivate();
					sFreeCamActive = false;
					g_Director.Deactivate(true);
				}
			})->SetVectorIndex(CurrentCameraMode());

		sub->AddBoolOption("High Detail Streaming", "Stream the world around the free camera for full detail anywhere", &g_Config.FreeCamHighDetail);

		// Scene control without leaving the camera: the same Time/Weather/Clouds
		// page used by Director, reachable right here while you're filming.
		sub->AddSubmenuOption("Time, Weather & World", "Freeze the game, time of day, weather and clouds - tweak the scene without backing out", Submenu_World);

		sub->AddVectorOption("Cutscene Unlock", "Auto keeps editor cameras rendering during cutscenes (anim scenes); Manual leaves control to you", CutsceneUnlockNames, [] {
			g_Director.CutsceneUnlock = g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex();
		})->SetVectorIndex(g_Director.CutsceneUnlock);

		sub->AddBoolOption("Automatic Camera Switching", "Cycle through enabled cameras", &sAutoSwitching, [] {
			if (sAutoSwitching) g_Director.StartAutoSwitching();
			else g_Director.StopAutoSwitching();
			sAutoSwitching = g_Director.IsAutoSwitching();
		});
	});
}

void CEditorMenus::BuildInterfaceMenu()
{
	g_Menu->AddSubmenu("DIRECTOR'S SUITE", "Screen Overlays", Submenu_Interface, 10, [](Submenu* sub)
	{
		sub->AddBoolOption("Hide HUD & Prompts", "Hide the HUD, radar and button prompts", &g_Overlays.HideHud);
		sub->AddBoolOption("Cinematic Letterbox", "Show the black cinematic bars", &g_Overlays.Letterbox);
		sub->AddBoolOption("Native Letterbox", "Use the engine letterbox instead of drawn bars", &g_Overlays.UseNativeLetterbox);

		auto sizes = FloatRange(0.04f, 0.30f, 0.01f, 2);
		sub->AddVectorOption("Letterbox Size", "Bar height (drawn letterbox only)", sizes, [] {
			g_Config.LetterboxRatio = FloatFromIndex(g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex(), 0.04f, 0.01f);
		})->SetVectorIndex(FloatIndex(g_Config.LetterboxRatio, 0.04f, 0.01f, (int)sizes.size()));

		sub->AddBoolOption("Grid Overlay", "Composition grid (rule of thirds by default)", &g_Overlays.ShowGrid);

		sub->AddVectorOption("Grid Rows", "", 9, "", "", [] {
			g_Config.GridRows = g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex() + 1;
		})->SetVectorIndex(g_Config.GridRows - 1);

		sub->AddVectorOption("Grid Columns", "", 9, "", "", [] {
			g_Config.GridColumns = g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex() + 1;
		})->SetVectorIndex(g_Config.GridColumns - 1);

		sub->AddBoolOption("Camera Markers", "Place a physical camera-box prop at every stored camera. Hidden automatically while a camera renders", &g_Overlays.ShowCameraMarkers);
		sub->AddBoolOption("Marker Name Labels", "Float each camera's name above its marker", &g_Overlays.ShowMarkerLabels);
		sub->AddBoolOption("Transition Progress Bar", "Show a progress bar while cameras blend", &g_Overlays.ShowProgressBar);
	});
}

void CEditorMenus::BuildWorldMenus()
{
	g_Menu->AddSubmenu("DIRECTOR'S SUITE", "Time & World Control", Submenu_World, 10, [](Submenu* sub)
	{
		sub->AddBoolOption("Freeze Game", "Photo Mode style freeze - everything halts but cameras stay free", &sFreezeGame, [] {
			g_World.SetFreezeGame(sFreezeGame);
		});

		const std::vector<const char*> scales = { "1.00", "0.90", "0.80", "0.70", "0.60", "0.50", "0.40", "0.30", "0.25", "0.20", "0.15", "0.10", "0.05", "0.02" };
		sub->AddVectorOption("Time Scale", "Slow motion factor", scales, [] {
			const float values[] = { 1.0f, 0.9f, 0.8f, 0.7f, 0.6f, 0.5f, 0.4f, 0.3f, 0.25f, 0.2f, 0.15f, 0.1f, 0.05f, 0.02f };
			g_World.TimeScale = values[g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex()];
			g_World.ApplyTimeScale();
		});

		sub->AddBoolOption("Freeze Time Of Day", "Pause the in-game clock", &sFreezeTimeOfDay, [] {
			g_World.SetFreezeTimeOfDay(sFreezeTimeOfDay);
		});

		sub->AddVectorOption("Hour", "Set the clock hour", 24, "", ":00", [] {
			CLOCK::SET_CLOCK_TIME(g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex(), CLOCK::GET_CLOCK_MINUTES(), 0);
		});

		sub->AddVectorOption("Minute", "Set the clock minute", 60, "", "", [] {
			CLOCK::SET_CLOCK_TIME(CLOCK::GET_CLOCK_HOURS(), g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex(), 0);
		});

		sub->AddSubmenuOption("Weather", "Set and freeze the weather", Submenu_World_Weather);
		sub->AddSubmenuOption("Advanced Cloud Controls", "Freeze and move clouds. Precision values live in the INI [Clouds] section", Submenu_World_Clouds);
	});

	g_Menu->AddSubmenu("DIRECTOR'S SUITE", "Weather", Submenu_World_Weather, 12, [](Submenu* sub)
	{
		sub->AddBoolOption("Freeze Weather", "Stop the weather from changing on its own", &sWeatherFrozen, [] {
			g_World.SetWeatherFrozen(sWeatherFrozen);
		});

		for (int i = 1; i < (int)WeatherTypeNames.size(); i++) {
			sub->AddRegularOption(WeatherTypeNames[i], "", [i] {
				g_World.SetWeather(i);
				UIUtil::PrintSubtitle("Weather: ~COLOR_BLUE~" + std::string(WeatherTypeNames[i]) + "~s~");
			});
		}
	});

	g_Menu->AddSubmenu("DIRECTOR'S SUITE", "Advanced Cloud Controls", Submenu_World_Clouds, 8, [](Submenu* sub)
	{
		sub->AddBoolOption("Freeze Clouds", "Hold the cloud layer in place", &g_World.FreezeClouds);
		sub->AddBoolOption("Move Clouds", "Drift the cloud layer using SpeedX/SpeedY from the INI", &g_World.MoveClouds);
		sub->AddBoolOption("Override Cloud Height", "", &g_World.CloudHeightOverride);

		sub->AddVectorOption("Cloud Height", "Cloud layer altitude (uses HeightStep from the INI)", 60, "", "", [] {
			g_World.CloudHeight = (float)g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex() * g_Config.CloudHeightStep;
		});

		sub->AddRegularOption("Reset Cloud Position", "", [] {
			g_World.CloudPosX = 0.0f;
			g_World.CloudPosY = 0.0f;
			UIUtil::PrintSubtitle("Cloud position reset");
		});
	});
}

void CEditorMenus::BuildAimingMenus()
{
	g_Menu->AddSubmenu("DIRECTOR'S SUITE", "Aim Assist", Submenu_Aiming, 8, [](Submenu* sub)
	{
		sub->AddVectorOption("Aim Mode", "Which direction the player aims while editor cameras render. Camera Direction I/II are for the custom player camera", AimModeNames, [] {
			g_Aiming.AimMode = g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex();
		});

		sub->AddBoolOption("Highlight Aim Target", "Draw a marker on whatever is being aimed at", &g_Aiming.HighlightAimTarget);

		sub->AddSubmenuOption("Aim Assist", "Snap-to-target assistance (hold the assist key while aiming)", Submenu_AimAssist);
	});

	g_Menu->AddSubmenu("DIRECTOR'S SUITE", "Aim Assist", Submenu_AimAssist, 8, [](Submenu* sub)
	{
		sub->AddBoolOption("Aim Assist Enabled", "Hold the assist key (default C) while free aiming to snap onto targets", &g_Aiming.AimAssistEnabled);
		sub->AddBoolOption("Animal Aim Assist", "Include animals when the Animals target is enabled", &g_Aiming.AnimalAimAssist);

		sub->AddSubmenuOption("Targets", "Who the assist may lock onto", Submenu_AimAssist_Targets);
		sub->AddSubmenuOption("Body Part", "Which body part the assist aims for", Submenu_AimAssist_BodyParts);

		std::vector<std::string> strengths;
		for (const auto& s : AimSnapStrengths) strengths.push_back(s.label);
		sub->AddVectorOption("Snap Strength", "Soft, Medium, Hard or Ultra Hard", strengths, [] {
			g_Aiming.SnapStrengthIndex = g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex();
		})->SetVectorIndex(g_Aiming.SnapStrengthIndex);
	});

	g_Menu->AddSubmenu("DIRECTOR'S SUITE", "Aim Assist Targets", Submenu_AimAssist_Targets, 8, [](Submenu* sub)
	{
		sub->AddBoolOption("Humans In Combat", "Lock onto humans currently fighting you", &sTargetHumansCombat, [] { SyncTargetMaskFromStatics(); });
		sub->AddBoolOption("All Humans", "Lock onto any human", &sTargetHumansAll, [] { SyncTargetMaskFromStatics(); });
		sub->AddBoolOption("Animals", "Lock onto animals (see Animal Aim Assist)", &sTargetAnimals, [] { SyncTargetMaskFromStatics(); });
		sub->AddBoolOption("Specific Models", "Lock onto the model names listed in the INI [AimAssist] Models entry", &sTargetModels, [] { SyncTargetMaskFromStatics(); });
		sub->AddBoolOption("Specific Ped", "Lock onto a single hand-picked ped", &sTargetSpecific, [] { SyncTargetMaskFromStatics(); });

		sub->AddRegularOption("Pick Specific Ped", "Free aim at a ped first, then select this option to remember it", [] {
			g_Aiming.PickSpecificPedFromAim();
			UIUtil::PrintSubtitle(g_Aiming.SpecificPed != 0 ? "Specific ped captured" : "~COLOR_RED~No ped under the crosshair~s~");
		});
	});

	g_Menu->AddSubmenu("DIRECTOR'S SUITE", "Aim Assist Body Part", Submenu_AimAssist_BodyParts, 8, [](Submenu* sub)
	{
		std::vector<std::string> parts;
		for (const auto& p : AimBodyParts) parts.push_back(p.label);
		sub->AddVectorOption("Body Part", "Head, arms, hands, legs, feet, left/right limbs and more", parts, [] {
			g_Aiming.BodyPartIndex = g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex();
		})->SetVectorIndex(g_Aiming.BodyPartIndex);
	});
}

void CEditorMenus::BuildGameplayMenu()
{
	g_Menu->AddSubmenu("DIRECTOR'S SUITE", "Gameplay Options", Submenu_Gameplay, 8, [](Submenu* sub)
	{
		sub->AddBoolOption("Instant Kill", "Anything you damage dies instantly", &g_Gameplay.InstantKill);
		sub->AddBoolOption("Invincible Player", "", &g_Gameplay.InvinciblePlayer);
		sub->AddBoolOption("Invincible Horse", "", &g_Gameplay.InvincibleHorse);
	});
}

void CEditorMenus::BuildSettingsMenu()
{
	g_Menu->AddSubmenu("DIRECTOR'S SUITE", "Settings", Submenu_Settings, 8, [](Submenu* sub)
	{
		auto speeds = FloatRange(0.05f, 2.0f, 0.05f, 2);
		sub->AddVectorOption("Free Cam Speed", "Base movement speed of the free / Photo Mode camera", speeds, [] {
			g_Config.FreeCamSpeed = FloatFromIndex(g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex(), 0.05f, 0.05f);
		})->SetVectorIndex(FloatIndex(g_Config.FreeCamSpeed, 0.05f, 0.05f, (int)speeds.size()));

		auto fastMult = FloatRange(1.0f, 12.0f, 0.5f, 1);
		sub->AddVectorOption("Free Cam Fast Speed (Shift)", "Speed multiplier while holding Shift to accelerate", fastMult, [] {
			g_Config.FreeCamFastMultiplier = FloatFromIndex(g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex(), 1.0f, 0.5f);
		})->SetVectorIndex(FloatIndex(g_Config.FreeCamFastMultiplier, 1.0f, 0.5f, (int)fastMult.size()));

		auto slowMult = FloatRange(0.05f, 1.0f, 0.05f, 2);
		sub->AddVectorOption("Free Cam Slow Speed (Ctrl)", "Speed multiplier while holding Ctrl for precise moves", slowMult, [] {
			g_Config.FreeCamSlowMultiplier = FloatFromIndex(g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex(), 0.05f, 0.05f);
		})->SetVectorIndex(FloatIndex(g_Config.FreeCamSlowMultiplier, 0.05f, 0.05f, (int)slowMult.size()));

		auto sens = FloatRange(0.5f, 10.0f, 0.5f, 1);
		sub->AddVectorOption("Look Sensitivity", "Mouse / stick sensitivity for free cam and orbit cameras", sens, [] {
			g_Config.FreeCamMouseSensitivity = FloatFromIndex(g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex(), 0.5f, 0.5f);
		})->SetVectorIndex(FloatIndex(g_Config.FreeCamMouseSensitivity, 0.5f, 0.5f, (int)sens.size()));

		sub->AddRegularOption("Key Bindings", "Rebind the hotkeys (open menu, Photo Mode, screenshot, cameras...)", [] {
			g_Menu->AddSubmenu("DIRECTOR'S SUITE", "Key Bindings", Submenu_Settings_Keys, 12, [](Submenu* s) { KeyBind::BuildInto(s); });
			g_Menu->GoToSubmenu(Submenu_Settings_Keys);
		});

		sub->AddRegularOption("Save Settings To INI", "Write the current settings to DirectorsSuite.ini", [] {
			g_Config.Save();
			UIUtil::PrintSubtitle("Settings saved");
		});

		sub->AddRegularOption("Reload INI", "Re-read DirectorsSuite.ini (keybinds, cloud precision, aim assist models...)", [] {
			g_Config.Load();
			UIUtil::PrintSubtitle("Settings reloaded");
		});

		sub->AddRegularOption("OBS Recording Setup", "Connect to OBS for automatic recording during project playback", [] {
			CEditorMenus::RebuildOBSMenu();
			g_Menu->GoToSubmenu(Submenu_Settings_OBS);
		});
	});
}

// ---------------------------------------------------------------------------
// Help - a reading interface, not a wall of buttons.
//
// Topic pages are built from two row types:
//   - heading rows: selectable (so Up/Down scrolls section to section),
//     highlighted, but purely structural - they perform no action
//   - body rows: word-wrapped paragraphs rendered left-aligned in the body
//     font with no button background (see drawEmptyOptionText in Drawing.cpp).
//     The selection skips them automatically.
// Nothing informational lives in the bottom "tip" footer anymore; footers on
// help pages stay empty.
// ---------------------------------------------------------------------------

static void AddHelpHeading(Submenu* sub, const std::string& text)
{
	sub->AddRegularOption("~COLOR_OBJECTIVE~" + text + "~s~", "");
}

static void AddHelpBody(Submenu* sub, const std::string& text)
{
	// Word-wrap into separate non-interactive rows. Pack lines fuller so the
	// paragraph does not look sparse/stretched across the panel width.
	constexpr size_t maxChars = 60;
	std::stringstream ss(text);
	std::string word, line;
	while (ss >> word) {
		if (!line.empty() && line.size() + 1 + word.size() > maxChars) {
			sub->AddEmptyOption(line);
			line.clear();
		}
		if (!line.empty()) line += " ";
		line += word;
	}
	if (!line.empty()) sub->AddEmptyOption(line);
}

void CEditorMenus::BuildHelpMenus()
{
	g_Menu->AddSubmenu("DIRECTOR'S SUITE", "Help", Submenu_Help, 10, [](Submenu* sub)
	{
		AddHelpBody(sub, "Pick a topic. Inside a topic, Up/Down jumps between sections.");

		sub->AddSubmenuOption("Getting Started", "", Submenu_Help_GettingStarted);
		sub->AddSubmenuOption("Camera Modes", "", Submenu_Help_CameraModes);
		sub->AddSubmenuOption("The Player Camera", "", Submenu_Help_PlayerCam);
		sub->AddSubmenuOption("Recording A Video", "", Submenu_Help_Recording);
		sub->AddSubmenuOption("Keys & Controls", "", Submenu_Help_Keys);
		sub->AddSubmenuOption("Troubleshooting", "", Submenu_Help_Troubleshooting);
		sub->AddSubmenuOption("Director Mode", "", Submenu_Help_DirectorMode);
	});

	g_Menu->AddSubmenu("HELP", "Getting Started", Submenu_Help_GettingStarted, 12, [](Submenu* sub)
	{
		AddHelpHeading(sub, "1. Open Placement Camera Mode");
		AddHelpBody(sub, "Camera Shots > Place a Camera detaches you from the player so you fly freely. WASD moves, the mouse looks, Shift is fast, Ctrl is slow, Space and Z go up and down, Q and E zoom. On-screen instructions stay visible the whole time.");

		AddHelpHeading(sub, "2. Frame your shot, press INSERT");
		AddHelpBody(sub, "What you see is exactly what the camera will record. INSERT stores a camera at the current view and a camera-box prop appears there. Cameras are named automatically: CAM 1, CAM 2, and so on.");

		AddHelpHeading(sub, "3. Repeat for every shot");
		AddHelpBody(sub, "Place up to 300 cameras. Every box you see in the world is one stored camera.");

		AddHelpHeading(sub, "4. Play it back");
		AddHelpBody(sub, "Leave placement mode (Place a Camera again, or N / B to jump to a camera). Use Camera System > Play Full Project to watch the whole sequence start to finish - it can record itself through OBS automatically.");

		AddHelpHeading(sub, "5. Edit any camera later");
		AddHelpBody(sub, "Camera System > Camera List holds every camera. Open one to change its Camera Properties, or use Edit Camera Placement to fly it to a new position.");
	});

	g_Menu->AddSubmenu("HELP", "Camera Modes", Submenu_Help_CameraModes, 12, [](Submenu* sub)
	{
		AddHelpHeading(sub, "Static (no modes ticked)");
		AddHelpBody(sub, "The camera stays exactly where you placed it, pointing where you aimed it. Best for landscapes and walk-through shots.");

		AddHelpHeading(sub, "Follow");
		AddHelpBody(sub, "The camera keeps its offset and moves with the target, like a drone on a leash. 'Offset Rotates With Target' makes it swing around as the target turns.");

		AddHelpHeading(sub, "Look At");
		AddHelpBody(sub, "The camera stays put while its view tracks the target - a security-camera pan that works at any distance.");

		AddHelpHeading(sub, "Same Angle");
		AddHelpBody(sub, "The camera moves with the target but never rotates: the classic side-scrolling tracking shot. Great for horseback runs.");

		AddHelpHeading(sub, "Free Angle (orbit)");
		AddHelpBody(sub, "You orbit the target live with the mouse or right stick at a fixed radius (set with Orbit Distance).");

		AddHelpHeading(sub, "Combine them");
		AddHelpBody(sub, "Modes are checkboxes - tick several. Follow plus Look At gives a chase cam that always faces the target. Add Free Angle and you have the custom player camera.");

		AddHelpHeading(sub, "Targets");
		AddHelpBody(sub, "Every mode tracks the camera's target ped, which defaults to you. Each camera can target any nearby ped or animal, with dead or alive animal priority.");
	});

	g_Menu->AddSubmenu("HELP", "The Player Camera", Submenu_Help_PlayerCam, 12, [](Submenu* sub)
	{
		AddHelpHeading(sub, "What it is");
		AddHelpBody(sub, "A replacement third-person camera you fully control: it follows the player, always looks at them, and you orbit it freely with the look controls.");

		AddHelpHeading(sub, "Activating it");
		AddHelpBody(sub, "Cinematic & Exploration > Activate Player Camera. It starts at your current view distance. Assign KeyPlayerCamToggle in the INI for a hotkey.");

		AddHelpHeading(sub, "Shooting with it");
		AddHelpBody(sub, "Set Aiming > Aim Mode to Camera Direction I (bullets follow the camera natively) or Camera Direction II (scripted aim along the camera).");

		AddHelpHeading(sub, "Leaving it");
		AddHelpBody(sub, "Cinematic & Exploration > Deactivate Camera. The view eases back to the normal gameplay camera.");
	});

	g_Menu->AddSubmenu("HELP", "Recording A Video", Submenu_Help_Recording, 12, [](Submenu* sub)
	{
		AddHelpHeading(sub, "1. Stage the scene");
		AddHelpBody(sub, "Time & World Control sets weather, freezes the clock, slows time or freezes the whole game Photo Mode style. Director Mode places and directs NPCs with lighting.");

		AddHelpHeading(sub, "2. Place and polish cameras");
		AddHelpBody(sub, "Camera Shots > Place a Camera, then INSERT for each shot. Under Camera Properties set Duration (how long the shot holds), Transition (how it blends in), zoom, filter, blur and shake.");

		AddHelpHeading(sub, "3. Clean the screen");
		AddHelpBody(sub, "Interface & Visual Tools: hide the HUD and enable the letterbox. Camera markers and progress bars hide themselves automatically while recording.");

		AddHelpHeading(sub, "4. One-click record with OBS");
		AddHelpBody(sub, "Set up OBS once under Camera System > OBS Recording Setup, then Play Full Project starts the recording, plays every enabled camera in order and stops the recording at the end.");

		AddHelpHeading(sub, "Without OBS");
		AddHelpBody(sub, "Start any capture tool, then use Play Full Project (No Recording) or cut manually with N and B. The game has no clip-export API, so external capture is how video is saved.");

		AddHelpHeading(sub, "Save your work");
		AddHelpBody(sub, "Camera System > Save Cameras To File keeps the whole shoot in DirectorsSuite_Cameras.ini for later sessions.");
	});

	g_Menu->AddSubmenu("HELP", "Keys & Controls", Submenu_Help_Keys, 12, [](Submenu* sub)
	{
		AddHelpHeading(sub, "Defaults");
		AddHelpBody(sub, "F2 opens this menu (controller: hold RB, press A). F1 opens Photo Mode. INSERT places a camera, or saves a reposition. N and B switch cameras. Hold C while free aiming for Aim Assist. Placement Camera Mode is started from Camera Shots > Place a Camera.");

		AddHelpHeading(sub, "Placement Camera Mode controls");
		AddHelpBody(sub, "WASD moves, mouse looks, Shift fast, Ctrl slow, Space up, Z down, Q and E zoom.");

		AddHelpHeading(sub, "Optional hotkeys (off by default)");
		AddHelpBody(sub, "KeyCameraAutoSwitchingStartStop starts or stops automatic switching. KeyPlayerCamToggle toggles the player camera. Both are set in the INI.");

		AddHelpHeading(sub, "Rebinding");
		AddHelpBody(sub, "All keys live in the [Keys] section of DirectorsSuite.ini as Windows virtual-key codes; common codes are listed in the file header comment. Use Settings > Reload INI after editing.");
	});

	g_Menu->AddSubmenu("HELP", "Troubleshooting", Submenu_Help_Troubleshooting, 12, [](Submenu* sub)
	{
		AddHelpHeading(sub, "I can't see my placed cameras");
		AddHelpBody(sub, "Enable Interface & Visual Tools > Camera Markers: every camera gets a physical camera-box prop and a name label. Markers hide while a camera renders so they never appear in shots.");

		AddHelpHeading(sub, "Blur is far too strong");
		AddHelpBody(sub, "Depth-of-field blur reads much stronger in dark scenes. At night keep Blur Strength around 0.05 to 0.15 and set an explicit Focus Distance.");

		AddHelpHeading(sub, "Playback skips cameras");
		AddHelpBody(sub, "Auto switching and Play Full Project only use ENABLED cameras. Check each camera's Enabled toggle.");

		AddHelpHeading(sub, "Cutscene camera takes over");
		AddHelpBody(sub, "Set Cinematic & Exploration > Cutscene Unlock to Auto to keep your cameras rendering through cutscenes.");

		AddHelpHeading(sub, "OBS recording won't start");
		AddHelpBody(sub, "OBS must be running with its WebSocket server enabled (Tools menu), and Host, Port and Password under [OBS] in the INI must match exactly. Use OBS Recording Setup > Test Connection and read the status line.");

		AddHelpHeading(sub, "Aim Assist grabs nothing");
		AddHelpBody(sub, "It needs Aim Assist enabled, free aiming, the assist key held (default C), and a valid target type ticked under Targets.");

		AddHelpHeading(sub, "Reset everything");
		AddHelpBody(sub, "Delete DirectorsSuite.ini next to RDR2.exe; a fresh one with defaults is created on the next launch.");
	});

	g_Menu->AddSubmenu("HELP", "Director Mode", Submenu_Help_DirectorMode, 12, [](Submenu* sub)
	{
		AddHelpHeading(sub, "Building a scene");
		AddHelpBody(sub, "Director Mode > Scene NPCs > Add NPC spawns story characters, townfolk, lawmen, gangs or animals in front of the camera. Use Placement & Composition to position, rotate and nudge them.");

		AddHelpHeading(sub, "Directing characters");
		AddHelpBody(sub, "Each NPC has properties (health, hostility, weapons, outfits), a base task, a scenario action and a facial expression. The player gets expressions and scenarios too.");

		AddHelpHeading(sub, "When expressions play");
		AddHelpBody(sub, "Facial expressions and scenarios only run while the scene is live: previewing through a camera, recording through OBS, or with the Preview Scene toggle.");

		AddHelpHeading(sub, "Hero Lighting");
		AddHelpBody(sub, "Per character: the authentic Rockstar light rig from the photo studio, plus a key/fill/back three-point rig with full colour, intensity, range and position control. Active while editing, previewing and recording.");

		AddHelpHeading(sub, "Clearing the set");
		AddHelpBody(sub, "World Controls drops pedestrian density to zero and Clear Area removes wanderers - your placed cast, you and your horse are always kept.");
	});
}
void CEditorMenus::BuildCreditsMenu()
{
	g_Menu->AddSubmenu("DIRECTOR'S SUITE", "Credits", Submenu_Credits, 12, [](Submenu* sub)
	{
		struct Credit { const char* name; const char* role; };
		const Credit credits[] = {
			{ "videotech",       "Mod Creator" },
			{ "Alexander Blade", "ScriptHookRDR SDK" },
			{ "Disquse",         "Timecycle Editor" },
			{ "Cfx.re",          "Timecycle Editor" },
			{ "Halen84",         "Script Research & UI Base" },
			{ "kepmehz",         "Script Research" },
			{ "femga",           "Game Research" },
			{ "TheNathanNS",     "QA" },
			{ "WesternSpace",    "Patcher for black bars in Photo Mode for ultra-wide displays" },
			{ "Claude (Fable 5)","Research, Code, Bug Fixes" },
		};
		for (const auto& c : credits) {
			sub->AddRegularOption(c.name, c.role);
		}
		sub->AddEmptyOption(" ");
		sub->AddEmptyOption("Thank you to everyone who made");
		sub->AddEmptyOption("Director's Suite possible.");
	});
}

void CEditorMenus::Init()
{
	BuildEntryMenu();
	BuildCamerasMenu();
	BuildNamingMenu();
	RebuildCinematicMenu();
	BuildInterfaceMenu();
	BuildWorldMenus();
	BuildAimingMenus();
	BuildGameplayMenu();
	BuildSettingsMenu();
	BuildHelpMenus();
	BuildCreditsMenu();
	BuildDirectorMenus();

	// Director Mode dynamic pages need an initial build so the IDs exist
	RebuildDirectorNPCList();
	RebuildDirectorAddNPCList();
	RebuildDirectorPlayer();
	RebuildDirectorHeroLight(true);
	RebuildDirectorSceneLight();
	RebuildDirectorSceneLightEdit();

	// Dynamic pages get an initial build so their IDs always exist
	RebuildCameraList();
	RebuildPresetList();
	RebuildOBSMenu();

	SyncTargetMaskFromStatics();
}
