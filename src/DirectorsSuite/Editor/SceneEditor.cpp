// Director's Suite - Scene Editor core (see SceneEditor.h).

#include "SceneEditor.h"
#include "DirectorMode.h"   // actors spawned via the Scene Editor live in the Director cast
#include "EditorMath.h"
#include "..\script.h"
#include "..\UI\Drawing.h"
#include "..\UI\UIUtil.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// REMOVE_MODEL_HIDE / REMOVE_FORCED_OBJECT are undecoded in the native DB (Any
// params), so floats must be passed by bit pattern, not value-converted to int.
static Any SFloatArg(float f)
{
	Any a = 0;
	*reinterpret_cast<float*>(&a) = f;
	return a;
}

bool CSceneEditor::StreamModel(Hash model)
{
	if (!STREAMING::IS_MODEL_IN_CDIMAGE(model) || !STREAMING::IS_MODEL_VALID(model))
		return false;

	STREAMING::REQUEST_MODEL(model, false);
	DWORD start = GetTickCount();
	while (!STREAMING::HAS_MODEL_LOADED(model)) {
		if (GetTickCount() - start > 5000) return false;
		WAIT(10);
	}
	return true;
}

// Active-view ray (works under the Photo Mode free cam, an editor cam or the
// gameplay cam) from the lens forward by `dist` metres.
bool CSceneEditor::ViewRay(Vector3& outStart, Vector3& outEnd, float dist)
{
	outStart = CAM::GET_FINAL_RENDERED_CAM_COORD();
	Vector3 fwd = EMath::RotationToDirection(CAM::GET_FINAL_RENDERED_CAM_ROT(2));
	outEnd = EMath::Add(outStart, EMath::Scale(fwd, dist));
	return true;
}

SceneObject* CSceneEditor::Get(int index)
{
	if (index < 0 || index >= (int)Objects.size()) return nullptr;
	return &Objects[index];
}

// ---------------------------------------------------------------------------
// Spawning / lifetime
// ---------------------------------------------------------------------------

int CSceneEditor::SpawnObject(const std::string& name)
{
	Hash model = MISC::GET_HASH_KEY(name.c_str());
	if (!StreamModel(model)) return -1;

	// Drop ~3 m ahead of the lens, ground-snapped.
	Vector3 camPos = CAM::GET_FINAL_RENDERED_CAM_COORD();
	Vector3 fwd = EMath::RotationToDirection(CAM::GET_FINAL_RENDERED_CAM_ROT(2));
	Vector3 pos = EMath::Add(camPos, EMath::Scale(fwd, 3.0f));

	float groundZ = pos.z;
	if (MISC::GET_GROUND_Z_FOR_3D_COORD(pos.x, pos.y, pos.z + 2.0f, &groundZ, false))
		pos.z = groundZ;

	Object obj = OBJECT::CREATE_OBJECT(model, pos.x, pos.y, pos.z, false, false, false, false, false);
	STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
	if (obj == 0) return -1;

	SceneObject o;
	o.handle = obj;
	o.model = model;
	o.name = name;
	o.pos = pos;
	o.rot = Vector3{ 0.0f, 0.0f, 0.0f };
	o.frozen = true;
	o.collision = false;

	ENTITY::SET_ENTITY_COLLISION(obj, false, false);
	ENTITY::FREEZE_ENTITY_POSITION(obj, true);
	ApplyTransform(o);

	Objects.push_back(o);
	Selected = (int)Objects.size() - 1;
	return Selected;
}

