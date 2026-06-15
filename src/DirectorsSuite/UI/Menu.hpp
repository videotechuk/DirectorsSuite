// Licensed under the MIT License.

#pragma once
#include "Submenu.hpp"
#include "SubmenuIDs.h"
#include "../script.h"
#include "../common.hpp"

#define BUILD_1311_COMPATIBLE 0 // If true, the menu will be compatible with game version <= 1311.12
#define SUBMENU g_Menu->AddSubmenu

class CNativeMenu
{
private:
	std::unordered_map<int, Submenu> m_SubmenusMap{};

	Submenu* m_CurrentSubmenu = nullptr;                          // Current Submenu
	eSubmenuID m_CurrentSubmenuID = eSubmenuID::Submenu_Invalid;  // Current Submenu ID
	int m_SelectionIndex = 0;                                     // Current Selected Option Index
public:

	// ctor
	CNativeMenu()
	{
		this->RegisterUIPrompts();
	}

	void AddSubmenu(const std::string& header, const std::string& subHeader, eSubmenuID id, int numVisibleOptions, std::function<void(Submenu*)> submenuFunc)
	{
		Submenu submenu = Submenu(header, subHeader, id, numVisibleOptions, submenuFunc);
		m_SubmenusMap[id] = std::move(submenu);
	}


	// Go to a submenu
	// 
	// Note: If you're skipping nested submenus by calling this, it might be a good idea to
	// call this function on those submenus right before the one you need to go to so data gets properly set
	void GoToSubmenu(eSubmenuID id)
	{
		bool setRememberedSelection = false;

		if (!m_SubmenusMap.contains(id)) {
			PRINT_ERROR("Submenu ID ",(int)id," doesn't exist");
			return;
		}

		// This should ONLY be the case when the menu is first created
		if (m_CurrentSubmenu == nullptr) {
			m_CurrentSubmenu = &m_SubmenusMap[id];
			return;
		}

		if (id > m_CurrentSubmenu->ID) {
			// went forward
			m_PreviousSubmenus.push_back(m_CurrentSubmenu->ID);
			m_LastSubmenuSelections[m_CurrentSubmenu->ID] = m_SelectionIndex;
		}
		else if (id < m_CurrentSubmenu->ID) {
			// went back
			m_PreviousSubmenus.pop_back();
			m_LastSubmenuSelections.erase(m_CurrentSubmenu->ID);
			setRememberedSelection = true;
		}

		m_CurrentSubmenu = &m_SubmenusMap[id];

		if (setRememberedSelection) {
			m_SelectionIndex = m_LastSubmenuSelections[m_CurrentSubmenu->ID];
		}
		else {
			m_SelectionIndex = 0;
			// Skip any leading title / empty rows so a real option is highlighted
			// on entry (pages can now open with a section-title header).
			int n = m_CurrentSubmenu->GetNumberOfOptions();
			while (m_SelectionIndex < n) {
				Option* o = m_CurrentSubmenu->GetOption(m_SelectionIndex);
				if (!o || !o->IsEmptyOption) break;
				m_SelectionIndex++;
			}
			if (m_SelectionIndex >= n) m_SelectionIndex = 0;
		}
	}

	// Gets a submenu via its ID
	Submenu* GetSubmenu(eSubmenuID id)
	{
		if (m_SubmenusMap.contains(id)) {
			return &m_SubmenusMap[id];
		}

		PRINT_ERROR("Submenu ID ",(int)id," doesn't exist, returning null");
		return nullptr;
	}

	// Gets the currently selected option in the current submenu
	Option* GetSelectedOption()
	{
		Option* option = m_SubmenusMap[m_CurrentSubmenu->ID].GetOption(m_SelectionIndex);
		if (option == nullptr) {
			PRINT_ERROR("Failed to find option in submenu ", (int)m_CurrentSubmenu->ID, " with index ", m_SelectionIndex, ", returning null");
		}
		return option;
	}

	// Returns a pointer to the current submenu
	inline Submenu* GetCurrentSubmenu()
	{
		return m_CurrentSubmenu;
	}

	// Get the current selection index
	inline int GetSelectionIndex()
	{
		return m_SelectionIndex;
	}

	// Set the current selection index
	void SetSelectionIndex(int newIndex)
	{
		if (newIndex > m_CurrentSubmenu->GetNumberOfOptions() - 1)
		{
			newIndex = m_CurrentSubmenu->GetNumberOfOptions() - 1;
		}
		m_SelectionIndex = newIndex;
	}

