// Director's Suite - Photo Mode world-space overlays.
//
// The Photo Mode interface is now the RDR2 Native Menu Base (see the page
// builders in PhotoMode.cpp). What remains here are the cheap in-world overlays
// that are part of the shot or composition: cinematic aspect-ratio crop bars,
// light gizmos and the rule-of-thirds grid. Each is a handful of DRAW_RECTs -
// well within the engine's per-frame script-draw budget that the old hand-drawn
// panel used to overrun.

#include "PhotoMode.h"
#include "EditorMath.h"
#include "ScreenCapture.h"
#include "SceneEditor.h"
#include "..\script.h"
#include "..\UI\Drawing.h"
#include "..\UI\UIUtil.h"
#include "..\UI\Menu.hpp"

// ---------------------------------------------------------------------------
// Cinematic aspect frames (crop bars drawn by the mod, so the user's real
// screen ratio is respected - unlike the engine letterbox, which is 16:9
// based and crops ultrawide displays)
// ---------------------------------------------------------------------------

struct PMAspectDef { const char* label; float ratio; };
static const PMAspectDef PMAspects[] = {
	{ "Off",                   0.0f },
	{ "Anamorphic 2.39:1",     2.39f },
	{ "Cinema 1.85:1",         1.85f },
	{ "Super Widescreen 21:9", 21.0f / 9.0f },
	{ "Widescreen 16:9",       16.0f / 9.0f },
	{ "Classic TV 4:3",        4.0f / 3.0f },
	{ "Square 1:1",            1.0f },
	{ "Portrait 4:5",          0.8f },
	{ "Phone 9:16",            9.0f / 16.0f },
};

extern const int PMAspectCount = (int)(sizeof(PMAspects) / sizeof(PMAspects[0]));
extern const char* PMAspectName(int idx)
{
	if (idx < 0 || idx >= PMAspectCount) return "Off";
	return PMAspects[idx].label;
}

void CPhotoMode::DrawAspectFrame()
{
	if (m_aspectIdx <= 0 || m_aspectIdx >= PMAspectCount) return;

	float target = PMAspects[m_aspectIdx].ratio;

	// Real viewport ratio: game window if known, desktop as fallback
	float screen = (UIUtil::g_screenWidth > 0 && UIUtil::g_screenHeight > 0)
		? (float)UIUtil::g_screenWidth / (float)UIUtil::g_screenHeight
		: (SCREEN_HEIGHT > 0.0f ? SCREEN_WIDTH / SCREEN_HEIGHT : 16.0f / 9.0f);

	// Bars are part of the shot: full alpha, not faded with the UI
	if (target < screen) {
		// pillarbox: vertical bars left/right
		float frac = (1.0f - target / screen) * 0.5f;
		GRAPHICS::DRAW_RECT(frac * 0.5f, 0.5f, frac, 1.0f, 0, 0, 0, 255, false, false);
		GRAPHICS::DRAW_RECT(1.0f - frac * 0.5f, 0.5f, frac, 1.0f, 0, 0, 0, 255, false, false);
	}
	else if (target > screen) {
		// letterbox: horizontal bars top/bottom
		float frac = (1.0f - screen / target) * 0.5f;
		GRAPHICS::DRAW_RECT(0.5f, frac * 0.5f, 1.0f, frac, 0, 0, 0, 255, false, false);
		GRAPHICS::DRAW_RECT(0.5f, 1.0f - frac * 0.5f, 1.0f, frac, 0, 0, 0, 255, false, false);
	}
}

// ---------------------------------------------------------------------------
// Light gizmo. The placed light props are physical models, so they show their
// own position in-world; this overlay is the editing aid: a selection bracket +
// label over the SELECTED light prop, tinted with its colour, so you can tell
// which one the editor is targeting. Hidden with the UI / during capture
// (handled by the caller).
// ---------------------------------------------------------------------------

void CPhotoMode::DrawLightGizmos()
{
	if (m_artSel < 0 || m_artSel >= PM_MAX_ARTLIGHTS) return;
	const PMArtLight& a = m_artLights[m_artSel];
	if (!a.used) return;

	float sx = 0.0f, sy = 0.0f;
	if (!GRAPHICS::GET_SCREEN_COORD_FROM_WORLD_COORD(a.pos.x, a.pos.y, a.pos.z + 0.1f, &sx, &sy)) return;

	// hollow selection bracket tinted with the light's own colour
	GRAPHICS::DRAW_RECT(sx, sy - 0.018f, 0.020f, 0.0028f, a.r, a.g, a.b, 235, false, false);
	GRAPHICS::DRAW_RECT(sx, sy + 0.018f, 0.020f, 0.0028f, a.r, a.g, a.b, 235, false, false);
	GRAPHICS::DRAW_RECT(sx - 0.011f, sy, 0.0028f, 0.036f, a.r, a.g, a.b, 235, false, false);
	GRAPHICS::DRAW_RECT(sx + 0.011f, sy, 0.0028f, 0.036f, a.r, a.g, a.b, 235, false, false);

	std::string label = "LIGHT " + std::to_string(m_artSel + 1);
	Drawing::DrawFormattedText(label, Font::Body, 255, 255, 255, 220, Alignment::Center, 16,
		sx * SCREEN_WIDTH, (sy - 0.045f) * SCREEN_HEIGHT);
}

