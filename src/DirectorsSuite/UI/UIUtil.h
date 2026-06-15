// Licensed under the MIT License.

#pragma once
#include <windows.h>
#include "../common.hpp"

namespace UIUtil
{
	extern int g_screenWidth;
	extern int g_screenHeight;
	extern HWND g_windowHandle;

	bool GetScreenDimensions();

	void PrintSubtitle(const char* msg);
	void PrintSubtitle(const std::string& msg);
	void PrintSubtitle(const int msg);
	void PrintSubtitle(const float msg);

	// Script-drawn on-screen notification. Photo Mode freezes the world / runs a
	// slow timescale, and in that state the native subtitle/objective UI never
	// renders. While "script mode" is on (Photo Mode turns it on for its whole
	// session), PrintSubtitle() routes here instead of the native, and the owner
	// draws it every frame with the mod's own text drawing - which works frozen.
	namespace Notify
	{
		void SetScriptMode(bool on);
		bool ScriptMode();
		void Show(const std::string& msg, int durationMs = 3500);
		// A longer, word-wrapped "TIP" card drawn near the top of the screen -
		// used for first-time feature hints (see CPhotoMode::ShowTabTip).
		void ShowTip(const std::string& msg, int durationMs = 9000);
		void Draw();   // call every frame from a drawer that runs while frozen
	}
}
