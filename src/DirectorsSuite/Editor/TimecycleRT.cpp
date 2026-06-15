#include "TimecycleRT.h"
#include <windows.h>
#include <cstdio>
#include <map>

// ---------------------------------------------------------------------------
// Game structures (ported from citizenfx Timecycle.h, IS_RDR3 layouts;
// static_asserts mirror the originals)
// ---------------------------------------------------------------------------

namespace
{
#pragma pack(push, 1)
	template<typename T>
	struct atArray
	{
		T* m_data;
		unsigned short m_count;
		unsigned short m_capacity;
		char m_pad[4];
	};

	struct tcModData
	{
		int m_index;
		float m_value1;
		float m_value2;
	};

	struct tcModifier
	{
		atArray<tcModData> m_modData; // 16 bytes
		unsigned m_nameHash;          // +16
		char m_pad[4];
		void* m_varMap;               // +24
		unsigned m_userFlags;         // +32
		unsigned m_unk;               // +36
	};

	struct tcVarInfo // RDR3 layout: 0x18
	{
		int m_index;
		unsigned m_nameHash;
		float m_value;
		char m_end[12];
	};

	struct tcManager
	{
		char m_pad[0x40];
		atArray<tcModifier*> m_modifiers;
		// (binary map follows; not needed)
	};

	struct TimecycleScriptData // RDR3: no extra-modifier tail
	{
		int m_primaryModifierIndex;
		float m_primaryModifierStrength;
		int m_transitionModifierIndex;
		float m_transitionModifierStrength;
		float m_transitionModifierSpeed;
	};
#pragma pack(pop)

	static_assert(sizeof(tcModData) == 0xC, "tcModData size");
	static_assert(sizeof(tcModifier) == 0x28, "tcModifier size");
	static_assert(sizeof(tcVarInfo) == 0x18, "tcVarInfo size");

	// resolved statics
	int* g_numVars = nullptr;
	tcVarInfo** g_pVarInfos = nullptr;
	tcManager* g_tcManager = nullptr;
	TimecycleScriptData* g_scriptData = nullptr;
	bool g_initTried = false;
	bool g_available = false;

	// snapshots for RestoreAllEdits: (modIndex, varIndex) -> original values
	struct EditKey { int mod; int var; bool operator<(const EditKey& o) const { return mod != o.mod ? mod < o.mod : var < o.var; } };
	struct EditVal { float v1, v2; };
	std::map<EditKey, EditVal> g_edits;

	// -----------------------------------------------------------------------
	// Pattern scanner ("48 8B ?? 05" style, ? = wildcard byte)
	// -----------------------------------------------------------------------

	const unsigned char* FindPattern(const char* pattern)
	{
		HMODULE module = GetModuleHandleA(nullptr);
		if (!module) return nullptr;

		auto dos = (const IMAGE_DOS_HEADER*)module;
		auto nt = (const IMAGE_NT_HEADERS*)((const char*)module + dos->e_lfanew);
		const unsigned char* base = (const unsigned char*)module;
		size_t size = nt->OptionalHeader.SizeOfImage;

		// parse "AA BB ? CC" into bytes + mask
		unsigned char bytes[64];
		bool wild[64];
		int n = 0;
		for (const char* p = pattern; *p && n < 64;) {
			if (*p == ' ') { p++; continue; }
			if (*p == '?') {
				wild[n] = true; bytes[n] = 0; n++;
				p++; if (*p == '?') p++;
				continue;
			}
			auto hex = [](char c) -> int {
				if (c >= '0' && c <= '9') return c - '0';
				if (c >= 'A' && c <= 'F') return c - 'A' + 10;
				if (c >= 'a' && c <= 'f') return c - 'a' + 10;
				return -1;
			};
			int hi = hex(p[0]), lo = hex(p[1]);
			if (hi < 0 || lo < 0) return nullptr;
			wild[n] = false;
			bytes[n] = (unsigned char)((hi << 4) | lo);
			n++;
			p += 2;
		}
		if (n == 0) return nullptr;

		for (size_t i = 0; i + n <= size; i++) {
			bool match = true;
			for (int j = 0; j < n; j++) {
				if (!wild[j] && base[i + j] != bytes[j]) { match = false; break; }
			}
			if (match) return base + i;
		}
		return nullptr;
	}

	// rel32 resolution, mirroring fivem's hook::get_address overloads
	template<typename T>
	T GetAddress(const unsigned char* at, int relOffset, int instrLen)
	{
		if (!at) return nullptr;
		int rel = *(const int*)(at + relOffset);
		return (T)(at + instrLen + rel);
	}

	template<typename T>
	T GetAddressShort(const unsigned char* at) // rel32 right at `at`, +4
	{
		if (!at) return nullptr;
		int rel = *(const int*)at;
		return (T)(at + 4 + rel);
	}

	tcModifier* GetModifier(int index)
	{
		if (!g_available || !g_tcManager) return nullptr;
		auto& arr = g_tcManager->m_modifiers;
		if (index < 0 || index >= (int)arr.m_count || !arr.m_data) return nullptr;
		return arr.m_data[index];
	}