// ---------------------------------------------------------------------------
// Control hint bar. RDR2's native UI prompts get wiped by the photo-mode HUD
// hide (and only flash for a frame on unpause), so we draw our own tip bar - a
// dark strip across the bottom with the key controls. Fully under our control,
// always visible while editing, and hidden during a clean shot by the caller.
// ---------------------------------------------------------------------------

void CPhotoMode::DrawControlHints()
{
	// dark backing strip
	GRAPHICS::DRAW_RECT(0.5f, 0.962f, 1.0f, 0.060f, 0, 0, 0, 160, false, false);

	const char* line1 = "Arrows: Navigate / Adjust      Enter: Select      Backspace: Back";
	const char* line2 = "WASD: Move    Space/Z: Up/Down    Mouse: Look    Shift: Fast    Q/E: Roll    H: Hide UI    F3: Photo";

	Drawing::DrawFormattedText(line1, Font::Body, 245, 245, 245, 230, Alignment::Center, 17,
		SCREEN_WIDTH * 0.5f, SCREEN_HEIGHT * 0.945f);
	Drawing::DrawFormattedText(line2, Font::Body, 210, 210, 210, 220, Alignment::Center, 16,
		SCREEN_WIDTH * 0.5f, SCREEN_HEIGHT * 0.967f);
}

// ---------------------------------------------------------------------------
// Composition grid (rule of thirds)
// ---------------------------------------------------------------------------

static void PMDrawGrid()
{
	const float thickness = 0.0015f;
	for (int c = 1; c < 3; c++) {
		GRAPHICS::DRAW_RECT((float)c / 3.0f, 0.5f, thickness, 1.0f, 255, 255, 255, 90, false, false);
	}
	for (int r = 1; r < 3; r++) {
		GRAPHICS::DRAW_RECT(0.5f, (float)r / 3.0f, 1.0f, thickness * (SCREEN_WIDTH / SCREEN_HEIGHT), 255, 255, 255, 90, false, false);
	}
}

// ---------------------------------------------------------------------------
// Scene Editor aiming crosshair. The "Hide Object Under Crosshair" / "Force Map
// Object" actions act on the centre of the view, but Photo Mode hides the HUD,
// so we draw our own reticle at screen centre while the Map Edits page is open.
// Aspect-corrected so the cross reads square; dark outline keeps it readable on
// any background.
// ---------------------------------------------------------------------------

static void PMDrawCrosshair()
{
	const float aspect = SCREEN_WIDTH / SCREEN_HEIGHT;
	const float lenX = 0.020f, thickX = 0.0016f;        // horizontal arm length / vertical arm thickness (x units)
	const float lenY = lenX * aspect, thickY = thickX * aspect; // matching y units

	// dark outline
	GRAPHICS::DRAW_RECT(0.5f, 0.5f, lenX + 0.0014f, thickY + 0.0020f, 0, 0, 0, 150, false, false);
	GRAPHICS::DRAW_RECT(0.5f, 0.5f, thickX + 0.0014f, lenY + 0.0020f, 0, 0, 0, 150, false, false);
	// white cross
	GRAPHICS::DRAW_RECT(0.5f, 0.5f, lenX, thickY, 245, 245, 245, 230, false, false);
	GRAPHICS::DRAW_RECT(0.5f, 0.5f, thickX, lenY, 245, 245, 245, 230, false, false);
	// centre dot (the exact point the actions hit) tinted with the gizmo accent
	GRAPHICS::DRAW_RECT(0.5f, 0.5f, thickX, thickY, 80, 200, 255, 255, false, false);
}

// ---------------------------------------------------------------------------
// Sun-position compass (Lighting tab). A top-down sky map: the ring is the
// horizon, the centre is straight overhead (zenith). The sun marker's angle is
// its azimuth (compass bearing) and its distance from the centre is its
// elevation (centre = overhead, ring = horizon). Lets the user see exactly
// where the sun sits while dragging Sun Azimuth / Sun Elevation.
// ---------------------------------------------------------------------------

