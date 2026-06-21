// Director's Suite - Director Mode types and data tables.
//
// Every name below was verified against the game's own data:
// ped models from rdr3_discoveries peds_list.lua, scenario names harvested
// from the decompiled R* scripts, facial moods from SET_FACIAL_IDLE_ANIM_OVERRIDE
// call sites, light rigs from shop_photo_studio.c / the gambling minigames.

#pragma once
#include <string>
#include <vector>
#include "..\..\..\inc\types.h"

// ---------------------------------------------------------------------------
// Hero lighting
// ---------------------------------------------------------------------------

// Internal state of an authentic Rockstar light rig (anim scene)
enum eRigState : int
{
	RIG_OFF = 0,
	RIG_CREATING,   // anim scene created, waiting for metadata
	RIG_LOADING,    // LOAD_ANIM_SCENE issued, waiting for load
	RIG_RUNNING,
};

// Per-character lighting setup: an authentic R* light rig pinned to the
// character (cutscene-grade lighting with real shadows). Free-placed scene
// lights live separately in the shared SceneLights system.
struct HeroLightSetup
{
	// Rockstar rig (the exact system the photo studio / catalogue uses)
	bool rigEnabled = false;
	int  rigIndex = 0;                // index into HeroLightRigs
	AnimScene rigScene = 0;           // engine handle (runtime only)
	int  rigState = RIG_OFF;          // eRigState (runtime only)
	int  activeRigIndex = -1;         // rig the scene was created with

	bool AnyEnabled() const { return rigEnabled; }
};

// Authentic R* light rig anim scenes (harvested from the game's .yas assets).
// Names are the anim-scene paths with the .yas extension dropped.
inline const std::vector<const char*> HeroLightRigs = {
	// Catalogue / journal / character
	"lightrig@player_journal",             // first option
	"lightrig@catalogue_generic",         // the shop catalogue character rig
	// Card & table games (clean, even key/fill)
	"lightrig@blackjack_generic",
	"lightrig@dominoes_generic",
	"lightrig@five_finger_fillet_generic",
	"lightrig@poker_cards_generic",
	"lightrig@poker_cards_generic_dark",
	"lightrig@poker_cards_swanson_station",
	"lightrig@poker_cards_tumbleweed",
	// Barbers
	"lightrig@barber@barber_blackwater",
	"lightrig@barber@barber_camp_generic",
	"lightrig@barber@barber_saint_denis",
	"lightrig@barber@barber_valentine",
	// Shops & wardrobe
	"lightrig@shops@butcher_generic",
	"lightrig@shops@trapper_generic",
	"lightrig@wardrobe@wardrobe_camp_generic",
};

// ---------------------------------------------------------------------------
// Scene NPCs
// ---------------------------------------------------------------------------

enum eHostility : int
{
	HOSTILITY_NEUTRAL = 0,  // ignores the player
	HOSTILITY_FRIENDLY,     // likes the player
	HOSTILITY_HOSTILE,      // hates and fights the player
	HOSTILITY_SCARED,       // dislikes and flees
};

enum eNPCTask : int
{
	NPCTASK_STAND_STILL = 0,
	NPCTASK_WANDER,
	NPCTASK_COMBAT_PLAYER,
	NPCTASK_FLEE_PLAYER,
	NPCTASK_FOLLOW_PLAYER,
	NPCTASK_AMBIENT_AI,     // hand control back to ambient AI
};

struct SceneNPC
{
	Ped  handle = 0;
	Hash model = 0;
	std::string modelName;
	std::string name;        // display name in the menu

	int  outfit = 0;
	int  health = 600;
	bool invincible = false;
	bool frozen = false;
	int  hostility = HOSTILITY_NEUTRAL;
	int  accuracy = 50;
	int  weaponIndex = 0;    // 0 = keep model default loadout
	int  taskIndex = NPCTASK_STAND_STILL;
	int  scenarioIndex = 0;  // 0 = none; only active during preview/recording
	int  facialIndex = 0;    // 0 = none; only active during preview/recording

