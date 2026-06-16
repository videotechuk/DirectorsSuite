#include "ScreenCapture.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_5.h>
#include <wincodec.h>
#include <atomic>
#include <thread>
#include <string>
#include <vector>
#include <cstring>
#include <cmath>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace
{
	std::atomic<bool> s_busy{ false };
	std::atomic<bool> s_done{ false };
	std::atomic<int>  s_result{ ScreenCapture::CAP_FAILED };
	std::atomic<float> s_cropAspect{ 0.0f }; // target W:H for the saved file (0 = full frame)
	std::string s_lastError;
	std::string s_lastPath;
	std::string s_outDir;
	std::string s_hdrPngDir;        // "Converted HDR Screenshots" (SDR PNG copies)
	CRITICAL_SECTION s_cs;
	bool s_csInit = false;

	template<typename T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

	std::string TimeStampName(const char* ext)
	{
		SYSTEMTIME st; GetLocalTime(&st);
		char buf[128];
		sprintf_s(buf, "DirectorScreenshot_%04d-%02d-%02d_%02d-%02d-%02d-%03d.%s",
			st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, ext);
		return buf;
	}

	std::wstring Widen(const std::string& s)
	{
		int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
		std::wstring w(n ? n - 1 : 0, L'\0');
		if (n) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
		return w;
	}

	void EnsureOutDir()
	{
		if (!s_outDir.empty()) return;
		char path[MAX_PATH];
		GetModuleFileNameA(nullptr, path, MAX_PATH);
		std::string dir(path);
		size_t slash = dir.find_last_of("\\/");
		dir = (slash == std::string::npos) ? "." : dir.substr(0, slash);
		s_outDir = dir + "\\Captured Screenshots";
		CreateDirectoryA(s_outDir.c_str(), nullptr);
	}

	// Sibling folder for the SDR PNG copies auto-converted from HDR (.jxr) shots.
	void EnsureHdrPngDir()
	{
		if (!s_hdrPngDir.empty()) return;
		char path[MAX_PATH];
		GetModuleFileNameA(nullptr, path, MAX_PATH);
		std::string dir(path);
		size_t slash = dir.find_last_of("\\/");
		dir = (slash == std::string::npos) ? "." : dir.substr(0, slash);
		s_hdrPngDir = dir + "\\Converted HDR Screenshots";
		CreateDirectoryA(s_hdrPngDir.c_str(), nullptr);
	}

	std::string BaseNameNoExt(const std::string& full)
	{
		size_t slash = full.find_last_of("\\/");
		std::string name = (slash == std::string::npos) ? full : full.substr(slash + 1);
		size_t dot = name.find_last_of('.');
		return (dot == std::string::npos) ? name : name.substr(0, dot);
	}

	// IEEE-754 half (binary16) -> float, for the R16G16B16A16_FLOAT HDR buffer.
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
		float out; memcpy(&out, &f, 4); return out;
	}

	// Linear -> display sRGB OETF (gamma). The tone map below works in linear and
	// hands display-ready [0,1] values here for 8-bit quantisation.
	inline float SrgbEncode(float c)
	{
		if (c < 0.0f) c = 0.0f; if (c > 1.0f) c = 1.0f;
		return c <= 0.0031308f ? 12.92f * c : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
	}

	inline float Luma(float r, float g, float b) { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }

	inline BYTE To8(float c) { int v = (int)(c * 255.0f + 0.5f); return v < 0 ? 0 : (v > 255 ? 255 : (BYTE)v); }

	// Tonemap the HDR float frame (cropped to the same rect as the .jxr) to an
	// SDR PNG in "Converted HDR Screenshots". Best-effort: the .jxr is the primary
	// artifact, so a failure here never fails the capture.
	//
	// The duplication buffer is scRGB (linear, 1.0 = 80 nits) and carries ABSOLUTE
	// luminance, so we must NOT auto-expose: scene-adaptive exposure normalises
	// every shot to the same brightness and lifts night scenes to look like day.
	// Instead map a fixed reference paper-white to display white (so day stays
	// bright, night stays dark - matching how Windows tonemaps the .jxr), then
	// roll off highlights with an extended Reinhard curve and apply sRGB gamma.
	//
	// kPaperWhiteNits is the one knob: it is the HDR white level the shot was
	// produced at (RDR2's in-game paper white, ~200 nits by default). Raise it if
	// shots come out too bright, lower it if too dark.
	void SaveHdrAsPng(IWICImagingFactory* factory, const D3D11_MAPPED_SUBRESOURCE& map,
		UINT cropX, UINT cropY, UINT cropW, UINT cropH, const std::string& jxrPath)
	{
		if (!factory || cropW == 0 || cropH == 0) return;

		const BYTE* base = static_cast<const BYTE*>(map.pData);
		auto RowAt = [&](UINT y) {
			return reinterpret_cast<const unsigned short*>(base + (size_t)(cropY + y) * map.RowPitch) + (size_t)cropX * 4;
		};

		// Fixed exposure: scRGB 1.0 == 80 nits, so dividing by (paperWhite/80)
		// puts reference white at display 1.0 and keeps absolute brightness.
		const float kPaperWhiteNits = 200.0f;
		const float exposure = 80.0f / kPaperWhiteNits;

		// Expose, compress highlights on luminance (preserving colour ratios),
		// gamma-encode.
		const float Lw = 4.0f;            // white point: ~4x reference white rolls to display white
		const float Lw2 = Lw * Lw;
		std::vector<BYTE> bgra((size_t)cropW * cropH * 4);
		for (UINT y = 0; y < cropH; ++y) {
			const unsigned short* src = RowAt(y);
			BYTE* dst = bgra.data() + (size_t)y * cropW * 4;
			for (UINT x = 0; x < cropW; ++x) {
				float r = HalfToFloat(src[0]) * exposure; if (r < 0.0f) r = 0.0f;
				float g = HalfToFloat(src[1]) * exposure; if (g < 0.0f) g = 0.0f;
				float b = HalfToFloat(src[2]) * exposure; if (b < 0.0f) b = 0.0f;
				float lum = Luma(r, g, b);
				float tl = lum * (1.0f + lum / Lw2) / (1.0f + lum); // extended Reinhard
				float ratio = (lum > 1e-6f) ? (tl / lum) : 0.0f;
				if (ratio > 2.0f) ratio = 2.0f;                    // guard against oversaturation
				dst[0] = To8(SrgbEncode(b * ratio)); // B
				dst[1] = To8(SrgbEncode(g * ratio)); // G
				dst[2] = To8(SrgbEncode(r * ratio)); // R
				dst[3] = 255;
				src += 4; dst += 4;
			}
		}

		EnsureHdrPngDir();
		std::wstring wpath = Widen(s_hdrPngDir + "\\" + BaseNameNoExt(jxrPath) + ".png");

		IWICStream* st = nullptr; IWICBitmapEncoder* enc = nullptr; IWICBitmapFrameEncode* fr = nullptr;
		do {
			if (FAILED(factory->CreateStream(&st))) break;
			if (FAILED(st->InitializeFromFilename(wpath.c_str(), GENERIC_WRITE))) break;
			if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc))) break;
			if (FAILED(enc->Initialize(st, WICBitmapEncoderNoCache))) break;
			if (FAILED(enc->CreateNewFrame(&fr, nullptr))) break;
			if (FAILED(fr->Initialize(nullptr))) break;
			if (FAILED(fr->SetSize(cropW, cropH))) break;
			WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
			fr->SetPixelFormat(&pf);
			if (FAILED(fr->WritePixels(cropH, cropW * 4, (UINT)bgra.size(), bgra.data()))) break;
			fr->Commit(); enc->Commit();
		} while (false);
		SafeRelease(fr); SafeRelease(enc); SafeRelease(st);
	}

	void SetError(const std::string& msg) { s_lastError = msg; }

	// Encode a mapped staging texture to disk. Float formats -> HDR JPEG-XR,
	// integer formats -> lossless PNG. Returns the eResult code.
	int EncodeFrame(const D3D11_MAPPED_SUBRESOURCE& map, UINT w, UINT h, DXGI_FORMAT fmt)
	{
		bool isHDR = (fmt == DXGI_FORMAT_R16G16B16A16_FLOAT);

		IWICImagingFactory* factory = nullptr;
		HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&factory));
		if (FAILED(hr)) { SetError("WIC factory failed"); return ScreenCapture::CAP_FAILED; }

		EnsureOutDir();
		std::string path = s_outDir + "\\" + TimeStampName(isHDR ? "jxr" : "png");
		std::wstring wpath = Widen(path);

		IWICStream* stream = nullptr;
		IWICBitmapEncoder* encoder = nullptr;
		IWICBitmapFrameEncode* frame = nullptr;
		int rc = ScreenCapture::CAP_FAILED;

		do {
			if (FAILED(factory->CreateStream(&stream))) { SetError("WIC stream"); break; }
			if (FAILED(stream->InitializeFromFilename(wpath.c_str(), GENERIC_WRITE))) { SetError("file open"); break; }

			GUID container = isHDR ? GUID_ContainerFormatWmp : GUID_ContainerFormatPng;
			if (FAILED(factory->CreateEncoder(container, nullptr, &encoder))) { SetError("WIC encoder"); break; }
			if (FAILED(encoder->Initialize(stream, WICBitmapEncoderNoCache))) { SetError("encoder init"); break; }
			if (FAILED(encoder->CreateNewFrame(&frame, nullptr))) { SetError("frame create"); break; }
			if (FAILED(frame->Initialize(nullptr))) { SetError("frame init"); break; }
			// Source pixel format from the duplicated buffer
			WICPixelFormatGUID srcFmt;
			switch (fmt) {
				case DXGI_FORMAT_R16G16B16A16_FLOAT: srcFmt = GUID_WICPixelFormat64bppRGBAHalf; break;
				case DXGI_FORMAT_R10G10B10A2_UNORM:  srcFmt = GUID_WICPixelFormat32bppR10G10B10A2; break;
				case DXGI_FORMAT_B8G8R8A8_UNORM:     srcFmt = GUID_WICPixelFormat32bppBGRA; break;
				case DXGI_FORMAT_R8G8B8A8_UNORM:     srcFmt = GUID_WICPixelFormat32bppRGBA; break;
				default:                             srcFmt = GUID_WICPixelFormat32bppBGRA; break;
			}

			// Centre-crop to the chosen aspect ratio so "Square 1:1", "9:16" etc.
			// produce a file that is actually that shape - not the full frame with
			// black bars. The math mirrors DrawAspectFrame's centred bars, so the
			// crop keeps exactly the region inside the on-screen guide.
			UINT cropX = 0, cropY = 0, cropW = w, cropH = h;
			float aspect = s_cropAspect.load();
			if (aspect > 0.0001f && w > 0 && h > 0) {
				float screen = (float)w / (float)h;
				if (aspect < screen) {           // taller target -> trim width (pillarbox)
					cropW = (UINT)((float)h * aspect + 0.5f);
					if (cropW < 1) cropW = 1; if (cropW > w) cropW = w;
					cropX = (w - cropW) / 2;
				}
				else if (aspect > screen) {      // wider target -> trim height (letterbox)
					cropH = (UINT)((float)w / aspect + 0.5f);
					if (cropH < 1) cropH = 1; if (cropH > h) cropH = h;
					cropY = (h - cropH) / 2;
				}
			}
			bool cropped = (cropW != w || cropH != h);

			if (FAILED(frame->SetSize(cropW, cropH))) { SetError("set size"); break; }

			WICPixelFormatGUID outFmt = srcFmt;
			frame->SetPixelFormat(&outFmt);

			// Wrap the duplicated pixels in a WIC source, clip to the crop rect,
			// and convert if the encoder cannot take the source format directly.
			IWICBitmap* bmp = nullptr;
			if (FAILED(factory->CreateBitmapFromMemory(w, h, srcFmt, map.RowPitch,
				map.RowPitch * h, (BYTE*)map.pData, &bmp))) { SetError("bmp from mem"); break; }

			IWICBitmapSource*  src = bmp;
			IWICBitmapClipper* clip = nullptr;
			IWICFormatConverter* conv = nullptr;
			HRESULT wr = S_OK;
			do {
				if (cropped) {
					if (FAILED(factory->CreateBitmapClipper(&clip))) { wr = E_FAIL; SetError("clipper"); break; }
					WICRect rc{ (INT)cropX, (INT)cropY, (INT)cropW, (INT)cropH };
					if (FAILED(clip->Initialize(bmp, &rc))) { wr = E_FAIL; SetError("clip init"); break; }
					src = clip;
				}
				if (outFmt != srcFmt) {
					if (FAILED(factory->CreateFormatConverter(&conv))) { wr = E_FAIL; SetError("converter"); break; }
					if (FAILED(conv->Initialize(src, outFmt, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
						wr = E_FAIL; SetError("convert init"); break;
					}
					src = conv;
				}
				wr = frame->WriteSource(src, nullptr);
				if (FAILED(wr)) SetError("write source");
			} while (false);
			SafeRelease(conv); SafeRelease(clip); bmp->Release();
			if (FAILED(wr)) break;

			if (FAILED(frame->Commit())) { SetError("frame commit"); break; }
			if (FAILED(encoder->Commit())) { SetError("encoder commit"); break; }

			s_lastPath = path;
			rc = isHDR ? ScreenCapture::CAP_OK_HDR : ScreenCapture::CAP_OK_PNG;

			// HDR shots also get a tonemapped SDR PNG copy in a sibling folder, so
			// they are viewable anywhere while the lossless .jxr stays the master.
			if (isHDR) SaveHdrAsPng(factory, map, cropX, cropY, cropW, cropH, path);
		} while (false);

		SafeRelease(frame);
		SafeRelease(encoder);
		SafeRelease(stream);
		SafeRelease(factory);
		return rc;
	}

	// One full duplication capture. Self-contained: own D3D11 device.
	int DoCapture()
	{
		HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		bool comInit = SUCCEEDED(hr);

		ID3D11Device* dev = nullptr;
		ID3D11DeviceContext* ctx = nullptr;
		IDXGIOutputDuplication* dupl = nullptr;
		ID3D11Texture2D* staging = nullptr;
		int rc = ScreenCapture::CAP_FAILED;

		do {
			D3D_FEATURE_LEVEL fl;
			if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
				D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx))) {
				SetError("D3D11 device create failed"); break;
			}

			IDXGIDevice* dxgiDev = nullptr;
			if (FAILED(dev->QueryInterface(IID_PPV_ARGS(&dxgiDev)))) { SetError("IDXGIDevice"); break; }
			IDXGIAdapter* adapter = nullptr;
			hr = dxgiDev->GetAdapter(&adapter); dxgiDev->Release();
			if (FAILED(hr)) { SetError("adapter"); break; }

			IDXGIOutput* output = nullptr;
			hr = adapter->EnumOutputs(0, &output); adapter->Release();
			if (FAILED(hr)) { SetError("enum output"); break; }

			IDXGIOutput5* output5 = nullptr;
			hr = output->QueryInterface(IID_PPV_ARGS(&output5)); output->Release();
			if (FAILED(hr)) { SetError("IDXGIOutput5 (needs Win10 1803+)"); break; }

			// Ask for HDR float first, then 10-bit, then 8-bit
			DXGI_FORMAT formats[] = {
				DXGI_FORMAT_R16G16B16A16_FLOAT,
				DXGI_FORMAT_R10G10B10A2_UNORM,
				DXGI_FORMAT_B8G8R8A8_UNORM,
			};
			hr = output5->DuplicateOutput1(dev, 0, _countof(formats), formats, &dupl);
			output5->Release();
			if (FAILED(hr)) { SetError("DuplicateOutput1 failed"); break; }

			// Grab a frame (retry briefly - the first AcquireNextFrame after
			// duplication often times out until the desktop next composites)
			IDXGIResource* res = nullptr;
			DXGI_OUTDUPL_FRAME_INFO info{};
			bool got = false;
			for (int attempt = 0; attempt < 12; attempt++) {
				hr = dupl->AcquireNextFrame(250, &info, &res);
				if (hr == DXGI_ERROR_WAIT_TIMEOUT) { continue; }
				if (FAILED(hr)) { SetError("AcquireNextFrame"); break; }
				// require a real frame with content
				if (info.LastPresentTime.QuadPart == 0 && attempt < 6) {
					res->Release(); res = nullptr;
					dupl->ReleaseFrame();
					continue;
				}
				got = true; break;
			}
			if (!got || !res) { if (res) res->Release(); SetError("no frame acquired"); break; }

			ID3D11Texture2D* frameTex = nullptr;
			hr = res->QueryInterface(IID_PPV_ARGS(&frameTex)); res->Release();
			if (FAILED(hr)) { dupl->ReleaseFrame(); SetError("frame texture"); break; }

			D3D11_TEXTURE2D_DESC desc{};
			frameTex->GetDesc(&desc);

			D3D11_TEXTURE2D_DESC sd = desc;
			sd.Usage = D3D11_USAGE_STAGING;
			sd.BindFlags = 0;
			sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			sd.MiscFlags = 0;
			if (FAILED(dev->CreateTexture2D(&sd, nullptr, &staging))) {
				frameTex->Release(); dupl->ReleaseFrame(); SetError("staging texture"); break;
			}
			ctx->CopyResource(staging, frameTex);
			frameTex->Release();

			D3D11_MAPPED_SUBRESOURCE map{};
			if (FAILED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map))) {
				dupl->ReleaseFrame(); SetError("map staging"); break;
			}

			rc = EncodeFrame(map, desc.Width, desc.Height, desc.Format);

			ctx->Unmap(staging, 0);
			dupl->ReleaseFrame();
		} while (false);

		SafeRelease(staging);
		SafeRelease(dupl);
		SafeRelease(ctx);
		SafeRelease(dev);
		if (comInit) CoUninitialize();
		return rc;
	}
}

