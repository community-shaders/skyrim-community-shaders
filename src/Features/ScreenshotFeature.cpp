// Screenshot Feature
// Non-blocking screenshot tool for flat (SE/AE) and VR. GPU copy runs on the
// render thread; encoding and disk I/O run on a dedicated worker thread so
// capture does not stall the frame.

#include "Features/ScreenshotFeature.h"

#include <PCH.h>

#include "Features/HDRDisplay.h"
#include "Features/Upscaling.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "Utils/FileSystem.h"

#define I18N_KEY_PREFIX "feature.screenshot."

#include <DirectXTex.h>
#include <sk_hdr_png.hpp>

#include <format>
#include <functional>
#include <malloc.h>

namespace
{
	// Capture source for the current runtime. SRV is non-owning - the texture's
	// lifetime is owned by the slot or a caller-held com_ptr.
	struct CaptureSource
	{
		ID3D11Texture2D* texture = nullptr;
		ID3D11ShaderResourceView* srv = nullptr;
		// kFRAMEBUFFER's SRV aliases the swap-chain backbuffer, which ImGui's DX11
		// backend can't sample directly. When true, the preview path copies through
		// the SRV-readable cache instead.
		bool needsPreviewCache = false;
		const char* description = "(none)";
	};

	struct D3D11MultithreadGuard
	{
		winrt::com_ptr<REX::W32::ID3D11Multithread> multithread;

		explicit D3D11MultithreadGuard(ID3D11DeviceContext* context)
		{
			if (context && SUCCEEDED(context->QueryInterface(multithread.put()))) {
				multithread->SetMultithreadProtected(TRUE);
				multithread->Enter();
			}
		}

		~D3D11MultithreadGuard()
		{
			if (multithread) {
				multithread->Leave();
				multithread->SetMultithreadProtected(FALSE);
			}
		}
	};

