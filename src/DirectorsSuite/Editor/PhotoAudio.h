// Director's Suite - Photo Mode audio layer.
//
// Two jobs, both built from the game's own audio data:
//
//  1. Ambience swap. When Photo Mode opens we duck the live world (the dynamic
//     score, crowd "walla" murmur) and optionally trigger a music event so the
//     editor sits on a calm soundtrack bed instead of gunfire and chatter.
//     Everything is restored on close. RDR2 exposes no single "master mute"
//     native to scripts, so this is a layered duck (OpenWorldMusicDisabled +
//     walla density 0 + our music event), not a hard silence - the levers that
//     ARE reachable, applied together.
//
//  2. Navigation SFX, drawn from the game's own "Photo_Mode_Sounds" soundset -
//     the only one reliably resident in our free-roam photo-mode context (the
//     shared menu soundsets Ledger_Sounds / HUD_SHOP_SOUNDSET refuse to play
//     here and were silent). Mapped to the NON-zoom entries - "effects" for
//     nav/tab/confirm, "filter_left/right" for value/choice changes, plus
//     back / hide_hud / reset / take_photo - so scrolling no longer plays the
//     camera zoom tone. The set is kept resident each frame so SFX never fall
//     silent over time.

#pragma once

namespace PhotoAudio
{
	// One-shot UI sounds, mapped to the official Photo_Mode_Sounds soundset.
	enum PMSound
	{
		PM_NAV_UP = 0,   // move selection up
		PM_NAV_DOWN,     // move selection down
		PM_TAB,          // switch tab / category
		PM_CHOICE_LEFT,  // cycle a choice left
		PM_CHOICE_RIGHT, // cycle a choice right
		PM_ADJUST_DOWN,  // slider value down
		PM_ADJUST_UP,    // slider value up
		PM_TOGGLE,       // flip a toggle
		PM_CONFIRM,      // run an action / ENTER
		PM_BACK,         // leave / cancel
		PM_HIDE_UI,      // hide the interface
		PM_RESET,        // reset action
		PM_SHUTTER,      // take a photo
	};

	// Plays a one-shot from Photo_Mode_Sounds (prepares the set on first use).
	void Play(PMSound sound);

	// Ambience swap. Enter ducks the world + starts the soundtrack; Exit puts
	// everything back exactly as it was. Tick re-asserts the per-frame levers
	// (walla density resets itself every frame) while Photo Mode is open.
	void Enter();
	void Tick();
	void Exit();

	// --- background music selection (live; no need to leave Photo Mode) ---
	// Music is OFF by default. The user enables it explicitly and picks one of
	// the ambient free-roam exploration beds. Index 0 of the track list is still
	// "Off"; selecting a track stops whatever is playing and starts the new bed.
	bool MusicEnabled();
	void SetMusicEnabled(bool enabled);
	int  TrackCount();
	const char* TrackLabel(int idx);
	int  CurrentTrack();
	void SelectTrack(int idx);

	// World-audio duck (the score + crowd murmur), toggled live from the menu.
	bool MuteWorld();
	void SetMuteWorld(bool mute);
}
