// Director's Suite - in-game key rebinding shared by the main menu and the
// Photo Mode Settings page. A binding row is a VectorOption whose right-text is
// the current key; activating it captures the next key press and writes it
// straight to g_Config (saved to the INI). Header-only so both menus share one
// implementation.

#pragma once
#include "..\script.h"
#include "..\keyboard.h"
#include "..\UI\Menu.hpp"
#include "..\UI\Drawing.h"
#include "Config.h"
#include <string>
#include <vector>

namespace KeyBind
{
	// Human-readable name for a Windows virtual-key code.
	inline std::string Name(int vk)
	{
		if (vk == 0) return "(Unbound)";
		if (vk >= 'A' && vk <= 'Z') return std::string(1, (char)vk);
		if (vk >= '0' && vk <= '9') return std::string(1, (char)vk);
		if (vk >= VK_F1 && vk <= VK_F24) return "F" + std::to_string(vk - VK_F1 + 1);
		if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return "Num " + std::to_string(vk - VK_NUMPAD0);
		switch (vk) {
			case VK_SPACE:    return "Space";
			case VK_RETURN:   return "Enter";
			case VK_TAB:      return "Tab";
			case VK_BACK:     return "Backspace";
			case VK_ESCAPE:   return "Esc";
			case VK_INSERT:   return "Insert";
			case VK_DELETE:   return "Delete";
			case VK_HOME:     return "Home";
			case VK_END:      return "End";
			case VK_PRIOR:    return "Page Up";
			case VK_NEXT:     return "Page Down";
			case VK_LEFT:     return "Left Arrow";
			case VK_RIGHT:    return "Right Arrow";
			case VK_UP:       return "Up Arrow";
			case VK_DOWN:     return "Down Arrow";
			case VK_SHIFT: case VK_LSHIFT:   return "Shift";
			case VK_RSHIFT:   return "Right Shift";
			case VK_CONTROL: case VK_LCONTROL: return "Ctrl";
			case VK_RCONTROL: return "Right Ctrl";
			case VK_MENU: case VK_LMENU:     return "Alt";
			case VK_RMENU:    return "Right Alt";
			case VK_OEM_3:    return "`";
			case VK_OEM_MINUS:return "-";
			case VK_OEM_PLUS: return "=";
			case VK_OEM_4:    return "[";
			case VK_OEM_6:    return "]";
			case VK_OEM_5:    return "\\";
			case VK_OEM_1:    return ";";
			case VK_OEM_7:    return "'";
			case VK_OEM_COMMA:return ",";
			case VK_OEM_PERIOD:return ".";
			case VK_OEM_2:    return "/";
			case VK_MULTIPLY: return "Num *";
			case VK_ADD:      return "Num +";
			case VK_SUBTRACT: return "Num -";
			case VK_DIVIDE:   return "Num /";
			case VK_DECIMAL:  return "Num .";
		}
		char buf[16]; sprintf_s(buf, "Key 0x%02X", vk);
		return buf;
	}

	// Draws the "press a key" prompt for one frame.
	inline void DrawPrompt()
	{
		GRAPHICS::DRAW_RECT(0.5f, 0.5f, 0.34f, 0.080f, 0, 0, 0, 205, false, false);
		GRAPHICS::DRAW_RECT(0.5f, 0.4655f, 0.34f, 0.004f, 80, 200, 255, 235, false, false);
		Drawing::DrawFormattedText("Press a key to bind", Font::Body, 245, 245, 245, 240,
			Alignment::Center, 20, SCREEN_WIDTH * 0.5f, SCREEN_HEIGHT * 0.482f);
		Drawing::DrawFormattedText("Esc = cancel", Font::Body, 200, 200, 200, 220,
			Alignment::Center, 15, SCREEN_WIDTH * 0.5f, SCREEN_HEIGHT * 0.516f);
	}

	// Blocks until the user presses a key; returns its VK code, or 0 if cancelled
	// (Esc) or timed out. Waits for everything to be released first so the menu's
	// own confirm key isn't captured.
	inline int Capture()
	{
		// 1) wait until no key is held (lets the select/Enter key clear)
		for (DWORD t = GetTickCount(); GetTickCount() - t < 5000; ) {
			bool anyDown = false;
			for (int vk = 0x08; vk <= 0xFE; vk++) if (IsKeyDown(vk)) { anyDown = true; break; }
			if (!anyDown) break;
			PAD::DISABLE_ALL_CONTROL_ACTIONS(0);
			DrawPrompt();
			WAIT(0);
		}
		// 2) capture the next press
		for (DWORD t = GetTickCount(); GetTickCount() - t < 15000; ) {
			for (int vk = 0x08; vk <= 0xFE; vk++) {
				if (!IsKeyDown(vk)) continue;
				int chosen = (vk == VK_ESCAPE) ? 0 : vk;
				while (IsKeyDown(vk)) { PAD::DISABLE_ALL_CONTROL_ACTIONS(0); DrawPrompt(); WAIT(0); }
				return chosen;
			}
			PAD::DISABLE_ALL_CONTROL_ACTIONS(0);
			DrawPrompt();
			WAIT(0);
		}
		return 0; // timed out - keep current
	}

	struct Def { const char* label; int CConfig::* member; };

	inline const std::vector<Def>& List()
	{
		static const std::vector<Def> defs = {
			{ "Open Menu",              &CConfig::KeyOpenMenu },
			{ "Toggle Photo Mode",      &CConfig::KeyPhotoMode },
			{ "Insert Camera",          &CConfig::KeyAddCamera },
			{ "Take Screenshot",        &CConfig::KeyScreenshot },
			{ "Next Camera",            &CConfig::KeyNextCamera },
			{ "Previous Camera",        &CConfig::KeyPrevCamera },
			{ "Aim Assist (hold)",      &CConfig::KeyAimAssist },
			{ "Auto Camera Switching",  &CConfig::KeyCameraAutoSwitchingStartStop },
			{ "Toggle Player Camera",   &CConfig::KeyPlayerCamToggle },
		};
		return defs;
	}

	// Populate a submenu with one rebind row per key. Shared by both menus.
	inline void BuildInto(Submenu* sub)
	{
		sub->AddEmptyOption("Select a binding, then press the new key");
		for (const auto& kd : List()) {
			VectorOption* o = sub->AddVectorOption(kd.label, "Press to rebind; Esc keeps the current key",
				std::vector<std::string>{ "-" }, [kd] {
					VectorOption* v = g_Menu->GetSelectedOption()->As<VectorOption*>();
					int vk = Capture();
					if (vk != 0) { g_Config.*(kd.member) = vk; g_Config.Save(); }
					v->RightText = Name(g_Config.*(kd.member)); // refresh in place (no rebuild)
				});
			o->RightText = Name(g_Config.*(kd.member));
		}
	}
}
