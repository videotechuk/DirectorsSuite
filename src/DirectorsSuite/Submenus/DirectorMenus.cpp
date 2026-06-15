// Director's Suite - Director Mode menu pages (CEditorMenus methods, split into
// their own translation unit; see EditorMenus.cpp for the rest of the UI).

#include "EditorMenus.h"

// ---------------------------------------------------------------------------
// Local helpers (duplicated from EditorMenus.cpp - file-static there)
// ---------------------------------------------------------------------------

static std::vector<std::string> DFloatRange(float start, float end, float step, int decimals = 2)
{
	std::vector<std::string> out;
	char buf[32];
	for (float v = start; v <= end + step * 0.5f; v += step) {
		sprintf_s(buf, "%.*f", decimals, v);
		out.emplace_back(buf);
	}
	return out;
}

static int DFloatIndex(float value, float start, float step, int count)
{
	int idx = (int)((value - start) / step + 0.5f);
	if (idx < 0) idx = 0;
	if (idx > count - 1) idx = count - 1;
	return idx;
}

static float DFloatFromIndex(int idx, float start, float step)
{
	return start + (float)idx * step;
}

static SceneNPC* SelNPC()
{
	return g_DirectorMode.GetSelected();
}

// Mirror statics for BoolOptions (they need a stable bool*)
static bool sNpcInvincible = false;
static bool sNpcFrozen = false;
static bool sRigEnabled = false;
static bool sPointEnabled = false;
static bool sPreviewScene = false;
static bool sOverrideDensity = false;
static bool sKeepAreaClear = false;

// Hero light page target: player or the selected NPC
static bool s_lightForPlayer = false;
static int  s_lightPoint = 0; // 0 key, 1 fill, 2 back

static HeroLightSetup* CurLight()
{
	if (s_lightForPlayer) return &g_DirectorMode.PlayerLight;
	SceneNPC* npc = SelNPC();
	return npc ? &npc->light : nullptr;
}

static HeroLightPoint* CurPoint()
{
	HeroLightSetup* light = CurLight();
	if (!light) return nullptr;
	switch (s_lightPoint) {
		case 1: return &light->fill;
		case 2: return &light->back;
		default: return &light->key;
	}
}

// Add-NPC category being browsed
static int s_pedCategory = 0;
static const std::vector<const char*> PedCategoryNames = {
	"Story Mode Characters", "Ambient World NPCs", "Lawmen, Gangs & Workers", "Animals", "Custom (from INI)",
};

// Colour presets for the light point page
struct ColorPreset { const char* label; int r, g, b; };
static const std::vector<ColorPreset> LightColorPresets = {
	{ "Warm White",    255, 244, 224 },
	{ "White",         255, 255, 255 },
	{ "Cool White",    205, 220, 255 },
	{ "Candle Orange", 255, 180, 110 },
	{ "Sunset Red",    255, 110,  90 },
	{ "Moonlight Blue",120, 160, 255 },
	{ "Green",         120, 255, 140 },
	{ "Magenta",       255, 100, 230 },
	{ "Cyan",          100, 235, 255 },
	{ "Gold",          255, 215, 130 },
};

// ---------------------------------------------------------------------------
// Static pages
// ---------------------------------------------------------------------------