	tcModData* FindModData(tcModifier* mod, int varIndex)
	{
		if (!mod || !mod->m_modData.m_data) return nullptr;
		for (int i = 0; i < (int)mod->m_modData.m_count; i++) {
			if (mod->m_modData.m_data[i].m_index == varIndex) {
				return &mod->m_modData.m_data[i];
			}
		}
		return nullptr;
	}

	// -----------------------------------------------------------------------
	// Known-name dictionary (hash -> name). Sun/light candidates first (used
	// by FindSunCandidates), then generally useful vars for dump labelling.
	// -----------------------------------------------------------------------

	const char* g_sunCandidates[] = {
		"sun_azimuth", "sun_zenith", "sun_direction", "sun_direction_x", "sun_direction_y", "sun_direction_z",
		"sun_dir", "sun_dir_x", "sun_dir_y", "sun_dir_z", "sun_pitch", "sun_yaw", "sun_roll", "sun_angle",
		"sun_height", "sun_position", "sun_pos", "azimuth", "azimuth_offset", "sun_azimuth_offset",
		"azimuth_east", "azimuth_west", "azimuth_transition", "azimuth_transition_position",
		"light_direction", "light_direction_x", "light_direction_y", "light_direction_z",
		"light_dir", "light_dir_x", "light_dir_y", "light_dir_z",
		"dir_light_pitch", "dir_light_yaw", "moon_azimuth", "moon_direction", "moon_pitch",
	};

	const char* g_extraKnown[] = {
		"exposure", "exposure_min", "exposure_max", "gamma", "contrast", "saturation", "saturation_far",
		"white_point", "black_point", "brightness", "vignette", "vignetting_intensity", "vignetting_radius",
		"grain_intensity", "film_grain", "bloom_intensity", "bloom_threshold", "desaturation",
		"sky_brightness", "sky_hdr", "cloud_brightness", "fog_density", "fog_start", "fog_end",
		"dir_light_intensity", "dir_light_col_r", "dir_light_col_g", "dir_light_col_b",
		"sun_disc_size", "sun_disc_brightness", "sun_col_r", "sun_col_g", "sun_col_b",
		"ambient_intensity", "ambient_down_intensity", "ambient_up_intensity",
		"shadow_strength", "shadow_softness", "moon_brightness", "moon_disc_size",
		"water_reflection", "dof_strength", "motion_blur_strength",
	};

	std::map<unsigned, const char*> g_knownByHash;

	void BuildDictionary()
	{
		if (!g_knownByHash.empty()) return;
		for (const char* name : g_sunCandidates) g_knownByHash[TimecycleRT::Joaat(name)] = name;
		for (const char* name : g_extraKnown) g_knownByHash[TimecycleRT::Joaat(name)] = name;
	}
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

namespace TimecycleRT
{
	unsigned Joaat(const char* text)
	{
		unsigned hash = 0;
		for (const char* p = text; *p; p++) {
			char c = *p;
			if (c >= 'A' && c <= 'Z') c += 32;
			hash += (unsigned char)c;
			hash += hash << 10;
			hash ^= hash >> 6;
		}
		hash += hash << 3;
		hash ^= hash >> 11;
		hash += hash << 15;
		return hash;
	}

	bool Init()
	{
		if (g_initTried) return g_available;
		g_initTried = true;
		BuildDictionary();

		// rage::tcConfig (RedM RDR3 pattern)
		if (auto loc = FindPattern("7E 58 48 8B EB 48 8B 05")) {
			g_numVars = GetAddress<int*>(loc - 6, 2, 6);
			g_pVarInfos = GetAddress<tcVarInfo**>(loc + 5, 3, 7);
		}

		// rage::tcManager::ms_Instance
		if (auto loc = FindPattern("F3 0F 10 BB 80 00 00 00 48 8D 0D ? ? ? ? 0F")) {
			g_tcManager = GetAddress<tcManager*>(loc + 8, 3, 7);
		}

		// TimecycleScriptData
		if (auto loc = FindPattern("83 0D ? ? ? ? FF 8B 1D ? ? ? ? 89 ? ? ? ? 03")) {
			g_scriptData = GetAddressShort<TimecycleScriptData*>(loc + 15);
		}

		// sanity: every static resolved and the var count looks plausible
		g_available = g_numVars && g_pVarInfos && g_tcManager && g_scriptData
			&& *g_numVars > 0 && *g_numVars < 4096 && *g_pVarInfos != nullptr;

		return g_available;
	}

	bool Available() { return g_available; }

	bool VarsAvailable()
	{
		return g_numVars && g_pVarInfos && *g_numVars > 0 && *g_numVars < 4096 && *g_pVarInfos != nullptr;
	}

