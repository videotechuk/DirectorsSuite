// Director's Suite - runtime PNG icon loader for the Photo Mode tab bar.
//
// ScriptHookRDR2 exposes no texture-creation API (unlike GTA V's ScriptHook),
// so PNGs can't become engine textures without touching game assets. Instead
// the icons are decoded at runtime with Windows GDI+ (ships with Windows),
// max-pooled into a small intensity grid and pre-meshed into a handful of
// rectangles, which the UI draws through DRAW_RECT. The engine drops script
// draws beyond a per-frame budget, so the mesh step matters: a whole icon
// costs ~15-40 rects instead of hundreds of per-cell runs.
//
// Files: <RDR2.exe folder>\DirectorsSuite_Icons\{Camera,World,Character,
// Lighting,Post,Effects}.png  (any size; alpha channel used as the shape).
// Missing files fall back to the built-in pixel glyphs.

#pragma once
#include <vector>

// Quantization grid the PNG icons are meshed onto. Kept at 20: pushing it to
// 32 multiplied the per-frame DRAW_RECT count across all six icons and, with
// the rest of the panel, overran the engine's script-draw budget - the engine
// then silently dropped the last-drawn elements (rightmost icon, toggle knobs).
constexpr int PMICON_GRID = 20;

// One pre-meshed icon rectangle in grid cells; level 1 = soft edge, 2 = solid
struct PMIconRect
{
	unsigned char x, y, w, h, level;
};

struct PMIconGrid
{
	bool loaded = false;
	std::vector<PMIconRect> rects;
};

namespace PMIcons
{
	// Lazily loads all six icons on first call. Returns null when the PNG
	// for this tab is unavailable (caller falls back to built-in glyphs).
	const PMIconGrid* Get(int tab);
}
