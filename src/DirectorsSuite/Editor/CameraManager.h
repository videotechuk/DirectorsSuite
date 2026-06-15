// Director's Suite - camera storage: insert/edit/delete, naming templates,
// presets, and INI persistence for up to MAX_CAMERAS cameras.

#pragma once
#include <vector>
#include "EditorTypes.h"

class CCameraManager
{
public:
	std::vector<EditorCamera> Cameras;
	std::vector<CameraPreset> Presets;

	int SelectedIndex = -1;       // camera currently opened in the edit submenu
	int SelectedPresetIndex = -1;
	int NewCameraPresetIndex = 0; // 0 = none; preset auto-applied to inserted cameras

	// Insert a camera at the current view (free cam pose if active, otherwise
	// the rendered cam). Returns the index or -1 when the limit is reached.
	int InsertCamera();

	void DeleteCamera(int index);
	void DeleteAllCameras();

	EditorCamera* Get(int index)
	{
		if (index < 0 || index >= (int)Cameras.size()) return nullptr;
		return &Cameras[index];
	}

	EditorCamera* GetSelected() { return Get(SelectedIndex); }

	int Count() const { return (int)Cameras.size(); }
	int EnabledCount() const;

	// Re-capture position/rotation/fov of a camera from the current view
	void UpdateCameraFromCurrentView(int index);

	// Naming
	std::string BuildName(int index) const; // expands the INI naming template
	// Expand an arbitrary template for a camera slot (used for menu previews)
	std::string PreviewName(const std::string& tpl, int index) const;
	void RenameAll();                       // re-apply template to every camera

	// Presets
	void ApplyPreset(const CameraPreset& preset, EditorCamera& cam);
	void SavePresetFromCamera(const EditorCamera& cam, const std::string& name);

	// Persistence (DirectorsSuite_Cameras.ini next to the game exe)
	void SaveToFile();
	void LoadFromFile();

private:
	int m_nextId = 1;
	std::string CamerasFilePath() const;
};

inline CCameraManager g_CameraManager;
