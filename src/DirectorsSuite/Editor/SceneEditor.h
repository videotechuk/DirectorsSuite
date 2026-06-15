// Director's Suite - Scene Editor core: placed objects and temporary,
// runtime-only world (YMAP) edits made during a Photo Mode session.
//
// Nothing here is written to disk and nothing permanently modifies game files.
// Every placed object and every map edit is tracked so RevertAll() (called from
// CPhotoMode::Deactivate) can restore the world exactly as it was on entry.
//
// YMAP entities in RDR2 are keyed by archetypeName = GET_HASH_KEY(modelName)
// (confirmed against CodeX's RDR2 map reader), so the object catalogue names
// double as the hideable / forceable map archetypes here.

#pragma once
#include <string>
#include <vector>
#include "..\..\..\inc\types.h"

// A prop the user spawned and can freely transform.
struct SceneObject
{
	Object  handle = 0;
	Hash    model = 0;
	std::string name;
	Vector3 pos{};
	Vector3 rot{};          // x pitch, y roll, z heading (degrees)
	bool    frozen = true;  // frozen in place by default (placement, not physics)
	bool    collision = false;
};

// A temporary edit to the streamed world, undone on exit.
struct YmapEdit
{
	enum Type { HIDE = 0, SWAP, FORCE };
	int     type = HIDE;
	Hash    model = 0;       // archetype to hide / original to swap / forced model
	Hash    newModel = 0;    // SWAP target
	Vector3 pos{};
	float   radius = 8.0f;
	std::string label;       // for the menu list
};

class CSceneEditor
{
public:
	// --- placed objects ---
	std::vector<SceneObject> Objects;
	int Selected = -1;

	// Actors spawned *through the Scene Editor*, tracked by ped handle so exit
	// cleanup removes only these and never NPCs placed independently in Director
	// Mode. The actors themselves live in g_DirectorMode.NPCs (reused cast).
	std::vector<Ped> Actors;
	void RegisterActor(Ped handle) { if (handle != 0) Actors.push_back(handle); }

	SceneObject* Get(int index);
	SceneObject* GetSelected() { return Get(Selected); }

	// Spawn the named archetype a couple of metres ahead of the active view,
	// ground-snapped and frozen. Blocks (bounded) while the model streams.
	// Returns the new index or -1 on failure.
	int  SpawnObject(const std::string& name);
	int  DuplicateObject(int index);   // copy + small offset; returns new index
	void DeleteObject(int index);
	void DeleteAllObjects();
	void DeleteAllActors();   // remove every Scene-Editor-spawned actor

	// --- transforms (camera/heading relative, like the art-light editor) ---
	void PlaceAtCurrentView(SceneObject& o); // move to where the view looks
	void NudgeObject(SceneObject& o, float forward, float right, float up);
	void RotateObject(SceneObject& o, float dPitch, float dRoll, float dYaw);
	void SetHeading(SceneObject& o, float heading);
	void SnapToGround(SceneObject& o);
	void SetFrozen(SceneObject& o, bool frozen);
	void SetCollision(SceneObject& o, bool on);
	void ApplyTransform(SceneObject& o);     // push pos/rot to the engine object

	// --- temporary YMAP / world edits ---
	std::vector<YmapEdit> Edits;
	// Hide every instance of `model` within `radius` of `pos`.
	void HideMapModel(Hash model, const Vector3& pos, float radius, const std::string& label);
	// Shape-test from the active camera; hide whatever map entity it hits.
	bool HideEntityUnderView(float radius = 6.0f);
	// Replace `original` with `replacement` within a sphere (CREATE_MODEL_SWAP).
	void SwapMapModel(Hash original, Hash replacement, const Vector3& pos, float radius, const std::string& label);
	// Force a (normally blocked) map object to spawn (CREATE_FORCED_OBJECT).
	void ForceMapObject(Hash model, const Vector3& pos, const std::string& label);
	void RemoveEdit(int index);  // undo a single edit
	void RestoreAllEdits();      // undo every map edit (objects untouched)

	// --- lifecycle ---
	void Tick();        // keep placed props frozen; draw the gizmo. Per frame.
	void DrawGizmo();   // selection box for the selected object (screen-space)
	void RevertAll();   // delete all objects + undo all edits (called on PM exit)

	bool HasContent() const { return !Objects.empty() || !Edits.empty() || !Actors.empty(); }

private:
	void ValidateObjects();              // drop entries whose entity vanished
	static bool StreamModel(Hash model); // bounded REQUEST_MODEL + wait
	static bool ViewRay(Vector3& outStart, Vector3& outEnd, float dist);
};

inline CSceneEditor g_SceneEditor;