int CSceneEditor::DuplicateObject(int index)
{
	SceneObject* src = Get(index);
	if (!src) return -1;

	if (!StreamModel(src->model)) return -1;
	// Offset the copy slightly so it isn't hidden inside the original.
	Vector3 pos = src->pos; pos.x += 0.5f; pos.y += 0.5f;

	Object obj = OBJECT::CREATE_OBJECT(src->model, pos.x, pos.y, pos.z, false, false, false, false, false);
	STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(src->model);
	if (obj == 0) return -1;

	SceneObject o = *src;
	o.handle = obj;
	o.pos = pos;

	ENTITY::SET_ENTITY_COLLISION(obj, o.collision, o.collision);
	ENTITY::FREEZE_ENTITY_POSITION(obj, o.frozen);
	ApplyTransform(o);

	Objects.push_back(o);
	Selected = (int)Objects.size() - 1;
	return Selected;
}

void CSceneEditor::DeleteObject(int index)
{
	SceneObject* o = Get(index);
	if (!o) return;
	if (o->handle != 0 && ENTITY::DOES_ENTITY_EXIST(o->handle)) {
		Object h = o->handle;
		OBJECT::DELETE_OBJECT(&h);
	}
	Objects.erase(Objects.begin() + index);
	if (Selected >= (int)Objects.size()) Selected = (int)Objects.size() - 1;
}

void CSceneEditor::DeleteAllObjects()
{
	while (!Objects.empty()) DeleteObject((int)Objects.size() - 1);
	Selected = -1;
}

void CSceneEditor::ValidateObjects()
{
	for (int i = (int)Objects.size() - 1; i >= 0; i--) {
		if (Objects[i].handle == 0 || !ENTITY::DOES_ENTITY_EXIST(Objects[i].handle)) {
			Objects.erase(Objects.begin() + i);
			if (Selected == i) Selected = -1;
		}
	}
}

// ---------------------------------------------------------------------------
// Transforms
// ---------------------------------------------------------------------------

void CSceneEditor::ApplyTransform(SceneObject& o)
{
	if (o.handle == 0 || !ENTITY::DOES_ENTITY_EXIST(o.handle)) return;
	ENTITY::SET_ENTITY_COORDS(o.handle, o.pos.x, o.pos.y, o.pos.z, false, false, false, false);
	ENTITY::SET_ENTITY_ROTATION(o.handle, o.rot.x, o.rot.y, o.rot.z, 2, true);
}

void CSceneEditor::PlaceAtCurrentView(SceneObject& o)
{
	if (o.handle == 0 || !ENTITY::DOES_ENTITY_EXIST(o.handle)) return;
	Vector3 camPos = CAM::GET_FINAL_RENDERED_CAM_COORD();
	Vector3 fwd = EMath::RotationToDirection(CAM::GET_FINAL_RENDERED_CAM_ROT(2));
	Vector3 pos = EMath::Add(camPos, EMath::Scale(fwd, 3.0f));
	float groundZ = pos.z;
	if (MISC::GET_GROUND_Z_FOR_3D_COORD(pos.x, pos.y, pos.z + 2.0f, &groundZ, false))
		pos.z = groundZ;
	o.pos = pos;
	ApplyTransform(o);
}

void CSceneEditor::NudgeObject(SceneObject& o, float forward, float right, float up)
{
	// Move relative to the object's own heading so "forward" is where it faces.
	float h = o.rot.z * EMath::DEG2RAD;
	Vector3 f{ -sinf(h), cosf(h), 0.0f };
	Vector3 r{ cosf(h), sinf(h), 0.0f };
	o.pos = EMath::Add(o.pos, EMath::Scale(f, forward));
	o.pos = EMath::Add(o.pos, EMath::Scale(r, right));
	o.pos.z += up;
	ApplyTransform(o);
}

void CSceneEditor::RotateObject(SceneObject& o, float dPitch, float dRoll, float dYaw)
{
	o.rot.x += dPitch;
	o.rot.y += dRoll;
	o.rot.z += dYaw;
	o.rot.z = EMath::NormalizeAngle(o.rot.z);
	ApplyTransform(o);
}

void CSceneEditor::SetHeading(SceneObject& o, float heading)
{
	o.rot.z = heading;
	ApplyTransform(o);
}

