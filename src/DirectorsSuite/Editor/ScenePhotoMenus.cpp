// Director's Suite - Scene Editor menu pages (CPhotoMode methods, split into
// their own translation unit; see PhotoMode.cpp for the rest of the tab UI).
//
// The Scene Editor turns Photo Mode into a lightweight staging tool:
//   * Objects   - browse / search the catalogue, spawn and transform props
//   * Actors    - spawn humans and animals (reusing the Director cast) and
//                 assign scenarios that are VALIDATED against the entity type
//   * World     - temporary, reverted-on-exit YMAP edits (hide / swap / force)
//
// Scenarios are never mixed up: the picker is built from
// g_SceneData.ScenariosForPed(modelName), which only returns scenarios valid
// for that entity (human scenarios for humans; species-matched + generic-animal
// scenarios for animals), so an incompatible action can't even be selected.

#include "PhotoMode.h"
#include "SceneEditor.h"
#include "SceneData.h"
#include "DirectorMode.h"
#include "DirectorTypes.h"
#include "EditorMath.h"
#include "SceneSteppers.h"
#include "Config.h"
#include "..\script.h"
#include "..\UI\UIUtil.h"
#include "..\UI\Menu.hpp"
#include <vector>
#include <string>
#include <cctype>
#include <cstdio>
#include <functional>

// ---------------------------------------------------------------------------
// Local helpers / state
// ---------------------------------------------------------------------------

static int SCurIdx()
{
	return g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex();
}

// Result set shared between the browse pages and the dynamic object list.
static std::vector<int> s_objResults;
static std::string      s_objListTitle = "Objects";

// Ped browser state (full catalogue, split into humans / animals).
static std::vector<int> s_pedResults;
static std::string      s_pedListTitle = "Peds";
static bool             s_pedBrowseAnimals = false;

// Actor model groups (reuse the Director tables).
static int s_actorGroup = 0;
static const char* kActorGroupNames[] = {
	"Story Mode Characters", "Ambient World NPCs", "Lawmen, Gangs & Workers", "Animals", "Custom (from INI)",
};

// Bool mirrors for the object editor (BoolOption needs a stable bool*).
static bool sObjFrozen = true;
static bool sObjCollision = false;

// Temporary-world-edit tuning.
static float s_hideRadius = 10.0f;

// Scenario list filter (humans have thousands of scenarios, so the picker is
// capped and searchable - same approach as the object browser).
static std::string s_scenarioFilter;
static const int kScenarioCap = 300;

// Deferred same-page refresh (see PumpSceneRebuild). Set from a callback that
// needs to rebuild the page it is currently running inside.
enum SceneRebuild { SR_NONE = 0, SR_SCENE_ROOT, SR_OBJECT_EDIT, SR_ACTOR_EDIT, SR_SCENARIO, SR_WORLD };
static SceneRebuild s_pendingRebuild = SR_NONE;

static std::string SLower(const std::string& s)
{
	std::string o = s;
	for (char& c : o) c = (char)tolower((unsigned char)c);
	return o;
}

static SceneObject* SelObj() { return g_SceneEditor.GetSelected(); }

// Blocking on-screen-keyboard prompt. Safe here: while we spin, the main loop is
// suspended on our stack, so Photo Mode isn't re-disabling controls and the
// keyboard receives input normally.
static bool PromptKeyboard(const char* title, std::string& out)
{
	MISC::DISPLAY_ONSCREEN_KEYBOARD(1, title, "", "", "", "", "", 64);
	DWORD start = GetTickCount();
	while (GetTickCount() - start < 60000) {
		int status = MISC::UPDATE_ONSCREEN_KEYBOARD();
		if (status == 1) {
			const char* r = MISC::GET_ONSCREEN_KEYBOARD_RESULT();
			out = r ? r : "";
			return !out.empty();
		}
		if (status == 2 || status == 3) return false; // cancelled / not displaying
		WAIT(0);
	}
	return false;
}

static const std::vector<PedModelDef>* ActorGroupList(int group)
{
	switch (group) {
		case 0: return &StoryPeds;
		case 1: return &AmbientPeds;
		case 2: return &GangLawPeds;
		case 3: return &AnimalPeds;
		default: return nullptr; // custom handled separately
	}
}

// ---------------------------------------------------------------------------
// Root page
// ---------------------------------------------------------------------------

