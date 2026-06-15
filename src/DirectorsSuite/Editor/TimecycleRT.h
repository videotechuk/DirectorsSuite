// Director's Suite - runtime timecycle access, ported from the RedM/FiveM
// timecycle editor (citizenfx commit 1b212f34, gta-game-five/src/Timecycle.cpp
// IS_RDR3 branches; data structures from include/Timecycle.h).
//
// What this gives us that natives can't: direct read/write access to the
// game's timecycle variable table (rage::tcConfig), the modifier list
// (rage::tcManager) and the script-modifier slot (TimecycleScriptData), so
// Photo Mode can apply ANY modifier by index and live-edit its variable
// values - the exact mechanism behind RedM's timecycle editor natives.
//
// Safety: only the three DATA statics are resolved (no code hooks, no
// jitasm stubs from the original). Every byte pattern is the RedM-shipped
// RDR3 pattern; if any of them misses on a future game build, the module
// reports unavailable and every entry point no-ops.
//
// RDR3 quirk: tcVarInfo stores variable NAME HASHES only (the editor
// recovers names by hooking the loader, which we deliberately don't do in
// SP). We match known/candidate names by joaat hash and can dump the whole
// table for offline identification.

#pragma once
#include <string>
#include <vector>
#include "..\..\..\inc\types.h"

namespace TimecycleRT
{
	// Pattern-scans once; safe to call repeatedly. False = unsupported build.
	bool Init();
	bool Available();      // all statics resolved: full live editing
	bool VarsAvailable();  // variable table resolved: dump + browsing only
	const char* StatusText(); // human-readable resolution state for the UI

	// --- variable table (rage::tcConfig) ---
	int VarCount();
	unsigned VarHash(int index);              // 0 when out of range
	const char* VarKnownName(int index);      // nullptr if not in our dictionary
	// Variable index whose name hashes to `name` (joaat), or -1 if absent.
	int FindVarByName(const char* name);

	// --- modifiers (rage::tcManager) ---
	int ModifierCount();
	unsigned ModifierHash(int index);
	// first modifier whose mod-data contains the given variable, or -1
	int FindModifierWithVar(int varIndex, int startAt = 0);

	// --- script slot (what SET_TIMECYCLE_MODIFIER drives) ---
	int  GetPrimaryIndex();
	void SetPrimary(int modifierIndex, float strength);

	// --- live editing ---
	// Reads/writes value1+value2 of `varIndex` inside modifier `modIndex`.
	bool GetModVar(int modIndex, int varIndex, float& outValue);
	bool SetModVar(int modIndex, int varIndex, float value); // snapshots original first

	// Applies a variable to the scene: finds a modifier containing it,
	// makes it the primary script modifier at full strength and writes the
	// value. Returns the modifier index used, or -1.
	int ApplyVarToScene(int varIndex, float value);

	// Undo every SetModVar made this session (called on Photo Mode exit)
	void RestoreAllEdits();

	// Search the var table for sun/azimuth/light-direction candidates.
	// Returns var indices (may be empty - RDR3 may not expose sun direction
	// as a timecycle var at all; the dump exists to find out).
	std::vector<int> FindSunCandidates();

	// Writes index/hash/known-name/default for every variable to
	// <exe dir>\DirectorsSuite_Timecycle.txt. Returns var count or -1.
	int DumpVarsToFile();

	unsigned Joaat(const char* text); // lowercase Jenkins hash
}