	HeroLightSetup light;
};

// ---------------------------------------------------------------------------
// Data tables
// ---------------------------------------------------------------------------

struct PedModelDef { const char* label; const char* model; };

// Story Mode characters (cutscene models, verified in peds_list.lua)
inline const std::vector<PedModelDef> StoryPeds = {
	{ "Arthur Morgan",     "player_zero" },
	{ "John Marston",      "player_three" },
	{ "John (story)",      "cs_johnmarston" },
	{ "Dutch van der Linde","cs_dutch" },
	{ "Hosea Matthews",    "cs_hoseamatthews" },
	{ "Micah Bell",        "cs_micahbell" },
	{ "Bill Williamson",   "cs_billwilliamson" },
	{ "Javier Escuella",   "cs_javierescuella" },
	{ "Charles Smith",     "cs_charlessmith" },
	{ "Lenny Summers",     "cs_lenny" },
	{ "Sean MacGuire",     "cs_sean" },
	{ "Kieran Duffy",      "cs_kieran" },
	{ "Uncle",             "cs_uncle" },
	{ "Sadie Adler",       "cs_mrsadler" },
	{ "Abigail Roberts",   "cs_abigailroberts" },
	{ "Jack Marston",      "cs_jackmarston" },
	{ "Karen Jones",       "cs_karen" },
	{ "Mary-Beth Gaskill", "cs_marybeth" },
	{ "Tilly Jackson",     "cs_tilly" },
	{ "Susan Grimshaw",    "cs_susangrimshaw" },
	{ "Molly O'Shea",      "cs_mollyoshea" },
	{ "Mr. Pearson",       "cs_mrpearson" },
	{ "Rev. Swanson",      "cs_revswanson" },
	{ "Leopold Strauss",   "cs_leostrauss" },
	{ "Josiah Trelawny",   "cs_josiahtrelawny" },
	{ "Angelo Bronte",     "cs_bronte" },
	{ "Agent Milton",      "cs_miltonandrews" },
	{ "Edgar Ross",        "cs_edgarross" },
};

// Ambient world NPCs
inline const std::vector<PedModelDef> AmbientPeds = {
	{ "Valentine Townfolk (M)",  "a_m_m_valtownfolk_01" },
	{ "Armadillo Townfolk (M)",  "a_m_m_armtownfolk_01" },
	{ "Armadillo Townfolk (F)",  "a_f_m_armtownfolk_01" },
	{ "Valentine Townfolk (F)",  "a_f_m_valtownfolk_01" },
	{ "Saint Denis Chinatown (M)","a_m_m_sdchinatown_01" },
	{ "Blackwater Townfolk (F)", "a_f_m_blwtownfolk_01" },
	{ "Blackwater Upperclass (F)","a_f_m_blwupperclass_01" },
	{ "Annesburg Miner",         "a_m_m_asbminer_01" },
	{ "Annesburg Townfolk (M)",  "a_m_m_asbtownfolk_01" },
	{ "Annesburg Townfolk (F)",  "a_f_m_asbtownfolk_01" },
	{ "Fancy Traveller (F)",     "a_f_m_bivfancytravellers_01" },
	{ "Hillbilly (F)",           "a_f_m_btchillbilly_01" },
	{ "Family Traveler",         "a_f_m_familytravelers_cool_01" },
	{ "Boat Crew",               "a_m_m_asbboatcrew_01" },
	{ "Deputy (Armadillo)",      "a_m_m_armdeputyresident_01" },
	{ "Deputy (Annesburg)",      "a_m_m_asbdeputyresident_01" },
};