void CPhotoMode::DrawSunCompass()
{
	const float aspect = SCREEN_WIDTH / SCREEN_HEIGHT;
	const float boxCx = 0.885f, boxCy = 0.235f, boxW = 0.200f, boxH = 0.350f;
	const float boxTop = boxCy - boxH * 0.5f;
	const float cx = 0.885f, cy = 0.245f, R = 0.108f, lo = 0.022f;

	// square dot helper (width corrected so it reads square on a wide screen)
	auto dot = [aspect](float nx, float ny, float sizeY, int r, int g, int b, int a) {
		GRAPHICS::DRAW_RECT(nx, ny, sizeY / aspect, sizeY, r, g, b, a, false, false);
	};

	GRAPHICS::DRAW_RECT(boxCx, boxCy, boxW, boxH, 0, 0, 0, 185, false, false);
	GRAPHICS::DRAW_RECT(boxCx, boxTop + 0.004f, boxW, 0.0040f, 255, 200, 90, 235, false, false);
	Drawing::DrawFormattedText("SUN POSITION", Font::Body, 255, 200, 90, 240,
		Alignment::Center, 14, SCREEN_WIDTH * boxCx, SCREEN_HEIGHT * (boxTop + 0.020f));

	// horizon ring + zenith centre
	for (int k = 0; k < 36; k++) {
		float a = k * 10.0f * EMath::DEG2RAD;
		dot(cx + sinf(a) * R / aspect, cy - cosf(a) * R, 0.0055f, 140, 140, 145, 210);
	}
	dot(cx, cy, 0.0060f, 120, 120, 125, 210);

	// cardinal labels (azimuth: 0 = N, 90 = E ...)
	Drawing::DrawFormattedText("N", Font::Body, 230, 230, 230, 235, Alignment::Center, 13, SCREEN_WIDTH * cx, SCREEN_HEIGHT * (cy - R - lo));
	Drawing::DrawFormattedText("S", Font::Body, 230, 230, 230, 235, Alignment::Center, 13, SCREEN_WIDTH * cx, SCREEN_HEIGHT * (cy + R + lo * 0.3f));
	Drawing::DrawFormattedText("E", Font::Body, 230, 230, 230, 235, Alignment::Center, 13, SCREEN_WIDTH * (cx + (R + lo) / aspect), SCREEN_HEIGHT * (cy - 0.012f));
	Drawing::DrawFormattedText("W", Font::Body, 230, 230, 230, 235, Alignment::Center, 13, SCREEN_WIDTH * (cx - (R + lo) / aspect), SCREEN_HEIGHT * (cy - 0.012f));

	// sun marker: angle = azimuth, radius shrinks toward the centre as elevation rises
	float el = m_sunElevation; if (el > 90.0f) el = 90.0f; if (el < -90.0f) el = -90.0f;
	float elR = (el < 0.0f) ? 0.0f : el;
	float r = R * (1.0f - elR / 90.0f);
	float a = m_sunAzimuth * EMath::DEG2RAD;
	float sx = cx + sinf(a) * r / aspect;
	float sy = cy - cosf(a) * r;
	bool below = (el < 0.0f);
	int sr = below ? 210 : 255, sg = below ? 110 : 225, sb = below ? 60 : 70;

	for (int i = 1; i <= 4; i++) { // direction trail from centre to sun
		float t = i / 5.0f;
		dot(cx + sinf(a) * r * t / aspect, cy - cosf(a) * r * t, 0.0040f, sr, sg, sb, 150);
	}
	dot(sx, sy, 0.0220f, sr, sg, sb, 90);   // glow
	dot(sx, sy, 0.0130f, sr, sg, sb, 255);  // core

	// readout
	int az = (int)(m_sunAzimuth + 0.5f) % 360; if (az < 0) az += 360;
	int eli = (int)(m_sunElevation >= 0.0f ? m_sunElevation + 0.5f : m_sunElevation - 0.5f);
	std::string read = "Dir " + std::to_string(az) + "   Height " + std::to_string(eli);
	if (below) read += "  (below)";
	Drawing::DrawFormattedText(read, Font::Body, below ? 255 : 235, below ? 180 : 235, below ? 120 : 235, 240,
		Alignment::Center, 13, SCREEN_WIDTH * cx, SCREEN_HEIGHT * (cy + R + lo + 0.020f));
}

// ---------------------------------------------------------------------------
// Overlay entry point (called every frame from CPhotoMode::Tick)
// ---------------------------------------------------------------------------

void CPhotoMode::DrawOverlays()
{
	// Aspect crop bars are part of the composition, not editor chrome - they
	// stay up when the UI is hidden and during a screenshot grab.
	DrawAspectFrame();

	// Grid, gizmos and control hints are editor aids: hidden with the UI and
	// during capture.
	if (m_uiHidden || ScreenCapture::IsCapturing()) return;

	if (m_grid) PMDrawGrid();
	if (m_gizmos) DrawLightGizmos();

	// Scene Editor selection gizmo - only while a Scene Editor page is open, so
	// the bounding box never appears during normal Photo Mode framing.
	if (Submenu* cur = g_Menu->GetCurrentSubmenu()) {
		if (cur->ID >= Submenu_PhotoMode_Scene && cur->ID <= Submenu_PhotoMode_Scene_World)
			g_SceneEditor.DrawGizmo();
		// Aiming reticle for the centre-of-view Map Edit actions (hide / force).
		if (cur->ID == Submenu_PhotoMode_Scene_World)
			PMDrawCrosshair();
		// Sun-position sky map while adjusting the sun on the Lighting tab.
		if (cur->ID == Submenu_PhotoMode_Lighting && m_sunVarX >= 0 && m_sunVarY >= 0)
			DrawSunCompass();
	}

	DrawControlHints();

	// Script-drawn notifications (native subtitles don't show while frozen).
	UIUtil::Notify::Draw();
}