void CEditorMenus::BuildDirectorMenus()
{
	g_Menu->AddSubmenu("DIRECTOR'S SUITE", "Scene & World", Submenu_Director, 10, [](Submenu* sub)
	{
		sub->AddRegularOption("Scene NPCs", "Place and direct Story Mode, ambient and special NPCs", [] {
			CEditorMenus::RebuildDirectorNPCList();
			g_Menu->GoToSubmenu(Submenu_Director_NPCList);
		});

		sub->AddRegularOption("Player Character", "Facial expression, scenario and hero lighting for your character", [] {
			CEditorMenus::RebuildDirectorPlayer();
			g_Menu->GoToSubmenu(Submenu_Director_Player);
		});

		// World / scene / gameplay control now lives under Director Mode.
		sub->AddSubmenuOption("Time, Weather & Clouds", "Freeze the game, time scale, time of day, weather and clouds", Submenu_World);
		sub->AddSubmenuOption("Pedestrians & Clear Area", "Ambient ped density and Clear Area tools", Submenu_Director_World);
		sub->AddSubmenuOption("Gameplay", "Instant kill, player and horse invincibility", Submenu_Gameplay);

		sub->AddBoolOption("Preview Scene", "Apply facial expressions and scenarios now. They also activate automatically while an editor camera renders or OBS records", &sPreviewScene, [] {
			g_DirectorMode.ManualPreview = sPreviewScene;
		});

		sub->AddRegularOption("Trigger Hostile Scene (5s)", "Counts down 5 seconds (time to frame your shot), then every Hostile NPC attacks and you become invincible for safe filming", [] {
			g_DirectorMode.StartHostileCountdown(5);
		});

		sub->AddRegularOption("Stand Down / Stop Hostile", "Cancel the firefight and drop invincibility", [] {
			g_DirectorMode.CancelHostileCountdown();
		});

		sub->AddRegularOption("~COLOR_RED~Clear Scene~s~", "Delete every placed NPC and their lights", [] {
			g_DirectorMode.CancelHostileCountdown();
			g_DirectorMode.DeleteAllNPCs();
			UIUtil::PrintSubtitle("Scene cleared");
		});
	});

	g_Menu->AddSubmenu("DIRECTOR MODE", "World Controls", Submenu_Director_World, 8, [](Submenu* sub)
	{
		sub->AddBoolOption("Override Pedestrian Density", "Take control of how many ambient peds spawn", &sOverrideDensity, [] {
			g_DirectorMode.OverrideDensity = sOverrideDensity;
		});

		auto density = DFloatRange(0.0f, 2.0f, 0.1f, 1);
		sub->AddVectorOption("Pedestrian Density", "0.0 = empty streets, 1.0 = normal, 2.0 = crowded", density, [] {
			g_DirectorMode.PedDensity = DFloatFromIndex(g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex(), 0.0f, 0.1f);
		})->SetVectorIndex(DFloatIndex(g_DirectorMode.PedDensity, 0.0f, 0.1f, (int)density.size()));

		auto radius = DFloatRange(10.0f, 200.0f, 10.0f, 0);
		sub->AddVectorOption("Clear Area Radius", "Radius in meters around the player", radius, [] {
			g_DirectorMode.ClearRadius = DFloatFromIndex(g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex(), 10.0f, 10.0f);
		})->SetVectorIndex(DFloatIndex(g_DirectorMode.ClearRadius, 10.0f, 10.0f, (int)radius.size()));

		sub->AddRegularOption("Clear Area Now", "Remove all ambient NPCs and animals in the radius. Scene NPCs, you and your horse are kept", [] {
			g_DirectorMode.ClearAreaNow();
			UIUtil::PrintSubtitle("Area cleared");
		});

		sub->AddBoolOption("Keep Area Clear", "Continuously sweep the area so nothing wanders into the shot. Combine with Pedestrian Density 0.0", &sKeepAreaClear, [] {
			g_DirectorMode.KeepAreaClear = sKeepAreaClear;
		});
	});

	g_Menu->AddSubmenu("DIRECTOR MODE", "Add NPC", Submenu_Director_AddNPC, 8, [](Submenu* sub)
	{
		for (int i = 0; i < (int)PedCategoryNames.size(); i++) {
			const char* footer = (i == 4)
				? "Add your own model names under [Director] CustomPeds in DirectorsSuite.ini (comma separated)"
				: "";
			sub->AddRegularOption(PedCategoryNames[i], footer, [i] {
				s_pedCategory = i;
				CEditorMenus::RebuildDirectorAddNPCList();
				g_Menu->GoToSubmenu(Submenu_Director_AddNPC_List);
			});
		}
	});
}

// ---------------------------------------------------------------------------
// Dynamic pages
// ---------------------------------------------------------------------------

