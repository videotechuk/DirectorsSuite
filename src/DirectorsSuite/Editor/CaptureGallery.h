// Director's Suite - in-game captures viewer.
//
// Displays the saved screenshots (the PNG/JPEG-XR files ScreenCapture writes to
// "Captured Screenshots") on top of the running game so the user can review
// their shots without alt-tabbing.
//
// Why an overlay window and not the game's renderer:
//   ScriptHookRDR2 exposes no swapchain and the RDR2 native set has no
//   runtime-texture-from-file path (DRAW_SPRITE only draws packaged .ytd
//   dictionaries), so a saved PNG cannot be handed to the engine to draw. RDR2
//   also ships on DX12 or Vulkan, so hooking the game's Present would be
//   backend-specific and fragile. Instead we own a separate top-most, layered,
//   click-through window and composite the decoded image over the game with
//   UpdateLayeredWindow - backend-independent and it never touches the game's
//   renderer (matching ScreenCapture, which uses Desktop Duplication for the
//   same reason). Requires the game in Borderless/Windowed (a layered window
//   cannot draw over exclusive fullscreen); the capture path assumes the same.
//
// The window lives on its own worker thread (creation, message pump and the
// WIC decode all run there, so the script thread never blocks on a large
// image). The script thread only issues commands (open/close/next/prev) and
// reads cheap snapshots (count / current name) for the menu page.

#pragma once
#include <string>

namespace CaptureGallery
{
	// Stand up / tear down the worker thread and overlay window. Init is
	// idempotent; Shutdown hides the window, joins the thread and frees GDI.
	void Init();
	void Shutdown();

	// Rescan the captures folder (newest first) and clamp the selection. Cheap;
	// safe to call on entry to the Captures page.
	void Refresh();

	// Show / hide the overlay. Open() rescans first and starts in the thumbnail
	// grid; it is a no-op (returns false) when there is nothing to show.
	bool Open();
	void Close();
	bool IsOpen();

	// True while looking around a 360 panorama in the single view - the caller
	// drives continuous (held-key) panning instead of discrete navigation.
	bool IsLooking360();

	// Navigation intents. Meaning depends on the current view (grid vs single
	// image) and is resolved on the worker thread:
	//   Grid   - arrows move the highlight, Activate opens the selected shot,
	//            Back closes the gallery, CycleFilter steps the date filter.
	//   Single - Left/Right browse the filtered set, Up/Down ignored, Activate
	//            and Back return to the grid.
	// All are no-ops when the overlay is closed.
	void NavLeft();
	void NavRight();
	void NavUp();
	void NavDown();
	void Activate();
	void Back();
	void CycleFilter();

	// Cheap snapshots for the menu page.
	int  Count();
	std::string CurrentName(); // file name only, or "" when empty

	// Open the captures folder in Windows Explorer (script thread is fine).
	void OpenFolderInExplorer();
}
