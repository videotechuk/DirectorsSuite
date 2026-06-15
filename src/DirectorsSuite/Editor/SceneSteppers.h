// Director's Suite - shared X/Y/Z value-stepper helpers for the Scene Editor.
//
// The Scene Editor positions everything (objects, actors and the free lights) by
// direct value: each axis shows its live value and is nudged by a selectable
// step. These helpers are header-only so the object/actor pages (ScenePhotoMenus
// .cpp) and the lighting page (PhotoMode.cpp) share one implementation.

#pragma once
#include "..\UI\Menu.hpp"
#include <vector>
#include <string>
#include <functional>
#include <cstdio>

namespace SceneStep
{
	inline const float PosSteps[] = { 0.05f, 0.1f, 0.25f, 0.5f, 1.0f, 5.0f };
	inline const float RotSteps[] = { 1.0f, 5.0f, 15.0f, 45.0f, 90.0f };
	inline int PosStepIdx = 2; // 0.25 m
	inline int RotStepIdx = 2; // 15 deg

	inline float MoveStep() { return PosSteps[PosStepIdx]; }
	inline float RotStep()  { return RotSteps[RotStepIdx]; }

	inline std::string Fmt(float v, int dec) { char b[40]; sprintf_s(b, "%.*f", dec, v); return b; }
	inline int CurIdx() { return g_Menu->GetSelectedOption()->As<VectorOption*>()->GetVectorIndex(); }

	// A live-value stepper. onAdjust(dir) applies the change (dir = +1 right /
	// -1 left) and returns the new value to display. Backed by the menu's
	// VectorOption as a 1-element list, so it behaves as a pure +/- stepper.
	inline VectorOption* AddStepper(Submenu* sub, const std::string& label, const std::string& footer,
		float initial, int decimals, std::function<float(int)> onAdjust)
	{
		VectorOption* opt = sub->AddVectorOption(label, footer, std::vector<std::string>{ "0" }, [onAdjust, decimals] {
			VectorOption* v = g_Menu->GetSelectedOption()->As<VectorOption*>();
			bool wentLeft = false, wentRight = false;
			v->GetVectorDirection(&wentLeft, &wentRight);
			v->RightText = Fmt(onAdjust(wentRight ? +1 : -1), decimals);
		});
		opt->RightText = Fmt(initial, decimals);
		return opt;
	}

	// Adds the shared Move-Step / Rotate-Step selectors to a page.
	inline void AddStepSelectors(Submenu* sub, bool withRotation = true)
	{
		std::vector<std::string> ps; for (float s : PosSteps) ps.push_back(Fmt(s, 2));
		sub->AddVectorOption("Move Step (m)", "Increment for the X/Y/Z position values", ps, [] {
			PosStepIdx = CurIdx();
		})->SetVectorIndex(PosStepIdx);

		if (withRotation) {
			std::vector<std::string> rs; for (float s : RotSteps) rs.push_back(Fmt(s, 0));
			sub->AddVectorOption("Rotate Step (deg)", "Increment for the rotation values", rs, [] {
				RotStepIdx = CurIdx();
			})->SetVectorIndex(RotStepIdx);
		}
	}
}
