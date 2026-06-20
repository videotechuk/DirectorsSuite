#include "CaptureGallery.h"
#include "ScreenCapture.h"   // OutputDir()
#include "HdrTonemap.h"
#include <windows.h>
#include <wincodec.h>
#include <shellapi.h>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cmath>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shell32.lib")

// ---------------------------------------------------------------------------
// The overlay is a top-most, layered, click-through window we own. The script
// thread queues intents (open / nav / activate / filter); the worker thread
// decodes images (WIC) and composites a thumbnail grid or a single full-screen
// image over the game with UpdateLayeredWindow. See CaptureGallery.h for why
// this is an OS overlay and not a draw into the game's own renderer.
// ---------------------------------------------------------------------------

namespace
{
	template<typename T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

	const wchar_t* kClassName = L"DSuiteCaptureOverlay";

	enum Mode { GRID = 0, SINGLE = 1 };
	enum Cmd  { C_LEFT, C_RIGHT, C_UP, C_DOWN, C_ACTIVATE, C_BACK, C_FILTER };

	struct FileEntry { std::string path; std::string name; std::string date; }; // date = "YYYY-MM-DD"

	struct State
	{
		CRITICAL_SECTION       cs;
		std::vector<FileEntry> files;     // all captures, newest first
		std::vector<std::string> dates;   // distinct dates, newest first
		int   filterIdx = -1;             // -1 = all dates, else index into dates
		std::vector<int> filtered;        // indices into files passing the filter
		int   sel = 0;                    // index into filtered
		int   mode = GRID;
		bool  visible = false;
		std::vector<int> cmds;            // queued intents, drained by the worker
		// 360 panorama look-around (single view of an equirectangular image)
		bool  look360 = false;            // panning the current 360 image
		bool  cur360 = false;             // current single image is equirectangular (set by worker)
		float yaw360 = 0.0f, pitch360 = 0.0f, fov360 = 90.0f;
	};
	State s;

	std::thread       g_thread;
	std::atomic<bool> g_running{ false };
	std::atomic<bool> g_inited{ false };
	HANDLE            g_wake = nullptr;   // auto-reset: signalled on any command
	HWND              g_hwnd = nullptr;
	bool              g_shown = false;

	// GDI surface, recreated on size change (worker thread only).
	HDC      g_memDC = nullptr;
	HBITMAP  g_dib = nullptr;
	void*    g_bits = nullptr;
	int      g_surfW = 0, g_surfH = 0;

	IWICImagingFactory* g_wic = nullptr;
	HWND                g_gameWnd = nullptr;

	// Full-image cache for the single view (worker thread only).
	std::string       g_fullPath;
	UINT              g_fullBoxW = 0, g_fullBoxH = 0;
	std::vector<BYTE> g_fullImg;
	UINT              g_fullW = 0, g_fullH = 0;
	bool              g_fullOk = false;

	// Hi-res equirectangular source for the 360 look-around view (worker only).
	std::string       g_panoPath;
	std::vector<BYTE> g_pano;
	UINT              g_panoW = 0, g_panoH = 0;
	bool              g_panoOk = false;

	// Thumbnail cache for the grid (worker thread only), keyed by file path.
	struct Thumb { std::vector<BYTE> px; UINT w = 0, h = 0, boxW = 0, boxH = 0; bool ok = false; };
	std::map<std::string, Thumb> g_thumbs;

	// A 2:1 image is treated as a 360 equirectangular panorama.
	bool IsEquirect(UINT w, UINT h)
	{
		return h > 0 && fabs((double)w / (double)h - 2.0) < 0.06;
	}

	const COLORREF kAccent = RGB(255, 200, 90);

