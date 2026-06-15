#include "PMIcons.h"
#include "PhotoMode.h"
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>

#pragma comment(lib, "gdiplus.lib")

namespace
{
	PMIconGrid s_icons[PMTAB_COUNT];
	bool s_loadAttempted = false;

	// Filename per tab; "Charecter" kept as an accepted alternative since
	// the original icon pack shipped with that spelling.
	const wchar_t* s_names[PMTAB_COUNT][2] = {
		{ L"Camera",    nullptr },
		{ L"World",     nullptr },
		{ L"Character", L"Charecter" },
		{ L"Lighting",  nullptr },
		{ L"Post",      nullptr },
		{ L"Effects",   nullptr },
		{ L"Music",     nullptr },
	};

	std::wstring IconDir()
	{
		wchar_t path[MAX_PATH];
		GetModuleFileNameW(nullptr, path, MAX_PATH);
		std::wstring dir(path);
		size_t slash = dir.find_last_of(L"\\/");
		dir = (slash == std::wstring::npos) ? L"." : dir.substr(0, slash);
		return dir + L"\\DirectorsSuite_Icons\\";
	}

	// Greedy rectangle meshing: cover every non-zero cell of the quantized
	// grid with as few axis-aligned rectangles as possible (per level).
	void MeshGrid(const unsigned char level[PMICON_GRID][PMICON_GRID], std::vector<PMIconRect>& out)
	{
		bool used[PMICON_GRID][PMICON_GRID] = {};

		for (int y = 0; y < PMICON_GRID; y++) {
			for (int x = 0; x < PMICON_GRID; x++) {
				if (used[y][x] || level[y][x] == 0) continue;
				unsigned char lv = level[y][x];

				// grow right
				int w = 1;
				while (x + w < PMICON_GRID && !used[y][x + w] && level[y][x + w] == lv) w++;
				// grow down while the full row span matches
				int h = 1;
				bool grow = true;
				while (grow && y + h < PMICON_GRID) {
					for (int i = 0; i < w; i++) {
						if (used[y + h][x + i] || level[y + h][x + i] != lv) { grow = false; break; }
					}
					if (grow) h++;
				}

				for (int yy = 0; yy < h; yy++)
					for (int xx = 0; xx < w; xx++)
						used[y + yy][x + xx] = true;

				out.push_back({ (unsigned char)x, (unsigned char)y, (unsigned char)w, (unsigned char)h, lv });
			}
		}
	}

	bool LoadOne(const std::wstring& file, PMIconGrid& out)
	{
		Gdiplus::Bitmap* src = Gdiplus::Bitmap::FromFile(file.c_str(), FALSE);
		if (!src || src->GetLastStatus() != Gdiplus::Ok) {
			delete src;
			return false;
		}

		// Supersample at 3x the grid, then take the MAX per cell. Plain
		// averaging dissolves thin strokes; max-pooling keeps every stroke
		// that crosses a cell visible.
		constexpr int SS = 3;
		constexpr int BIG = PMICON_GRID * SS;
		Gdiplus::Bitmap scaled(BIG, BIG, PixelFormat32bppARGB);
		{
			Gdiplus::Graphics g(&scaled);
			g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
			g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
			g.DrawImage(src, 0, 0, BIG, BIG);
		}
		delete src;

		// Decide what carries the shape: the alpha channel (icon on
		// transparency) or inverse luminance (dark strokes on a light card).
		unsigned char minAlpha = 255;
		Gdiplus::Color c;
		for (int y = 0; y < BIG; y++) {
			for (int x = 0; x < BIG; x++) {
				scaled.GetPixel(x, y, &c);
				if (c.GetA() < minAlpha) minAlpha = c.GetA();
			}
		}
		bool useAlpha = (minAlpha < 240);

		unsigned char mask[PMICON_GRID][PMICON_GRID] = {};
		int maxVal = 0;
		for (int y = 0; y < PMICON_GRID; y++) {
			for (int x = 0; x < PMICON_GRID; x++) {
				int best = 0;
				for (int sy = 0; sy < SS; sy++) {
					for (int sx = 0; sx < SS; sx++) {
						scaled.GetPixel(x * SS + sx, y * SS + sy, &c);
						int v;
						if (useAlpha) {
							v = c.GetA();
						}
						else {
							int lum = (c.GetR() * 54 + c.GetG() * 183 + c.GetB() * 19) / 256;
							v = 255 - lum;
						}
						if (v > best) best = v;
					}
				}
				mask[y][x] = (unsigned char)best;
				if (best > maxVal) maxVal = best;
			}
		}
		if (maxVal <= 10) {
			return false; // effectively empty image
		}

		// Normalize, then quantize to two levels: soft edge / solid body.
		// Two levels keep the mesh small while preserving antialiasing.
		unsigned char level[PMICON_GRID][PMICON_GRID] = {};
		for (int y = 0; y < PMICON_GRID; y++) {
			for (int x = 0; x < PMICON_GRID; x++) {
				int v = (int)mask[y][x] * 255 / maxVal;
				level[y][x] = (v >= 140) ? 2 : (v >= 40 ? 1 : 0);
			}
		}

		out.rects.clear();
		MeshGrid(level, out.rects);
		out.loaded = !out.rects.empty();
		return out.loaded;
	}

	void LoadAll()
	{
		s_loadAttempted = true;

		Gdiplus::GdiplusStartupInput input;
		ULONG_PTR token = 0;
		if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) {
			return;
		}

		std::wstring dir = IconDir();
		for (int tab = 0; tab < PMTAB_COUNT; tab++) {
			for (int alt = 0; alt < 2 && !s_icons[tab].loaded; alt++) {
				if (!s_names[tab][alt]) continue;
				LoadOne(dir + s_names[tab][alt] + L".png", s_icons[tab]);
			}
		}

		Gdiplus::GdiplusShutdown(token);
	}
}

namespace PMIcons
{
	const PMIconGrid* Get(int tab)
	{
		if (!s_loadAttempted) {
			LoadAll();
		}
		if (tab < 0 || tab >= PMTAB_COUNT || !s_icons[tab].loaded) {
			return nullptr;
		}
		return &s_icons[tab];
	}
}
