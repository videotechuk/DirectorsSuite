// Director's Suite - Photo Mode audio layer.
//
// Navigation SFX, drawn from the game's own "Photo_Mode_Sounds" soundset - the
// only one reliably resident in our free-roam photo-mode context (the shared
// menu soundsets Ledger_Sounds / HUD_SHOP_SOUNDSET refuse to play here and were
// silent). Mapped to the NON-zoom entries - "effects" for nav/tab/confirm,
// "filter_left/right" for value/choice changes, plus back / hide_hud / reset /
// take_photo - so scrolling no longer plays the camera zoom tone. The set is
// kept resident each frame so SFX never fall silent over time.

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

	// Enter/Exit bracket a Photo Mode session; Tick keeps the UI soundset
	// resident each frame so navigation SFX never fall silent over time.
	void Enter();
	void Tick();
	void Exit();
}