	std::wstring Widen(const std::string& utf8)
	{
		int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
		std::wstring w(n ? n - 1 : 0, L'\0');
		if (n) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], n);
		return w;
	}

	bool HasImageExt(const char* name)
	{
		const char* dot = strrchr(name, '.');
		if (!dot) return false;
		char ext[16]; int i = 0;
		for (const char* p = dot; *p && i < 15; ++p) ext[i++] = (char)tolower(*p);
		ext[i] = 0;
		return !strcmp(ext, ".png") || !strcmp(ext, ".jpg") || !strcmp(ext, ".jpeg")
			|| !strcmp(ext, ".jxr") || !strcmp(ext, ".bmp");
	}

	std::string DateKeyOf(const FILETIME& ft)
	{
		FILETIME lf{}; SYSTEMTIME st{};
		FileTimeToLocalFileTime(&ft, &lf);
		FileTimeToSystemTime(&lf, &st);
		char b[16]; sprintf_s(b, "%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
		return b;
	}

	std::string BaseName(const std::string& full)
	{
		size_t slash = full.find_last_of("\\/");
		return slash == std::string::npos ? full : full.substr(slash + 1);
	}

	// --- the game's own top-level window (we are injected into its process) ---
	BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lp)
	{
		DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
		if (pid != GetCurrentProcessId()) return TRUE;
		if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
		RECT r; if (!GetClientRect(hwnd, &r)) return TRUE;
		if (r.right - r.left < 320 || r.bottom - r.top < 240) return TRUE;
		*reinterpret_cast<HWND*>(lp) = hwnd;
		return FALSE;
	}

	HWND FindGameWindow()
	{
		if (g_gameWnd && IsWindow(g_gameWnd)) return g_gameWnd;
		HWND found = nullptr;
		EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&found));
		g_gameWnd = found;
		return found;
	}

	RECT TargetRect()
	{
		HWND gw = FindGameWindow();
		RECT out{ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
		if (gw) {
			RECT cr; POINT tl{ 0, 0 };
			if (GetClientRect(gw, &cr) && ClientToScreen(gw, &tl)) {
				out.left = tl.x; out.top = tl.y;
				out.right = tl.x + (cr.right - cr.left);
				out.bottom = tl.y + (cr.bottom - cr.top);
			}
		}
		return out;
	}

	// --- GDI surface -------------------------------------------------------
	void DestroySurface()
	{
		if (g_memDC) { DeleteDC(g_memDC); g_memDC = nullptr; }
		if (g_dib) { DeleteObject(g_dib); g_dib = nullptr; }
		g_bits = nullptr; g_surfW = g_surfH = 0;
	}

	bool EnsureSurface(int w, int h)
	{
		if (g_dib && w == g_surfW && h == g_surfH) return true;
		DestroySurface();
		HDC screen = GetDC(nullptr);
		g_memDC = CreateCompatibleDC(screen);
		ReleaseDC(nullptr, screen);
		if (!g_memDC) return false;

		BITMAPINFO bi{};
		bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bi.bmiHeader.biWidth = w;
		bi.bmiHeader.biHeight = -h; // top-down
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;
		g_dib = CreateDIBSection(g_memDC, &bi, DIB_RGB_COLORS, &g_bits, nullptr, 0);
		if (!g_dib) { DeleteDC(g_memDC); g_memDC = nullptr; return false; }
		SelectObject(g_memDC, g_dib);
		g_surfW = w; g_surfH = h;
		return true;
	}

	// --- raw BGRA helpers (premultiplied) ----------------------------------
	void FillRect32(int x, int y, int w, int h, BYTE b, BYTE g, BYTE r, BYTE a)
	{
		if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; }
		if (x + w > g_surfW) w = g_surfW - x; if (y + h > g_surfH) h = g_surfH - y;
		if (w <= 0 || h <= 0) return;
		BYTE* p = static_cast<BYTE*>(g_bits);
		for (int yy = y; yy < y + h; ++yy) {
			BYTE* row = p + ((size_t)yy * g_surfW + x) * 4;
			for (int xx = 0; xx < w; ++xx) { row[0] = b; row[1] = g; row[2] = r; row[3] = a; row += 4; }
		}
	}

	void StampAlpha(int x, int y, int w, int h, BYTE a)
	{
		if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; }
		if (x + w > g_surfW) w = g_surfW - x; if (y + h > g_surfH) h = g_surfH - y;
		if (w <= 0 || h <= 0) return;
		BYTE* p = static_cast<BYTE*>(g_bits);
		for (int yy = y; yy < y + h; ++yy) {
			BYTE* row = p + ((size_t)yy * g_surfW + x) * 4 + 3;
			for (int xx = 0; xx < w; ++xx) { *row = a; row += 4; }
		}
	}

	void ComposeBackdrop(int w, int h) { FillRect32(0, 0, w, h, 0, 0, 0, 214); }

	// Draw centered text inside a rect. GDI leaves alpha untouched, so the caller
	// places this over an opaque fill and re-stamps alpha afterwards.
	void DrawTextIn(int x, int y, int w, int h, const std::string& text, int fontPx, COLORREF col, UINT fmt)
	{
		HFONT font = CreateFontA(-fontPx, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
			DEFAULT_PITCH | FF_SWISS, "Segoe UI");
		HGDIOBJ old = SelectObject(g_memDC, font);
		SetBkMode(g_memDC, TRANSPARENT);
		SetTextColor(g_memDC, col);
		RECT rc{ x, y, x + w, y + h };
		DrawTextA(g_memDC, text.c_str(), -1, &rc, fmt);
		GdiFlush();
		SelectObject(g_memDC, old);
		DeleteObject(font);
	}

	// Opaque chip with centered (ellipsised) text - used for the header bar,
	// footer hint and per-thumbnail filename labels.
	void DrawChip(int x, int y, int w, int h, const std::string& text, int fontPx,
		BYTE bg = 18, BYTE bgA = 235, UINT extraFmt = 0)
	{
		FillRect32(x, y, w, h, bg, bg, bg, bgA);
		DrawTextIn(x, y, w, h, text, fontPx, RGB(238, 238, 238),
			DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS | extraFmt);
		StampAlpha(x, y, w, h, 255);
	}

	void DrawBorder(int x, int y, int w, int h, int t, COLORREF c)
	{
		BYTE r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
		FillRect32(x, y, w, t, b, g, r, 255);
		FillRect32(x, y + h - t, w, t, b, g, r, 255);
		FillRect32(x, y, t, h, b, g, r, 255);
		FillRect32(x + w - t, y, t, h, b, g, r, 255);
	}

	bool IsHdr(const std::string& name)
	{
		size_t n = name.size();
		return n >= 4 && _stricmp(name.c_str() + n - 4, ".jxr") == 0;
	}

	// HSV (h in [0,360), s,v in [0,1]) -> RGB bytes.
	void HsvToRgb(float h, float s, float v, BYTE& R, BYTE& G, BYTE& B)
	{
		float c = v * s;
		float hp = h / 60.0f;
		float xx = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
		float r = 0, g = 0, b = 0;
		if (hp < 1) { r = c; g = xx; }
		else if (hp < 2) { r = xx; g = c; }
		else if (hp < 3) { g = c; b = xx; }
		else if (hp < 4) { g = xx; b = c; }
		else if (hp < 5) { r = xx; b = c; }
		else { r = c; b = xx; }
		float m = v - c;
		R = (BYTE)((r + m) * 255.0f + 0.5f);
		G = (BYTE)((g + m) * 255.0f + 0.5f);
		B = (BYTE)((b + m) * 255.0f + 0.5f);
	}

	// Badge with BOLD text filled by a horizontal rainbow gradient (e.g. "HDR").
	// GDI can't gradient-fill glyphs, so we draw the text white (grayscale AA) on
	// a dark chip, then recolour each pixel by its coverage with a hue ramp.
	void DrawBadge(int x, int y, int w, int h, const std::string& text, int fontPx)
	{
		const BYTE bg = 16;
		FillRect32(x, y, w, h, bg, bg, bg, 255);

		HFONT font = CreateFontA(-fontPx, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
			DEFAULT_PITCH | FF_SWISS, "Segoe UI");
		HGDIOBJ old = SelectObject(g_memDC, font);
		SetBkMode(g_memDC, TRANSPARENT);
		SetTextColor(g_memDC, RGB(255, 255, 255));
		RECT rc{ x, y, x + w, y + h };
		DrawTextA(g_memDC, text.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		GdiFlush();
		SelectObject(g_memDC, old);
		DeleteObject(font);

		BYTE* p = static_cast<BYTE*>(g_bits);
		float denom = (float)((w > 1) ? (w - 1) : 1);
		for (int yy = y; yy < y + h; ++yy) {
			if (yy < 0 || yy >= g_surfH) continue;
			for (int xx = x; xx < x + w; ++xx) {
				if (xx < 0 || xx >= g_surfW) continue;
				BYTE* px = p + ((size_t)yy * g_surfW + xx) * 4;
				int lum = (px[0] + px[1] + px[2]) / 3;   // dark bg ~16, white glyph up to 255
				int cov = lum - bg; if (cov <= 0) continue;
				float coverage = (float)cov / (float)(255 - bg); if (coverage > 1.0f) coverage = 1.0f;
				BYTE R, G, B; HsvToRgb(((float)(xx - x) / denom) * 300.0f, 1.0f, 1.0f, R, G, B);
				px[0] = (BYTE)(bg * (1.0f - coverage) + B * coverage); // B
				px[1] = (BYTE)(bg * (1.0f - coverage) + G * coverage); // G
				px[2] = (BYTE)(bg * (1.0f - coverage) + R * coverage); // R
				px[3] = 255;
			}
		}
		DrawBorder(x, y, w, h, 1, RGB(0, 0, 0));
	}

	bool DecodeScaled(const std::wstring& path, UINT boxW, UINT boxH,
		std::vector<BYTE>& out, UINT& outW, UINT& outH)
	{
		if (!g_wic || boxW == 0 || boxH == 0) return false;
		IWICBitmapDecoder*     dec = nullptr;
		IWICBitmapFrameDecode* frm = nullptr;
		IWICBitmapScaler*      scl = nullptr;
		IWICFormatConverter*   cvt = nullptr;
		bool ok = false;
		do {
			if (FAILED(g_wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
				WICDecodeMetadataCacheOnDemand, &dec))) break;
			if (FAILED(dec->GetFrame(0, &frm))) break;
			UINT iw = 0, ih = 0;
			if (FAILED(frm->GetSize(&iw, &ih)) || iw == 0 || ih == 0) break;

			double sx = (double)boxW / iw, sy = (double)boxH / ih;
			double sc = (sx < sy) ? sx : sy;
			UINT tw = (UINT)(iw * sc); if (tw < 1) tw = 1;
			UINT th = (UINT)(ih * sc); if (th < 1) th = 1;

			if (FAILED(g_wic->CreateBitmapScaler(&scl))) break;
			if (FAILED(scl->Initialize(frm, tw, th, WICBitmapInterpolationModeFant))) break;
			if (FAILED(g_wic->CreateFormatConverter(&cvt))) break;

			// HDR (.jxr) is scRGB float - the generic 8-bit conversion clamps and
			// skips gamma, so it would read far too dark. Decode it to float and run
			// the same tonemap as the PNG export so the preview matches the file.
			bool isHdr = path.size() >= 4 &&
				(_wcsicmp(path.c_str() + path.size() - 4, L".jxr") == 0);

			if (isHdr) {
				if (FAILED(cvt->Initialize(scl, GUID_WICPixelFormat128bppRGBAFloat,
					WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) break;
				std::vector<float> f((size_t)tw * th * 4);
				if (FAILED(cvt->CopyPixels(nullptr, tw * 16, (UINT)(f.size() * sizeof(float)), (BYTE*)f.data()))) break;
				out.resize((size_t)tw * th * 4);
				for (size_t i = 0, n = (size_t)tw * th; i < n; ++i) {
					HdrTonemap::ScrgbToBgr8(f[i * 4 + 0], f[i * 4 + 1], f[i * 4 + 2],
						out[i * 4 + 0], out[i * 4 + 1], out[i * 4 + 2]); // B,G,R
					out[i * 4 + 3] = 255;
				}
			}
			else {
				if (FAILED(cvt->Initialize(scl, GUID_WICPixelFormat32bppPBGRA,
					WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) break;
				out.resize((size_t)tw * th * 4);
				if (FAILED(cvt->CopyPixels(nullptr, tw * 4, (UINT)out.size(), out.data()))) break;
			}
			outW = tw; outH = th; ok = true;
		} while (false);
		SafeRelease(cvt); SafeRelease(scl); SafeRelease(frm); SafeRelease(dec);
		return ok;
	}

	// Blit a decoded PBGRA image centered within an area rect (clipped).
	void BlitInto(const std::vector<BYTE>& img, UINT iw, UINT ih, int ax, int ay, int aw, int ah)
	{
		int dx = ax + (aw - (int)iw) / 2;
		int dy = ay + (ah - (int)ih) / 2;
		BYTE* dst = static_cast<BYTE*>(g_bits);
		for (UINT y = 0; y < ih; ++y) {
			int ry = dy + (int)y; if (ry < 0 || ry >= g_surfH) continue;
			int rx = dx; UINT cols = iw; UINT srcX = 0;
			if (rx < 0) { srcX = (UINT)(-rx); if (srcX >= cols) continue; cols -= srcX; rx = 0; }
			if (rx + (int)cols > g_surfW) cols = (UINT)(g_surfW - rx);
			if ((int)cols <= 0) continue;
			memcpy(dst + ((size_t)ry * g_surfW + rx) * 4,
				img.data() + ((size_t)y * iw + srcX) * 4, (size_t)cols * 4);
		}
	}

	Thumb& GetThumb(const std::string& path, UINT boxW, UINT boxH)
	{
		auto it = g_thumbs.find(path);
		if (it != g_thumbs.end() && it->second.boxW == boxW && it->second.boxH == boxH)
			return it->second;
		Thumb t; t.boxW = boxW; t.boxH = boxH;
		t.ok = DecodeScaled(Widen(path), boxW, boxH, t.px, t.w, t.h);
		g_thumbs[path] = std::move(t);
		return g_thumbs[path];
	}

	// Reproject the equirectangular g_pano into the [0,W]x[0,areaH] image area as a
	// perspective view looking at (yaw, pitch) with vertical 'fov'. For each output
	// pixel we build a view ray, rotate it by pitch (X) then yaw (Y), convert the
	// world ray to longitude/latitude, and bilinear-sample the panorama (u wraps).
	void Reproject360(int W, int areaH, float yawDeg, float pitchDeg, float fovDeg)
	{
		if (!g_panoOk || g_panoW == 0 || g_panoH == 0 || W <= 0 || areaH <= 0) return;
		const float PI = 3.14159265358979f, D2R = PI / 180.0f;
		const float tanV = tanf(fovDeg * 0.5f * D2R);
		const float tanH = tanV * ((float)W / (float)areaH);
		const float cp = cosf(pitchDeg * D2R), sp = sinf(pitchDeg * D2R);
		const float cy = cosf(yawDeg * D2R), sy = sinf(yawDeg * D2R);
		const int pw = (int)g_panoW, ph = (int)g_panoH;
		const BYTE* pano = g_pano.data();
		BYTE* dst = static_cast<BYTE*>(g_bits);

		for (int py = 0; py < areaH; ++py) {
			float vy = (1.0f - 2.0f * ((float)py + 0.5f) / (float)areaH) * tanV; // +1 top
			BYTE* drow = dst + (size_t)py * g_surfW * 4;
			for (int px = 0; px < W; ++px) {
				float vx = (2.0f * ((float)px + 0.5f) / (float)W - 1.0f) * tanH;
				// pitch about X, then yaw about Y (view forward = +Z)
				float ry = vy * cp + 1.0f * sp;
				float rz = -vy * sp + 1.0f * cp;
				float rxw = vx * cy + rz * sy;
				float rzw = -vx * sy + rz * cy;
				float ryw = ry;
				float len = sqrtf(rxw * rxw + ryw * ryw + rzw * rzw);
				float inv = (len > 1e-6f) ? 1.0f / len : 0.0f;
				float ny = ryw * inv;
				float lon = atan2f(rxw, rzw);                 // -pi..pi
				float lat = asinf(ny < -1.f ? -1.f : (ny > 1.f ? 1.f : ny));
				float u = 0.5f + lon / (2.0f * PI);
				float v = 0.5f - lat / PI;
				u -= floorf(u);                                // wrap horizontally
				if (v < 0) v = 0; else if (v > 1) v = 1;

				float sxf = u * pw - 0.5f, syf = v * ph - 0.5f;
				int x0 = (int)floorf(sxf), y0 = (int)floorf(syf);
				float fx = sxf - x0, fyy = syf - y0;
				int x0w = ((x0 % pw) + pw) % pw, x1w = (x0w + 1) % pw;
				int y0c = y0 < 0 ? 0 : (y0 >= ph ? ph - 1 : y0);
				int y1c = (y0c + 1 < ph) ? y0c + 1 : y0c;
				const BYTE* p00 = pano + ((size_t)y0c * pw + x0w) * 4;
				const BYTE* p10 = pano + ((size_t)y0c * pw + x1w) * 4;
				const BYTE* p01 = pano + ((size_t)y1c * pw + x0w) * 4;
				const BYTE* p11 = pano + ((size_t)y1c * pw + x1w) * 4;
				BYTE* d = drow + (size_t)px * 4;
				for (int ch = 0; ch < 4; ++ch) {
					float top = p00[ch] + (p10[ch] - p00[ch]) * fx;
					float bot = p01[ch] + (p11[ch] - p01[ch]) * fx;
					float val = top + (bot - top) * fyy;
					d[ch] = (BYTE)(val + 0.5f);
				}
				d[3] = 255;
			}
		}
	}

	// --- the two views -----------------------------------------------------

	void RenderGrid(int W, int H, int cols, int rows, int cellW, int thumbH, int capH,
		int x0, int y0, int gap, int headerH,
		const std::vector<FileEntry>& page, int selCell,
		const std::string& filterLabel, int selOneBased, int total)
	{
		// header
		char hdr[256];
		sprintf_s(hdr, "CAPTURES      %s      %d / %d", filterLabel.c_str(),
			total > 0 ? selOneBased : 0, total);
		DrawChip(0, 0, W, headerH, hdr, headerH * 4 / 11);

		if (page.empty()) {
			DrawChip(W / 4, H / 2 - 24, W / 2, 48, "No captures for this filter", 22);
		}
		else {
			int cellH = thumbH + capH;
			for (size_t k = 0; k < page.size(); ++k) {
				int cc = (int)k % cols, cr = (int)k / cols;
				int x = x0 + cc * (cellW + gap);
				int y = y0 + cr * (cellH + gap);
				// thumb backing panel
				FillRect32(x, y, cellW, thumbH, 10, 10, 10, 235);
				StampAlpha(x, y, cellW, thumbH, 255);
				Thumb& t = GetThumb(page[k].path, (UINT)cellW, (UINT)thumbH);
				if (t.ok) BlitInto(t.px, t.w, t.h, x, y, cellW, thumbH);
				else DrawChip(x, y, cellW, thumbH, "load failed", 16, 10, 235);
				// Tags in the thumbnail corner (HDR / 360).
				int bw = cellW / 6, bh = capH * 4 / 5, bpad = capH / 4;
				int tbx = x + cellW - bw - bpad;
				if (IsHdr(page[k].name)) { DrawBadge(tbx, y + bpad, bw, bh, "HDR", bh * 5 / 9); tbx -= bw + bpad; }
				if (t.ok && IsEquirect(t.w, t.h)) DrawBadge(tbx, y + bpad, bw, bh, "360", bh * 5 / 9);
				// filename label
				DrawChip(x, y + thumbH, cellW, capH, page[k].name, capH * 5 / 11, 22, 235);
				if ((int)k == selCell) DrawBorder(x - 3, y - 3, cellW + 6, thumbH + capH + 6, 3, kAccent);
			}
		}

		int hintH = headerH * 9 / 10;
		DrawChip(0, H - hintH, W, hintH,
			"Arrows  Move        Enter  Open        F  Date Filter        Backspace  Close",
			hintH * 4 / 11);
	}

	void RenderSingle(int W, int H, const std::string& path, const std::string& name,
		int oneBased, int total, bool look360, float yaw, float pitch, float fov)
	{
		const int capH = (H / 22 < 30) ? 30 : H / 22;
		const int areaH = H - capH;
		UINT boxW = (UINT)(W * 0.96), boxH = (UINT)(areaH * 0.96);

		// Flat decode (cached) - also tells us whether this is a 2:1 panorama.
		if (!(g_fullOk && g_fullPath == path && g_fullBoxW == boxW && g_fullBoxH == boxH)) {
			g_fullOk = DecodeScaled(Widen(path), boxW, boxH, g_fullImg, g_fullW, g_fullH);
			g_fullPath = path; g_fullBoxW = boxW; g_fullBoxH = boxH;
		}
		bool is360 = g_fullOk && IsEquirect(g_fullW, g_fullH);
		EnterCriticalSection(&s.cs); s.cur360 = is360; LeaveCriticalSection(&s.cs);

		if (look360 && is360) {
			// Decode the panorama at full (capped) resolution once for crisp sampling.
			if (!(g_panoOk && g_panoPath == path)) {
				g_panoOk = DecodeScaled(Widen(path), 4096, 2048, g_pano, g_panoW, g_panoH);
				g_panoPath = path;
			}
			if (g_panoOk) Reproject360(W, areaH, yaw, pitch, fov);
			else if (g_fullOk) BlitInto(g_fullImg, g_fullW, g_fullH, 0, 0, W, areaH);
		}
		else if (g_fullOk) {
			BlitInto(g_fullImg, g_fullW, g_fullH, 0, 0, W, areaH);
		}
		else {
			DrawChip(W / 4, H / 2 - capH / 2, W / 2, capH, "Could not load  " + name, capH * 5 / 11);
		}

		// Tags in the top-right corner.
		int bx = W - capH / 2;
		if (IsHdr(name)) {
			int bw = capH * 2, bh = capH * 3 / 4;
			bx -= bw; DrawBadge(bx, capH / 2, bw, bh, "HDR", bh * 5 / 9);
			bx -= capH / 3;
		}
		if (is360) {
			int bw = capH * 2, bh = capH * 3 / 4;
			bx -= bw; DrawBadge(bx, capH / 2, bw, bh, "360", bh * 5 / 9);
		}

		char info[512];
		if (look360 && is360)
			sprintf_s(info, "%d / %d   %s   Arrows  Look      F  Recenter      Enter  Flat      Backspace  Grid",
				oneBased, total, name.c_str());
		else if (is360)
			sprintf_s(info, "%d / %d   %s   Enter  360 View      <  >  Browse      Backspace  Grid",
				oneBased, total, name.c_str());
		else
			sprintf_s(info, "%d / %d      %s      <  >  Browse      Enter / Backspace  Back",
				oneBased, total, name.c_str());
		DrawChip(0, H - capH, W, capH, info, capH * 4 / 11);
	}

	// --- frame --------------------------------------------------------------
	void RenderNow()
	{
		RECT r = TargetRect();
		int W = r.right - r.left, H = r.bottom - r.top;
		if (W <= 0 || H <= 0) return;

		// Grid layout (also used to resolve up/down navigation).
		const int margin = (W / 60 < 16) ? 16 : W / 60;
		const int gap = margin / 2;
		const int headerH = (H / 16 < 36) ? 36 : H / 16;
		const int hintH = headerH * 9 / 10;
		int cols = (W - 2 * margin + gap) / (340 + gap); if (cols < 2) cols = 2; if (cols > 6) cols = 6;
		int cellW = (W - 2 * margin - (cols - 1) * gap) / cols;
		int thumbH = cellW * 9 / 16;
		int capH = (cellW / 12 < 20) ? 20 : cellW / 12;
		int cellH = thumbH + capH;
		int gridTop = headerH + margin;
		int gridBottom = H - hintH - gap;
		int rows = (gridBottom - gridTop + gap) / (cellH + gap); if (rows < 1) rows = 1;
		int perPage = cols * rows;

		// Drain queued intents and snapshot what we need to draw.
		bool vis; int mode, selOneBased = 0, total = 0;
		std::string filterLabel, singlePath, singleName;
		std::vector<FileEntry> page; int selCell = 0;
		bool look360 = false; float yaw360 = 0, pitch360 = 0, fov360 = 90;

		EnterCriticalSection(&s.cs);
		{
			for (int c : s.cmds) {
				int n = (int)s.filtered.size();
				if (s.mode == GRID) {
					switch (c) {
						case C_LEFT:  if (s.sel > 0) s.sel--; break;
						case C_RIGHT: if (s.sel < n - 1) s.sel++; break;
						case C_UP:    if (s.sel - cols >= 0) s.sel -= cols; break;
						case C_DOWN:  if (s.sel + cols < n) s.sel += cols; break;
						case C_ACTIVATE: if (n > 0) { s.mode = SINGLE; s.look360 = false; } break;
						case C_BACK:  s.visible = false; break;
						case C_FILTER:
							s.filterIdx = (s.filterIdx + 2 <= (int)s.dates.size()) ? s.filterIdx + 1 : -1;
							// rebuild filtered set for the new filter
							s.filtered.clear();
							for (int i = 0; i < (int)s.files.size(); ++i)
								if (s.filterIdx < 0 || s.files[i].date == s.dates[s.filterIdx])
									s.filtered.push_back(i);
							s.sel = 0;
							break;
					}
				}
				else if (s.look360) { // SINGLE - panning a 360 panorama
					const float step = 2.5f;
					switch (c) {
						case C_LEFT:  s.yaw360 -= step; break;
						case C_RIGHT: s.yaw360 += step; break;
						case C_UP:    s.pitch360 += step; if (s.pitch360 > 85.0f) s.pitch360 = 85.0f; break;
						case C_DOWN:  s.pitch360 -= step; if (s.pitch360 < -85.0f) s.pitch360 = -85.0f; break;
						case C_ACTIVATE: s.look360 = false; break;              // back to flat
						case C_BACK:  s.mode = GRID; s.look360 = false; break;  // back to grid
						case C_FILTER: s.yaw360 = 0.0f; s.pitch360 = 0.0f; s.fov360 = 90.0f; break; // recenter
					}
				}
				else { // SINGLE - flat view
					switch (c) {
						case C_LEFT:  if (n > 0) s.sel = (s.sel - 1 + n) % n; break;
						case C_RIGHT: if (n > 0) s.sel = (s.sel + 1) % n; break;
						case C_ACTIVATE: if (s.cur360) { s.look360 = true; s.yaw360 = 0.0f; s.pitch360 = 0.0f; s.fov360 = 90.0f; } break;
						case C_BACK:  s.mode = GRID; break;
						default: break;
					}
				}
			}
			s.cmds.clear();

			vis = s.visible; mode = s.mode;
			total = (int)s.filtered.size();
			if (s.sel < 0) s.sel = 0;
			if (s.sel >= total) s.sel = total > 0 ? total - 1 : 0;
			selOneBased = total > 0 ? s.sel + 1 : 0;
			filterLabel = (s.filterIdx < 0 || s.filterIdx >= (int)s.dates.size())
				? std::string("All dates") : s.dates[s.filterIdx];

			look360 = s.look360; yaw360 = s.yaw360; pitch360 = s.pitch360; fov360 = s.fov360;
			if (mode == SINGLE && total > 0) {
				singlePath = s.files[s.filtered[s.sel]].path;
				singleName = s.files[s.filtered[s.sel]].name;
			}
			else if (mode == GRID) {
				int pageNo = perPage > 0 ? s.sel / perPage : 0;
				int first = pageNo * perPage;
				selCell = s.sel - first;
				for (int k = first; k < first + perPage && k < total; ++k)
					page.push_back(s.files[s.filtered[k]]);
			}
		}
		LeaveCriticalSection(&s.cs);

		if (!vis) {
			if (g_shown && g_hwnd) { ShowWindow(g_hwnd, SW_HIDE); g_shown = false; }
			return;
		}
		if (!EnsureSurface(W, H)) return;

		ComposeBackdrop(W, H);
		if (mode == SINGLE && !singlePath.empty())
			RenderSingle(W, H, singlePath, singleName, selOneBased, total, look360, yaw360, pitch360, fov360);
		else
			RenderGrid(W, H, cols, rows, cellW, thumbH, capH, margin, gridTop, gap, headerH,
				page, selCell, filterLabel, selOneBased, total);

		POINT ptDst{ r.left, r.top };
		SIZE  sz{ W, H };
		POINT ptSrc{ 0, 0 };
		BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
		HDC screen = GetDC(nullptr);
		UpdateLayeredWindow(g_hwnd, screen, &ptDst, &sz, g_memDC, &ptSrc, 0, &bf, ULW_ALPHA);
		ReleaseDC(nullptr, screen);

		if (!g_shown) { ShowWindow(g_hwnd, SW_SHOWNA); g_shown = true; }
		SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}

	LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) { return DefWindowProc(h, msg, w, l); }

	void WorkerMain()
	{
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic));

		WNDCLASSEXW wc{};
		wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = WndProc;
		wc.hInstance = GetModuleHandle(nullptr);
		wc.lpszClassName = kClassName;
		RegisterClassExW(&wc);

		g_hwnd = CreateWindowExW(
			WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
			kClassName, L"", WS_POPUP, 0, 0, 16, 16, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

		while (g_running.load())
		{
			DWORD wr = MsgWaitForMultipleObjects(1, &g_wake, FALSE, 250, QS_ALLINPUT);
			MSG m;
			while (PeekMessage(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessage(&m); }
			if (!g_running.load()) break;
			if (wr == WAIT_OBJECT_0 || wr == WAIT_TIMEOUT) RenderNow();
		}

		if (g_hwnd) { DestroyWindow(g_hwnd); g_hwnd = nullptr; }
		UnregisterClassW(kClassName, GetModuleHandle(nullptr));
		DestroySurface();
		g_thumbs.clear();
		g_fullImg.clear(); g_fullImg.shrink_to_fit();
		g_fullOk = false; g_fullPath.clear(); g_fullBoxW = g_fullBoxH = 0;
		g_pano.clear(); g_pano.shrink_to_fit();
		g_panoOk = false; g_panoPath.clear(); g_panoW = g_panoH = 0;
		SafeRelease(g_wic);
		CoUninitialize();
	}

	void Wake() { if (g_wake) SetEvent(g_wake); }

	void PushCmd(int c)
	{
		if (!g_inited.load()) return;
		EnterCriticalSection(&s.cs);
		if (s.visible) s.cmds.push_back(c);
		LeaveCriticalSection(&s.cs);
		Wake();
	}

	// Rebuild s.filtered from the current filter (caller holds s.cs).
	void RebuildFilteredLocked()
	{
		s.filtered.clear();
		for (int i = 0; i < (int)s.files.size(); ++i)
			if (s.filterIdx < 0 || (s.filterIdx < (int)s.dates.size() && s.files[i].date == s.dates[s.filterIdx]))
				s.filtered.push_back(i);
		if (s.sel >= (int)s.filtered.size()) s.sel = s.filtered.empty() ? 0 : (int)s.filtered.size() - 1;
		if (s.sel < 0) s.sel = 0;
	}

	// Rescan the captures folder, newest first. Runs on the caller (script)
	// thread - a directory listing is cheap; only image decode is offloaded.
	void ScanFolder()
	{
		const std::string& dir = ScreenCapture::OutputDir();
		std::vector<std::pair<FILETIME, std::string>> found;

		WIN32_FIND_DATAA fd{};
		HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
		if (h != INVALID_HANDLE_VALUE) {
			do {
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
				if (!HasImageExt(fd.cFileName)) continue;
				found.emplace_back(fd.ftLastWriteTime, dir + "\\" + fd.cFileName);
			} while (FindNextFileA(h, &fd));
			FindClose(h);
		}
		std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
			return CompareFileTime(&a.first, &b.first) > 0; // newest first
		});

		EnterCriticalSection(&s.cs);
		s.files.clear(); s.dates.clear();
		for (auto& f : found) {
			FileEntry e; e.path = f.second; e.name = BaseName(f.second); e.date = DateKeyOf(f.first);
			s.files.push_back(std::move(e));
			if (s.dates.empty() || s.dates.back() != s.files.back().date) {
				// keep distinct dates in newest-first order (files already sorted)
				if (std::find(s.dates.begin(), s.dates.end(), s.files.back().date) == s.dates.end())
					s.dates.push_back(s.files.back().date);
			}
		}
		if (s.filterIdx >= (int)s.dates.size()) s.filterIdx = -1;
		RebuildFilteredLocked();
		LeaveCriticalSection(&s.cs);
	}
}