void CEditorMenus::RebuildDirectorNPCList()
{
	g_Menu->AddSubmenu("DIRECTOR MODE", "Scene NPCs (" + std::to_string((int)g_DirectorMode.NPCs.size()) + ")", Submenu_Director_NPCList, 10, [](Submenu* sub)
	{
		sub->AddRegularOption("~COLOR_BLUE~Add NPC...~s~", "Spawn a new NPC in front of the camera", [] {
			g_Menu->GoToSubmenu(Submenu_Director_AddNPC);
		});

		for (int i = 0; i < (int)g_DirectorMode.NPCs.size(); i++) {
			SceneNPC& npc = g_DirectorMode.NPCs[i];
			std::string label = npc.name;
			if (npc.light.AnyEnabled()) label += " ~COLOR_YELLOW~[lit]~s~";
			sub->AddRegularOption(label, npc.modelName, [i] {
				g_DirectorMode.SelectedNPC = i;
				CEditorMenus::RebuildDirectorNPCEdit();
				g_Menu->GoToSubmenu(Submenu_Director_NPCEdit);
			});
		}
	});
}

void CEditorMenus::RebuildDirectorAddNPCList()
{
	g_Menu->AddSubmenu("DIRECTOR MODE", PedCategoryNames[s_pedCategory], Submenu_Director_AddNPC_List, 10, [](Submenu* sub)
	{
		auto addSpawnOption = [sub](const std::string& label, const std::string& model) {
			sub->AddRegularOption(label, model, [label, model] {
				int idx = g_DirectorMode.SpawnNPC(label.c_str(), model.c_str());
				if (idx < 0) {
					UIUtil::PrintSubtitle("~COLOR_RED~Failed to spawn " + model + "~s~");
				}
				else {
					g_DirectorMode.SelectedNPC = idx;
					UIUtil::PrintSubtitle("Spawned ~COLOR_BLUE~" + g_DirectorMode.NPCs[idx].name + "~s~");
					// Rebuild the NPC list now so it is fresh the moment the
					// user backs out (it used to need a menu reopen to refresh)
					CEditorMenus::RebuildDirectorNPCList();
					CEditorMenus::RebuildDirectorNPCEdit();
					g_Menu->GoToSubmenu(Submenu_Director_NPCEdit);
				}
			});
		};

		switch (s_pedCategory) {
			case 0: for (const auto& p : StoryPeds)   addSpawnOption(p.label, p.model); break;
			case 1: for (const auto& p : AmbientPeds) addSpawnOption(p.label, p.model); break;
			case 2: for (const auto& p : GangLawPeds) addSpawnOption(p.label, p.model); break;
			case 3: for (const auto& p : AnimalPeds)  addSpawnOption(p.label, p.model); break;
			case 4:
			{
				if (g_Config.DirectorCustomPeds.empty()) {
					sub->AddRegularOption("No custom peds configured", "Add model names under [Director] CustomPeds in DirectorsSuite.ini, then Reload INI");
				}
				for (const auto& model : g_Config.DirectorCustomPeds) {
					addSpawnOption(model, model);
				}
				break;
			}
		}
	});
}

void CEditorMenus::RebuildDirectorNPCEdit()
{
	SceneNPC* npc = SelNPC();
	if (!npc) return;

	g_Menu->AddSubmenu("DIRECTOR MODE", npc->name, Submenu_Director_NPCEdit, 8, [](Submenu* sub)
	{
		sub->AddRegularOption("Placement & Composition", "Move, rotate and compose this NPC in the scene", [] {
			CEditorMenus::RebuildDirectorNPCPlace();
			g_Menu->GoToSubmenu(Submenu_Director_NPCEdit_Place);
		});

		sub->AddRegularOption("Properties", "Health, invincibility, hostility, accuracy, weapons, outfit", [] {
			CEditorMenus::RebuildDirectorNPCProps();
			g_Menu->GoToSubmenu(Submenu_Director_NPCEdit_Props);
		});

		sub->AddRegularOption("Behaviour & Expression", "Tasks, scenarios and facial expressions", [] {
			CEditorMenus::RebuildDirectorNPCBehaviour();
			g_Menu->GoToSubmenu(Submenu_Director_NPCEdit_Behaviour);
		});

		sub->AddRegularOption("Hero Lighting", "Cinematic character lighting (Rockstar rig + three-point lights)", [] {
			CEditorMenus::RebuildDirectorHeroLight(false);
			g_Menu->GoToSubmenu(Submenu_Director_HeroLight);
		});

		sub->AddRegularOption("~COLOR_RED~Delete NPC~s~", "", [] {
			g_DirectorMode.DeleteNPC(g_DirectorMode.SelectedNPC);
			CEditorMenus::RebuildDirectorNPCList();
			g_Menu->GoToSubmenu(Submenu_Director_NPCList);
			g_Menu->SetSelectionIndex(g_Menu->GetSelectionIndex());
			UIUtil::PrintSubtitle("NPC deleted");
		});
	});
}

