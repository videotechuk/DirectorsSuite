// Licensed under the MIT License.

#include "UIUtil.h"
#include "../script.h"
#include "Drawing.h"
#include <string>
#include <vector>
#include <sstream>

namespace UIUtil
{
	int g_screenWidth = GetSystemMetrics(SM_CXSCREEN);
	int g_screenHeight = GetSystemMetrics(SM_CYSCREEN);
	HWND g_windowHandle = NULL;

	bool FindGameWindow()
	{
		g_windowHandle = FindWindowA(0, "Red Dead Redemption 2"); // sgaWindow
		return g_windowHandle != NULL;
	}

	bool GetScreenDimensions()
	{
		if (!FindGameWindow()) {
			PRINT_ERROR("Failed to find Red Dead Redemption 2 game window");
			return false;
		}

		RECT windowRect;
		if (!GetWindowRect(g_windowHandle, &windowRect)) {
			PRINT_ERROR("GetWindowRect() failed");
			return false;
		}

		g_screenWidth = windowRect.right - windowRect.left;
		g_screenHeight = windowRect.bottom - windowRect.top;

		return true;
	}

	// --- script-drawn notification -----------------------------------------
	namespace Notify
	{
		static std::string s_msg;
		static DWORD       s_expire = 0;
		static bool        s_scriptMode = false;
		static bool        s_isTip = false;   // tip card (top) vs quick toast (bottom)

		// Greedy word-wrap into lines of at most maxLen characters.
		static std::vector<std::string> WrapWords(const std::string& s, size_t maxLen)
		{
			std::vector<std::string> lines;
			std::istringstream iss(s);
			std::string word, cur;
			while (iss >> word) {
				if (cur.empty()) cur = word;
				else if (cur.size() + 1 + word.size() <= maxLen) cur += " " + word;
				else { lines.push_back(cur); cur = word; }
			}
			if (!cur.empty()) lines.push_back(cur);
			return lines;
		}

		void SetScriptMode(bool on)
		{
			s_scriptMode = on;
			if (!on) { s_msg.clear(); s_expire = 0; s_isTip = false; }
		}
		bool ScriptMode() { return s_scriptMode; }

		void Show(const std::string& msg, int durationMs)
		{
			s_msg = msg;
			s_expire = GetTickCount() + (DWORD)durationMs;
			s_isTip = false;
		}

		void ShowTip(const std::string& msg, int durationMs)
		{
			s_msg = msg;
			s_expire = GetTickCount() + (DWORD)durationMs;
			s_isTip = true;
		}

		void Draw()
		{
			if (s_msg.empty() || GetTickCount() >= s_expire) return;

			if (s_isTip) {
				// Word-wrapped "TIP" box anchored to the top-right, clear of the
				// menu panel (which sits on the left) and out of the shot.
				const float right = 0.990f;      // right edge of the box
				const float w     = 0.320f;      // box width
				const float cx    = right - w * 0.5f;
				const float leftEdge = right - w;
				const float padX  = 0.012f;
				const float top   = 0.050f;
				const float lineH = 0.028f;

				std::vector<std::string> lines = WrapWords(s_msg, 24);
				const float boxH = 0.062f + lines.size() * lineH;

				GRAPHICS::DRAW_RECT(cx, top + boxH * 0.5f, w, boxH, 0, 0, 0, 195, false, false);
				GRAPHICS::DRAW_RECT(cx, top + 0.004f, w, 0.0040f, 80, 200, 255, 235, false, false); // top accent

				const float textX = (leftEdge + padX) * SCREEN_WIDTH;
				Drawing::DrawFormattedText("TIP", Font::Body, 80, 200, 255, 240,
					Alignment::Left, 14, textX, SCREEN_HEIGHT * (top + 0.016f));
				float ty = top + 0.048f;
				for (const std::string& ln : lines) {
					Drawing::DrawFormattedText(ln, Font::Body, 245, 245, 245, 240,
						Alignment::Left, 16, textX, SCREEN_HEIGHT * ty);
					ty += lineH;
				}
				return;
			}

			// Quick toast: width grows with the message (markup tags slightly
			// over-estimate it, harmless). Sits above the control-hint bar.
			float w = 0.04f + s_msg.size() * 0.0085f;
			if (w > 0.92f) w = 0.92f;
			GRAPHICS::DRAW_RECT(0.5f, 0.900f, w, 0.050f, 0, 0, 0, 175, false, false);
			Drawing::DrawFormattedText(s_msg, Font::Body, 245, 245, 245, 235,
				Alignment::Center, 18, SCREEN_WIDTH * 0.5f, SCREEN_HEIGHT * 0.884f);
		}
	}

	void PrintSubtitle(const char* msg)
	{
		// While Photo Mode is active the native objective UI is suppressed by the
		// freeze / slow timescale, so draw the message ourselves instead.
		if (Notify::ScriptMode()) {
			Notify::Show(msg ? msg : "");
			return;
		}

		UILOG::_UILOG_SET_CACHED_OBJECTIVE(msg);
		UILOG::_UILOG_PRINT_CACHED_OBJECTIVE();
		UILOG::_UILOG_CLEAR_HAS_DISPLAYED_CACHED_OBJECTIVE();
		UILOG::_UILOG_CLEAR_CACHED_OBJECTIVE();
	}

	void PrintSubtitle(const std::string& msg) { PrintSubtitle(msg.c_str()); }
	void PrintSubtitle(const int msg) { PrintSubtitle(std::to_string(msg).c_str()); }
	void PrintSubtitle(const float msg) { PrintSubtitle(std::to_string(msg).c_str()); }

	void ShowGameTip(const std::string& msg, int durationMs)
	{
		// RDR2 help-text feed (the top-left tip toast). The native takes two small
		// structs by pointer: p0 = { durationMs, 0, 0, 0 }, p1 = { 0, textPtr }.
		const char* str = MISC::VAR_STRING(10, "LITERAL_STRING", msg.c_str());
		UINT64 data0[4] = { (UINT64)(unsigned int)durationMs, 0, 0, 0 };
		UINT64 data1[2] = { 0, (UINT64)str };
		UIFEED::_UI_FEED_POST_HELP_TEXT((Any*)data0, (Any*)data1, TRUE);
	}
}