namespace CaptureGallery
{
	void Init()
	{
		if (g_inited.load()) return;
		InitializeCriticalSection(&s.cs);
		g_wake = CreateEvent(nullptr, FALSE, FALSE, nullptr); // auto-reset
		g_running.store(true);
		g_thread = std::thread(WorkerMain);
		g_inited.store(true);
	}

	void Shutdown()
	{
		if (!g_inited.load()) return;
		g_running.store(false);
		Wake();
		if (g_thread.joinable()) g_thread.join();
		if (g_wake) { CloseHandle(g_wake); g_wake = nullptr; }
		DeleteCriticalSection(&s.cs);
		g_shown = false;
		g_inited.store(false);
	}

	void Refresh() { if (g_inited.load()) ScanFolder(); }

	bool Open()
	{
		if (!g_inited.load()) return false;
		ScanFolder();
		EnterCriticalSection(&s.cs);
		bool empty = s.files.empty();
		if (!empty) {
			s.filterIdx = -1;
			RebuildFilteredLocked();
			s.sel = 0; s.mode = GRID; s.visible = true;
			s.look360 = false; s.cur360 = false;
		}
		LeaveCriticalSection(&s.cs);
		if (empty) return false;
		Wake();
		return true;
	}

	void Close()
	{
		if (!g_inited.load()) return;
		EnterCriticalSection(&s.cs);
		s.visible = false;
		LeaveCriticalSection(&s.cs);
		Wake();
	}