	bool PopulateScratchImageFromStagingTexture(
		ID3D11DeviceContext* context,
		ID3D11Texture2D* stagingTexture,
		DXGI_FORMAT format,
		uint32_t width,
		uint32_t height,
		DirectX::ScratchImage& image)
	{
		D3D11MultithreadGuard guard(context);

		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped))) {
			return false;
		}
		if (!mapped.pData || mapped.RowPitch == 0) {
			context->Unmap(stagingTexture, 0);
			return false;
		}

		const HRESULT initHr = image.Initialize2D(format, width, height, 1, 1);
		if (FAILED(initHr)) {
			context->Unmap(stagingTexture, 0);
			return false;
		}

		const auto* destImage = image.GetImage(0, 0, 0);
		if (!destImage) {
			context->Unmap(stagingTexture, 0);
			return false;
		}

		// Driver-mapped region can be smaller than height * mapped.RowPitch
		// (alignment quirks, partial mappings). Cap by mapped.DepthPitch and
		// clamp each row's copy to whichever of source/dest pitches is smaller -
		// stepping past either side hits unmapped memory and the worker crashes
		// inside rep movsb (see crash 2026-05-19).
		const size_t bytesPerRow = std::min<size_t>(destImage->rowPitch, mapped.RowPitch);
		const size_t mappedDepth = mapped.DepthPitch != 0 ? mapped.DepthPitch :
		                                                    mapped.RowPitch * destImage->height;
		const size_t maxRowsBySize = mapped.RowPitch > 0 ? (mappedDepth / mapped.RowPitch) : 0;
		const size_t rowsToCopy = std::min<size_t>(destImage->height, maxRowsBySize);

		auto* destPixels = image.GetPixels();
		const auto* srcPixels = static_cast<const uint8_t*>(mapped.pData);

		// Initialize2D leaves the pixel buffer uninitialized. If the mapped
		// region is short (rowsToCopy < height) or narrow (bytesPerRow <
		// destImage->rowPitch), the gaps would otherwise read back as
		// undefined memory and SaveToWICFile would encode garbage. Zero-fill
		// up front so any uncopied bytes encode as deterministic black.
		std::memset(destPixels, 0, image.GetPixelsSize());

		for (size_t row = 0; row < rowsToCopy; ++row) {
			memcpy(
				destPixels + row * destImage->rowPitch,
				srcPixels + row * mapped.RowPitch,
				bytesPerRow);
		}

		context->Unmap(stagingTexture, 0);
		return true;
	}

	void StripAlphaForBmp(DirectX::ScratchImage& image)
	{
		const DirectX::Image* firstImage = image.GetImage(0, 0, 0);
		if (!firstImage || firstImage->pixels == nullptr) {
			return;
		}

		const DXGI_FORMAT format = firstImage->format;
		if (format != DXGI_FORMAT_R8G8B8A8_UNORM &&
			format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
			format != DXGI_FORMAT_B8G8R8A8_UNORM &&
			format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
			return;
		}

		auto* pixels = image.GetPixels();
		const size_t rowPitch = firstImage->rowPitch;
		for (size_t y = 0; y < firstImage->height; ++y) {
			uint8_t* row = pixels + y * rowPitch;
			for (size_t x = 0; x < firstImage->width; ++x) {
				row[x * 4 + 3] = 0xFF;
			}
		}
	}

	// Tonemaps a linear RGB ScratchImage in-place: Reinhard c/(1+c), then gamma-2.2.
	void TonemapHdrToSrgb(DirectX::ScratchImage& image)
	{
		using namespace DirectX;
		DirectX::ScratchImage tonemapped;
		const HRESULT hr = TransformImage(
			image.GetImages(),
			image.GetImageCount(),
			image.GetMetadata(),
			[](XMVECTOR* outPixels, const XMVECTOR* inPixels, size_t width, size_t /*y*/) {
				const XMVECTOR one = XMVectorSplatOne();
				const XMVECTOR invGamma = XMVectorReplicate(1.0f / 2.2f);
				for (size_t i = 0; i < width; ++i) {
					XMVECTOR c = XMVectorMax(inPixels[i], XMVectorZero());
					const XMVECTOR rgb = XMVectorDivide(c, XMVectorAdd(c, one));
					const XMVECTOR gammaCorrected = XMVectorPow(rgb, invGamma);
					outPixels[i] = XMVectorSelect(gammaCorrected, c, g_XMSelect1110);
				}
			},
			tonemapped);
		if (SUCCEEDED(hr)) {
			image = std::move(tonemapped);
		}
	}

	const DirectX::Image* PrepareBmpImage(DirectX::ScratchImage& sourceImage, DirectX::ScratchImage& convertedImage)
	{
		if (sourceImage.GetMetadata().format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
			TonemapHdrToSrgb(sourceImage);
		}

		if (SUCCEEDED(DirectX::Convert(
				sourceImage.GetImages(),
				sourceImage.GetImageCount(),
				sourceImage.GetMetadata(),
				DXGI_FORMAT_B8G8R8X8_UNORM,
				DirectX::TEX_FILTER_DEFAULT,
				0.0f,
				convertedImage))) {
			return convertedImage.GetImage(0, 0, 0);
		}

		return sourceImage.GetImage(0, 0, 0);
	}

	// Game-root-relative paths (e.g. "Screenshots") must be absolute for CF_HDROP / Discord.
	std::filesystem::path ResolveToAbsoluteGamePath(const std::filesystem::path& path)
	{
		if (path.is_absolute()) {
			return path;
		}
		wchar_t buffer[MAX_PATH]{};
		const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
		if (length > 0 && length < MAX_PATH) {
			return std::filesystem::path(buffer).parent_path() / path;
		}
		std::error_code ec;
		return std::filesystem::absolute(path, ec);
	}

	bool CopyFilePathToClipboardHDrop(const std::wstring& absolutePath)
	{
		if (absolutePath.empty()) {
			return false;
		}

		const size_t pathChars = absolutePath.size();
		const size_t bytes = sizeof(DROPFILES) + (pathChars + 2) * sizeof(wchar_t);
		HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
		if (!hMem) {
			return false;
		}

		auto* drop = static_cast<DROPFILES*>(GlobalLock(hMem));
		if (!drop) {
			GlobalFree(hMem);
			return false;
		}

		drop->pFiles = sizeof(DROPFILES);
		drop->fWide = TRUE;

		auto* files = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(drop) + sizeof(DROPFILES));
		memcpy(files, absolutePath.c_str(), (pathChars + 1) * sizeof(wchar_t));

		GlobalUnlock(hMem);

		for (int attempt = 0; attempt < 8; ++attempt) {
			if (attempt > 0) {
				Sleep(1 << (attempt - 1));
			}
			if (!OpenClipboard(nullptr)) {
				continue;
			}
			EmptyClipboard();
			const bool placed = SetClipboardData(CF_HDROP, hMem) != nullptr;
			CloseClipboard();
			if (placed) {
				return true;
			}
		}

		GlobalFree(hMem);
		return false;
	}

	void RunOnMainThread(std::function<void()> fn)
	{
		if (auto* taskInterface = SKSE::GetTaskInterface()) {
			taskInterface->AddTask(std::move(fn));
		} else {
			fn();
		}
	}

	void CopySavedPathToClipboard(bool enabled, const std::filesystem::path& path)
	{
		if (!enabled || path.empty()) {
			return;
		}

		const auto absolutePath = ResolveToAbsoluteGamePath(path);
		std::error_code ec;
		if (!std::filesystem::exists(absolutePath, ec)) {
			logger::warn("Screenshot not found for clipboard: {}", absolutePath.string());
			return;
		}
		if (std::filesystem::file_size(absolutePath, ec) == 0) {
			logger::warn("Screenshot file is empty, skipping clipboard: {}", absolutePath.string());
			return;
		}

		if (!CopyFilePathToClipboardHDrop(absolutePath.wstring())) {
			logger::warn("Screenshot saved but clipboard copy failed.");
		}
	}

	// Resolves the slot's underlying texture, falling back to QueryInterface on
	// SRV/RTV when slot.texture is null (kFRAMEBUFFER on flat aliases the swap-
	// chain backbuffer that way). `holder` keeps the QI refcount alive across
	// the caller's use of the returned pointer.
	ID3D11Texture2D* ResolveSlotTexture(
		const RE::BSGraphics::RenderTargetData& slot,
		winrt::com_ptr<ID3D11Texture2D>& holder)
	{
		if (slot.texture) {
			return slot.texture;
		}
		auto resolveFromView = [&](ID3D11View* view) -> ID3D11Texture2D* {
			if (!view) {
				return nullptr;
			}
			winrt::com_ptr<ID3D11Resource> resource;
			view->GetResource(resource.put());
			if (!resource) {
				return nullptr;
			}
			if (FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D), holder.put_void()))) {
				return nullptr;
			}
			return holder.get();
		};
		if (auto* tex = resolveFromView(slot.SRV)) {
			return tex;
		}
		return resolveFromView(slot.RTV);
	}

	// Returns the texture that was presented to the display (post-ApplyHDR).
	ID3D11Texture2D* ResolveDisplayedBackBuffer(winrt::com_ptr<ID3D11Texture2D>& holder)
	{
		auto& upscaling = globals::features::upscaling;
		if (upscaling.d3d12SwapChainActive &&
			upscaling.dx12SwapChain.swapChainBufferWrapped &&
			upscaling.dx12SwapChain.swapChainBufferWrapped->resource11) {
			holder.copy_from(upscaling.dx12SwapChain.swapChainBufferWrapped->resource11);
			return holder.get();
		}

		if (!globals::d3d::swapChain) {
			return nullptr;
		}

		winrt::com_ptr<ID3D11Texture2D> backBuffer;
		if (FAILED(globals::d3d::swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), backBuffer.put_void()))) {
			return nullptr;
		}
		holder = std::move(backBuffer);
		return holder.get();
	}

	bool IsFlatHdrScreenshotCapture()
	{
		return !globals::game::isVR &&
		       globals::features::hdrDisplay.loaded &&
		       globals::features::hdrDisplay.settings.enableHDR;
	}

	// Picks the capture source:
	//   VR              -> kVR_FRAMEBUFFER (SBS).
	//   HDR enabled     -> swap-chain back buffer after ApplyHDR (PQ HDR10 / PQ float).
	//   otherwise       -> kFRAMEBUFFER (tonemapped UNORM).
	CaptureSource SelectCaptureSource(winrt::com_ptr<ID3D11Texture2D>& holder)
	{
		CaptureSource src;
		auto* renderer = globals::game::renderer;
		if (!renderer) {
			return src;
		}

		if (globals::game::isVR) {
			auto& slot = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kVR_FRAMEBUFFER];
			src.texture = ResolveSlotTexture(slot, holder);
			src.srv = slot.SRV;
			src.description = "VR SBS framebuffer";
			return src;
		}

		if (IsFlatHdrScreenshotCapture()) {
			src.texture = ResolveDisplayedBackBuffer(holder);
			src.needsPreviewCache = true;
			src.description = "Swap chain back buffer (HDR display composite)";
			return src;
		}

		auto& slot = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER];
		src.texture = ResolveSlotTexture(slot, holder);
		src.srv = slot.SRV;
		src.needsPreviewCache = true;
		src.description = "kFRAMEBUFFER";
		return src;
	}

	// True when our hotkey is the single PrintScreen key vanilla binds. Anything
	// else (different key, chord, modifier) means the user wants both ours and
	// vanilla independently.
	bool HotkeyCollidesWithVanilla()
	{
		const auto& combo = Menu::GetSingleton()->GetSettings().ScreenshotKey;
		return combo.size() == 1 &&
		       combo[0].GetDevice() == InputDeviceType::Keyboard &&
		       combo[0].GetKey() == VK_SNAPSHOT;
	}

	// Blend state used around the preview's ImGui::Image draw. Two regression
	// risks if this is changed:
	//   1. BlendEnable must stay FALSE - the source texture carries non-1 alpha
	//      where Skyrim composited UI plates; default SRC_ALPHA blend lets the
	//      host window background show through (visible on the desktop mirror).
	//   2. WriteMask must exclude alpha (RGB only). In VR, Skyrim's menu UI
	//      shader recomposites our menu plate over the SBS framebuffer with
	//      alpha blending; writing texture alpha into the menu plate RT
	//      produces a cutout visible only through the HMD. RGB-only writes
	//      leave the plate's pre-cleared alpha=1 in place.
	// Paired with ImDrawCallback_ResetRenderState queued by Subrect::DrawEditor
	// immediately after the image draw.
	void OpaquePreviewBlendCallback(const ImDrawList*, const ImDrawCmd*)
	{
		static winrt::com_ptr<ID3D11BlendState> opaqueBlend;
		if (!opaqueBlend) {
			D3D11_BLEND_DESC desc{};
			desc.RenderTarget[0].BlendEnable = FALSE;
			desc.RenderTarget[0].RenderTargetWriteMask =
				D3D11_COLOR_WRITE_ENABLE_RED |
				D3D11_COLOR_WRITE_ENABLE_GREEN |
				D3D11_COLOR_WRITE_ENABLE_BLUE;
			globals::d3d::device->CreateBlendState(&desc, opaqueBlend.put());
		}
		if (opaqueBlend) {
			globals::d3d::context->OMSetBlendState(opaqueBlend.get(), nullptr, 0xFFFFFFFF);
		}
	}

	std::filesystem::path BuildScreenshotPath(const std::string& screenshotPath, bool usePng)
	{
		SYSTEMTIME st;
		GetLocalTime(&st);
		char buf[80];
		const char* extension = usePng ? ".png" : ".bmp";
		snprintf(buf, sizeof(buf), "CS_%04d-%02d-%02d_%02d-%02d-%02d_%03d%s",
			st.wYear, st.wMonth, st.wDay,
			st.wHour, st.wMinute, st.wSecond,
			st.wMilliseconds,
			extension);
		return ResolveToAbsoluteGamePath(std::filesystem::path(screenshotPath) / buf);
	}

	struct HdrFormatInfo
	{
		DXGI_FORMAT dxgi;
		sk_hdr_png::format png;
		size_t bytesPerPixel;
	};

	constexpr HdrFormatInfo kHdrFormats[] = {
		{ DXGI_FORMAT_R10G10B10A2_UNORM, sk_hdr_png::format::r10g10b10a2_unorm, 4 },
		{ DXGI_FORMAT_R16G16B16A16_FLOAT, sk_hdr_png::format::r16g16b16a16_pq, 8 },
	};

	const HdrFormatInfo* LookupHdrFormat(DXGI_FORMAT format)
	{
		for (const auto& info : kHdrFormats) {
			if (info.dxgi == format) {
				return &info;
			}
		}
		return nullptr;
	}

	bool IsHdrCaptureFormat(DXGI_FORMAT format)
	{
		return LookupHdrFormat(format) != nullptr;
	}

	// sk_hdr_png requires 16-byte aligned pixel memory.
	bool CopyToAlignedPixelBuffer(
		const DirectX::Image& image,
		size_t bytesPerPixel,
		void*& outAligned,
		size_t& outByteSize)
	{
		if (bytesPerPixel == 0) {
			return false;
		}

		const size_t tightRowBytes = static_cast<size_t>(image.width) * bytesPerPixel;
		outByteSize = tightRowBytes * image.height;

		outAligned = _aligned_malloc(outByteSize, 16);
		if (!outAligned) {
			return false;
		}

		auto* dest = static_cast<uint8_t*>(outAligned);
		const auto* src = image.pixels;
		for (size_t row = 0; row < image.height; ++row) {
			memcpy(dest + row * tightRowBytes, src + row * image.rowPitch, tightRowBytes);
		}
		return true;
	}

	bool SaveHdrPng(
		const DirectX::ScratchImage& image,
		const std::filesystem::path& outputPath,
		int quantizationBits,
		DXGI_FORMAT format)
	{
		const DirectX::Image* firstImage = image.GetImage(0, 0, 0);
		const HdrFormatInfo* hdrInfo = firstImage ? LookupHdrFormat(format) : nullptr;
		if (!firstImage || !hdrInfo || firstImage->format != format) {
			return false;
		}

		void* alignedPixels = nullptr;
		size_t byteSize = 0;
		if (!CopyToAlignedPixelBuffer(*firstImage, hdrInfo->bytesPerPixel, alignedPixels, byteSize)) {
			return false;
		}

		const bool saved = sk_hdr_png::write_image_to_disk(
			outputPath.wstring().c_str(),
			static_cast<unsigned int>(firstImage->width),
			static_cast<unsigned int>(firstImage->height),
			alignedPixels,
			quantizationBits,
			hdrInfo->png,
			false);

		_aligned_free(alignedPixels);
		return saved;
	}

	bool SaveSdrScreenshot(
		DirectX::ScratchImage& image,
		const std::filesystem::path& outputPath,
		bool saveAsPng)
	{
		StripAlphaForBmp(image);
		DirectX::ScratchImage convertedImage;
		const DirectX::Image* saveImage = PrepareBmpImage(image, convertedImage);
		if (!saveImage) {
			return false;
		}

		const GUID& codec = saveAsPng ?
		                        DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG) :
		                        DirectX::GetWICCodec(DirectX::WIC_CODEC_BMP);
		return SUCCEEDED(DirectX::SaveToWICFile(
			*saveImage,
			DirectX::WIC_FLAGS_NONE,
			codec,
			outputPath.c_str()));
	}

	bool SaveScreenshotToDisk(
		DirectX::ScratchImage& image,
		const std::filesystem::path& outputPath,
		DXGI_FORMAT format,
		int hdrPngBitDepth,
		bool saveAsHdrPng,
		bool saveAsSdrPng)
	{
		if (saveAsHdrPng) {
			return SaveHdrPng(image, outputPath, hdrPngBitDepth, format);
		}
		return SaveSdrScreenshot(image, outputPath, saveAsSdrPng);
	}

}