void CPhotoMode::RebuildScenePage()
{
	g_Menu->AddSubmenu("PHOTO MODE", "Scene Editor (Beta)", Submenu_PhotoMode_Scene, 10, [this](Submenu* sub)
	{
		if (!g_SceneData.Warning().empty())
			sub->AddEmptyOption("~COLOR_YELLOW~" + g_SceneData.Warning() + "~s~");

		sub->AddRegularOption("Objects (" + std::to_string(g_SceneData.ObjectCount()) + ")",
			"Browse or search the prop catalogue and place objects", [this] {
				RebuildSceneObjectsPage(); g_Menu->GoToSubmenu(Submenu_PhotoMode_Scene_Objects);
			});

		sub->AddRegularOption("Placed Objects (" + std::to_string((int)g_SceneEditor.Objects.size()) + ")",
			"Select, move, duplicate or delete objects you have placed", [this] {
				RebuildScenePlacedList(); g_Menu->GoToSubmenu(Submenu_PhotoMode_Scene_Placed);
			});

		sub->AddRegularOption("Actors & Scenarios",
			"Spawn humans and animals and assign validated action scenarios", [this] {
				RebuildSceneActorsPage(); g_Menu->GoToSubmenu(Submenu_PhotoMode_Scene_Actors);
			});

		sub->AddRegularOption("World / Map Edits (" + std::to_string((int)g_SceneEditor.Edits.size()) + ")",
			"Temporarily hide, swap or force world props. All reverted when Photo Mode exits", [this] {
				RebuildSceneWorldPage(); g_Menu->GoToSubmenu(Submenu_PhotoMode_Scene_World);
			});

		sub->AddEmptyOption();
		sub->AddRegularOption("~COLOR_RED~Clear All Scene Edits~s~",
			"Delete every placed object and undo every temporary map edit", [] {
				g_SceneEditor.RevertAll();
				UIUtil::PrintSubtitle("Scene edits cleared");
				s_pendingRebuild = SR_SCENE_ROOT; // refresh counts after this returns
			});
	});
}

// ---------------------------------------------------------------------------
// Objects: browse / search -> list -> place
// ---------------------------------------------------------------------------

void CPhotoMode::RebuildSceneObjectsPage()
{
	g_Menu->AddSubmenu("PHOTO MODE", "Objects", Submenu_PhotoMode_Scene_Objects, 12, [this](Submenu* sub)
	{
		sub->AddRegularOption("~COLOR_BLUE~Search by name...~s~",
			"Type part of a model name (e.g. 'campfire', 'chair', 'barrel')", [this] {
				std::string q;
				if (PromptKeyboard("Search objects", q)) {
					s_objResults = g_SceneData.SearchObjects(q, 200);
					s_objListTitle = "Search '" + q + "' (" + std::to_string((int)s_objResults.size()) + ")";
					RebuildSceneObjectList();
					g_Menu->GoToSubmenu(Submenu_PhotoMode_Scene_ObjectList);
				}
				else {
					UIUtil::PrintSubtitle("Search cancelled");
				}
			});

		sub->AddEmptyOption("--- Browse alphabetically ---");
		for (const std::string& b : g_SceneData.ObjectBuckets()) {
			std::string label = (b == "_") ? "_ (underscore)" : ("Starts with '" + b + "'");
			sub->AddRegularOption(label, "", [this, b] {
				s_objResults = g_SceneData.ObjectsInBucket(b, 300);
				s_objListTitle = "Objects '" + b + "' (" + std::to_string((int)s_objResults.size()) + ")";
				RebuildSceneObjectList();
				g_Menu->GoToSubmenu(Submenu_PhotoMode_Scene_ObjectList);
			});
		}
	});
}

void CPhotoMode::RebuildSceneObjectList()
{
	g_Menu->AddSubmenu("PHOTO MODE", s_objListTitle, Submenu_PhotoMode_Scene_ObjectList, 12, [this](Submenu* sub)
	{
		if (s_objResults.empty()) {
			sub->AddEmptyOption("No matching objects");
			return;
		}
		for (int idx : s_objResults) {
			if (idx < 0 || idx >= g_SceneData.ObjectCount()) continue;
			std::string name = g_SceneData.Objects()[idx].name;
			sub->AddRegularOption(name, "Spawn this prop in front of the camera", [this, name] {
				int i = g_SceneEditor.SpawnObject(name);
				if (i < 0) {
					UIUtil::PrintSubtitle("~COLOR_RED~Failed to spawn " + name + "~s~");
				}
				else {
					UIUtil::PrintSubtitle("Placed ~COLOR_BLUE~" + name + "~s~");
					RebuildSceneObjectEdit();
					g_Menu->GoToSubmenu(Submenu_PhotoMode_Scene_ObjectEdit);
				}
			});
		}
	});
}

