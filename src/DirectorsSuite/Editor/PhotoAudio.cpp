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

	// -----------------------------------------------------------------------
	// Background music tracks - BOB_* names per request (.awc extension stripped).
	//
	// NOTE: these are RDR2 .awc audio-CONTAINER names, not the named music EVENTS
	// the trigger system normally expects, so they may not play via
	// TRIGGER_MUSIC_EVENT (the music-event DB only lists BOB_FISHING_START/STOP,
	// not these stems). If they stay silent, the play mechanism needs changing,
	// not the list. Index 0 stays "Off"; music is OFF by default.
	// -----------------------------------------------------------------------
	struct TrackDef { const char* label; const char* event; };
	const TrackDef kTracks[] = {
		{ "Off", nullptr },
		{ "BOB_ABERDEEN_PIG_FARM_ONESHOT", "BOB_ABERDEEN_PIG_FARM_ONESHOT" },
		{ "BOB_ABIGAIL_3", "BOB_ABIGAIL_3" },
		{ "BOB_ABIGAIL_3_OS1", "BOB_ABIGAIL_3_OS1" },
		{ "BOB_ABIGAIL_3_OS2", "BOB_ABIGAIL_3_OS2" },
		{ "BOB_ABIGAIL_3_OS3", "BOB_ABIGAIL_3_OS3" },
		{ "BOB_BEECHERS12_OS1", "BOB_BEECHERS12_OS1" },
		{ "BOB_BEECHERS12_OS2", "BOB_BEECHERS12_OS2" },
		{ "BOB_BEECHERS_2_HOUSE_BUILD", "BOB_BEECHERS_2_HOUSE_BUILD" },
		{ "BOB_CARD_GAME_AM_STEM", "BOB_CARD_GAME_AM_STEM" },
		{ "BOB_CARD_GAME_EM_STEM", "BOB_CARD_GAME_EM_STEM" },
		{ "BOB_CRACKPOT_2", "BOB_CRACKPOT_2" },
		{ "BOB_DINO_BONES_ONESHOT_2", "BOB_DINO_BONES_ONESHOT_2" },
		{ "BOB_DOMINOES_BM_STEM", "BOB_DOMINOES_BM_STEM" },
		{ "BOB_DOMINOES_EM_STEM", "BOB_DOMINOES_EM_STEM" },
		{ "BOB_EDITH_DOWNES_21", "BOB_EDITH_DOWNES_21" },
		{ "BOB_EDITH_DOWNES_21_OS1", "BOB_EDITH_DOWNES_21_OS1" },
		{ "BOB_EDITH_DOWNES_21_OS2", "BOB_EDITH_DOWNES_21_OS2" },
		{ "BOB_EDITH_DOWNES_21_OS3", "BOB_EDITH_DOWNES_21_OS3" },
		{ "BOB_EDITH_DOWNES_22", "BOB_EDITH_DOWNES_22" },
		{ "BOB_EDITH_DOWNES_22_OS1", "BOB_EDITH_DOWNES_22_OS1" },
		{ "BOB_EDITH_DOWNES_22_OS2", "BOB_EDITH_DOWNES_22_OS2" },
		{ "BOB_ENDLESS_SUMMER_2_CT", "BOB_ENDLESS_SUMMER_2_CT" },
		{ "BOB_EPILOGUE_PT1_CT", "BOB_EPILOGUE_PT1_CT" },
		{ "BOB_FIN3_END_CREDITS", "BOB_FIN3_END_CREDITS" },
		{ "BOB_FISHING_AMAJOR_V1_1", "BOB_FISHING_AMAJOR_V1_1" },
		{ "BOB_FISHING_AMAJOR_V2_1", "BOB_FISHING_AMAJOR_V2_1" },
		{ "BOB_FISHING_AMAJOR_V3_1", "BOB_FISHING_AMAJOR_V3_1" },
		{ "BOB_FISHING_AMAJOR_V7_1", "BOB_FISHING_AMAJOR_V7_1" },
		{ "BOB_FISHING_AMINOR_V1_1", "BOB_FISHING_AMINOR_V1_1" },
		{ "BOB_FISHING_AMINOR_V2_1", "BOB_FISHING_AMINOR_V2_1" },
		{ "BOB_FISHING_AMINOR_V5_1", "BOB_FISHING_AMINOR_V5_1" },
		{ "BOB_FISHING_BMAJOR_V1_1", "BOB_FISHING_BMAJOR_V1_1" },
		{ "BOB_FISHING_BMAJOR_V4_1", "BOB_FISHING_BMAJOR_V4_1" },
		{ "BOB_FISHING_BMAJOR_V5_1", "BOB_FISHING_BMAJOR_V5_1" },
		{ "BOB_FISHING_BMINOR_V1_1", "BOB_FISHING_BMINOR_V1_1" },
		{ "BOB_FISHING_BMINOR_V2_1", "BOB_FISHING_BMINOR_V2_1" },
		{ "BOB_FISHING_BMINOR_V3_1", "BOB_FISHING_BMINOR_V3_1" },
		{ "BOB_FISHING_CMAJOR_V1_1", "BOB_FISHING_CMAJOR_V1_1" },
		{ "BOB_FISHING_CMAJOR_V2_1", "BOB_FISHING_CMAJOR_V2_1" },
		{ "BOB_FISHING_CMAJOR_V4_1", "BOB_FISHING_CMAJOR_V4_1" },
		{ "BOB_FISHING_CMAJOR_V5_1", "BOB_FISHING_CMAJOR_V5_1" },
		{ "BOB_FISHING_CMINOR_V1_1", "BOB_FISHING_CMINOR_V1_1" },
		{ "BOB_FISHING_CMINOR_V4_1", "BOB_FISHING_CMINOR_V4_1" },
		{ "BOB_FISHING_CMINOR_V5_1", "BOB_FISHING_CMINOR_V5_1" },
		{ "BOB_FISHING_EMAJOR_V1_1", "BOB_FISHING_EMAJOR_V1_1" },
		{ "BOB_FISHING_EMAJOR_V2_1", "BOB_FISHING_EMAJOR_V2_1" },
		{ "BOB_FISHING_EMAJOR_V4_1", "BOB_FISHING_EMAJOR_V4_1" },
		{ "BOB_FISHING_EMAJOR_V5_1", "BOB_FISHING_EMAJOR_V5_1" },
		{ "BOB_FISHING_EMINOR_V1_1", "BOB_FISHING_EMINOR_V1_1" },
		{ "BOB_FISHING_EMINOR_V3_1", "BOB_FISHING_EMINOR_V3_1" },
		{ "BOB_FISHING_EMINOR_V4_1", "BOB_FISHING_EMINOR_V4_1" },
		{ "BOB_FISHING_EMINOR_V5_1", "BOB_FISHING_EMINOR_V5_1" },
		{ "BOB_FIVE_FINGER_BURNOUT", "BOB_FIVE_FINGER_BURNOUT" },
		{ "BOB_FIVE_FINGER_ONESHOT", "BOB_FIVE_FINGER_ONESHOT" },
		{ "BOB_FIVE_FINGER_SOLO", "BOB_FIVE_FINGER_SOLO" },
		{ "BOB_FUSSAR_2_OS1", "BOB_FUSSAR_2_OS1" },
		{ "BOB_FUSSAR_2_OS2", "BOB_FUSSAR_2_OS2" },
		{ "BOB_FUSSAR_2_OS3", "BOB_FUSSAR_2_OS3" },
		{ "BOB_FUSSAR_2_OS4", "BOB_FUSSAR_2_OS4" },
		{ "BOB_FUSSAR_2_OS5", "BOB_FUSSAR_2_OS5" },
		{ "BOB_FUSSAR_2_OS6", "BOB_FUSSAR_2_OS6" },
		{ "BOB_FUSSAR_2_OS7", "BOB_FUSSAR_2_OS7" },
		{ "BOB_FUSSAR_2_PT_1", "BOB_FUSSAR_2_PT_1" },
		{ "BOB_FUSSAR_2_PT_2", "BOB_FUSSAR_2_PT_2" },
		{ "BOB_GUARMA_1_CT", "BOB_GUARMA_1_CT" },
		{ "BOB_HEARTLANDS_1_CT", "BOB_HEARTLANDS_1_CT" },
		{ "BOB_HEARTLANDS_2_CT", "BOB_HEARTLANDS_2_CT" },
		{ "BOB_HEARTLANDS_3_CT", "BOB_HEARTLANDS_3_CT" },
		{ "BOB_HEARTLANDS_7_CT", "BOB_HEARTLANDS_7_CT" },
		{ "BOB_HERE_KITTY_1", "BOB_HERE_KITTY_1" },
		{ "BOB_HERE_KITTY_2", "BOB_HERE_KITTY_2" },
		{ "BOB_HERE_KITTY_3", "BOB_HERE_KITTY_3" },
		{ "BOB_HERE_KITTY_4", "BOB_HERE_KITTY_4" },
		{ "BOB_HERE_KITTY_4_ONESHOT_1_OS", "BOB_HERE_KITTY_4_ONESHOT_1_OS" },
		{ "BOB_HERE_KITTY_4_ONESHOT_2_OS", "BOB_HERE_KITTY_4_ONESHOT_2_OS" },
		{ "BOB_HERE_KITTY_5", "BOB_HERE_KITTY_5" },
		{ "BOB_HUNTING_01_BEAR2_OS", "BOB_HUNTING_01_BEAR2_OS" },
		{ "BOB_HUNTING_01_BEAR_OS", "BOB_HUNTING_01_BEAR_OS" },
		{ "BOB_HUNTING_1", "BOB_HUNTING_1" },
		{ "BOB_HUNTING_1_ONESHOT", "BOB_HUNTING_1_ONESHOT" },
		{ "BOB_LEMOYNE_1_CT", "BOB_LEMOYNE_1_CT" },
		{ "BOB_LEMOYNE_2_CT", "BOB_LEMOYNE_2_CT" },
		{ "BOB_LEMOYNE_3_CT", "BOB_LEMOYNE_3_CT" },
		{ "BOB_LEMOYNE_4_CT", "BOB_LEMOYNE_4_CT" },
		{ "BOB_LOONY_CULT_ONESHOT", "BOB_LOONY_CULT_ONESHOT" },
		{ "BOB_MARSTON_53", "BOB_MARSTON_53" },
		{ "BOB_MARSTON_53_OS1", "BOB_MARSTON_53_OS1" },
		{ "BOB_MARSTON_53_OS2", "BOB_MARSTON_53_OS2" },
		{ "BOB_MARY_3", "BOB_MARY_3" },
		{ "BOB_MARY_3_OS1", "BOB_MARY_3_OS1" },
		{ "BOB_MASON_1", "BOB_MASON_1" },
		{ "BOB_MASON_1_ONESHOT_1_OS", "BOB_MASON_1_ONESHOT_1_OS" },
		{ "BOB_MASON_2", "BOB_MASON_2" },
		{ "BOB_MASON_3", "BOB_MASON_3" },
		{ "BOB_MASON_3_ONESHOT_1_OS", "BOB_MASON_3_ONESHOT_1_OS" },
		{ "BOB_MASON_4", "BOB_MASON_4" },
		{ "BOB_MASON_5_ONESHOT_1", "BOB_MASON_5_ONESHOT_1" },
		{ "BOB_MUD1_SHENANDOAH", "BOB_MUD1_SHENANDOAH" },
		{ "BOB_POKER_STEM", "BOB_POKER_STEM" },
		{ "BOB_RC_CRACKPOT_1", "BOB_RC_CRACKPOT_1" },
		{ "BOB_RC_CRACKPOT_2_ONESHOT_1", "BOB_RC_CRACKPOT_2_ONESHOT_1" },
		{ "BOB_RC_CRACKPOT_2_ONESHOT_2", "BOB_RC_CRACKPOT_2_ONESHOT_2" },
		{ "BOB_RC_CRACKPOT_3_ONESHOT_1", "BOB_RC_CRACKPOT_3_ONESHOT_1" },
		{ "BOB_RC_OBROTHER_1", "BOB_RC_OBROTHER_1" },
		{ "BOB_RC_OBROTHER_2", "BOB_RC_OBROTHER_2" },
		{ "BOB_RC_OBROTHER_2_ONESHOT_1", "BOB_RC_OBROTHER_2_ONESHOT_1" },
		{ "BOB_RC_OBROTHER_2_ONESHOT_2", "BOB_RC_OBROTHER_2_ONESHOT_2" },
		{ "BOB_RC_OBROTHER_3", "BOB_RC_OBROTHER_3" },
		{ "BOB_RC_OBROTHER_3_ONESHOT_1", "BOB_RC_OBROTHER_3_ONESHOT_1" },
		{ "BOB_RC_OBROTHER_3_ONESHOT_2", "BOB_RC_OBROTHER_3_ONESHOT_2" },
		{ "BOB_RC_OBROTHER_3_ONESHOT_3", "BOB_RC_OBROTHER_3_ONESHOT_3" },
		{ "BOB_RC_OBROTHER_ONESHOT_1", "BOB_RC_OBROTHER_ONESHOT_1" },
		{ "BOB_RC_OBROTHER_ONESHOT_2", "BOB_RC_OBROTHER_ONESHOT_2" },
		{ "BOB_RE_DINO_BONES_ONESHOT_1", "BOB_RE_DINO_BONES_ONESHOT_1" },
		{ "BOB_RE_TREASURE_HUNTER", "BOB_RE_TREASURE_HUNTER" },
		{ "BOB_RE_TREASURE_HUNTER_ONESHOT_1", "BOB_RE_TREASURE_HUNTER_ONESHOT_1" },
		{ "BOB_ROANOKE_1_CT", "BOB_ROANOKE_1_CT" },
		{ "BOB_ROANOKE_5_CT", "BOB_ROANOKE_5_CT" },
		{ "BOB_ROANOKE_6_CT", "BOB_ROANOKE_6_CT" },
		{ "BOB_SMUGGLER_2", "BOB_SMUGGLER_2" },
		{ "BOB_SMUGGLER_2_ONESHOT_1", "BOB_SMUGGLER_2_ONESHOT_1" },
		{ "BOB_SMUGGLER_2_ONESHOT_2", "BOB_SMUGGLER_2_ONESHOT_2" },
		{ "BOB_SMUGGLER_2_ONESHOT_3", "BOB_SMUGGLER_2_ONESHOT_3" },
		{ "BOB_SMUGGLER_2_ONESHOT_4", "BOB_SMUGGLER_2_ONESHOT_4" },
		{ "BOB_SPIRIT_ANIMAL_HIGH", "BOB_SPIRIT_ANIMAL_HIGH" },
		{ "BOB_SPIRIT_ANIMAL_LOW", "BOB_SPIRIT_ANIMAL_LOW" },
		{ "BOB_STDENIS_2", "BOB_STDENIS_2" },
		{ "BOB_STDENIS_3_CT", "BOB_STDENIS_3_CT" },
		{ "BOB_STDENIS_5_CT", "BOB_STDENIS_5_CT" },
		{ "BOB_STDENIS_6_CT", "BOB_STDENIS_6_CT" },
		{ "BOB_SWAMP_2_CT", "BOB_SWAMP_2_CT" },
		{ "BOB_SWAMP_5_CT", "BOB_SWAMP_5_CT" },
		{ "BOB_SWAMP_6_CT", "BOB_SWAMP_6_CT" },
		{ "BOB_UTOPIA_1", "BOB_UTOPIA_1" },
		{ "BOB_UTOPIA_1_OS1", "BOB_UTOPIA_1_OS1" },
		{ "BOB_WARVET2_FISH_OS", "BOB_WARVET2_FISH_OS" },
		{ "BOB_WINTER_1_CT", "BOB_WINTER_1_CT" },
	};
	const int kTrackCount = (int)(sizeof(kTracks) / sizeof(kTracks[0]));
	const char* MUSIC_STOP = "MC_MUSIC_STOP";

	bool g_entered = false;       // ambience swap active
	bool g_musicPlaying = false;  // a track is currently triggered
	bool g_owmWasDisabled = false;

	bool g_cfgInit = false;       // pulled defaults from the INI yet?
	bool g_muteWorld = true;      // duck the live world audio
	bool g_musicEnabled = false;  // master "play a music bed" switch (off by default)
	int  g_trackIdx = 1;          // selected track (1 = first real bed)

	int FindTrackForEvent(const std::string& ev)
	{
		for (int i = 1; i < kTrackCount; i++) {
			if (ev == kTracks[i].event) return i;
		}
		return 1; // unknown configured event -> first real track
	}

	void InitFromConfigOnce()
	{
		if (g_cfgInit) return;
		g_cfgInit = true;
		g_muteWorld = g_Config.PhotoMuteWorld;
		g_musicEnabled = g_Config.PhotoMusicEnabled;
		// Remember the user's last track even when music is disabled, so toggling
		// it back on resumes their choice. Fall back to the first real bed.
		int t = (!g_Config.PhotoMusicEvent.empty()) ? FindTrackForEvent(g_Config.PhotoMusicEvent) : 1;
		g_trackIdx = (t >= 1 && t < kTrackCount) ? t : 1;
	}

	void StopMusic()
	{
		if (!g_musicPlaying) return;
		// Immediate stop: the universal trigger plus cancelling the specific event
		// so the bed does not linger on its long natural tail (the over-long fade
		// the user reported when switching tracks / leaving Photo Mode).
		AUDIO::TRIGGER_MUSIC_EVENT(MUSIC_STOP);
		if (g_trackIdx > 0 && g_trackIdx < kTrackCount && kTracks[g_trackIdx].event) {
			AUDIO::CANCEL_MUSIC_EVENT(kTracks[g_trackIdx].event);
		}
		g_musicPlaying = false;
	}

	void StartMusic()
	{
		if (!g_musicEnabled) return;
		if (g_trackIdx <= 0 || g_trackIdx >= kTrackCount) return;
		const char* ev = kTracks[g_trackIdx].event;
		if (!ev) return;
		AUDIO::PREPARE_MUSIC_EVENT(ev);
		g_musicPlaying = AUDIO::TRIGGER_MUSIC_EVENT(ev) != 0;
	}

	void ApplyMute()
	{
		g_owmWasDisabled = true;
		AUDIO::SET_AUDIO_FLAG("OpenWorldMusicDisabled", true);
		AUDIO::SET_PED_WALLA_DENSITY(0.0f, 0.0f);
		AUDIO::SET_PED_INTERIOR_WALLA_DENSITY(0.0f, 0.0f);
	}

	void ReleaseMute()
	{
		if (g_owmWasDisabled) {
			AUDIO::SET_AUDIO_FLAG("OpenWorldMusicDisabled", false);
			g_owmWasDisabled = false;
		}
		AUDIO::SET_PED_WALLA_DENSITY(1.0f, 1.0f);
		AUDIO::SET_PED_INTERIOR_WALLA_DENSITY(1.0f, 1.0f);
	}
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

	InitFromConfigOnce();
	PrepareAllSoundsets();

	if (g_muteWorld) ApplyMute();
	StartMusic();
}

