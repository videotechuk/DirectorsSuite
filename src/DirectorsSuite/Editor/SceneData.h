// Director's Suite - Scene Editor data layer.
//
// The three catalogues are compiled into the ASI (generated from the source
// .lua lists into Editor/Data/Scene*Data.h), so nothing has to ship next to
// RDR2.exe:
//   Objects    - placeable / hideable map archetypes
//   Peds       - spawnable actors (a_c_* = animals, else humans)
//   Scenarios  - TASK_START_SCENARIO_IN_PLACE_HASH ids
//
// The Scene Editor's "no incompatible scenario on the wrong entity" guarantee
// is structural: scenarios are categorised Human/Animal from their names and,
// for animals, tagged with a species token derived from the name. The picker is
// always built from ScenariosForPed(), which is already filtered - a dog can
// never be shown a human (or horse) scenario.

#pragma once
#include <string>
#include <vector>
#include "..\..\..\inc\types.h"

enum eScenarioCategory : int
{
	SCENARIO_HUMAN = 0,
	SCENARIO_ANIMAL,
};

struct ScenarioDef
{
	std::string name;     // raw scenario id (passed to GET_HASH_KEY on use)
	std::string label;    // display name (underscores -> spaces)
	std::string species;  // lowercase species token for animals; "" = human / any-animal
	eScenarioCategory category = SCENARIO_HUMAN;
};

struct ObjectDef
{
	std::string name;     // raw archetype / model name
	std::string lower;    // cached lowercase for searching
};

struct PedDef
{
	std::string name;     // raw ped model name
	std::string lower;    // cached lowercase for searching
	bool        animal;   // a_c_* models are animals; everything else is human
};

class CSceneData
{
public:
	// Parse both data files. Falls back to the built-in lists (DirectorTypes.h
	// ScenarioNames + a tiny embedded object set) when a file is missing, and
	// records a warning the UI can surface. Call once at startup.
	void Load();

	// --- status (for the UI / diagnostics) ---
	bool ObjectsFromFile = false;
	bool ScenariosFromFile = false;
	bool PedsFromFile = false;
	const std::string& Warning() const { return m_warning; }

	// --- scenarios ---
	const std::vector<ScenarioDef>& Scenarios() const { return m_scenarios; }
	const ScenarioDef* Scenario(int index) const;

	// True if `modelName` is an animal (RDR2 animal models are `a_c_*`).
	static bool IsAnimalModel(const std::string& modelName);
	// Lowercase species token of an animal model ("a_c_horse_arabian_white" ->
	// "horse"); "" for non-animals or unknown.
	static std::string SpeciesOfModel(const std::string& modelName);

	// THE VALIDATION CORE. Returns indices into Scenarios() that are valid for
	// this ped (by model name); index 0 ("None") is added by the caller:
	//  - human model  -> every human scenario
	//  - animal model -> generic-animal scenarios + those whose species matches
	std::vector<int> ScenariosForPed(const std::string& modelName) const;

	// --- objects ---
	const std::vector<ObjectDef>& Objects() const { return m_objects; }
	int ObjectCount() const { return (int)m_objects.size(); }

	// Case-insensitive substring search, capped so the menu never has to build a
	// 16k-row page. Returns indices into Objects().
	std::vector<int> SearchObjects(const std::string& query, int maxResults = 200) const;
	// First-character buckets ("a".."z","0-9","_") that contain at least one
	// object, in display order. Used for browse-without-typing.
	std::vector<std::string> ObjectBuckets() const;
	// Indices of objects whose name begins with `bucket` (single char), capped.
	std::vector<int> ObjectsInBucket(const std::string& bucket, int maxResults = 300) const;

	// --- peds (spawnable actors) ---
	const std::vector<PedDef>& Peds() const { return m_peds; }
	int PedCount() const { return (int)m_peds.size(); }

	// Search / browse, filtered to one category. `animalsOnly` true = animal
	// models, false = human models. Symmetric to the object browser.
	std::vector<int> SearchPeds(const std::string& query, bool animalsOnly, int maxResults = 200) const;
	std::vector<std::string> PedBuckets(bool animalsOnly) const;
	std::vector<int> PedsInBucket(const std::string& bucket, bool animalsOnly, int maxResults = 300) const;

private:
	std::vector<ScenarioDef> m_scenarios;
	std::vector<ObjectDef>   m_objects;
	std::vector<PedDef>      m_peds;
	std::string m_warning;

	// All distinct animal species tokens seen in the scenario list, sorted
	// longest-first. A spawned animal's raw species token (e.g. a dog breed like
	// "dogcollie") is reduced to the longest of these it begins with ("dog"),
	// which is what scenario species are matched against. This makes "dogcollie"
	// match DOG scenarios while keeping "pigeon" out of PIG and "bearblack" out
	// of BEAR.
	std::vector<std::string> m_animalSpecies;

	void BuildScenarioDefs(const std::vector<std::string>& names);

	// Reduce a raw model species token to the canonical base species used by the
	// scenario data (longest known prefix; the token itself if none matches).
	std::string CanonicalSpecies(const std::string& modelSpecies) const;

	static std::string Lower(const std::string& s);
	// Classify a raw scenario name into category + species token.
	static void Classify(const std::string& name, eScenarioCategory& cat, std::string& species);
};

inline CSceneData g_SceneData;