void CPhotoMode::RebuildScenePlacedList()
{
	g_Menu->AddSubmenu("PHOTO MODE", "Placed Objects (" + std::to_string((int)g_SceneEditor.Objects.size()) + ")",
		Submenu_PhotoMode_Scene_Placed, 12, [this](Submenu* sub)
	{
		if (g_SceneEditor.Objects.empty()) {
			sub->AddEmptyOption("No objects placed yet - add some from the Objects page");
			return;
		}
		for (int i = 0; i < (int)g_SceneEditor.Objects.size(); i++) {
			std::string label = std::to_string(i + 1) + ".  " + g_SceneEditor.Objects[i].name;
			sub->AddRegularOption(label, "Select and edit this object", [this, i] {
				g_SceneEditor.Selected = i;
				RebuildSceneObjectEdit();
				g_Menu->GoToSubmenu(Submenu_PhotoMode_Scene_ObjectEdit);
			});
		}
	});
}

void CPhotoMode::RebuildSceneObjectEdit()
{
	SceneObject* o = SelObj();
	if (!o) return;
	sObjFrozen = o->frozen;
	sObjCollision = o->collision;

	g_Menu->AddSubmenu("PHOTO MODE", "Edit: " + o->name, Submenu_PhotoMode_Scene_ObjectEdit, 14, [this](Submenu* sub)
	{
		SceneObject* o = SelObj();
		if (!o) { sub->AddEmptyOption("Object no longer exists"); return; }

		sub->AddRegularOption("Place At Current View", "Jump the object to where the camera is looking", [this] {
			if (SceneObject* x = SelObj()) { g_SceneEditor.PlaceAtCurrentView(*x); s_pendingRebuild = SR_OBJECT_EDIT; }
		});
		sub->AddRegularOption("Snap To Ground", "Drop the object onto the ground below it", [this] {
			if (SceneObject* x = SelObj()) { g_SceneEditor.SnapToGround(*x); s_pendingRebuild = SR_OBJECT_EDIT; }
		});

		SceneStep::AddStepSelectors(sub);

		// World-space X/Y/Z position values.
		SceneStep::AddStepper(sub, "Position X", "World X (left/right adjusts by Move Step)", o->pos.x, 3, [](int d) -> float {
			SceneObject* x = SelObj(); if (!x) return 0.0f;
			x->pos.x += SceneStep::MoveStep() * d; g_SceneEditor.ApplyTransform(*x); return x->pos.x;
		});
		SceneStep::AddStepper(sub, "Position Y", "World Y", o->pos.y, 3, [](int d) -> float {
			SceneObject* x = SelObj(); if (!x) return 0.0f;
			x->pos.y += SceneStep::MoveStep() * d; g_SceneEditor.ApplyTransform(*x); return x->pos.y;
		});
		SceneStep::AddStepper(sub, "Position Z", "World Z (height)", o->pos.z, 3, [](int d) -> float {
			SceneObject* x = SelObj(); if (!x) return 0.0f;
			x->pos.z += SceneStep::MoveStep() * d; g_SceneEditor.ApplyTransform(*x); return x->pos.z;
		});

		// Rotation X/Y/Z (pitch / roll / yaw) values.
		SceneStep::AddStepper(sub, "Rotation X (Pitch)", "Adjusts by Rotate Step", o->rot.x, 1, [](int d) -> float {
			SceneObject* x = SelObj(); if (!x) return 0.0f;
			x->rot.x += SceneStep::RotStep() * d; g_SceneEditor.ApplyTransform(*x); return x->rot.x;
		});
		SceneStep::AddStepper(sub, "Rotation Y (Roll)", "", o->rot.y, 1, [](int d) -> float {
			SceneObject* x = SelObj(); if (!x) return 0.0f;
			x->rot.y += SceneStep::RotStep() * d; g_SceneEditor.ApplyTransform(*x); return x->rot.y;
		});
		SceneStep::AddStepper(sub, "Rotation Z (Yaw)", "", o->rot.z, 1, [](int d) -> float {
			SceneObject* x = SelObj(); if (!x) return 0.0f;
			x->rot.z += SceneStep::RotStep() * d; g_SceneEditor.ApplyTransform(*x); return x->rot.z;
		});

		sub->AddBoolOption("Freeze In Place", "Lock the object so physics can't move it", &sObjFrozen, [this] {
			if (SceneObject* x = SelObj()) g_SceneEditor.SetFrozen(*x, sObjFrozen);
		});
		sub->AddBoolOption("Collision", "Let the object collide with the world and entities", &sObjCollision, [this] {
			if (SceneObject* x = SelObj()) g_SceneEditor.SetCollision(*x, sObjCollision);
		});

		sub->AddRegularOption("Duplicate", "Create a copy of this object nearby", [] {
			int i = g_SceneEditor.DuplicateObject(g_SceneEditor.Selected);
			if (i >= 0) { UIUtil::PrintSubtitle("Duplicated"); s_pendingRebuild = SR_OBJECT_EDIT; }
		});
		sub->AddRegularOption("~COLOR_RED~Delete Object~s~", "", [this] {
			g_SceneEditor.DeleteObject(g_SceneEditor.Selected);
			UIUtil::PrintSubtitle("Object deleted");
			RebuildScenePlacedList();
			g_Menu->GoToSubmenu(Submenu_PhotoMode_Scene_Placed);
		});
	});
}