ScreenshotFeature::~ScreenshotFeature()
{
	StopWorkerThread();
}

bool ScreenshotFeature::IsInMenu() const
{
	return true;
}

void ScreenshotFeature::PostPostLoad()
{
	// Seed VR-specific presets here rather than in LoadSettings: Feature::Load
	// only dispatches to LoadSettings when the JSON already has a settings
	// block, so a fresh install would skip a seed placed there. Left first so
	// it's the initial selection (matches vanilla Skyrim VR's left-eye save).
	if (REL::Module::IsVR()) {
		subrect.SeedDefaultPresets({
			{ .name = "Left Eye", .uv = { 0.0f, 0.0f, 0.5f, 1.0f } },
			{ .name = "Right Eye", .uv = { 0.5f, 0.0f, 0.5f, 1.0f } },
			{ .name = "Full Frame", .uv = { 0.0f, 0.0f, 1.0f, 1.0f } },
		});
	}
}

void ScreenshotFeature::LoadSettings(json& a_json)
{
	if (a_json.contains("ScreenshotPath"))
		screenshotPath = a_json["ScreenshotPath"];
	if (a_json.contains("ApplyCropToScreenshot"))
		applyCropToScreenshot = a_json["ApplyCropToScreenshot"];
	if (a_json.contains("HdrPngBitDepth"))
		hdrPngBitDepth = std::clamp<unsigned int>(a_json["HdrPngBitDepth"], 7u, 16u);
	if (a_json.contains("SdrUsePng"))
		sdrUsePng = a_json["SdrUsePng"];
	if (a_json.contains("CopyToClipboard"))
		copyToClipboard = a_json["CopyToClipboard"];

	subrect.LoadSettings(a_json);
}