	bool IsOpen()
	{
		if (!g_inited.load()) return false;
		EnterCriticalSection(&s.cs);
		bool v = s.visible;
		LeaveCriticalSection(&s.cs);
		return v;
	}

	bool IsLooking360()
	{
		if (!g_inited.load()) return false;
		EnterCriticalSection(&s.cs);
		bool v = s.visible && s.mode == SINGLE && s.look360;
		LeaveCriticalSection(&s.cs);
		return v;
	}

	void NavLeft()  { PushCmd(C_LEFT); }
	void NavRight() { PushCmd(C_RIGHT); }
	void NavUp()    { PushCmd(C_UP); }
	void NavDown()  { PushCmd(C_DOWN); }
	void Activate() { PushCmd(C_ACTIVATE); }
	void Back()     { PushCmd(C_BACK); }
	void CycleFilter() { PushCmd(C_FILTER); }

	int Count()
	{
		if (!g_inited.load()) return 0;
		EnterCriticalSection(&s.cs);
		int n = (int)s.files.size();
		LeaveCriticalSection(&s.cs);
		return n;
	}

	std::string CurrentName()
	{
		if (!g_inited.load()) return "";
		EnterCriticalSection(&s.cs);
		std::string name;
		if (s.sel >= 0 && s.sel < (int)s.filtered.size())
			name = s.files[s.filtered[s.sel]].name;
		LeaveCriticalSection(&s.cs);
		return name;
	}

	void OpenFolderInExplorer()
	{
		const std::string& dir = ScreenCapture::OutputDir();
		ShellExecuteA(nullptr, "open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}
}