// ---------------------------------------------------------------------------
// Actors & scenarios
// ---------------------------------------------------------------------------

void CPhotoMode::RebuildSceneActorsPage()
{
	g_Menu->AddSubmenu("PHOTO MODE", "Actors & Scenarios", Submenu_PhotoMode_Scene_Actors, 12, [this](Submenu* sub)
	{
		sub->AddRegularOption("~COLOR_BLUE~Browse All Humans...~s~",
			"Search / browse the full human ped catalogue (" + std::to_string((int)g_SceneData.Peds().size()) + " models total)", [this] {
				s_pedBrowseAnimals = false;
				RebuildScenePedBrowse();
				g_Menu->GoToSubmenu(Submenu_PhotoMode_Scene_PedBrowse);
			});
		sub->AddRegularOption("~COLOR_BLUE~Browse All Animals...~s~",
			"Search / browse the full animal catalogue (species drive the scenario list)", [this] {
				s_pedBrowseAnimals = true;
				RebuildScenePedBrowse();
				g_Menu->GoToSubmenu(Submenu_PhotoMode_Scene_PedBrowse);
			});
		sub->AddEmptyOption("--- Quick picks ---");

		for (int g = 0; g < 5; g++) {
			sub->AddRegularOption(std::string("Add: ") + kActorGroupNames[g], "Spawn a named model from this curated group", [this, g] {
				s_actorGroup = g;
				RebuildSceneAddActor();
				g_Menu->GoToSubmenu(Submenu_PhotoMode_Scene_AddActor);
			});
		}

		sub->AddEmptyOption("--- Scene Cast (" + std::to_string((int)g_DirectorMode.NPCs.size()) + ") ---");
		for (int i = 0; i < (int)g_DirectorMode.NPCs.size(); i++) {
			SceneNPC& npc = g_DirectorMode.NPCs[i];
			const char* kind = g_SceneData.IsAnimalModel(npc.modelName) ? "Animal" : "Human";
			sub->AddRegularOption(npc.name, std::string(kind) + " - " + npc.modelName, [this, i] {
				g_DirectorMode.SelectedNPC = i;
				RebuildSceneActorEdit();
				g_Menu->GoToSubmenu(Submenu_PhotoMode_Scene_ActorEdit);
			});
		}
	});
}

void CPhotoMode::RebuildSceneAddActor()
{
	g_Menu->AddSubmenu("PHOTO MODE", kActorGroupNames[s_actorGroup], Submenu_PhotoMode_Scene_AddActor, 12, [this](Submenu* sub)
	{
		auto spawn = [this, sub](const std::string& label, const std::string& model) {
			sub->AddRegularOption(label, model, [this, label, model] {
				int idx = g_DirectorMode.SpawnNPC(label.c_str(), model.c_str());
				if (idx < 0) {
					UIUtil::PrintSubtitle("~COLOR_RED~Failed to spawn " + model + "~s~");
				}
				else {
					g_DirectorMode.SelectedNPC = idx;
					// Track this actor so it's removed when Photo Mode exits.
					g_SceneEditor.RegisterActor(g_DirectorMode.NPCs[idx].handle);
					UIUtil::PrintSubtitle("Spawned ~COLOR_BLUE~" + g_DirectorMode.NPCs[idx].name + "~s~");
					RebuildSceneActorEdit();
					g_Menu->GoToSubmenu(Submenu_PhotoMode_Scene_ActorEdit);
				}
			});
		};

		if (s_actorGroup == 4) {
			if (g_Config.DirectorCustomPeds.empty())
				sub->AddEmptyOption("No custom peds - add them under [Director] CustomPeds in the INI");
			for (const auto& m : g_Config.DirectorCustomPeds) spawn(m, m);
		}
		else {
			const std::vector<PedModelDef>* list = ActorGroupList(s_actorGroup);
			if (list) for (const auto& p : *list) spawn(p.label, p.model);
		}
	});
}