void PhotoAudio::Tick()
{
	if (!g_entered) return;

	// Keep the UI soundsets loaded so navigation never falls silent over time.
	PrepareAllSoundsets();

	// Walla density resets itself every frame, so hold it down while open.
	if (g_muteWorld) {
		AUDIO::SET_PED_WALLA_DENSITY(0.0f, 0.0f);
		AUDIO::SET_PED_INTERIOR_WALLA_DENSITY(0.0f, 0.0f);
	}
}

void PhotoAudio::Exit()
{
	if (!g_entered) return;
	g_entered = false;

	StopMusic();
	ReleaseMute();
}

// --- music selection -------------------------------------------------------

bool PhotoAudio::MusicEnabled()
{
	InitFromConfigOnce();
	return g_musicEnabled;
}

void PhotoAudio::SetMusicEnabled(bool enabled)
{
	InitFromConfigOnce();
	if (g_musicEnabled == enabled) return;
	g_musicEnabled = enabled;
	g_Config.PhotoMusicEnabled = enabled;
	if (enabled) {
		if (g_trackIdx <= 0) g_trackIdx = 1; // pick the first bed if currently Off
		if (g_entered) StartMusic();
	}
	else {
		StopMusic();
	}
}

int PhotoAudio::TrackCount() { return kTrackCount; }