void ScreenshotFeature::SaveSettings(json& a_json)
{
	a_json["ScreenshotPath"] = screenshotPath;
	a_json["ApplyCropToScreenshot"] = applyCropToScreenshot;
	a_json["HdrPngBitDepth"] = hdrPngBitDepth;
	a_json["SdrUsePng"] = sdrUsePng;
	a_json["CopyToClipboard"] = copyToClipboard;
	subrect.SaveSettings(a_json);
}

void ScreenshotFeature::DrawSettings()
{
	ImGui::TextWrapped("%s", T(TKEY("async_note"), "Capture and save run asynchronously without stalling the game."));

	const bool hdrCaptureAvailable = globals::features::hdrDisplay.loaded &&
	                                 globals::features::hdrDisplay.settings.enableHDR;

	if (hdrCaptureAvailable) {
		ImGui::TextWrapped("%s",
			T(TKEY("hdr_note"),
				"HDR enabled: saves the displayed frame as PNG with HDR10 metadata (48 bpp RGB, cICP/cLLi). "
				"Use an HDR-aware viewer such as Windows Photos (HDR on) or Special K SKIF."));
		ImGui::SliderInt(
			T(TKEY("hdr_bit_depth"), "HDR PNG bit depth"),
			reinterpret_cast<int*>(&hdrPngBitDepth),
			7,
			16,
			"%d-bit",
			ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(
				"%s", T(TKEY("hdr_bit_depth_tooltip"),
						  "Quantization for the 48 bpp RGB PNG payload. 11-bit is a good default; "
						  "higher values increase file size with diminishing returns."));

	} else {
		ImGui::TextWrapped("%s",
			T(TKEY("sdr_note"),
				"Enable HDR Display to capture HDR PNG screenshots with HDR10 metadata. "
				"SDR and VR captures use the lossless format selected below."));
	}

	if (ImGui::Button(T(TKEY("take_screenshot"), "Take Screenshot Now"))) {
		captureRequested = true;
	}
	ImGui::SameLine();
	ImGui::Checkbox(T(TKEY("apply_crop"), "Apply crop"), &applyCropToScreenshot);

	ImGui::SeparatorText(T(TKEY("output"), "Output"));

	ImGui::Checkbox("Copy saved file to clipboard", &copyToClipboard);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Places the saved screenshot on the clipboard as a file (paste in Explorer or attach in chat apps).");

	if (!hdrCaptureAvailable || globals::game::isVR) {
		int sdrFormat = sdrUsePng ? 1 : 0;
		ImGui::RadioButton("BMP (lossless)", &sdrFormat, 0);
		ImGui::SameLine();
		ImGui::RadioButton("PNG (lossless)", &sdrFormat, 1);
		sdrUsePng = sdrFormat != 0;
		if (hdrCaptureAvailable && globals::game::isVR) {
			ImGui::TextWrapped("VR captures use this format. Flat HDR mode always saves HDR PNG.");
		}
	}

	char buf[260];
	strncpy_s(buf, sizeof(buf), screenshotPath.c_str(), _TRUNCATE);
	ImGui::PushItemWidth(-FLT_MIN - 120.0f);
	if (ImGui::InputText("##ScreenshotFolder", buf, sizeof(buf))) {
		screenshotPath = buf;
	}
	ImGui::PopItemWidth();
	ImGui::SameLine();
	const bool canOpen = !screenshotPath.empty();
	ImGui::BeginDisabled(!canOpen);
	if (ImGui::Button(T(TKEY("open"), "Open"))) {
		std::error_code ec;
		std::filesystem::create_directories(screenshotPath, ec);
		ShellExecuteA(nullptr, "open", screenshotPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::Text("%s", T(TKEY("folder"), "Folder"));
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("folder_tooltip"),
							  "Relative paths resolve against the Skyrim install dir.\n"
							  "Absolute paths (e.g. D:\\Captures) save there directly."));
	}

	auto& menuSettings = Menu::GetSingleton()->GetSettings();
	Util::InputComboWidget(
		T(TKEY("hotkey"), "Hotkey"),
		menuSettings.ScreenshotKey,
		Menu::GetSingleton()->settingScreenshotKey,
		"Change##ScreenshotFeature");

	if (HotkeyCollidesWithVanilla()) {
		Util::Text::WrappedWarning(
			T(TKEY("hotkey_collision"),
				"This hotkey collides with vanilla PrintScreen; both saves will fire. "
				"Set bAllowScreenShot=0 in Skyrim.ini to suppress vanilla, or pick a different hotkey above."));
	}

	ImGui::SeparatorText(T(TKEY("crop"), "Crop"));

	// Preview reflects what Capture() would save. Full source frame so VR users
	// can drag-crop across the eye boundary if a seeded preset doesn't fit.
	winrt::com_ptr<ID3D11Texture2D> previewTextureKeepAlive;
	const auto src = SelectCaptureSource(previewTextureKeepAlive);

	ID3D11ShaderResourceView* previewView = src.srv;
	if (src.texture && (src.needsPreviewCache || !previewView)) {
		EnsurePreviewCache(src.texture);
		if (previewCacheSRV && previewCacheTexture) {
			globals::d3d::context->CopySubresourceRegion(
				previewCacheTexture.get(), 0, 0, 0, 0, src.texture, 0, nullptr);
			previewView = previewCacheSRV.get();
		}
	}

	subrect.DrawEditor(previewView, src.texture, 1.0f, 0.0f, OpaquePreviewBlendCallback);
}

