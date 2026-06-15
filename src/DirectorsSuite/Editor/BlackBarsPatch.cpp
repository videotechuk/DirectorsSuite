#include "BlackBarsPatch.h"
#include <windows.h>
#include <cstdint>

// ---------------------------------------------------------------------------
// Signature definitions (mirrors RDR2NoBlackBars.asi, verified by disassembly).
//
//  A: C6 05 ?? ?? ?? ?? FF 0F 28 74 24 60
//     = mov byte ptr [rip+disp32], 0FFh  (sets the "bars enabled" flag true),
//       right before a `movaps xmm6,[rsp+60]` epilogue. We change the 0xFF
//       immediate (offset +6) to 0x00 so the flag is set false instead.
//
//  B: 48 8B C4 48 89 58 08 56 57 41 56 48 81 EC C0 00 00 00 0F 29 70 D8
//     = the prologue of the routine that applies the cinematic bars. We write
//       0xC3 (RET) over its first byte (offset +0) so it does nothing.
// ---------------------------------------------------------------------------

namespace
{
	struct Sig
	{
		const uint8_t* bytes;   // pattern bytes (mask byte value ignored where mask==0)
		const uint8_t* mask;    // 1 = must match, 0 = wildcard
		size_t len;
		size_t patchOffset;     // byte within the match to overwrite
		uint8_t patchValue;     // value to write
	};

	// Pattern A
	const uint8_t kA_bytes[] = { 0xC6, 0x05, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x0F, 0x28, 0x74, 0x24, 0x60 };
	const uint8_t kA_mask[]  = { 1,    1,    0,    0,    0,    0,    1,    1,    1,    1,    1,    1    };

	// Pattern B
	const uint8_t kB_bytes[] = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x56, 0x57, 0x41, 0x56,
	                             0x48, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00, 0x0F, 0x29, 0x70, 0xD8 };
	const uint8_t kB_mask[]  = { 1,1,1, 1,1,1,1, 1,1, 1,1, 1,1,1, 1,1,1,1, 1,1,1,1 };

	const Sig kSigs[] = {
		{ kA_bytes, kA_mask, sizeof(kA_bytes), 6, 0x00 },
		{ kB_bytes, kB_mask, sizeof(kB_bytes), 0, 0xC3 },
	};
	const int kSigCount = (int)(sizeof(kSigs) / sizeof(kSigs[0]));

	// What we patched, so we can put it back exactly.
	struct PatchSite { uint8_t* addr; uint8_t original; bool valid; };
	PatchSite g_sites[kSigCount] = {};

	bool g_applied = false;
	const char* g_result = "Not applied";

	bool MatchAt(const uint8_t* p, const Sig& s)
	{
		for (size_t i = 0; i < s.len; i++) {
			if (s.mask[i] && p[i] != s.bytes[i]) return false;
		}
		return true;
	}

	// Scan only the executable sections of the main module's in-memory image.
	uint8_t* ScanModule(const Sig& s)
	{
		HMODULE mod = GetModuleHandleA(nullptr); // the host process = RDR2.exe
		if (!mod) mod = GetModuleHandleA("RDR2.exe");
		if (!mod) return nullptr;

		uint8_t* base = reinterpret_cast<uint8_t*>(mod);
		auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
		if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
		auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
		if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

		auto* sec = IMAGE_FIRST_SECTION(nt);
		for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
			if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;

			uint8_t* start = base + sec->VirtualAddress;
			DWORD size = sec->Misc.VirtualSize ? sec->Misc.VirtualSize : sec->SizeOfRawData;
			if (size < s.len) continue;

			uint8_t* end = start + (size - s.len);
			for (uint8_t* p = start; p <= end; p++) {
				if (MatchAt(p, s)) return p;
			}
		}
		return nullptr;
	}

	bool WriteByte(uint8_t* addr, uint8_t value, uint8_t* outOriginal)
	{
		DWORD oldProt = 0;
		if (!VirtualProtect(addr, 1, PAGE_EXECUTE_READWRITE, &oldProt)) return false;
		if (outOriginal) *outOriginal = *addr;
		*addr = value;
		DWORD tmp = 0;
		VirtualProtect(addr, 1, oldProt, &tmp);
		FlushInstructionCache(GetCurrentProcess(), addr, 1);
		return true;
	}
}

namespace BlackBarsPatch
{
	bool Apply()
	{
		if (g_applied) { g_result = "Already applied"; return true; }

		// Locate every signature first; only commit if all are present so we
		// never leave the game half-patched.
		uint8_t* found[kSigCount] = {};
		for (int i = 0; i < kSigCount; i++) {
			found[i] = ScanModule(kSigs[i]);
			if (!found[i]) {
				g_result = "Signature not found (game build mismatch?)";
				return false;
			}
		}

		bool ok = true;
		for (int i = 0; i < kSigCount; i++) {
			uint8_t* target = found[i] + kSigs[i].patchOffset;
			uint8_t original = 0;
			if (WriteByte(target, kSigs[i].patchValue, &original)) {
				g_sites[i] = { target, original, true };
			}
			else {
				g_sites[i].valid = false;
				ok = false;
			}
		}

		if (!ok) {
			// Roll back anything that did take, so a partial failure is clean.
			Revert();
			g_result = "Patch write failed (protection?)";
			return false;
		}

		g_applied = true;
		g_result = "Black bars removed";
		return true;
	}

	bool Revert()
	{
		bool any = false;
		for (int i = 0; i < kSigCount; i++) {
			if (g_sites[i].valid && g_sites[i].addr) {
				WriteByte(g_sites[i].addr, g_sites[i].original, nullptr);
				any = true;
			}
			g_sites[i] = {};
		}
		g_applied = false;
		g_result = any ? "Black bars restored" : "Nothing to restore";
		return any;
	}

	bool IsApplied() { return g_applied; }
	const char* LastResult() { return g_result; }
}
