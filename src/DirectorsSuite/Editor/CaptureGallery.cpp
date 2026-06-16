#include "CaptureGallery.h"
#include "ScreenCapture.h"   // OutputDir()
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

	// Thumbnail cache for the grid (worker thread only), keyed by file path.
	struct Thumb { std::vector<BYTE> px; UINT w = 0, h = 0, boxW = 0, boxH = 0; bool ok = false; };
	std::map<std::string, Thumb> g_thumbs;

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
			if (FAILED(cvt->Initialize(scl, GUID_WICPixelFormat32bppPBGRA,
				WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) break;

			out.resize((size_t)tw * th * 4);
			if (FAILED(cvt->CopyPixels(nullptr, tw * 4, (UINT)out.size(), out.data()))) break;
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
		int oneBased, int total)
	{
		const int capH = (H / 22 < 30) ? 30 : H / 22;
		UINT boxW = (UINT)(W * 0.96), boxH = (UINT)((H - capH) * 0.96);
		if (!(g_fullOk && g_fullPath == path && g_fullBoxW == boxW && g_fullBoxH == boxH)) {
			g_fullOk = DecodeScaled(Widen(path), boxW, boxH, g_fullImg, g_fullW, g_fullH);
			g_fullPath = path; g_fullBoxW = boxW; g_fullBoxH = boxH;
		}
		if (g_fullOk) BlitInto(g_fullImg, g_fullW, g_fullH, 0, 0, W, H - capH);
		else DrawChip(W / 4, H / 2 - capH / 2, W / 2, capH, "Could not load  " + name, capH * 5 / 11);

		char info[512];
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
						case C_ACTIVATE: if (n > 0) s.mode = SINGLE; break;
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
				else { // SINGLE
					switch (c) {
						case C_LEFT:  if (n > 0) s.sel = (s.sel - 1 + n) % n; break;
						case C_RIGHT: if (n > 0) s.sel = (s.sel + 1) % n; break;
						case C_ACTIVATE:
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
			RenderSingle(W, H, singlePath, singleName, selOneBased, total);
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
