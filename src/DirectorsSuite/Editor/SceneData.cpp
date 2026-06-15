// Director's Suite - Scene Editor data layer (see SceneData.h).

#include "SceneData.h"
#include "Data\SceneObjectsData.h"
#include "Data\ScenePedsData.h"
#include "Data\SceneScenariosData.h"
#include "..\console.h"
#include <algorithm>
#include <cctype>
#include <cstring>

std::string CSceneData::Lower(const std::string& s)
{
	std::string out = s;
	std::transform(out.begin(), out.end(), out.begin(),
		[](unsigned char c) { return (char)tolower(c); });
	return out;
}

// ---------------------------------------------------------------------------
// Scenario classification
// ---------------------------------------------------------------------------

static bool StartsWith(const std::string& s, const char* p)
{
	size_t n = strlen(p);
	return s.size() >= n && s.compare(0, n, p) == 0;
}

// First underscore-delimited token starting at `from`.
static std::string TokenAt(const std::string& s, size_t from)
{
	size_t end = s.find('_', from);
	return (end == std::string::npos) ? s.substr(from) : s.substr(from, end - from);
}

void CSceneData::Classify(const std::string& name, eScenarioCategory& cat, std::string& species)
{
	cat = SCENARIO_HUMAN;
	species.clear();

	// Animal scenarios encode the species as the token right after the prefix.
	if (StartsWith(name, "WORLD_ANIMAL_")) {
		cat = SCENARIO_ANIMAL;
		species = Lower(TokenAt(name, strlen("WORLD_ANIMAL_")));
	}
	else if (StartsWith(name, "WORLD_ANIMALS_")) {
		cat = SCENARIO_ANIMAL;
		species = Lower(TokenAt(name, strlen("WORLD_ANIMALS_")));
	}
	else if (StartsWith(name, "PROP_ANIMAL_")) {
		cat = SCENARIO_ANIMAL;
		species = Lower(TokenAt(name, strlen("PROP_ANIMAL_")));
	}
	else if (StartsWith(name, "ANIMAL_") || name == "FARMANIMAL") {
		// e.g. ANIMAL_CARCASS_* - an animal action with no clear species.
		cat = SCENARIO_ANIMAL;
		species.clear(); // generic: offered to any animal
	}
	// Everything else (WORLD_HUMAN_*, PROP_HUMAN_*, WORLD_PLAYER_*, CIG_CARD_*,
	// RANSACK_*, MP_*, WB_*, WORLD_CAMP*, CNV_*, LEVDES_*, SCRIPT_*, ...) is a
	// human scenario.
}

std::string CSceneData::CanonicalSpecies(const std::string& modelSpecies) const
{
	if (modelSpecies.empty()) return "";
	// m_animalSpecies is sorted longest-first, so the first prefix match is the
	// most specific base (e.g. "pigeon" before "pig", "bearblack" before "bear").
	for (const std::string& base : m_animalSpecies) {
		if (modelSpecies.size() >= base.size() &&
			modelSpecies.compare(0, base.size(), base) == 0)
			return base;
	}
	return modelSpecies;
}

// ---------------------------------------------------------------------------

bool CSceneData::IsAnimalModel(const std::string& modelName)
{
	std::string m = Lower(modelName);
	return StartsWith(m, "a_c_");
}

std::string CSceneData::SpeciesOfModel(const std::string& modelName)
{
	std::string m = Lower(modelName);
	if (!StartsWith(m, "a_c_")) return "";
	return TokenAt(m, strlen("a_c_"));
}

const ScenarioDef* CSceneData::Scenario(int index) const
{
	if (index < 0 || index >= (int)m_scenarios.size()) return nullptr;
	return &m_scenarios[index];
}

std::vector<int> CSceneData::ScenariosForPed(const std::string& modelName) const
{
	std::vector<int> out;
	const bool animal = IsAnimalModel(modelName);
	const std::string canon = animal ? CanonicalSpecies(SpeciesOfModel(modelName)) : std::string();

	for (int i = 0; i < (int)m_scenarios.size(); i++) {
		const ScenarioDef& s = m_scenarios[i];
		if (animal) {
			// Generic-animal scenarios (no species) suit any animal; otherwise the
			// species must match the canonical base of the model.
			if (s.category == SCENARIO_ANIMAL && (s.species.empty() || s.species == canon))
				out.push_back(i);
		}
		else {
			if (s.category == SCENARIO_HUMAN)
				out.push_back(i);
		}
	}
	return out;
}

// ---------------------------------------------------------------------------
// Objects: search + browse helpers
// ---------------------------------------------------------------------------

std::vector<int> CSceneData::SearchObjects(const std::string& query, int maxResults) const
{
	std::vector<int> out;
	const std::string q = Lower(query);
	if (q.empty()) return out;
	for (int i = 0; i < (int)m_objects.size(); i++) {
		if (m_objects[i].lower.find(q) != std::string::npos) {
			out.push_back(i);
			if ((int)out.size() >= maxResults) break;
		}
	}
	return out;
}

std::vector<std::string> CSceneData::ObjectBuckets() const
{
	bool seen[256] = {};
	for (const auto& o : m_objects) {
		if (!o.lower.empty()) seen[(unsigned char)o.lower[0]] = true;
	}
	std::vector<std::string> out;
	for (char c = 'a'; c <= 'z'; c++) if (seen[(unsigned char)c]) out.push_back(std::string(1, c));
	for (char c = '0'; c <= '9'; c++) if (seen[(unsigned char)c]) out.push_back(std::string(1, c));
	if (seen[(unsigned char)'_']) out.push_back("_");
	return out;
}