// Lawmen, gangs, workers
inline const std::vector<PedModelDef> GangLawPeds = {
	{ "Rural Lawman",        "s_m_m_ambientlawrural_01" },
	{ "Saint Denis Police",  "s_m_m_ambientsdpolice_01" },
	{ "Blackwater Police",   "s_m_m_ambientblwpolice_01" },
	{ "US Army Soldier",     "s_m_m_army_01" },
	{ "Cowpoke",             "s_m_m_asbcowpoke_01" },
	{ "Card Dealer",         "s_m_m_asbdealer_01" },
	{ "Bank Clerk",          "s_m_m_bankclerk_01" },
	{ "Barber",              "s_m_m_barber_01" },
	{ "O'Driscoll (Duster)", "g_m_m_uniduster_01" },
	{ "Bounty Hunter",       "g_m_m_bountyhunters_01" },
	{ "Del Lobo Bandito",    "g_m_m_unibanditos_01" },
	{ "Braithwaite Gunman",  "g_m_m_unibraithwaites_01" },
	{ "Bronte Goon",         "g_m_m_unibrontegoons_01" },
	{ "Cornwall Goon",       "g_m_m_unicornwallgoons_01" },
	{ "Criminal",            "g_m_m_unicriminals_01" },
	{ "Announcer",           "u_m_m_announcer_01" },
	{ "Undertaker",          "u_m_m_armundertaker_01" },
	{ "Gunsmith",            "u_m_m_asbgunsmith_01" },
	{ "Prisoner",            "u_m_m_asbprisoner_01" },
};

// Animals
inline const std::vector<PedModelDef> AnimalPeds = {
	{ "Horse (Arabian White)", "a_c_horse_arabian_white" },
	{ "Wolf",                  "a_c_wolf_medium" },
	{ "Grizzly Bear",          "a_c_bear_01" },
	{ "Black Bear",            "a_c_bearblack_01" },
	{ "Cougar",                "a_c_cougar_01" },
	{ "Panther",               "a_c_panther_01" },
	{ "Buffalo",               "a_c_buffalo_01" },
	{ "Deer",                  "a_c_deer_01" },
	{ "Buck",                  "a_c_buck_01" },
	{ "Elk",                   "a_c_elk_01" },
	{ "Coyote",                "a_c_coyote_01" },
	{ "Fox",                   "a_c_fox_01" },
	{ "Eagle",                 "a_c_eagle_01" },
	{ "Rabbit",                "a_c_rabbit_01" },
	{ "Alligator",             "a_c_alligator_01" },
	{ "Boar",                  "a_c_boar_01" },
	// Domestic / farm animals - these have dedicated scenarios (herding,
	// drinking, eating) and were previously unreachable for lack of a model.
	// Dog breeds canonicalise to the "dog" species, so DOG scenarios apply.
	{ "Dog (Hound)",           "a_c_dogamericanfoxhound_01" },
	{ "Dog (Rufus)",           "a_c_dogrufus_01" },
	{ "Cat",                   "a_c_cat_01" },
	{ "Cow",                   "a_c_cow_01" },
	{ "Bull",                  "a_c_bull_01" },
	{ "Pig",                   "a_c_pig_01" },
	{ "Sheep",                 "a_c_sheep_01" },
	{ "Goat",                  "a_c_goat_01" },
	{ "Ram",                   "a_c_ram_01" },
	{ "Donkey",                "a_c_donkey_01" },
	{ "Chicken",               "a_c_chicken_01" },
	{ "Rooster",               "a_c_rooster_01" },
	{ "Pigeon",                "a_c_pigeon_01" },
};

// Facial idle moods (SET_FACIAL_IDLE_ANIM_OVERRIDE, names from R* scripts)
inline const std::vector<const char*> FacialMoods = {
	"None",
	"mood_normal",
	"mood_happy",
	"mood_angry",
	"Mood_Angry_1",
	"mood_nervous",
	"mood_scared",
	"mood_cautious",
	"mood_sleeping",
	"mood_sleeping_2",
	"mood_dead_1",
	"mood_normal_neutral",
	"portrait_normal",
};