void ScreenshotFeature::EnsurePreviewCache(ID3D11Texture2D* sourceTexture)
{
	if (!sourceTexture) {
		return;
	}
	D3D11_TEXTURE2D_DESC srcDesc{};
	sourceTexture->GetDesc(&srcDesc);

	// Reuse the cache when the source dimensions/format haven't changed.
	if (previewCacheTexture) {
		D3D11_TEXTURE2D_DESC cacheDesc{};
		previewCacheTexture->GetDesc(&cacheDesc);
		if (cacheDesc.Width == srcDesc.Width &&
			cacheDesc.Height == srcDesc.Height &&
			cacheDesc.Format == srcDesc.Format) {
			return;
		}
		previewCacheSRV = nullptr;
		previewCacheTexture = nullptr;
	}

	// SRV-readable copy. Match source format for CopySubresourceRegion compatibility.
	D3D11_TEXTURE2D_DESC cacheDesc = srcDesc;
	cacheDesc.MipLevels = 1;
	cacheDesc.ArraySize = 1;
	cacheDesc.SampleDesc.Count = 1;
	cacheDesc.SampleDesc.Quality = 0;
	cacheDesc.Usage = D3D11_USAGE_DEFAULT;
	cacheDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	cacheDesc.CPUAccessFlags = 0;
	cacheDesc.MiscFlags = 0;

	if (FAILED(globals::d3d::device->CreateTexture2D(&cacheDesc, nullptr, previewCacheTexture.put()))) {
		previewCacheTexture = nullptr;
		return;
	}
	if (FAILED(globals::d3d::device->CreateShaderResourceView(
			previewCacheTexture.get(), nullptr, previewCacheSRV.put()))) {
		previewCacheSRV = nullptr;
		previewCacheTexture = nullptr;
	}
}