	// Checks if a submenu exists
	inline bool DoesSubmenuExist(eSubmenuID id)
	{
		return m_SubmenusMap.contains(id);
	}

	// Delete a submenu
	void DeleteSubmenu(Submenu* pSubmenu)
	{
		if (pSubmenu == nullptr) {
			PRINT_ERROR("Failed to delete submenu, pSubmenu is null");
			return;
		}

		if (DoesSubmenuExist(pSubmenu->ID)) {
			pSubmenu->Clear();
			m_SubmenusMap.erase(pSubmenu->ID);
		}
	}

	// Checks if the menu is currently open
	inline bool IsOpen() const
	{
		return m_IsOpen;
	}

	// --- External input driver -------------------------------------------------
	// Photo Mode flies a free camera while the menu is on screen, so it must own
	// input: it disables every game control (which also neuters this menu's
	// INPUT_GAME_MENU reads) and drives the menu with raw keys through these.
	// Internal input is suspended and nav sounds silenced (Photo Mode plays its
	// own from the Photo_Mode_Sounds set, the only one audible in that context).
	void SetInputSuspended(bool s) { m_InputSuspended = s; }
	void SetNavSoundSuppressed(bool s) { m_SuppressNavSound = s; }
	void SetForceHidden(bool h) { m_ForceHidden = h; }
	bool IsForceHidden() const { return m_ForceHidden; }

	// Force the menu open and point it at a submenu, clearing the back-stack so
	// backing out of that page returns control to the caller (Photo Mode).
	void OpenAt(eSubmenuID id);

	// Raw-key driven navigation (call from an external owner of input).
	void ExtNavUp()       { HandleUpKeyPressed(); }
	void ExtNavDown()     { HandleDownKeyPressed(); }
	void ExtAdjustLeft()  { HandleLeftKeyPressed(); }
	void ExtAdjustRight() { HandleRightKeyPressed(); }
	void ExtEnter()       { HandleEnterPressed(); }
	// Goes back one page; returns false if already at `root` (caller should exit)
	bool ExtBack(eSubmenuID root);

	// Force the Select/Back UI prompts hidden. Used by blocking routines (e.g.
	// the tiled super-res capture) that run their own WAIT loop, during which
	// Update() - and its normal prompt-visibility handling - never runs.
	void HidePrompts();

	/// <summary>
	/// Set if the menu should be open or closed
	/// </summary>
	/// <param name="bEnabled">- Open/Closed</param>
	/// <param name="bPlaySounds">- Play open and close sounds</param>
	void SetEnabled(bool bEnabled, bool bPlaySounds = true);

	// Renders the menu
	void Update();

private:
	// Misc Variables

	bool m_IsOpen = false;
	bool m_InputSuspended = false;   // external owner (Photo Mode) drives input
	bool m_SuppressNavSound = false; // external owner plays its own nav sounds
	bool m_ForceHidden = false;      // hide the menu render (clean photo shot)
	// Contains submenu IDs you were previously at to properly backtrack
	std::vector<eSubmenuID> m_PreviousSubmenus = {};
	// Contains the last selection you were at in a sub menu
	std::unordered_map<eSubmenuID, int> m_LastSubmenuSelections = {};

	// Input

	int m_ControlIndex = INPUT_GROUP_KEYBOARD;
	bool m_OpenKeyPressed = false;
	bool m_BackKeyPressed = false;
	bool m_EnterKeyPressed = false;
	bool m_UpKeyPressed = false;
	bool m_DownKeyPressed = false;
	bool m_LeftKeyPressed = false;
	bool m_RightKeyPressed = false;
	bool m_LMBPressed = false;

	// Prompts

	Prompt m_SelectPrompt = NULL;
	Prompt m_BackPrompt = NULL;
	bool m_SelectPromptCompleted = false;
	bool m_BackPromptCompleted = false;

	// Functions

	void CheckInput();
	void HandleInput();
	void DisableCommonInputs();
	void RegisterUIPrompts();

	void HandleEnterPressed();
	void HandleBackPressed();
	void HandleUpKeyPressed();
	void HandleDownKeyPressed();
	void HandleLeftKeyPressed();
	void HandleRightKeyPressed();
	void HandleMouseInput(); // TODO
};

inline std::unique_ptr<CNativeMenu> g_Menu;