void CSceneEditor::SnapToGround(SceneObject& o)
{
	float groundZ = o.pos.z;
	if (MISC::GET_GROUND_Z_FOR_3D_COORD(o.pos.x, o.pos.y, o.pos.z + 2.0f, &groundZ, false)) {
		o.pos.z = groundZ;
		ApplyTransform(o);
	}
}

void CSceneEditor::SetFrozen(SceneObject& o, bool frozen)
{
	o.frozen = frozen;
	if (ENTITY::DOES_ENTITY_EXIST(o.handle))
		ENTITY::FREEZE_ENTITY_POSITION(o.handle, frozen);
}

void CSceneEditor::SetCollision(SceneObject& o, bool on)
{
	o.collision = on;
	if (ENTITY::DOES_ENTITY_EXIST(o.handle))
		ENTITY::SET_ENTITY_COLLISION(o.handle, on, on);
}

// ---------------------------------------------------------------------------
// Temporary YMAP / world edits
// ---------------------------------------------------------------------------

void CSceneEditor::HideMapModel(Hash model, const Vector3& pos, float radius, const std::string& label)
{
	if (model == 0) return;
	ENTITY::CREATE_MODEL_HIDE(pos.x, pos.y, pos.z, radius, model, TRUE);
	YmapEdit e;
	e.type = YmapEdit::HIDE; e.model = model; e.pos = pos; e.radius = radius; e.label = label;
	Edits.push_back(e);
}

bool CSceneEditor::HideEntityUnderView(float radius)
{
	Vector3 from, to;
	ViewRay(from, to, 80.0f);

	// Synchronous probe against map/objects; ignore peds/the player.
	ScrHandle probe = SHAPETEST::START_EXPENSIVE_SYNCHRONOUS_SHAPE_TEST_LOS_PROBE(
		from.x, from.y, from.z, to.x, to.y, to.z, -1, 0, 7);
	BOOL hit = FALSE; Vector3 endCoords{}, normal{}; Entity entityHit = 0;
	SHAPETEST::GET_SHAPE_TEST_RESULT(probe, &hit, &endCoords, &normal, &entityHit);

	if (!hit || entityHit == 0 || !ENTITY::DOES_ENTITY_EXIST(entityHit))
		return false;

	Hash model = ENTITY::GET_ENTITY_MODEL(entityHit);
	if (model == 0) return false;

	std::string label = "Hidden @ view";
	HideMapModel(model, endCoords, radius, label);
	return true;
}

void CSceneEditor::SwapMapModel(Hash original, Hash replacement, const Vector3& pos, float radius, const std::string& label)
{
	if (original == 0 || replacement == 0) return;
	ENTITY::CREATE_MODEL_SWAP(pos.x, pos.y, pos.z, radius, original, replacement, TRUE);
	YmapEdit e;
	e.type = YmapEdit::SWAP; e.model = original; e.newModel = replacement;
	e.pos = pos; e.radius = radius; e.label = label;
	Edits.push_back(e);
}

void CSceneEditor::ForceMapObject(Hash model, const Vector3& pos, const std::string& label)
{
	if (model == 0) return;
	ENTITY::CREATE_FORCED_OBJECT(pos.x, pos.y, pos.z, 0, model, TRUE);
	YmapEdit e;
	e.type = YmapEdit::FORCE; e.model = model; e.pos = pos; e.label = label;
	Edits.push_back(e);
}

static void UndoEdit(const YmapEdit& e)
{
	switch (e.type) {
	case YmapEdit::HIDE:
		// Undecoded Any params: pass coords/radius by bit pattern.
		ENTITY::REMOVE_MODEL_HIDE(SFloatArg(e.pos.x), SFloatArg(e.pos.y), SFloatArg(e.pos.z),
			SFloatArg(e.radius), e.model, FALSE);
		break;
	case YmapEdit::SWAP:
		ENTITY::REMOVE_MODEL_SWAP(e.pos.x, e.pos.y, e.pos.z, e.radius, e.model, e.newModel, FALSE);
		break;
	case YmapEdit::FORCE:
		ENTITY::REMOVE_FORCED_OBJECT(SFloatArg(e.pos.x), SFloatArg(e.pos.y), SFloatArg(e.pos.z),
			SFloatArg(e.radius), e.model);
		break;
	}
}