void CPhotoMode::RebuildScenePedBrowse()
{
	g_Menu->AddSubmenu("PHOTO MODE", s_pedBrowseAnimals ? "Animals" : "Humans",
		Submenu_PhotoMode_Scene_PedBrowse, 12, [this](Submenu* sub)
	{
		sub->AddRegularOption("~COLOR_BLUE~Search by name...~s~",
			"Type part of a model name (e.g. 'lawman', 'valentine', 'wolf')", [this] {
				std::string q;
				if (PromptKeyboard("Search peds", q)) {
					s_pedResults = g_SceneData.SearchPeds(q, s_pedBrowseAnimals, 200);
					s_pedListTitle = "Search '" + q + "' (" + std::to_string((int)s_pedResults.size()) + ")";
					RebuildScenePedList();
					g_Menu->GoToSubmenu(Submenu_PhotoMode_Scene_PedList);
				}
				else {
					UIUtil::PrintSubtitle("Search cancelled");
				}
			});

		sub->AddEmptyOption("--- Browse alphabetically ---");
		for (const std::string& b : g_SceneData.PedBuckets(s_pedBrowseAnimals)) {
			std::string label = (b == "_") ? "_ (underscore)" : ("Starts with '" + b + "'");
			sub->AddRegularOption(label, "", [this, b] {
				s_pedResults = g_SceneData.PedsInBucket(b, s_pedBrowseAnimals, 300);
				s_pedListTitle = "Peds '" + b + "' (" + std::to_string((int)s_pedResults.size()) + ")";
				RebuildScenePedList();
				g_Menu->GoToSubmenu(Submenu_PhotoMode_Scene_PedList);
			});
		}
	});
}

void CPhotoMode::RebuildScenePedList()
{
	g_Menu->AddSubmenu("PHOTO MODE", s_pedListTitle, Submenu_PhotoMode_Scene_PedList, 12, [this](Submenu* sub)
	{
		if (s_pedResults.empty()) {
			sub->AddEmptyOption("No matching peds");
			return;
		}
		for (int idx : s_pedResults) {
			if (idx < 0 || idx >= g_SceneData.PedCount()) continue;
			std::string name = g_SceneData.Peds()[idx].name;
			sub->AddRegularOption(name, "Spawn this model in front of the camera", [this, name] {
				int i = g_DirectorMode.SpawnNPC(name.c_str(), name.c_str());
				if (i < 0) {
					UIUtil::PrintSubtitle("~COLOR_RED~Failed to spawn " + name + "~s~");
				}
				else {
					g_DirectorMode.SelectedNPC = i;
					g_SceneEditor.RegisterActor(g_DirectorMode.NPCs[i].handle);
					UIUtil::PrintSubtitle("Spawned ~COLOR_BLUE~" + g_DirectorMode.NPCs[i].name + "~s~");
					RebuildSceneActorEdit();
					g_Menu->GoToSubmenu(Submenu_PhotoMode_Scene_ActorEdit);
				}
			});
		}
	});
}

