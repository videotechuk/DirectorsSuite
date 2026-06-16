// Director's Suite - lossless / HDR screenshot capture.
//
// Feasibility (investigated): ScriptHookRDR2 exposes no swapchain, and the
// engine's BEGIN_TAKE_HIGH_QUALITY_PHOTO path produces compressed in-game
// photos with overlays. A true lossless grab therefore uses Windows' own
// DXGI **Desktop Duplication** API: we stand up an independent D3D11 device,
// duplicate the monitor output, and encode the frame with WIC. This needs no
// hook into the game's renderer, captures exactly what is on screen at the
// display's native bit depth, and - when the display is in HDR mode -
// receives a 16-bit float buffer that we save losslessly as JPEG-XR (.jxr),
// the standard HDR-capable container. SDR frames save as lossless 8/10-bit
// PNG.
//
// Everything runs on a detached worker thread and every COM call is checked,
// so a capture failure reports an error and never destabilises the game.

#pragma once
#include <string>
#include <vector>

namespace ScreenCapture
{
	enum eResult
	{
		CAP_OK_PNG = 0,   // saved lossless SDR PNG
		CAP_OK_HDR,       // saved HDR JPEG-XR
		CAP_FAILED,       // duplication/encode error (details in LastError)
		CAP_BUSY,         // a capture is already running
	};

	// Kicks off a capture on a worker thread. Returns immediately; the result
	// is delivered via Poll() so the caller can show an on-screen message.
	// `cropAspect` is the target width:height for the saved file (e.g. 1.0 for
	// 1:1, 9.0/16.0 for a phone shot); the grabbed frame is centre-cropped to it
	// so the chosen aspect ratio is baked into the image. 0 keeps the full frame.
	void RequestCapture(float cropAspect = 0.0f);

	// Call every frame. Returns true once when a capture finishes, filling
	// outResult and outPath; returns false otherwise.
	bool Poll(int& outResult, std::string& outPath);

	// True while a capture worker is in flight (used to hold UI hidden).
	bool IsCapturing();

	const std::string& LastError();
	const std::string& OutputDir(); // Captured Screenshots next to RDR2.exe

	// --- Experimental tiled super-resolution capture ---
	// A synchronous multi-frame grab sequence driven from the script thread:
	// BeginSequence() once, GrabFrame() per tile (forced 8-bit BGRA), then
	// EndSequence(). SaveBGRAImage() writes a stitched BGRA buffer to lossless PNG.
	bool BeginSequence();
	bool GrabFrame(std::vector<unsigned char>& outBGRA, int& outW, int& outH);
	void EndSequence();
	bool SaveBGRAImage(const std::vector<unsigned char>& bgra, int w, int h, std::string& outPath);
}