void CSceneEditor::RemoveEdit(int index)
{
	if (index < 0 || index >= (int)Edits.size()) return;
	UndoEdit(Edits[index]);
	Edits.erase(Edits.begin() + index);
}

void CSceneEditor::RestoreAllEdits()
{
	for (const auto& e : Edits) UndoEdit(e);
	Edits.clear();
}

// ---------------------------------------------------------------------------
// Per-frame upkeep + gizmo
// ---------------------------------------------------------------------------

void CSceneEditor::Tick()
{
	// Drop entries whose entity vanished (e.g. streamed out). Placed objects are
	// frozen at spawn time and stay put, so no per-frame re-assert is needed.
	ValidateObjects();
}

void CSceneEditor::DrawGizmo()
{
	SceneObject* o = GetSelected();
	if (!o || !ENTITY::DOES_ENTITY_EXIST(o->handle)) return;

	// Project the eight bounding-box corners and tick each one - a screen-space
	// selection box (RDR2 exposes no 3D debug-line native, same constraint the
	// camera markers and light gizmos work around).
	Vector3 mn{}, mx{};
	MISC::GET_MODEL_DIMENSIONS(o->model, &mn, &mx);

	const float lx[2] = { mn.x, mx.x };
	const float ly[2] = { mn.y, mx.y };
	const float lz[2] = { mn.z, mx.z };

	float cx = 0.0f, cy = 0.0f;
	int projected = 0;
	for (int i = 0; i < 2; i++)
		for (int j = 0; j < 2; j++)
			for (int k = 0; k < 2; k++) {
				Vector3 w = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(o->handle, lx[i], ly[j], lz[k]);
				float sx = 0.0f, sy = 0.0f;
				if (!GRAPHICS::GET_SCREEN_COORD_FROM_WORLD_COORD(w.x, w.y, w.z, &sx, &sy)) continue;
				// corner tick
				GRAPHICS::DRAW_RECT(sx, sy, 0.006f, 0.0010f, 80, 200, 255, 230, false, false);
				GRAPHICS::DRAW_RECT(sx, sy, 0.0010f, 0.011f, 80, 200, 255, 230, false, false);
				cx += sx; cy += sy; projected++;
			}

	if (projected == 0) return;
	cx /= projected; cy /= projected;

	// Center bracket + label so the selection reads clearly.
	GRAPHICS::DRAW_RECT(cx, cy - 0.020f, 0.022f, 0.0028f, 80, 200, 255, 200, false, false);
	GRAPHICS::DRAW_RECT(cx, cy + 0.020f, 0.022f, 0.0028f, 80, 200, 255, 200, false, false);
	GRAPHICS::DRAW_RECT(cx - 0.012f, cy, 0.0028f, 0.040f, 80, 200, 255, 200, false, false);
	GRAPHICS::DRAW_RECT(cx + 0.012f, cy, 0.0028f, 0.040f, 80, 200, 255, 200, false, false);

	std::string label = o->name + "  (#" + std::to_string(Selected + 1) + ")";
	Drawing::DrawFormattedText(label, Font::Body, 255, 255, 255, 225, Alignment::Center, 16,
		cx * SCREEN_WIDTH, (cy - 0.050f) * SCREEN_HEIGHT);
}

// ---------------------------------------------------------------------------
// Teardown
// ---------------------------------------------------------------------------

void CSceneEditor::DeleteAllActors()
{
	for (Ped p : Actors) g_DirectorMode.DeleteNPCByHandle(p);
	Actors.clear();
}

void CSceneEditor::RevertAll()
{
	DeleteAllObjects();
	DeleteAllActors();
	RestoreAllEdits();
}