void CPhotoMode::RebuildSceneActorEdit()
{
	SceneNPC* npc = g_DirectorMode.GetSelected();
	if (!npc) return;

	g_Menu->AddSubmenu("PHOTO MODE", npc->name, Submenu_PhotoMode_Scene_ActorEdit, 12, [this](Submenu* sub)
	{
		SceneNPC* npc = g_DirectorMode.GetSelected();
		if (!npc) { sub->AddEmptyOption("Actor no longer exists"); return; }

		const bool animal = g_SceneData.IsAnimalModel(npc->modelName);
		std::string kindLine = animal
			? ("Animal (" + g_SceneData.SpeciesOfModel(npc->modelName) + ")")
			: std::string("Human");
		sub->AddEmptyOption("--- " + kindLine + " ---");

		sub->AddRegularOption("Assign Scenario / Action...",
			"Only scenarios valid for this entity are shown", [this] {
				RebuildSceneActorScenario();
				g_Menu->GoToSubmenu(Submenu_PhotoMode_Scene_ActorScenario);
			});

		sub->AddRegularOption("Place At Current View", "Jump the actor to where the camera looks", [this] {
			if (SceneNPC* n = g_DirectorMode.GetSelected()) { g_DirectorMode.PlaceAtCurrentView(*n); s_pendingRebuild = SR_ACTOR_EDIT; }
		});
		sub->AddRegularOption("Face Camera", "", [this] {
			if (SceneNPC* n = g_DirectorMode.GetSelected()) { g_DirectorMode.FaceCamera(*n); s_pendingRebuild = SR_ACTOR_EDIT; }
		});

		SceneStep::AddStepSelectors(sub);

		// World-space X/Y/Z position values (read/written on the live entity).
		Vector3 c = ENTITY::GET_ENTITY_COORDS(npc->handle, true, true);
		SceneStep::AddStepper(sub, "Position X", "World X (left/right adjusts by Move Step)", c.x, 3, [](int d) -> float {
			SceneNPC* n = g_DirectorMode.GetSelected(); if (!n || !ENTITY::DOES_ENTITY_EXIST(n->handle)) return 0.0f;
			Vector3 p = ENTITY::GET_ENTITY_COORDS(n->handle, true, true); p.x += SceneStep::MoveStep() * d;
			ENTITY::SET_ENTITY_COORDS(n->handle, p.x, p.y, p.z, false, false, false, false); return p.x;
		});
		SceneStep::AddStepper(sub, "Position Y", "World Y", c.y, 3, [](int d) -> float {
			SceneNPC* n = g_DirectorMode.GetSelected(); if (!n || !ENTITY::DOES_ENTITY_EXIST(n->handle)) return 0.0f;
			Vector3 p = ENTITY::GET_ENTITY_COORDS(n->handle, true, true); p.y += SceneStep::MoveStep() * d;
			ENTITY::SET_ENTITY_COORDS(n->handle, p.x, p.y, p.z, false, false, false, false); return p.y;
		});
		SceneStep::AddStepper(sub, "Position Z", "World Z (height)", c.z, 3, [](int d) -> float {
			SceneNPC* n = g_DirectorMode.GetSelected(); if (!n || !ENTITY::DOES_ENTITY_EXIST(n->handle)) return 0.0f;
			Vector3 p = ENTITY::GET_ENTITY_COORDS(n->handle, true, true); p.z += SceneStep::MoveStep() * d;
			ENTITY::SET_ENTITY_COORDS(n->handle, p.x, p.y, p.z, false, false, false, false); return p.z;
		});
		SceneStep::AddStepper(sub, "Heading", "Facing direction (degrees), adjusts by Rotate Step", ENTITY::GET_ENTITY_HEADING(npc->handle), 1, [](int d) -> float {
			SceneNPC* n = g_DirectorMode.GetSelected(); if (!n || !ENTITY::DOES_ENTITY_EXIST(n->handle)) return 0.0f;
			ENTITY::SET_ENTITY_HEADING(n->handle, ENTITY::GET_ENTITY_HEADING(n->handle) + SceneStep::RotStep() * d);
			return ENTITY::GET_ENTITY_HEADING(n->handle);
		});

		sub->AddRegularOption("Snap To Ground", "Drop the actor onto the ground below", [this] {
			if (SceneNPC* n = g_DirectorMode.GetSelected()) { g_DirectorMode.SnapToGround(*n); s_pendingRebuild = SR_ACTOR_EDIT; }
		});

		sub->AddRegularOption("~COLOR_RED~Remove Actor~s~", "Delete this actor from the scene", [this] {
			g_DirectorMode.DeleteNPC(g_DirectorMode.SelectedNPC);
			UIUtil::PrintSubtitle("Actor removed");
			RebuildSceneActorsPage();
			g_Menu->GoToSubmenu(Submenu_PhotoMode_Scene_Actors);
		});
	});
}