const char* PhotoAudio::TrackLabel(int idx)
{
	if (idx < 0 || idx >= kTrackCount) return "Off";
	return kTracks[idx].label;
}

int PhotoAudio::CurrentTrack()
{
	InitFromConfigOnce();
	return g_trackIdx;
}

void PhotoAudio::SelectTrack(int idx)
{
	InitFromConfigOnce();
	idx = ((idx % kTrackCount) + kTrackCount) % kTrackCount;

	// Switch live: stop the current bed FIRST (so its tail is cancelled), then
	// adopt the new selection and start it.
	StopMusic();
	g_trackIdx = idx;
	if (idx > 0 && idx < kTrackCount && kTracks[idx].event) {
		g_Config.PhotoMusicEvent = kTracks[idx].event;
	}
	// Picking a real track also implies the user wants music on.
	if (idx > 0) g_musicEnabled = true;
	g_Config.PhotoMusicEnabled = g_musicEnabled;
	if (g_entered) StartMusic();
}

bool PhotoAudio::MuteWorld()
{
	InitFromConfigOnce();
	return g_muteWorld;
}

void PhotoAudio::SetMuteWorld(bool mute)
{
	InitFromConfigOnce();
	if (g_muteWorld == mute) return;
	g_muteWorld = mute;
	if (g_entered) {
		if (mute) ApplyMute();
		else ReleaseMute();
	}
}
