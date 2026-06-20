// Director's Suite - shared HDR -> SDR tonemapping.
//
// The HDR capture buffer (and the .jxr we save from it) is scRGB: linear, with
// 1.0 == 80 nits, carrying ABSOLUTE luminance. To show or export it on an SDR
// display we map a fixed reference paper-white to display white (so day stays
// bright and night stays dark - no scene-adaptive auto-exposure), roll off the
// highlights with an extended Reinhard curve, and apply the sRGB gamma.
//
// Both the captures viewer (CaptureGallery) and the HDR->PNG export
// (ScreenCapture) use this, so the on-screen preview matches the saved file.
// kPaperWhiteNits is the single brightness knob.

#pragma once
#include <cmath>
#include <cstring>

namespace HdrTonemap
{
	// HDR white level the shot was produced at (RDR2's in-game paper white,
	// ~200 nits by default). Raise if shots look too bright, lower if too dark.
	constexpr float kPaperWhiteNits = 200.0f;

	// IEEE-754 half (binary16) -> float, for raw R16G16B16A16_FLOAT buffers.
	inline float HalfToFloat(unsigned short h)
	{
		unsigned int sign = (h >> 15) & 1u;
		unsigned int exp = (h >> 10) & 0x1Fu;
		unsigned int mant = h & 0x3FFu;
		unsigned int f;
		if (exp == 0) {
			if (mant == 0) f = sign << 31;
			else {
				exp = 127 - 15 + 1;
				while ((mant & 0x400u) == 0) { mant <<= 1; exp--; }
				mant &= 0x3FFu;
				f = (sign << 31) | (exp << 23) | (mant << 13);
			}
		}
		else if (exp == 0x1Fu) f = (sign << 31) | (0xFFu << 23) | (mant << 13);
		else f = (sign << 31) | ((exp - 15 + 127) << 23) | (mant << 13);
		float out; std::memcpy(&out, &f, 4); return out;
	}

	inline float Luma(float r, float g, float b) { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }

	// Linear -> display sRGB OETF (gamma), clamped to [0,1].
	inline float SrgbEncode(float c)
	{
		if (c < 0.0f) c = 0.0f; if (c > 1.0f) c = 1.0f;
		return c <= 0.0031308f ? 12.92f * c : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
	}

	inline unsigned char To8(float c)
	{
		int v = (int)(c * 255.0f + 0.5f);
		return v < 0 ? 0 : (v > 255 ? 255 : (unsigned char)v);
	}

	// scRGB-linear RGB -> 8-bit sRGB, written as B,G,R (the byte order both the
	// PNG encoder and the layered overlay want). Fixed exposure + extended
	// Reinhard highlight roll-off, preserving colour ratios.
	inline void ScrgbToBgr8(float r, float g, float b,
		unsigned char& outB, unsigned char& outG, unsigned char& outR)
	{
		const float exposure = 80.0f / kPaperWhiteNits;   // reference white -> display 1.0
		const float Lw = 4.0f, Lw2 = Lw * Lw;             // ~4x white rolls to display white

		r *= exposure; g *= exposure; b *= exposure;
		if (r < 0.0f) r = 0.0f; if (g < 0.0f) g = 0.0f; if (b < 0.0f) b = 0.0f;

		float lum = Luma(r, g, b);
		float tl = lum * (1.0f + lum / Lw2) / (1.0f + lum); // extended Reinhard
		float ratio = (lum > 1e-6f) ? (tl / lum) : 0.0f;
		if (ratio > 2.0f) ratio = 2.0f;                     // guard against oversaturation

		outB = To8(SrgbEncode(b * ratio));
		outG = To8(SrgbEncode(g * ratio));
		outR = To8(SrgbEncode(r * ratio));
	}
}