void CPhotoMode::RebuildSceneActorScenario()
{
	SceneNPC* npc = g_DirectorMode.GetSelected();
	if (!npc) return;

	const bool animal = g_SceneData.IsAnimalModel(npc->modelName);
	std::string header = animal
		? ("Animal Scenarios: " + g_SceneData.SpeciesOfModel(npc->modelName))
		: std::string("Human Scenarios");

	g_Menu->AddSubmenu("PHOTO MODE", header, Submenu_PhotoMode_Scene_ActorScenario, 14, [this](Submenu* sub)
	{
		SceneNPC* npc = g_DirectorMode.GetSelected();
		if (!npc) { sub->AddEmptyOption("Actor no longer exists"); return; }

		sub->AddRegularOption("None (stop current action)", "Clear the scenario and stand the actor still", [this] {
			SceneNPC* n = g_DirectorMode.GetSelected();
			if (!n || !ENTITY::DOES_ENTITY_EXIST(n->handle)) return;
			TASK::CLEAR_PED_TASKS(n->handle, true, false);
			TASK::TASK_STAND_STILL(n->handle, -1);
			UIUtil::PrintSubtitle("Scenario cleared");
		});

		sub->AddRegularOption("~COLOR_BLUE~Search / Filter...~s~",
			s_scenarioFilter.empty() ? "Narrow the list by name (e.g. 'smoke', 'sit', 'guard')"
									  : ("Current filter: " + s_scenarioFilter), [] {
				std::string q;
				if (PromptKeyboard("Filter scenarios", q)) {
					s_scenarioFilter = q;
					s_pendingRebuild = SR_SCENARIO;
				}
			});
		if (!s_scenarioFilter.empty()) {
			sub->AddRegularOption("Clear Filter", "Show all compatible scenarios again", [] {
				s_scenarioFilter.clear();
				s_pendingRebuild = SR_SCENARIO;
			});
		}

		// THE VALIDATION CORE: only entity-appropriate scenarios are offered.
		std::vector<int> valid = g_SceneData.ScenariosForPed(npc->modelName);
		const std::string filt = SLower(s_scenarioFilter);

		// Build the matching set first so the count note can head the list.
		std::vector<int> matchIdx;
		for (int sidx : valid) {
			const ScenarioDef* sd = g_SceneData.Scenario(sidx);
			if (!sd || sd->name == "None") continue;
			if (!filt.empty() && SLower(sd->name).find(filt) == std::string::npos) continue;
			matchIdx.push_back(sidx);
		}

		const int matched = (int)matchIdx.size();
		const int shown = (matched > kScenarioCap) ? kScenarioCap : matched;
		std::string note = "--- " + std::to_string(matched) + " compatible";
		if (!s_scenarioFilter.empty()) note += " matching '" + s_scenarioFilter + "'";
		if (matched > shown) note += " (showing " + std::to_string(shown) + " - search to narrow)";
		note += " ---";
		sub->AddEmptyOption(note);

		for (int i = 0; i < shown; i++) {
			const ScenarioDef* sd = g_SceneData.Scenario(matchIdx[i]);
			if (!sd) continue;
			std::string raw = sd->name;
			sub->AddRegularOption(sd->label, raw, [this, raw] {
				SceneNPC* n = g_DirectorMode.GetSelected();
				if (!n || !ENTITY::DOES_ENTITY_EXIST(n->handle)) return;
				// Photo Mode is a live preview, so the action plays immediately.
				TASK::CLEAR_PED_TASKS(n->handle, true, false);
				TASK::TASK_START_SCENARIO_IN_PLACE_HASH(n->handle, MISC::GET_HASH_KEY(raw.c_str()),
					-1, true, 0, ENTITY::GET_ENTITY_HEADING(n->handle), false);
				UIUtil::PrintSubtitle("Playing ~COLOR_BLUE~" + raw + "~s~");
			});
		}
	});
}

// ---------------------------------------------------------------------------
// Deferred same-page refresh (called from CPhotoMode::Tick, after the menu's
// input handling has returned - so the option callback that requested it has
// finished and its owning submenu is safe to rebuild).
// ---------------------------------------------------------------------------

void CPhotoMode::PumpSceneRebuild()
{
	SceneRebuild r = s_pendingRebuild;
	if (r == SR_NONE) return;
	s_pendingRebuild = SR_NONE;

	switch (r) {
		case SR_SCENE_ROOT:  RebuildScenePage();          g_Menu->GoToSubmenu(Submenu_PhotoMode_Scene); break;
		case SR_OBJECT_EDIT: RebuildSceneObjectEdit();    g_Menu->GoToSubmenu(Submenu_PhotoMode_Scene_ObjectEdit); break;
		case SR_ACTOR_EDIT:  RebuildSceneActorEdit();     g_Menu->GoToSubmenu(Submenu_PhotoMode_Scene_ActorEdit); break;
		case SR_SCENARIO:    RebuildSceneActorScenario(); g_Menu->GoToSubmenu(Submenu_PhotoMode_Scene_ActorScenario); break;
		case SR_WORLD:       RebuildSceneWorldPage();     g_Menu->GoToSubmenu(Submenu_PhotoMode_Scene_World); break;
		default: break;
	}
}