void CEditorMenus::RebuildDirectorNPCPlace()
{
	SceneNPC* npc = SelNPC();
	if (!npc) return;

	sNpcFrozen = npc->frozen;

	g_Menu->AddSubmenu("DIRECTOR MODE", "Placement: " + npc->name, Submenu_Director_NPCEdit_Place, 12, [](Submenu* sub)
	{
		sub->AddRegularOption("Place At Current View", "Move the NPC to where the camera is looking (works with the free cam)", [] {
			if (SceneNPC* n = SelNPC()) g_DirectorMode.PlaceAtCurrentView(*n);
		});
		sub->AddRegularOption("Face Camera", "", [] {
			if (SceneNPC* n = SelNPC()) g_DirectorMode.FaceCamera(*n);
		});
		sub->AddRegularOption("Face Player", "", [] {
			if (SceneNPC* n = SelNPC()) g_DirectorMode.FacePlayer(*n);
		});
		sub->AddRegularOption("Snap To Ground", "", [] {
			if (SceneNPC* n = SelNPC()) g_DirectorMode.SnapToGround(*n);
		});

		std::vector<std::string> headings;
		for (int h = 0; h < 360; h += 5) headings.push_back(std::to_string(h));
		int currentHeading = 0;
		if (SceneNPC* n = SelNPC()) {
			float h = ENTITY::GET_ENTITY_HEADING(n->handle);
			while (h < 0.0f) h += 360.0f;
			currentHeading = ((int)(h + 2.5f) / 5) % 72;
		}
		sub->AddVectorOption("Heading", "Rotation in degrees", headings, [] {
			if (SceneNPC* n = SelNPC()) {
				g_DirectorMode.SetNPCHeading(*n, (float)(g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex() * 5));
			}
		})->SetVectorIndex(currentHeading);

		sub->AddRegularOption("Nudge Forward (0.25m)", "", [] { if (SceneNPC* n = SelNPC()) g_DirectorMode.NudgeNPC(*n, 0.25f, 0.0f, 0.0f); });
		sub->AddRegularOption("Nudge Back (0.25m)", "", [] { if (SceneNPC* n = SelNPC()) g_DirectorMode.NudgeNPC(*n, -0.25f, 0.0f, 0.0f); });
		sub->AddRegularOption("Nudge Left (0.25m)", "", [] { if (SceneNPC* n = SelNPC()) g_DirectorMode.NudgeNPC(*n, 0.0f, -0.25f, 0.0f); });
		sub->AddRegularOption("Nudge Right (0.25m)", "", [] { if (SceneNPC* n = SelNPC()) g_DirectorMode.NudgeNPC(*n, 0.0f, 0.25f, 0.0f); });
		sub->AddRegularOption("Nudge Up (0.25m)", "", [] { if (SceneNPC* n = SelNPC()) g_DirectorMode.NudgeNPC(*n, 0.0f, 0.0f, 0.25f); });
		sub->AddRegularOption("Nudge Down (0.25m)", "", [] { if (SceneNPC* n = SelNPC()) g_DirectorMode.NudgeNPC(*n, 0.0f, 0.0f, -0.25f); });

		sub->AddBoolOption("Freeze In Place", "Lock the NPC's position (useful on slopes or props)", &sNpcFrozen, [] {
			if (SceneNPC* n = SelNPC()) g_DirectorMode.SetFrozen(*n, sNpcFrozen);
		});
	});
}

