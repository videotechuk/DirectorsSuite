// Director's Suite - Photo Mode "remove engine black bars" patch.
//
// RDR2's native photo-mode render path forces a 16:9 cinematic crop, so on
// ultrawide displays you get black bars no script native can switch off. The
// only way to kill them is the engine itself: AOB-scan the live (decrypted)
// RDR2.exe image and neutralise the instructions that enable the bars - the
// same technique the standalone RDR2NoBlackBars.asi uses.
//
// RDR2.exe is encrypted on disk (Arxan anti-tamper) and only decrypts in
// memory, so the scan MUST run at runtime against the loaded module; it cannot
// be verified statically. Apply() therefore reports whether the signatures were
// actually found, and Revert() restores the original bytes so the toggle is
// fully reversible within a session.
//
// Caveats (surfaced in the menu): the byte signatures are game-build specific,
// and the patch is global - it removes the bars everywhere, including the
// game's own Photo Mode and cinematic cameras.

#pragma once

namespace BlackBarsPatch
{
	// Scan + patch the in-memory game module. Returns true only if BOTH
	// signatures were found and patched. Idempotent (re-applying is a no-op).
	bool Apply();

	// Restore the original bytes at every patched site. Safe to call when not
	// applied (no-op).
	bool Revert();

	// Whether the patch is currently applied this session.
	bool IsApplied();

	// Human-readable outcome of the last Apply()/Revert() (for a menu subtitle),
	// e.g. "Black bars removed" or "Signature not found (build mismatch?)".
	const char* LastResult();
}