std::vector<int> CSceneData::ObjectsInBucket(const std::string& bucket, int maxResults) const
{
	std::vector<int> out;
	if (bucket.empty()) return out;
	char c = (char)tolower((unsigned char)bucket[0]);
	for (int i = 0; i < (int)m_objects.size(); i++) {
		if (!m_objects[i].lower.empty() && m_objects[i].lower[0] == c) {
			out.push_back(i);
			if ((int)out.size() >= maxResults) break;
		}
	}
	return out;
}

// ---------------------------------------------------------------------------
// Peds: search + browse helpers (filtered to one category)
// ---------------------------------------------------------------------------

std::vector<int> CSceneData::SearchPeds(const std::string& query, bool animalsOnly, int maxResults) const
{
	std::vector<int> out;
	const std::string q = Lower(query);
	for (int i = 0; i < (int)m_peds.size(); i++) {
		if (m_peds[i].animal != animalsOnly) continue;
		if (!q.empty() && m_peds[i].lower.find(q) == std::string::npos) continue;
		out.push_back(i);
		if ((int)out.size() >= maxResults) break;
	}
	return out;
}

std::vector<std::string> CSceneData::PedBuckets(bool animalsOnly) const
{
	bool seen[256] = {};
	for (const auto& p : m_peds) {
		if (p.animal == animalsOnly && !p.lower.empty()) seen[(unsigned char)p.lower[0]] = true;
	}
	std::vector<std::string> out;
	for (char c = 'a'; c <= 'z'; c++) if (seen[(unsigned char)c]) out.push_back(std::string(1, c));
	for (char c = '0'; c <= '9'; c++) if (seen[(unsigned char)c]) out.push_back(std::string(1, c));
	if (seen[(unsigned char)'_']) out.push_back("_");
	return out;
}

std::vector<int> CSceneData::PedsInBucket(const std::string& bucket, bool animalsOnly, int maxResults) const
{
	std::vector<int> out;
	if (bucket.empty()) return out;
	char c = (char)tolower((unsigned char)bucket[0]);
	for (int i = 0; i < (int)m_peds.size(); i++) {
		if (m_peds[i].animal != animalsOnly) continue;
		if (!m_peds[i].lower.empty() && m_peds[i].lower[0] == c) {
			out.push_back(i);
			if ((int)out.size() >= maxResults) break;
		}
	}
	return out;
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

void CSceneData::BuildScenarioDefs(const std::vector<std::string>& names)
{
	m_scenarios.clear();
	m_scenarios.reserve(names.size() + 1);

	// Index 0 is always "None" so the picker can clear a scenario.
	ScenarioDef none;
	none.name = "None";
	none.label = "None";
	none.category = SCENARIO_HUMAN; // shown for everyone
	m_scenarios.push_back(none);

	for (const std::string& raw : names) {
		if (raw.empty() || raw == "None") continue;
		ScenarioDef d;
		d.name = raw;
		d.label = raw;
		std::replace(d.label.begin(), d.label.end(), '_', ' ');
		Classify(raw, d.category, d.species);
		m_scenarios.push_back(d);
	}

	// Collect the distinct animal species tokens, sorted longest-first so
	// CanonicalSpecies() prefers the most specific base ("pigeon" over "pig").
	m_animalSpecies.clear();
	for (const ScenarioDef& d : m_scenarios) {
		if (d.category != SCENARIO_ANIMAL || d.species.empty()) continue;
		if (std::find(m_animalSpecies.begin(), m_animalSpecies.end(), d.species) == m_animalSpecies.end())
			m_animalSpecies.push_back(d.species);
	}
	std::sort(m_animalSpecies.begin(), m_animalSpecies.end(),
		[](const std::string& a, const std::string& b) { return a.size() > b.size(); });
}

void CSceneData::Load()
{
	m_warning.clear();

	// All three catalogues are compiled in (Editor/Data/Scene*Data.h), so this
	// just copies the embedded arrays into the working structures.

	// --- scenarios ---
	{
		std::vector<std::string> names;
		names.reserve(SceneEmbedded::ScenariosCount);
		for (int i = 0; i < SceneEmbedded::ScenariosCount; i++)
			names.emplace_back(SceneEmbedded::Scenarios[i]);
		BuildScenarioDefs(names);
	}

	// --- objects ---
	m_objects.clear();
	m_objects.reserve(SceneEmbedded::ObjectsCount);
	for (int i = 0; i < SceneEmbedded::ObjectsCount; i++) {
		ObjectDef d; d.name = SceneEmbedded::Objects[i]; d.lower = Lower(d.name);
		m_objects.push_back(d);
	}

	// --- peds ---
	m_peds.clear();
	m_peds.reserve(SceneEmbedded::PedsCount);
	for (int i = 0; i < SceneEmbedded::PedsCount; i++) {
		PedDef d; d.name = SceneEmbedded::Peds[i]; d.lower = Lower(d.name); d.animal = IsAnimalModel(d.name);
		m_peds.push_back(d);
	}

	ObjectsFromFile = ScenariosFromFile = PedsFromFile = true; // embedded == always present
	PRINT_INFO("SceneData: embedded ", (int)m_objects.size(), " objects, ",
		(int)m_peds.size(), " peds, ", (int)m_scenarios.size(), " scenarios");
}
