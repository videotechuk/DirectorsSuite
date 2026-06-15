# Director's Suite

**Direct and edit video content in Red Dead Redemption 2.**

Red Dead Redemption 2 ships with no Rockstar Editor and no real video tools. Director's
Suite rebuilds the concept on top of what the engine *does* expose — scripted cameras,
the Photo Mode natives, the timecycle, weather and clock systems — into a single,
creator-focused suite that feels closer to a lightweight film-production editor than a
debug menu. Built on [ScriptHookRDR2](http://www.dev-c.com/rdr2/scripthookrdr2/) and the
[RDR2 Native Menu Base](https://github.com/Halen84/RDR2-Native-Menu-Base) (MIT).

## Features

- **Photo Mode** — a full capture suite with tabs for Camera, World, Character, Lighting,
  Post, Effects, Scene Editor, Music and Settings. Free-fly/orbit camera, real DOF, time
  & weather control, color grades, all 38 photo filters, lossless/HDR screenshots and an
  experimental tiled **super-resolution capture**.
- **Scene Editor (Beta)** — place props by X/Y/Z (catalogue of ~16,950 archetypes), spawn
  and direct actors (~1,980 ped models) with **validated scenarios** (humans only see
  human scenarios; animals see species-matched ones), and make temporary, reverted-on-exit
  YMAP edits (hide / swap / force world props).
- **Lighting** — scene lights positioned by X/Y/Z, the sun aimed with simple Direction /
  Height controls (plus an on-screen sun-position compass), the authentic Rockstar light
  rig and a 3-point hero rig.
- **Director Mode** — spawn and direct NPCs/animals: properties, scenarios, tasks, hero
  lighting and world controls.
- **Camera System** — up to 300 cameras with modes, transitions, presets, full-project
  playback and one-click OBS recording (obs-websocket 5.x).
- Cinematic free-cam exploration, aim modes / aim assist, gameplay options and an in-game
  help section.

All catalogue data (objects, peds, scenarios) is generated into headers under
`src/DirectorsSuite/Editor/Data/` and compiled into the ASI — no external data files ship.

## Building

Open `src/DirectorsSuite.sln` in **Visual Studio 2022** (C++ latest) and build
**Release | x64**. Output: `src/bin/DirectorsSuite.asi`.

The ScriptHookRDR2 SDK headers/lib used for linking live in `inc/` and `lib/`
(© Alexander Blade).

## Installation

1. Install ScriptHookRDR2 and an ASI loader.
2. Copy `DirectorsSuite.asi` and `DirectorsSuite.ini` next to `RDR2.exe`.
3. *(Optional)* Copy `Icons/` to `<game>\DirectorsSuite_Icons` for the Photo Mode tab icons.

Default keys (rebindable in `DirectorsSuite.ini`): **F2** menu · **F1** Photo Mode ·
**F4** placement camera · **INSERT** insert camera · **F3** screenshot · **N/B** next/prev
camera · **C** (hold while aiming) aim assist · **H** hide UI for a clean shot.

## Credits

videotech (creator) · Alexander Blade (ScriptHookRDR2 SDK) · Disquse & Cfx.re (timecycle
editor) · Halen84 (script research & native menu base) · kepmehz (script research) ·
femga (game research) · TheNathanNS (QA) · WesternSpace (ultra-wide black-bars patch) ·
Claude (research, code, bug fixes).