void CEditorMenus::RebuildDirectorNPCProps()
{
	SceneNPC* npc = SelNPC();
	if (!npc) return;

	sNpcInvincible = npc->invincible;

	g_Menu->AddSubmenu("DIRECTOR MODE", "Properties: " + npc->name, Submenu_Director_NPCEdit_Props, 8, [](Submenu* sub)
	{
		SceneNPC* npc = SelNPC();

		auto health = DFloatRange(50.0f, 2000.0f, 50.0f, 0);
		sub->AddVectorOption("Health", "", health, [] {
			if (SceneNPC* n = SelNPC()) {
				n->health = (int)DFloatFromIndex(g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex(), 50.0f, 50.0f);
				g_DirectorMode.ApplyHealth(*n);
			}
		})->SetVectorIndex(DFloatIndex((float)npc->health, 50.0f, 50.0f, (int)health.size()));

		sub->AddBoolOption("Invincible", "", &sNpcInvincible, [] {
			if (SceneNPC* n = SelNPC()) {
				n->invincible = sNpcInvincible;
				g_DirectorMode.ApplyInvincible(*n);
			}
		});

		sub->AddVectorOption("Hostility", "Relationship to the player: Neutral, Friendly, Hostile (fights) or Scared (flees)", HostilityNames, [] {
			if (SceneNPC* n = SelNPC()) {
				n->hostility = g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex();
				g_DirectorMode.ApplyHostility(*n);
			}
		})->SetVectorIndex(npc->hostility);

		auto accuracy = DFloatRange(0.0f, 100.0f, 5.0f, 0);
		sub->AddVectorOption("Combat Accuracy", "0 = can't hit anything, 100 = deadeye", accuracy, [] {
			if (SceneNPC* n = SelNPC()) {
				n->accuracy = (int)DFloatFromIndex(g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex(), 0.0f, 5.0f);
				g_DirectorMode.ApplyAccuracy(*n);
			}
		})->SetVectorIndex(DFloatIndex((float)npc->accuracy, 0.0f, 5.0f, (int)accuracy.size()));

		sub->AddVectorOption("Weapon Loadout", "Give a specific weapon (equipped immediately)", NPCWeapons, [] {
			if (SceneNPC* n = SelNPC()) {
				n->weaponIndex = g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex();
				g_DirectorMode.ApplyWeapon(*n);
			}
		})->SetVectorIndex(npc->weaponIndex);

		sub->AddVectorOption("Outfit", "Outfit preset for this model (count varies per model)", 50, "", "", [] {
			if (SceneNPC* n = SelNPC()) {
				n->outfit = g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex();
				g_DirectorMode.ApplyOutfit(*n);
			}
		})->SetVectorIndex(npc->outfit);
	});
}

void CEditorMenus::RebuildDirectorNPCBehaviour()
{
	SceneNPC* npc = SelNPC();
	if (!npc) return;

	g_Menu->AddSubmenu("DIRECTOR MODE", "Behaviour: " + npc->name, Submenu_Director_NPCEdit_Behaviour, 8, [](Submenu* sub)
	{
		SceneNPC* npc = SelNPC();

		sub->AddVectorOption("Task", "Base behaviour, applied immediately", NPCTaskNames, [] {
			if (SceneNPC* n = SelNPC()) {
				n->taskIndex = g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex();
				g_DirectorMode.ApplyTask(*n);
			}
		})->SetVectorIndex(npc->taskIndex);

		sub->AddVectorOption("Scenario", "Animation/action played while the scene is live (Preview Scene, camera preview or OBS recording)", ScenarioNames, [] {
			if (SceneNPC* n = SelNPC()) {
				n->scenarioIndex = g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex();
				if (g_DirectorMode.IsSceneLive()) {
					g_DirectorMode.ApplyTask(*n);
					if (n->scenarioIndex > 0) {
						TASK::TASK_START_SCENARIO_IN_PLACE_HASH(n->handle, MISC::GET_HASH_KEY(ScenarioNames[n->scenarioIndex]), -1, true, 0, ENTITY::GET_ENTITY_HEADING(n->handle), false);
					}
				}
			}
		})->SetVectorIndex(npc->scenarioIndex);

		sub->AddVectorOption("Facial Expression", "Facial mood active while the scene is live", FacialMoods, [] {
			if (SceneNPC* n = SelNPC()) {
				n->facialIndex = g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex();
				if (g_DirectorMode.IsSceneLive()) {
					if (n->facialIndex > 0) PED::SET_FACIAL_IDLE_ANIM_OVERRIDE(n->handle, FacialMoods[n->facialIndex], 0);
					else PED::CLEAR_FACIAL_IDLE_ANIM_OVERRIDE(n->handle);
				}
			}
		})->SetVectorIndex(npc->facialIndex);
	});
}

