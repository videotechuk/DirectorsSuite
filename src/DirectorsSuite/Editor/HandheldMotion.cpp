#include "HandheldMotion.h"
#include <cmath>

namespace
{
	constexpr float TWO_PI = 6.28318530718f;

	// Per-style tuning. Amplitudes are at intensity 1.0; the slider scales them.
	struct StyleDef
	{
		const char* name;
		float swayPos;    // m   - lateral / forward positional drift
		float swayRot;    // deg - slow yaw/pitch wander
		float roll;       // deg - left/right cant (the handheld giveaway)
		float breathe;    // m   - vertical breathing bob amplitude
		float breatheHz;  // Hz  - breathing rate
		float jitter;     // deg - high-frequency micro tremor
		float walkBounce; // m   - per-step vertical bounce (0 = stationary)
		float walkHz;     // Hz  - step cadence
	};

	const StyleDef kStyles[] = {
		//  name                 swayPos swayRot roll  breathe bHz   jitter walk   wHz
		{ "Off",                  0.000f, 0.00f, 0.00f, 0.000f, 0.00f, 0.00f, 0.000f, 0.0f },
		{ "Phone (Steady)",       0.010f, 0.45f, 0.35f, 0.012f, 0.24f, 0.05f, 0.000f, 0.0f },
		{ "Phone (Vlog)",         0.022f, 0.80f, 0.70f, 0.018f, 0.27f, 0.10f, 0.015f, 1.6f },
		{ "Handheld Walk",        0.032f, 1.05f, 1.00f, 0.018f, 0.27f, 0.13f, 0.045f, 1.8f },
		{ "Run & Gun",            0.055f, 1.85f, 1.55f, 0.020f, 0.30f, 0.26f, 0.075f, 2.6f },
	};
	const int kStyleCount = (int)(sizeof(kStyles) / sizeof(kStyles[0]));

	// Small fractal sum-of-sines in [-1, 1] (roughly): three octaves at
	// non-integer frequency ratios so the result never repeats on a short loop.
	// `seed` decorrelates the axes from one another.
	float Fbm(float t, float baseHz, float seed)
	{
		float w = TWO_PI * baseHz;
		float v = 0.60f * sinf(t * w          + seed)
		        + 0.30f * sinf(t * w * 2.137f  + seed * 1.7f)
		        + 0.15f * sinf(t * w * 4.413f  + seed * 2.9f);
		return v; // amplitude ~[-1.05, 1.05], close enough to unit
	}
}

namespace HandheldMotion
{
	int StyleCount() { return kStyleCount; }

	const char* StyleName(int style)
	{
		if (style < 0 || style >= kStyleCount) return "Off";
		return kStyles[style].name;
	}

	Offsets Evaluate(int style, float intensity, float t)
	{
		Offsets out{};
		if (style <= 0 || style >= kStyleCount || intensity <= 0.0f) return out;

		const StyleDef& s = kStyles[style];
		float k = intensity;

		// --- positional sway (camera-local) ---
		out.posLocal.x = s.swayPos * Fbm(t, 0.70f, 1.0f) * k;          // left/right
		out.posLocal.y = s.swayPos * 0.6f * Fbm(t, 0.52f, 2.0f) * k;   // forward/back (subtler)

		// vertical: breathing + (optional) per-step walk bounce
		float vert = s.breathe * sinf(t * TWO_PI * s.breatheHz);
		if (s.walkBounce > 0.0f) {
			// |sin| gives a double-bounce per stride, like real footfalls
			vert += s.walkBounce * fabsf(sinf(t * 3.14159265f * s.walkHz));
		}
		out.posLocal.z = vert * k;

		// --- rotational drift ---
		// slow wander + a faster micro-tremor layered on pitch and yaw
		out.rot.x = (s.swayRot * Fbm(t, 0.60f, 3.0f) + s.jitter * Fbm(t, 5.10f, 7.0f)) * k; // pitch
		out.rot.z = (s.swayRot * Fbm(t, 0.55f, 5.0f) + s.jitter * Fbm(t, 5.30f, 8.0f)) * k; // yaw
		out.rot.y = s.roll * Fbm(t, 0.40f, 4.0f) * k;                                       // roll

		// walk adds a rhythmic roll/pitch sway in step with the footfalls
		if (s.walkBounce > 0.0f) {
			out.rot.y += s.roll * 0.5f * sinf(t * TWO_PI * s.walkHz * 0.5f) * k;
			out.rot.x += s.swayRot * 0.3f * sinf(t * TWO_PI * s.walkHz * 0.5f + 1.2f) * k;
		}

		return out;
	}
}
