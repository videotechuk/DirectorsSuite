// Director's Suite - Submenu IDs
// NOTE: Order matters. CNativeMenu::GoToSubmenu() treats a larger ID as "forward"
// navigation and a smaller ID as "back" navigation, and the entry menu must be 0.

#pragma once

enum eSubmenuID : int
{
	Submenu_Invalid = -1,
	Submenu_EntryMenu,

	// Camera system
	Submenu_Cameras,
	Submenu_Cameras_List,
	Submenu_Camera_Edit,
	Submenu_Camera_Edit_Modes,
	Submenu_Camera_Edit_Properties,
	Submenu_Camera_Edit_Filter,
	Submenu_Camera_Edit_World,
	Submenu_Camera_Edit_Target,
	Submenu_Camera_Naming,
	Submenu_Camera_Presets,
	Submenu_Camera_Preset_Edit,

	// Cinematic & exploration
	Submenu_Cinematic,

	// Interface & visual tools
	Submenu_Interface,

	// Time & world control
	Submenu_World,
	Submenu_World_Weather,
	Submenu_World_Clouds,

	// Aiming
	Submenu_Aiming,
	Submenu_AimAssist,
	Submenu_AimAssist_Targets,
	Submenu_AimAssist_BodyParts,

	// Gameplay
	Submenu_Gameplay,

	// Settings
	Submenu_Settings,
	Submenu_Settings_OBS,
	Submenu_Settings_Keys,

	// Help
	Submenu_Help,
	Submenu_Help_GettingStarted,
	Submenu_Help_CameraModes,
	Submenu_Help_PlayerCam,
	Submenu_Help_Recording,
	Submenu_Help_Keys,
	Submenu_Help_Troubleshooting,

	// Director Mode
	// NOTE: child pages must keep larger IDs than their parents so
	// GoToSubmenu() treats the navigation as "forward".
	Submenu_Director,
	Submenu_Director_World,
	Submenu_Director_Player,
	Submenu_Director_NPCList,
	Submenu_Director_AddNPC,
	Submenu_Director_AddNPC_List,
	Submenu_Director_NPCEdit,
	Submenu_Director_NPCEdit_Place,
	Submenu_Director_NPCEdit_Props,
	Submenu_Director_NPCEdit_Behaviour,
	Submenu_Director_HeroLight,
	Submenu_Director_HeroLight_Point,

	// Appended late so existing IDs stay stable; still reachable from
	// Submenu_Help because this ID is larger (= "forward" navigation).
	Submenu_Help_DirectorMode,

	// Credits (reached from the entry menu; larger ID = forward nav)
	Submenu_Credits,

	// Photo Mode (native-menu port). Parent first; the tab pages must keep
	// larger IDs than the parent so submenu navigation reads as "forward".
	Submenu_PhotoMode,
	Submenu_PhotoMode_Camera,
	Submenu_PhotoMode_World,
	Submenu_PhotoMode_Character,
	Submenu_PhotoMode_Lighting,
	Submenu_PhotoMode_ArtLights,
	Submenu_PhotoMode_Post,
	Submenu_PhotoMode_Effects,
	Submenu_PhotoMode_Music,

	// Scene Editor (new Photo Mode tab). Appended at the end so every existing
	// ID stays stable; child pages keep larger IDs than their parent so submenu
	// navigation reads as "forward".
	Submenu_PhotoMode_Scene,
	Submenu_PhotoMode_Scene_Objects,      // browse buckets + search
	Submenu_PhotoMode_Scene_ObjectList,   // filtered / bucket result list
	Submenu_PhotoMode_Scene_Placed,       // already-placed objects
	Submenu_PhotoMode_Scene_ObjectEdit,   // transform the selected object
	Submenu_PhotoMode_Scene_Actors,       // scene cast (reuses Director NPCs)
	Submenu_PhotoMode_Scene_PedBrowse,    // browse all peds (buckets + search)
	Submenu_PhotoMode_Scene_PedList,      // filtered / bucket ped result list
	Submenu_PhotoMode_Scene_AddActor,     // pick a model to spawn (curated group)
	Submenu_PhotoMode_Scene_ActorEdit,    // place + assign action to an actor
	Submenu_PhotoMode_Scene_ActorScenario,// filtered scenario picker
	Submenu_PhotoMode_Scene_World,        // temporary YMAP / world edits

	// Photo Mode settings page (free-cam speed / sensitivity / streaming; shared
	// with the main Director's Suite Settings page via g_Config).
	Submenu_PhotoMode_Settings,
	Submenu_PhotoMode_Credits,            // Photo-Mode-local copy of the credits page
	Submenu_PhotoMode_Keys,               // Photo-Mode-local key bindings page
	Submenu_PhotoMode_Captures,           // browse / view saved screenshots in-game

	// Director Mode scene lighting (the Photo Mode "Improved Artificial Lighting"
	// system ported in). Appended at the end so every existing ID stays stable;
	// both pages keep larger IDs than their Director-menu parent so navigation
	// reads as "forward".
	Submenu_Director_SceneLight,          // scene-global art lights + sun
	Submenu_Director_SceneLight_Edit,     // transform / intensity / colour of the selected light
};
