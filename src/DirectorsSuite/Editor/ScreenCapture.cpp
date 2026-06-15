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

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace
{
	std::atomic<bool> s_busy{ false };
	std::atomic<bool> s_done{ false };
	std::atomic<int>  s_result{ ScreenCapture::CAP_FAILED };
	std::string s_lastError;
	std::string s_lastPath;
	std::string s_outDir;
	CRITICAL_SECTION s_cs;
	bool s_csInit = false;

	template<typename T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

	std::string TimeStampName(const char* ext)
	{
		SYSTEMTIME st; GetLocalTime(&st);
		char buf[128];
		sprintf_s(buf, "CSK_%04d-%02d-%02d_%02d-%02d-%02d-%03d.%s",
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
			if (FAILED(frame->SetSize(w, h))) { SetError("set size"); break; }

			// Source pixel format from the duplicated buffer
			WICPixelFormatGUID srcFmt;
			UINT bppSrc;
			switch (fmt) {
				case DXGI_FORMAT_R16G16B16A16_FLOAT: srcFmt = GUID_WICPixelFormat64bppRGBAHalf; bppSrc = 8; break;
				case DXGI_FORMAT_R10G10B10A2_UNORM:  srcFmt = GUID_WICPixelFormat32bppR10G10B10A2; bppSrc = 4; break;
				case DXGI_FORMAT_B8G8R8A8_UNORM:     srcFmt = GUID_WICPixelFormat32bppBGRA; bppSrc = 4; break;
				case DXGI_FORMAT_R8G8B8A8_UNORM:     srcFmt = GUID_WICPixelFormat32bppRGBA; bppSrc = 4; break;
				default:                             srcFmt = GUID_WICPixelFormat32bppBGRA; bppSrc = 4; break;
			}

			WICPixelFormatGUID outFmt = srcFmt;
			frame->SetPixelFormat(&outFmt);

			// If the encoder cannot take the source format directly, convert.
			if (outFmt != srcFmt) {
				IWICBitmap* bmp = nullptr;
				if (FAILED(factory->CreateBitmapFromMemory(w, h, srcFmt, map.RowPitch,
					map.RowPitch * h, (BYTE*)map.pData, &bmp))) { SetError("bmp from mem"); break; }
				IWICFormatConverter* conv = nullptr;
				if (FAILED(factory->CreateFormatConverter(&conv))) { bmp->Release(); SetError("converter"); break; }
				if (FAILED(conv->Initialize(bmp, outFmt, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
					conv->Release(); bmp->Release(); SetError("convert init"); break;
				}
				HRESULT wr = frame->WriteSource(conv, nullptr);
				conv->Release(); bmp->Release();
				if (FAILED(wr)) { SetError("write source"); break; }
			}
			else {
				if (FAILED(frame->WritePixels(h, map.RowPitch, map.RowPitch * h, (BYTE*)map.pData))) {
					SetError("write pixels"); break;
				}
			}

			if (FAILED(frame->Commit())) { SetError("frame commit"); break; }
			if (FAILED(encoder->Commit())) { SetError("encoder commit"); break; }

			s_lastPath = path;
			rc = isHDR ? ScreenCapture::CAP_OK_HDR : ScreenCapture::CAP_OK_PNG;
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

	void RequestCapture()
	{
		if (!s_csInit) { InitializeCriticalSection(&s_cs); s_csInit = true; }
		bool expected = false;
		if (!s_busy.compare_exchange_strong(expected, true)) {
			return; // already running
		}
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