// ---------------------------------------------------------------------------
// Temporary world (YMAP) edits
// ---------------------------------------------------------------------------

void CPhotoMode::RebuildSceneWorldPage()
{
	g_Menu->AddSubmenu("PHOTO MODE", "World / Map Edits", Submenu_PhotoMode_Scene_World, 14, [this](Submenu* sub)
	{
		sub->AddEmptyOption("All edits below are temporary and reverted on exit");

		sub->AddRegularOption("Hide Object Under Crosshair",
			"Hide whatever map prop the camera is pointing at (tree, fence, barrel...)", [this] {
				if (g_SceneEditor.HideEntityUnderView(s_hideRadius))
					UIUtil::PrintSubtitle("Map prop hidden");
				else
					UIUtil::PrintSubtitle("~COLOR_RED~Nothing hit - aim at a map object~s~");
			});

		sub->AddRegularOption("Hide By Model Name...",
			"Hide every instance of a model around the camera", [] {
				std::string m;
				if (PromptKeyboard("Model to hide", m)) {
					Vector3 c = CAM::GET_FINAL_RENDERED_CAM_COORD();
					g_SceneEditor.HideMapModel(MISC::GET_HASH_KEY(m.c_str()), c, s_hideRadius, "Hide " + m);
					UIUtil::PrintSubtitle("Hiding ~COLOR_BLUE~" + m + "~s~");
					s_pendingRebuild = SR_WORLD;
				}
			});

		sub->AddRegularOption("Force / Spawn Map Object...",
			"Force a (normally blocked) map archetype to appear in front of the camera", [] {
				std::string m;
				if (PromptKeyboard("Model to force", m)) {
					Vector3 camPos = CAM::GET_FINAL_RENDERED_CAM_COORD();
					Vector3 fwd = EMath::RotationToDirection(CAM::GET_FINAL_RENDERED_CAM_ROT(2));
					Vector3 pos = EMath::Add(camPos, EMath::Scale(fwd, 4.0f));
					float gz = pos.z;
					if (MISC::GET_GROUND_Z_FOR_3D_COORD(pos.x, pos.y, pos.z + 2.0f, &gz, false)) pos.z = gz;
					g_SceneEditor.ForceMapObject(MISC::GET_HASH_KEY(m.c_str()), pos, "Force " + m);
					UIUtil::PrintSubtitle("Forcing ~COLOR_BLUE~" + m + "~s~");
					s_pendingRebuild = SR_WORLD;
				}
			});

		std::vector<std::string> radii;
		for (int r = 5; r <= 100; r += 5) radii.push_back(std::to_string(r));
		sub->AddVectorOption("Hide / Edit Radius", "Sphere radius used by the hide / swap actions (m)", radii, [this] {
			s_hideRadius = (float)(5 + SCurIdx() * 5);
		})->SetVectorIndex((int)((s_hideRadius - 5.0f) / 5.0f + 0.5f));

		sub->AddEmptyOption("--- Active Edits (" + std::to_string((int)g_SceneEditor.Edits.size()) + ") ---");
		for (int i = 0; i < (int)g_SceneEditor.Edits.size(); i++) {
			const YmapEdit& e = g_SceneEditor.Edits[i];
			const char* type = (e.type == YmapEdit::HIDE) ? "HIDE" : (e.type == YmapEdit::SWAP) ? "SWAP" : "FORCE";
			sub->AddRegularOption(std::string("[") + type + "] " + e.label, "Undo this edit", [i] {
				g_SceneEditor.RemoveEdit(i);
				UIUtil::PrintSubtitle("Edit undone");
				s_pendingRebuild = SR_WORLD;
			});
		}

		if (!g_SceneEditor.Edits.empty()) {
			sub->AddRegularOption("~COLOR_RED~Restore All Map Edits~s~", "Undo every temporary world edit at once", [] {
				g_SceneEditor.RestoreAllEdits();
				UIUtil::PrintSubtitle("All map edits restored");
				s_pendingRebuild = SR_WORLD;
			});
		}
	});
}