	const char* StatusText()
	{
		if (!g_initTried) return "not initialized";
		if (g_available) return "active (full editing)";
		if (VarsAvailable()) return "partial (variables only)";
		// report which of the three scans missed
		static char buf[64];
		sprintf_s(buf, "unavailable (tc:%c mgr:%c data:%c)",
			(g_numVars && g_pVarInfos) ? 'Y' : 'N',
			g_tcManager ? 'Y' : 'N',
			g_scriptData ? 'Y' : 'N');
		return buf;
	}

	int VarCount()
	{
		return VarsAvailable() ? *g_numVars : 0;
	}

	unsigned VarHash(int index)
	{
		if (!VarsAvailable() || index < 0 || index >= *g_numVars) return 0;
		return (*g_pVarInfos)[index].m_nameHash;
	}

	const char* VarKnownName(int index)
	{
		unsigned hash = VarHash(index);
		if (hash == 0) return nullptr;
		auto it = g_knownByHash.find(hash);
		return it != g_knownByHash.end() ? it->second : nullptr;
	}

	int FindVarByName(const char* name)
	{
		if (!VarsAvailable() || !name) return -1;
		unsigned hash = Joaat(name);
		for (int i = 0; i < *g_numVars; i++) {
			if ((*g_pVarInfos)[i].m_nameHash == hash) return i;
		}
		return -1;
	}

	int ModifierCount()
	{
		return (g_available && g_tcManager) ? (int)g_tcManager->m_modifiers.m_count : 0;
	}

	unsigned ModifierHash(int index)
	{
		tcModifier* mod = GetModifier(index);
		return mod ? mod->m_nameHash : 0;
	}

	int FindModifierWithVar(int varIndex, int startAt)
	{
		for (int i = startAt; i < ModifierCount(); i++) {
			if (FindModData(GetModifier(i), varIndex)) return i;
		}
		return -1;
	}

	int GetPrimaryIndex()
	{
		return g_available ? g_scriptData->m_primaryModifierIndex : -1;
	}

	void SetPrimary(int modifierIndex, float strength)
	{
		if (!g_available) return;
		g_scriptData->m_primaryModifierIndex = modifierIndex;
		g_scriptData->m_primaryModifierStrength = strength;
	}

	bool GetModVar(int modIndex, int varIndex, float& outValue)
	{
		tcModData* data = FindModData(GetModifier(modIndex), varIndex);
		if (!data) return false;
		outValue = data->m_value1;
		return true;
	}

	bool SetModVar(int modIndex, int varIndex, float value)
	{
		tcModData* data = FindModData(GetModifier(modIndex), varIndex);
		if (!data) return false;

		EditKey key{ modIndex, varIndex };
		if (g_edits.find(key) == g_edits.end()) {
			g_edits[key] = { data->m_value1, data->m_value2 }; // first touch: snapshot
		}
		data->m_value1 = value;
		data->m_value2 = value;
		return true;
	}

	int ApplyVarToScene(int varIndex, float value)
	{
		if (!g_available) return -1;

		// prefer the modifier already in the script slot if it has the var
		int primary = GetPrimaryIndex();
		int target = (primary >= 0 && FindModData(GetModifier(primary), varIndex)) ? primary : FindModifierWithVar(varIndex);
		if (target < 0) return -1;

		SetModVar(target, varIndex, value);
		SetPrimary(target, 1.0f);
		return target;
	}

	void RestoreAllEdits()
	{
		for (const auto& [key, val] : g_edits) {
			if (tcModData* data = FindModData(GetModifier(key.mod), key.var)) {
				data->m_value1 = val.v1;
				data->m_value2 = val.v2;
			}
		}
		g_edits.clear();
	}

	std::vector<int> FindSunCandidates()
	{
		std::vector<int> out;
		if (!VarsAvailable()) return out;

		for (const char* name : g_sunCandidates) {
			unsigned hash = Joaat(name);
			for (int i = 0; i < *g_numVars; i++) {
				if ((*g_pVarInfos)[i].m_nameHash == hash) {
					out.push_back(i);
					break;
				}
			}
		}
		return out;
	}

	int DumpVarsToFile()
	{
		if (!VarsAvailable()) return -1; // dump only needs the variable table

		char path[MAX_PATH];
		GetModuleFileNameA(nullptr, path, MAX_PATH);
		std::string file(path);
		size_t slash = file.find_last_of("\\/");
		file = (slash == std::string::npos ? std::string(".") : file.substr(0, slash)) + "\\DirectorsSuite_Timecycle.txt";

		FILE* f = nullptr;
		if (fopen_s(&f, file.c_str(), "w") != 0 || !f) return -1;

		fprintf(f, "Director's Suite - timecycle variable dump\n");
		fprintf(f, "vars: %d, modifiers: %d\n", *g_numVars, ModifierCount());
		fprintf(f, "index\thash\tdefault\tknown name\n");
		for (int i = 0; i < *g_numVars; i++) {
			const tcVarInfo& vi = (*g_pVarInfos)[i];
			const char* known = VarKnownName(i);
			fprintf(f, "%d\t0x%08X\t%.4f\t%s\n", i, vi.m_nameHash, vi.m_value, known ? known : "");
		}
		fclose(f);
		return *g_numVars;
	}
}