void ScreenshotFeature::Reset()
{
}

void ScreenshotFeature::ProcessCaptureRequest()
{
	if (captureRequested.exchange(false)) {
		Capture();
	}
}

void ScreenshotFeature::EnsureWorkerThread()
{
	if (screenshotWorker.joinable()) {
		return;
	}

	screenshotWorkerRunning = true;
	screenshotWorker = std::thread(&ScreenshotFeature::ScreenshotWorkerLoop, this);
}

void ScreenshotFeature::StopWorkerThread()
{
	{
		std::lock_guard<std::mutex> lock(screenshotQueueMutex);
		screenshotWorkerRunning = false;
	}
	screenshotQueueCV.notify_all();

	if (screenshotWorker.joinable()) {
		screenshotWorker.join();
	}
}

void ScreenshotFeature::EnqueueScreenshot(PendingScreenshot&& screenshot)
{
	{
		std::lock_guard<std::mutex> lock(screenshotQueueMutex);
		screenshotQueue.push(std::move(screenshot));
	}
	screenshotQueueCV.notify_one();
}

void ScreenshotFeature::ScreenshotWorkerLoop()
{
	CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	auto* context = globals::d3d::context;
	while (true) {
		PendingScreenshot screenshot;
		{
			std::unique_lock<std::mutex> lock(screenshotQueueMutex);
			screenshotQueueCV.wait(lock, [this] {
				return !screenshotQueue.empty() || !screenshotWorkerRunning;
			});

			if (!screenshotWorkerRunning && screenshotQueue.empty()) {
				break;
			}

			screenshot = std::move(screenshotQueue.front());
			screenshotQueue.pop();
		}

		DirectX::ScratchImage image;
		if (!PopulateScratchImageFromStagingTexture(
				context,
				screenshot.stagingTexture.get(),
				screenshot.format,
				screenshot.width,
				screenshot.height,
				image)) {
			logger::error("Failed to map screenshot staging texture.");
			continue;
		}

		Util::FileHelpers::EnsureDirectoryExists(screenshot.outputPath.parent_path());

		const bool saveOk = SaveScreenshotToDisk(
			image,
			screenshot.outputPath,
			screenshot.format,
			screenshot.hdrPngBitDepth,
			screenshot.saveAsHdrPng,
			screenshot.saveAsSdrPng);
		if (!saveOk) {
			logger::error(
				"Failed to save {} screenshot.",
				screenshot.saveAsHdrPng ? "HDR PNG" : "SDR");
		}

		if (saveOk) {
			CopySavedPathToClipboard(screenshot.copyToClipboard, screenshot.outputPath);
		}

		if (!saveOk) {
			ShowInGameNotification("Screenshot failed - see CommunityShaders.log");
		} else {
			logger::info("Saved screenshot to {}", screenshot.outputPath.string());
			ShowInGameNotification(std::format("Screenshot saved: {}",
				screenshot.outputPath.filename().string()));
		}
	}
	CoUninitialize();
}