void CEditorMenus::RebuildDirectorPlayer()
{
	g_Menu->AddSubmenu("DIRECTOR MODE", "Player Character", Submenu_Director_Player, 8, [](Submenu* sub)
	{
		sub->AddVectorOption("Facial Expression", "Facial mood active while the scene is live", FacialMoods, [] {
			g_DirectorMode.PlayerFacialIndex = g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex();
			Ped player = PLAYER::PLAYER_PED_ID();
			if (g_DirectorMode.IsSceneLive()) {
				if (g_DirectorMode.PlayerFacialIndex > 0) PED::SET_FACIAL_IDLE_ANIM_OVERRIDE(player, FacialMoods[g_DirectorMode.PlayerFacialIndex], 0);
				else PED::CLEAR_FACIAL_IDLE_ANIM_OVERRIDE(player);
			}
		})->SetVectorIndex(g_DirectorMode.PlayerFacialIndex);

		sub->AddVectorOption("Scenario", "Action played by your character while the scene is live", ScenarioNames, [] {
			g_DirectorMode.PlayerScenarioIndex = g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex();
			Ped player = PLAYER::PLAYER_PED_ID();
			if (g_DirectorMode.IsSceneLive()) {
				TASK::CLEAR_PED_TASKS(player, true, false);
				if (g_DirectorMode.PlayerScenarioIndex > 0) {
					TASK::TASK_START_SCENARIO_IN_PLACE_HASH(player, MISC::GET_HASH_KEY(ScenarioNames[g_DirectorMode.PlayerScenarioIndex]), -1, true, 0, ENTITY::GET_ENTITY_HEADING(player), false);
				}
			}
		})->SetVectorIndex(g_DirectorMode.PlayerScenarioIndex);

		sub->AddRegularOption("Hero Lighting", "Cinematic lighting on your character", [] {
			CEditorMenus::RebuildDirectorHeroLight(true);
			g_Menu->GoToSubmenu(Submenu_Director_HeroLight);
		});
	});
}

void CEditorMenus::RebuildDirectorHeroLight(bool forPlayer)
{
	s_lightForPlayer = forPlayer;
	HeroLightSetup* light = CurLight();
	if (!light) return;

	sRigEnabled = light->rigEnabled;

	std::string who = forPlayer ? "Player" : (SelNPC() ? SelNPC()->name : "NPC");

	g_Menu->AddSubmenu("DIRECTOR MODE", "Hero Lighting: " + who, Submenu_Director_HeroLight, 8, [](Submenu* sub)
	{
		HeroLightSetup* light = CurLight();
		if (!light) return;

		sub->AddBoolOption("Rockstar Light Rig", "The authentic rig the game's photo studio and catalogue use to light characters. Follows the character automatically", &sRigEnabled, [] {
			if (HeroLightSetup* l = CurLight()) l->rigEnabled = sRigEnabled;
		});

		sub->AddVectorOption("Rig Style", "Different baked rigs (catalogue, journal, card tables...)", HeroLightRigs, [] {
			if (HeroLightSetup* l = CurLight()) l->rigIndex = g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex();
		})->SetVectorIndex(light->rigIndex);

		sub->AddEmptyOption("--- Three-Point Custom Rig ---");

		const char* pointNames[3] = { "Key Light", "Fill Light", "Back Light" };
		const char* pointFooters[3] = {
			"Main light - bright, warm, usually 30-60 degrees off the face",
			"Soft light filling the shadows from the other side",
			"Rim light from behind separating the character from the background",
		};
		for (int i = 0; i < 3; i++) {
			sub->AddRegularOption(pointNames[i], pointFooters[i], [i] {
				CEditorMenus::RebuildDirectorHeroLightPoint(i);
				g_Menu->GoToSubmenu(Submenu_Director_HeroLight_Point);
			});
		}

		sub->AddRegularOption("Disable All Lights", "", [] {
			if (HeroLightSetup* l = CurLight()) {
				HeroLight::Shutdown(*l);
				l->key.enabled = l->fill.enabled = l->back.enabled = false;
				sRigEnabled = false;
				UIUtil::PrintSubtitle("Hero lighting disabled");
			}
		});
	});
}