// Scenarios (TASK_START_SCENARIO_IN_PLACE_HASH, most used across R* scripts)
inline const std::vector<const char*> ScenarioNames = {
	"None",
	"WORLD_HUMAN_SMOKE",
	"WORLD_HUMAN_SMOKE_CIGAR",
	"WORLD_HUMAN_DRINKING",
	"WORLD_HUMAN_DRINKING_DRUNK",
	"WORLD_HUMAN_COFFEE_DRINK",
	"WORLD_HUMAN_DRINK_CHAMPAGNE",
	"WORLD_HUMAN_STARE_STOIC",
	"WORLD_HUMAN_STAND_WAITING",
	"WORLD_HUMAN_WAITING_IMPATIENT",
	"WORLD_HUMAN_BADASS",
	"WORLD_HUMAN_GUARD_SCOUT",
	"WORLD_HUMAN_GUARD_MILITARY",
	"WORLD_HUMAN_LEAN_BACK_WALL",
	"WORLD_HUMAN_LEAN_BACK_WALL_SMOKING",
	"WORLD_HUMAN_LEAN_RAILING",
	"WORLD_HUMAN_LEAN_POST_LEFT",
	"WORLD_HUMAN_SIT_GROUND",
	"WORLD_HUMAN_SIT_GROUND_READING",
	"WORLD_HUMAN_SIT_SMOKE",
	"WORLD_HUMAN_SIT_GUITAR_MALE_A",
	"WORLD_HUMAN_SIT_GUITAR_UPBEAT",
	"WORLD_HUMAN_SIT_BACK_EXHAUSTED",
	"WORLD_HUMAN_SIT_FALL_ASLEEP",
	"WORLD_HUMAN_SLEEP_GROUND_ARM",
	"WORLD_HUMAN_SLEEP_GROUND_PILLOW",
	"WORLD_HUMAN_CROUCH_INSPECT",
	"WORLD_HUMAN_INSPECT",
	"WORLD_HUMAN_SHOPKEEPER",
	"WORLD_HUMAN_BARCUSTOMER",
	"WORLD_HUMAN_BARCUSTOMER_BEER",
	"WORLD_HUMAN_SELL_PAPER",
	"WORLD_HUMAN_CLEAN_TABLE",
	"WORLD_HUMAN_WRITE_NOTEBOOK",
	"WORLD_HUMAN_CLIPBOARD",
	"WORLD_HUMAN_STERNGUY_IDLES",
	"WORLD_HUMAN_CAULDRON",
	"WORLD_HUMAN_PEE",
};

// Weapon loadouts (names verified in weapons.lua)
inline const std::vector<const char*> NPCWeapons = {
	"Default Loadout",
	"weapon_revolver_cattleman",
	"weapon_revolver_schofield",
	"weapon_revolver_doubleaction",
	"weapon_pistol_volcanic",
	"weapon_pistol_mauser",
	"weapon_pistol_semiauto",
	"weapon_pistol_m1899",
	"weapon_repeater_carbine",
	"weapon_repeater_winchester",
	"weapon_repeater_henry",
	"weapon_rifle_springfield",
	"weapon_rifle_boltaction",
	"weapon_sniperrifle_rollingblock",
	"weapon_shotgun_doublebarrel",
	"weapon_shotgun_pump",
	"weapon_shotgun_sawedoff",
	"weapon_bow",
	"weapon_lasso",
	"weapon_melee_knife",
	"weapon_melee_machete",
	"weapon_melee_torch",
	"weapon_thrown_dynamite",
	"weapon_thrown_molotov",
	"Unarmed (remove all)",
};

inline const std::vector<const char*> HostilityNames = {
	"Neutral", "Friendly", "Hostile", "Scared",
};

inline const std::vector<const char*> NPCTaskNames = {
	"Stand Still", "Wander", "Attack Player", "Flee From Player", "Follow Player", "Ambient AI",
};