void ScreenshotFeature::ShowInGameNotification(std::string message)
{
	// ShowHUDMessage must run on the game's main thread. Third arg dedupes spam-clicks.
	RunOnMainThread([msg = std::move(message)]() {
		RE::SendHUDMessage::ShowHUDMessage(msg.c_str(), nullptr, true);
	});
}

void ScreenshotFeature::Capture()
{
	auto device = globals::d3d::device;
	auto context = globals::d3d::context;

	if (!device || !context)
		return;

	winrt::com_ptr<ID3D11Texture2D> sourceTextureKeepAlive;
	const auto src = SelectCaptureSource(sourceTextureKeepAlive);
	logger::debug("Capturing from {}", src.description);

	if (!src.texture) {
		logger::error("Failed to acquire screenshot source texture ({}).", src.description);
		return;
	}
	ID3D11Texture2D* sourceTexture = src.texture;

	D3D11_TEXTURE2D_DESC srcDesc{};
	sourceTexture->GetDesc(&srcDesc);

	uint32_t copyX = 0;
	uint32_t copyY = 0;
	uint32_t copyW = srcDesc.Width;
	uint32_t copyH = srcDesc.Height;

	if (applyCropToScreenshot) {
		auto region = subrect.GetPixelRegion(srcDesc.Width, srcDesc.Height);
		copyX = region.x;
		copyY = region.y;
		copyW = region.w;
		copyH = region.h;
	}

	D3D11_TEXTURE2D_DESC stagingDesc = srcDesc;
	stagingDesc.Width = copyW;
	stagingDesc.Height = copyH;
	stagingDesc.MipLevels = 1;
	stagingDesc.ArraySize = 1;
	stagingDesc.SampleDesc.Count = 1;
	stagingDesc.SampleDesc.Quality = 0;
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.BindFlags = 0;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	stagingDesc.MiscFlags = 0;

	winrt::com_ptr<ID3D11Texture2D> stagingTexture;
	if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, stagingTexture.put()))) {
		logger::error("Failed to create screenshot staging texture.");
		return;
	}

	D3D11_BOX sourceRegion{};
	sourceRegion.left = copyX;
	sourceRegion.top = copyY;
	sourceRegion.front = 0;
	sourceRegion.right = copyX + copyW;
	sourceRegion.bottom = copyY + copyH;
	sourceRegion.back = 1;

	context->CopySubresourceRegion(stagingTexture.get(), 0, 0, 0, 0, sourceTexture, 0, &sourceRegion);

	// Match SelectCaptureSource: only the flat HDR back-buffer path uses HDR PNG.
	// Do not key off DXGI format alone — kFRAMEBUFFER can be float/HDR-sized in SDR mode.
	const bool flatHdrCapture = IsFlatHdrScreenshotCapture();
	if (flatHdrCapture && !IsHdrCaptureFormat(srcDesc.Format)) {
		logger::error("Unsupported HDR screenshot format: {}", static_cast<uint32_t>(srcDesc.Format));
		return;
	}
	const bool saveAsHdrPng = flatHdrCapture && IsHdrCaptureFormat(srcDesc.Format);
	const bool saveAsSdrPng = !saveAsHdrPng && sdrUsePng;

	EnsureWorkerThread();
	PendingScreenshot screenshot;
	screenshot.stagingTexture = std::move(stagingTexture);
	screenshot.format = srcDesc.Format;
	screenshot.width = copyW;
	screenshot.height = copyH;
	screenshot.saveAsHdrPng = saveAsHdrPng;
	screenshot.saveAsSdrPng = saveAsSdrPng;
	screenshot.hdrPngBitDepth = static_cast<int>(hdrPngBitDepth);
	screenshot.outputPath = BuildScreenshotPath(screenshotPath, saveAsHdrPng || saveAsSdrPng);
	screenshot.copyToClipboard = copyToClipboard;
	EnqueueScreenshot(std::move(screenshot));
}
#undef I18N_KEY_PREFIX