void CEditorMenus::RebuildDirectorHeroLightPoint(int pointIndex)
{
	s_lightPoint = pointIndex;
	HeroLightPoint* point = CurPoint();
	if (!point) return;

	sPointEnabled = point->enabled;

	const char* pointNames[3] = { "Key Light", "Fill Light", "Back Light" };

	g_Menu->AddSubmenu("DIRECTOR MODE", pointNames[pointIndex], Submenu_Director_HeroLight_Point, 12, [](Submenu* sub)
	{
		HeroLightPoint* point = CurPoint();
		if (!point) return;

		sub->AddBoolOption("Enabled", "", &sPointEnabled, [] {
			if (HeroLightPoint* p = CurPoint()) p->enabled = sPointEnabled;
		});

		auto intensity = DFloatRange(0.25f, 20.0f, 0.25f, 2);
		sub->AddVectorOption("Intensity", "Light brightness", intensity, [] {
			if (HeroLightPoint* p = CurPoint()) p->intensity = DFloatFromIndex(g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex(), 0.25f, 0.25f);
		})->SetVectorIndex(DFloatIndex(point->intensity, 0.25f, 0.25f, (int)intensity.size()));

		auto range = DFloatRange(1.0f, 30.0f, 0.5f, 1);
		sub->AddVectorOption("Range / Falloff", "How far the light reaches before falling off", range, [] {
			if (HeroLightPoint* p = CurPoint()) p->range = DFloatFromIndex(g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex(), 1.0f, 0.5f);
		})->SetVectorIndex(DFloatIndex(point->range, 1.0f, 0.5f, (int)range.size()));

		std::vector<std::string> angles;
		for (int a = -180; a <= 180; a += 15) angles.push_back(std::to_string(a));
		sub->AddVectorOption("Orbit Angle", "Direction around the character, relative to where they face. 0 = in front", angles, [] {
			if (HeroLightPoint* p = CurPoint()) p->orbitDeg = (float)(-180 + g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex() * 15);
		})->SetVectorIndex((int)((point->orbitDeg + 180.0f) / 15.0f + 0.5f));

		auto distance = DFloatRange(0.5f, 10.0f, 0.25f, 2);
		sub->AddVectorOption("Distance", "Orbit radius from the character", distance, [] {
			if (HeroLightPoint* p = CurPoint()) p->distance = DFloatFromIndex(g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex(), 0.5f, 0.25f);
		})->SetVectorIndex(DFloatIndex(point->distance, 0.5f, 0.25f, (int)distance.size()));

		auto height = DFloatRange(0.0f, 4.0f, 0.1f, 1);
		sub->AddVectorOption("Height", "Height above the character's feet", height, [] {
			if (HeroLightPoint* p = CurPoint()) p->height = DFloatFromIndex(g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex(), 0.0f, 0.1f);
		})->SetVectorIndex(DFloatIndex(point->height, 0.0f, 0.1f, (int)height.size()));

		std::vector<std::string> presets;
		for (const auto& p : LightColorPresets) presets.push_back(p.label);
		sub->AddVectorOption("Colour Preset", "Quick colour selection; fine-tune with the RGB sliders below", presets, [] {
			if (HeroLightPoint* p = CurPoint()) {
				const ColorPreset& preset = LightColorPresets[g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex()];
				p->r = preset.r; p->g = preset.g; p->b = preset.b;
			}
		});

		auto channel = DFloatRange(0.0f, 255.0f, 15.0f, 0);
		sub->AddVectorOption("Red", "", channel, [] {
			if (HeroLightPoint* p = CurPoint()) p->r = (int)DFloatFromIndex(g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex(), 0.0f, 15.0f);
		})->SetVectorIndex(DFloatIndex((float)point->r, 0.0f, 15.0f, (int)channel.size()));

		sub->AddVectorOption("Green", "", channel, [] {
			if (HeroLightPoint* p = CurPoint()) p->g = (int)DFloatFromIndex(g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex(), 0.0f, 15.0f);
		})->SetVectorIndex(DFloatIndex((float)point->g, 0.0f, 15.0f, (int)channel.size()));

		sub->AddVectorOption("Blue", "", channel, [] {
			if (HeroLightPoint* p = CurPoint()) p->b = (int)DFloatFromIndex(g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex(), 0.0f, 15.0f);
		})->SetVectorIndex(DFloatIndex((float)point->b, 0.0f, 15.0f, (int)channel.size()));
	});
}