// ---------------------------------------------------------------------------
// Experimental tiled super-resolution capture (synchronous; script thread).
// One persistent device + duplication is held for the whole tile sequence,
// forced to 8-bit BGRA so stitching is trivial.
// ---------------------------------------------------------------------------
namespace
{
	ID3D11Device*            s_seqDev = nullptr;
	ID3D11DeviceContext*     s_seqCtx = nullptr;
	IDXGIOutputDuplication*  s_seqDupl = nullptr;
	bool                     s_seqCom = false;
}

namespace ScreenCapture
{
	bool BeginSequence()
	{
		EndSequence(); // ensure clean state
		HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		s_seqCom = SUCCEEDED(hr);

		do {
			D3D_FEATURE_LEVEL fl;
			if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
				D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &s_seqDev, &fl, &s_seqCtx))) {
				SetError("seq: D3D11 device"); break;
			}
			IDXGIDevice* dxgiDev = nullptr;
			if (FAILED(s_seqDev->QueryInterface(IID_PPV_ARGS(&dxgiDev)))) { SetError("seq: IDXGIDevice"); break; }
			IDXGIAdapter* adapter = nullptr;
			hr = dxgiDev->GetAdapter(&adapter); dxgiDev->Release();
			if (FAILED(hr)) { SetError("seq: adapter"); break; }
			IDXGIOutput* output = nullptr;
			hr = adapter->EnumOutputs(0, &output); adapter->Release();
			if (FAILED(hr)) { SetError("seq: output"); break; }
			IDXGIOutput5* output5 = nullptr;
			hr = output->QueryInterface(IID_PPV_ARGS(&output5)); output->Release();
			if (FAILED(hr)) { SetError("seq: IDXGIOutput5"); break; }

			DXGI_FORMAT formats[] = { DXGI_FORMAT_B8G8R8A8_UNORM };
			hr = output5->DuplicateOutput1(s_seqDev, 0, _countof(formats), formats, &s_seqDupl);
			output5->Release();
			if (FAILED(hr)) { SetError("seq: DuplicateOutput1"); break; }
			return true;
		} while (false);

		EndSequence();
		return false;
	}

	bool GrabFrame(std::vector<unsigned char>& outBGRA, int& outW, int& outH)
	{
		outW = outH = 0;
		if (!s_seqDupl || !s_seqDev || !s_seqCtx) return false;

		IDXGIResource* res = nullptr;
		DXGI_OUTDUPL_FRAME_INFO info{};
		HRESULT hr = E_FAIL;
		bool got = false;
		for (int attempt = 0; attempt < 20; attempt++) {
			if (res) { res->Release(); res = nullptr; }
			hr = s_seqDupl->AcquireNextFrame(500, &info, &res);
			if (hr == DXGI_ERROR_WAIT_TIMEOUT) continue;
			if (FAILED(hr)) { SetError("seq: AcquireNextFrame"); return false; }
			got = true; break;
		}
		if (!got || !res) { if (res) res->Release(); SetError("seq: no frame"); return false; }

		ID3D11Texture2D* tex = nullptr;
		hr = res->QueryInterface(IID_PPV_ARGS(&tex)); res->Release();
		if (FAILED(hr)) { s_seqDupl->ReleaseFrame(); SetError("seq: frame tex"); return false; }

		D3D11_TEXTURE2D_DESC desc{}; tex->GetDesc(&desc);
		D3D11_TEXTURE2D_DESC sd = desc;
		sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0;
		sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.MiscFlags = 0;
		ID3D11Texture2D* staging = nullptr;
		if (FAILED(s_seqDev->CreateTexture2D(&sd, nullptr, &staging))) {
			tex->Release(); s_seqDupl->ReleaseFrame(); SetError("seq: staging"); return false;
		}
		s_seqCtx->CopyResource(staging, tex); tex->Release();

		D3D11_MAPPED_SUBRESOURCE map{};
		bool ok = false;
		if (SUCCEEDED(s_seqCtx->Map(staging, 0, D3D11_MAP_READ, 0, &map))) {
			outW = (int)desc.Width; outH = (int)desc.Height;
			outBGRA.resize((size_t)outW * outH * 4);
			for (int y = 0; y < outH; y++) {
				memcpy(&outBGRA[(size_t)y * outW * 4],
					(const BYTE*)map.pData + (size_t)y * map.RowPitch, (size_t)outW * 4);
			}
			s_seqCtx->Unmap(staging, 0);
			ok = true;
		}
		else SetError("seq: map");

		staging->Release();
		s_seqDupl->ReleaseFrame();
		return ok;
	}

	void EndSequence()
	{
		SafeRelease(s_seqDupl);
		SafeRelease(s_seqCtx);
		SafeRelease(s_seqDev);
		if (s_seqCom) { CoUninitialize(); s_seqCom = false; }
	}

	bool SaveBGRAImage(const std::vector<unsigned char>& bgra, int w, int h, std::string& outPath)
	{
		if (w <= 0 || h <= 0 || bgra.size() < (size_t)w * h * 4) return false;

		bool comInit = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
		IWICImagingFactory* factory = nullptr;
		if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
			if (comInit) CoUninitialize(); SetError("sr: WIC factory"); return false;
		}

		EnsureOutDir();
		std::string path = s_outDir + "\\" + TimeStampName("png");
		std::wstring wpath = Widen(path);

		IWICStream* stream = nullptr;
		IWICBitmapEncoder* encoder = nullptr;
		IWICBitmapFrameEncode* frame = nullptr;
		bool ok = false;
		do {
			if (FAILED(factory->CreateStream(&stream))) { SetError("sr: stream"); break; }
			if (FAILED(stream->InitializeFromFilename(wpath.c_str(), GENERIC_WRITE))) { SetError("sr: file"); break; }
			if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))) { SetError("sr: encoder"); break; }
			if (FAILED(encoder->Initialize(stream, WICBitmapEncoderNoCache))) { SetError("sr: enc init"); break; }
			if (FAILED(encoder->CreateNewFrame(&frame, nullptr))) { SetError("sr: frame"); break; }
			if (FAILED(frame->Initialize(nullptr))) { SetError("sr: frame init"); break; }
			if (FAILED(frame->SetSize((UINT)w, (UINT)h))) { SetError("sr: size"); break; }
			WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
			frame->SetPixelFormat(&fmt);
			if (FAILED(frame->WritePixels((UINT)h, (UINT)w * 4, (UINT)bgra.size(), (BYTE*)bgra.data()))) { SetError("sr: write"); break; }
			if (FAILED(frame->Commit())) { SetError("sr: frame commit"); break; }
			if (FAILED(encoder->Commit())) { SetError("sr: enc commit"); break; }
			s_lastPath = path; outPath = path; ok = true;
		} while (false);

		SafeRelease(frame); SafeRelease(encoder); SafeRelease(stream); SafeRelease(factory);
		if (comInit) CoUninitialize();
		return ok;
	}

	void RequestCapture(float cropAspect)
	{
		if (!s_csInit) { InitializeCriticalSection(&s_cs); s_csInit = true; }
		bool expected = false;
		if (!s_busy.compare_exchange_strong(expected, true)) {
			return; // already running
		}
		s_cropAspect.store(cropAspect);
		s_done.store(false);

		std::thread([] {
			// Brief delay so the caller's UI hide takes effect before the grab
			Sleep(120);
			int rc = DoCapture();
			s_result.store(rc);
			s_done.store(true);
			s_busy.store(false);
		}).detach();
	}

	bool Poll(int& outResult, std::string& outPath)
	{
		if (s_done.exchange(false)) {
			outResult = s_result.load();
			outPath = s_lastPath;
			return true;
		}
		return false;
	}

	bool IsCapturing() { return s_busy.load(); }
	const std::string& LastError() { return s_lastError; }

	const std::string& OutputDir()
	{
		EnsureOutDir();
		return s_outDir;
	}
}
