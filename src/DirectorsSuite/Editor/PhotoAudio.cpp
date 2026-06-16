#include "PhotoAudio.h"
#include "Config.h"
#include "..\script.h"

// ---------------------------------------------------------------------------
// Navigation SFX
//
// All UI sounds come from the game's own "Photo_Mode_Sounds" soundset (verified
// in the decompiled camera_photomode.c). This is the ONLY soundset that is
// reliably resident in our free-roam photo-mode context: the shared menu
// soundsets (Ledger_Sounds, HUD_SHOP_SOUNDSET) refuse to load/play here, so
// they were silent. We map our actions onto the NON-zoom entries: "effects"
// (the photo-mode UI click) for nav/tab/confirm, "filter_left/right" (the
// list-scroll swipes) for value/choice changes, plus back / hide_hud / reset /
// take_photo. The zoom tones (adjust_oneshot, lens_up/down) are deliberately
// avoided - those were the "zoom" sound on scroll the user reported.
// ---------------------------------------------------------------------------

namespace
{
	const char* SOUNDSET = "Photo_Mode_Sounds";

	const char* SoundName(PhotoAudio::PMSound s)
	{
		switch (s) {
			case PhotoAudio::PM_NAV_UP:       return "effects";
			case PhotoAudio::PM_NAV_DOWN:     return "effects";
			case PhotoAudio::PM_TAB:          return "effects";
			case PhotoAudio::PM_CHOICE_LEFT:  return "filter_left";
			case PhotoAudio::PM_CHOICE_RIGHT: return "filter_right";
			case PhotoAudio::PM_ADJUST_DOWN:  return "filter_left";
			case PhotoAudio::PM_ADJUST_UP:    return "filter_right";
			case PhotoAudio::PM_TOGGLE:       return "effects";
			case PhotoAudio::PM_CONFIRM:      return "effects";
			case PhotoAudio::PM_BACK:         return "back";
			case PhotoAudio::PM_HIDE_UI:      return "hide_hud";
			case PhotoAudio::PM_RESET:        return "reset";
			case PhotoAudio::PM_SHUTTER:      return "take_photo";
		}
		return "effects";
	}

	// Kept resident each frame (see Tick) so the SFX never go silent after the
	// game evicts an idle bank ("navigation sounds stop working after a minute").
	const char* kSoundsets[] = { "Photo_Mode_Sounds" };

	void PrepareAllSoundsets()
	{
		for (const char* set : kSoundsets) AUDIO::PREPARE_SOUNDSET(set, false);
	}

	bool g_entered = false;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void PhotoAudio::Play(PMSound sound)
{
	AUDIO::PREPARE_SOUNDSET(SOUNDSET, false);
	AUDIO::PLAY_SOUND_FRONTEND(SoundName(sound), SOUNDSET, true, 0);
}

void PhotoAudio::Enter()
{
	if (g_entered) return;
	g_entered = true;

	PrepareAllSoundsets();
}

void PhotoAudio::Tick()
{
	if (!g_entered) return;

	// Keep the UI soundsets loaded so navigation never falls silent over time.
	PrepareAllSoundsets();
}

void PhotoAudio::Exit()
{
	if (!g_entered) return;
	g_entered = false;
}
