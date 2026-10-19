// Screenshot Feature
// Non-blocking screenshot tool. GPU copy and readback run on the
// render thread; encoding and disk I/O run on a dedicated worker thread so
// capture does not stall the frame.

#include "Features/ScreenshotFeature.h"

#include <PCH.h>

#include "Features/HDRDisplay.h"
#include "Features/Screenshot/Avif.h"
#include "Features/Upscaling.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "Utils/D3D.h"
#include "Utils/FileSystem.h"

#define I18N_KEY_PREFIX "feature.screenshot."

#include <DirectXTex.h>
#pragma warning(push)
#pragma warning(disable: 4244)  // double->float conversion in third-party header
#include <sk_hdr_png.hpp>
#pragma warning(pop)

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
		ID3D11ShaderResourceView* uiSRV = nullptr;
		// kFRAMEBUFFER's SRV aliases the swap-chain backbuffer, which ImGui's DX11
		// backend can't sample directly. When true, the preview path copies through
		// the SRV-readable cache instead.
		bool needsPreviewCache = false;
		const char* description = "(none)";
	};

	struct CaptureStateGuard
	{
		ID3D11DeviceContext* context;
		ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
		winrt::com_ptr<ID3D11DepthStencilView> dsv;
		winrt::com_ptr<ID3D11ComputeShader> shader;
		ID3D11ClassInstance* instances[256]{};
		UINT instanceCount = ARRAYSIZE(instances);
		ID3D11ShaderResourceView* srvs[2]{};
		winrt::com_ptr<ID3D11UnorderedAccessView> uav;
		winrt::com_ptr<ID3D11Buffer> cb;

		explicit CaptureStateGuard(ID3D11DeviceContext* a_context) :
			context(a_context)
		{
			context->OMGetRenderTargets(ARRAYSIZE(rtvs), rtvs, dsv.put());
			context->CSGetShader(shader.put(), instances, &instanceCount);
			context->CSGetShaderResources(0, ARRAYSIZE(srvs), srvs);
			context->CSGetUnorderedAccessViews(0, 1, uav.put());
			context->CSGetConstantBuffers(0, 1, cb.put());
			context->OMSetRenderTargets(0, nullptr, nullptr);
			ID3D11UnorderedAccessView* nullUAV = nullptr;
			context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
		}

		~CaptureStateGuard()
		{
			ID3D11ShaderResourceView* nullSRVs[2]{};
			context->CSSetShaderResources(0, ARRAYSIZE(nullSRVs), nullSRVs);
			auto* savedUAV = uav.get();
			const UINT keepCounter = UINT32_MAX;
			context->CSSetUnorderedAccessViews(0, 1, &savedUAV, &keepCounter);
			context->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, dsv.get());
			context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);
			auto* savedCB = cb.get();
			context->CSSetConstantBuffers(0, 1, &savedCB);
			context->CSSetShader(shader.get(), instances, instanceCount);
			for (auto* rtv : rtvs)
				if (rtv)
					rtv->Release();
			for (auto* srv : srvs)
				if (srv)
					srv->Release();
			for (UINT i = 0; i < instanceCount; ++i)
				if (instances[i])
					instances[i]->Release();
		}
	};

	/**
	 * @brief Copies a staging texture into `image` without stalling on the GPU. Render thread only.
	 * @return DXGI_ERROR_WAS_STILL_DRAWING while the GPU copy is in flight, S_OK on success, E_FAIL otherwise.
	 */
	HRESULT PopulateScratchImageFromStagingTexture(
		ID3D11DeviceContext* context,
		ID3D11Texture2D* stagingTexture,
		DXGI_FORMAT format,
		uint32_t width,
		uint32_t height,
		DirectX::ScratchImage& image)
	{
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (const HRESULT mapHr = context->Map(stagingTexture, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped); FAILED(mapHr)) {
			return mapHr == DXGI_ERROR_WAS_STILL_DRAWING ? mapHr : E_FAIL;
		}
		if (!mapped.pData || mapped.RowPitch == 0) {
			context->Unmap(stagingTexture, 0);
			return E_FAIL;
		}

		const HRESULT initHr = image.Initialize2D(format, width, height, 1, 1);
		if (FAILED(initHr)) {
			context->Unmap(stagingTexture, 0);
			return E_FAIL;
		}

		const auto* destImage = image.GetImage(0, 0, 0);
		if (!destImage) {
			context->Unmap(stagingTexture, 0);
			return E_FAIL;
		}

		// Driver-mapped region can be smaller than height * mapped.RowPitch
		// (alignment quirks, partial mappings). Cap by mapped.DepthPitch and
		// clamp each row's copy to whichever of source/dest pitches is smaller -
		// stepping past either side hits unmapped memory and the copy crashes
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
		return S_OK;
	}

	const DirectX::Image* PrepareSdrImage(const DirectX::ScratchImage& sourceImage, DirectX::ScratchImage& convertedImage)
	{
		if (sourceImage.GetMetadata().format == DXGI_FORMAT_B8G8R8X8_UNORM) {
			return sourceImage.GetImage(0, 0, 0);
		}

		if (SUCCEEDED(DirectX::Convert(
				sourceImage.GetImages(),
				sourceImage.GetImageCount(),
				sourceImage.GetMetadata(),
				DXGI_FORMAT_B8G8R8X8_UNORM,
				DirectX::TEX_FILTER_SRGB,
				0.0f,
				convertedImage))) {
			return convertedImage.GetImage(0, 0, 0);
		}

		return nullptr;
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

	// ApplyHDR has encoded the back buffer before the actual Present.
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
		return globals::features::hdrDisplay.loaded &&
		       globals::features::hdrDisplay.settings.enableHDR;
	}

	// Picks the capture source:
	//   HDR + CS menu open -> clean HDR composite (no UI, no menu blur).
	//   HDR / D3D12        -> swap-chain back buffer after ApplyHDR, plus separate FG UI.
	//   otherwise          -> kFRAMEBUFFER (tonemapped UNORM).
	// forCapture: post-blur screenshot uses the snapshot; pre-blur preview uses hdrTexture.
	CaptureSource SelectCaptureSource(winrt::com_ptr<ID3D11Texture2D>& holder, bool forCapture)
	{
		CaptureSource src;
		auto* renderer = globals::game::renderer;
		if (!renderer) {
			return src;
		}

		if (IsFlatHdrScreenshotCapture()) {
			// Recompose from the clean scene with no UI buffer.
			auto& hdr = globals::features::hdrDisplay;
			if (!hdr.hdrTexture || !hdr.outputTexture || !hdr.hdrDataCB || !hdr.GetHDROutputCS())
				return src;

			if (Menu::GetSingleton()->IsEnabled && hdr.outputTexture && hdr.outputTexture->srv) {
				ID3D11ShaderResourceView* sceneSRV =
					(forCapture && hdr.IsCleanSceneCaptureFresh()) ? hdr.cleanSceneCapture->srv.get() :
																	 (hdr.hdrTexture ? hdr.hdrTexture->srv.get() : nullptr);
				if (sceneSRV) {
					if (ID3D11Texture2D* clean = hdr.ComposeCleanCapture(sceneSRV, /*sdrPreview=*/!forCapture)) {
						src.texture = clean;
						src.srv = hdr.outputTexture->srv.get();
						src.needsPreviewCache = false;
						src.description = "HDR clean composite (no UI, no menu blur)";
						return src;
					}
				}
			}
		}

		auto& upscaling = globals::features::upscaling;
		if (IsFlatHdrScreenshotCapture() || upscaling.d3d12SwapChainActive) {
			src.texture = ResolveDisplayedBackBuffer(holder);
			if (upscaling.d3d12SwapChainActive && upscaling.ShouldUseFrameGenerationThisFrame()) {
				if (!upscaling.dx12SwapChain.swapChainBufferWrapped || !upscaling.dx12SwapChain.uiBufferWrapped)
					return {};
				src.srv = upscaling.dx12SwapChain.swapChainBufferWrapped->srv;
				src.uiSRV = upscaling.dx12SwapChain.uiBufferWrapped->srv;
			}
			src.needsPreviewCache = true;
			src.description = "Swap chain back buffer (display composite)";
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

	// Forces BlendEnable=FALSE and opaque alpha for the preview Image draw.
	// Paired with ImDrawCallback_ResetRenderState queued by Subrect::DrawEditor.
	void OpaquePreviewBlendCallback(const ImDrawList*, const ImDrawCmd*)
	{
		const bool writeAlpha = IsFlatHdrScreenshotCapture() && Menu::GetSingleton()->IsEnabled;

		static winrt::com_ptr<ID3D11BlendState> rgbBlend;
		static winrt::com_ptr<ID3D11BlendState> rgbaBlend;
		auto& blend = writeAlpha ? rgbaBlend : rgbBlend;
		if (!blend) {
			D3D11_BLEND_DESC desc{};
			desc.RenderTarget[0].BlendEnable = FALSE;
			desc.RenderTarget[0].RenderTargetWriteMask =
				D3D11_COLOR_WRITE_ENABLE_RED |
				D3D11_COLOR_WRITE_ENABLE_GREEN |
				D3D11_COLOR_WRITE_ENABLE_BLUE |
				(writeAlpha ? D3D11_COLOR_WRITE_ENABLE_ALPHA : 0);
			globals::d3d::device->CreateBlendState(&desc, blend.put());
		}
		if (blend) {
			globals::d3d::context->OMSetBlendState(blend.get(), nullptr, 0xFFFFFFFF);
		}
	}

	std::filesystem::path BuildScreenshotPath(const std::string& screenshotPath, const char* extension)
	{
		SYSTEMTIME st;
		GetLocalTime(&st);
		char buf[80];
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
		DirectX::ScratchImage convertedImage;
		const DirectX::Image* saveImage = PrepareSdrImage(image, convertedImage);
		if (!saveImage) {
			return false;
		}

		const GUID& codec = saveAsPng ?
		                        DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG) :
		                        DirectX::GetWICCodec(DirectX::WIC_CODEC_BMP);
		return SUCCEEDED(DirectX::SaveToWICFile(
			*saveImage,
			DirectX::WIC_FLAGS_FORCE_SRGB,
			codec,
			outputPath.c_str()));
	}

	bool SaveScreenshotToDisk(
		DirectX::ScratchImage& image,
		const std::filesystem::path& outputPath,
		DXGI_FORMAT format,
		int hdrPngBitDepth,
		bool saveAsHdrPng,
		bool saveAsHdrAvif,
		bool saveAsSdrPng)
	{
		if (saveAsHdrAvif) {
			const auto* firstImage = image.GetImage(0, 0, 0);
			return firstImage && Screenshot::SaveHdrAvif(*firstImage, outputPath);
		}
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
}

void ScreenshotFeature::ClearShaderCache()
{
	compositeCS = nullptr;
}

void ScreenshotFeature::LoadSettings(json& a_json)
{
	if (a_json.contains("ScreenshotPath"))
		screenshotPath = a_json["ScreenshotPath"];
	if (a_json.contains("ApplyCropToScreenshot"))
		applyCropToScreenshot = a_json["ApplyCropToScreenshot"];
	if (a_json.contains("HdrPngBitDepth"))
		hdrPngBitDepth = std::clamp<unsigned int>(a_json["HdrPngBitDepth"], 7u, 16u);
	if (a_json.contains("HdrUseAvif") && a_json["HdrUseAvif"].is_boolean())
		hdrUseAvif = a_json["HdrUseAvif"];
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
	a_json["HdrUseAvif"] = hdrUseAvif;
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
		int hdrFormat = hdrUseAvif ? 1 : 0;
		ImGui::RadioButton(T(TKEY("hdr_png"), "HDR PNG"), &hdrFormat, 0);
		ImGui::SameLine();
		ImGui::RadioButton(T(TKEY("hdr_avif"), "HDR AVIF"), &hdrFormat, 1);
		hdrUseAvif = hdrFormat != 0;
		if (hdrUseAvif) {
			ImGui::TextWrapped("%s",
				T(TKEY("hdr_avif_note"),
					"Saves PQ/BT.2020 HDR with lossless 4:4:4 encoding. HDR10 pixels retain 10-bit precision; "
					"floating-point pixels are quantized to 12-bit. Encoding runs in the background and may take longer. "
					"Viewing requires HDR AVIF support."));
		} else {
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
		}
	} else {
		ImGui::TextWrapped("%s",
			T(TKEY("sdr_note"),
				"Enable HDR Display to capture HDR PNG or AVIF screenshots with HDR10 metadata. "
				"SDR captures use the lossless format selected below."));
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

	if (!hdrCaptureAvailable) {
		int sdrFormat = sdrUsePng ? 1 : 0;
		ImGui::RadioButton("BMP (lossless)", &sdrFormat, 0);
		ImGui::SameLine();
		ImGui::RadioButton("PNG (lossless)", &sdrFormat, 1);
		sdrUsePng = sdrFormat != 0;
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

	// Preview reflects what Capture() would save.
	CaptureStateGuard guard(globals::d3d::context);
	winrt::com_ptr<ID3D11Texture2D> previewTextureKeepAlive;
	auto src = SelectCaptureSource(previewTextureKeepAlive, /*forCapture=*/false);
	if (src.texture && src.uiSRV) {
		src.texture = CompositeCapture(src.texture, src.srv, src.uiSRV);
		src.srv = src.texture ? compositeTexture->srv.get() : nullptr;
		src.needsPreviewCache = false;
	}

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

ID3D11Texture2D* ScreenshotFeature::CompositeCapture(ID3D11Texture2D* sourceTexture, ID3D11ShaderResourceView* sceneSRV, ID3D11ShaderResourceView* uiSRV)
{
	if (!sourceTexture || !sceneSRV || !uiSRV)
		return nullptr;

	if (!compositeCS) {
		std::vector<std::pair<const char*, const char*>> defines;
		compositeCS.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Screenshot\\CompositeCS.hlsl", defines, "cs_5_0")));
		if (!compositeCS) {
			logger::error("Failed to compile screenshot compositor.");
			return nullptr;
		}
	}

	D3D11_TEXTURE2D_DESC desc{};
	sourceTexture->GetDesc(&desc);
	if (!compositeTexture || compositeTexture->desc.Width != desc.Width || compositeTexture->desc.Height != desc.Height || compositeTexture->desc.Format != desc.Format) {
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		auto texture = std::make_unique<Texture2D>(desc, "Screenshot::Composite");
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		texture->CreateSRV(srvDesc);
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = desc.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		texture->CreateUAV(uavDesc);
		compositeTexture = std::move(texture);
	}

	auto* context = globals::d3d::context;
	ID3D11ShaderResourceView* srvs[2] = { sceneSRV, uiSRV };
	context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);
	auto* uav = compositeTexture->uav.get();
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	context->CSSetShader(compositeCS.get(), nullptr, 0);
	context->Dispatch((desc.Width + 7) / 8, (desc.Height + 7) / 8, 1);
	srvs[0] = nullptr;
	srvs[1] = nullptr;
	context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);
	uav = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	return compositeTexture->resource.get();
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
	Util::SetResourceName(previewCacheTexture.get(), "Screenshot::PreviewCache");
	if (FAILED(globals::d3d::device->CreateShaderResourceView(
			previewCacheTexture.get(), nullptr, previewCacheSRV.put()))) {
		previewCacheSRV = nullptr;
		previewCacheTexture = nullptr;
		return;
	}
	Util::SetResourceName(previewCacheSRV.get(), "Screenshot::PreviewCache SRV");
}

void ScreenshotFeature::Reset()
{
	if (previewCacheTexture && !Menu::GetSingleton()->IsEnabled) {
		previewCacheSRV = nullptr;
		previewCacheTexture = nullptr;
	}
}

void ScreenshotFeature::ProcessCaptureRequest()
{
	ProcessPendingReadbacks();
	if (captureRequested.exchange(false)) {
		try {
			Capture();
		} catch (const std::exception& e) {
			logger::error("Screenshot capture failed: {}", e.what());
		} catch (...) {
			logger::error("Screenshot capture failed with an unknown exception.");
		}
	}
}

void ScreenshotFeature::ProcessPendingReadbacks()
{
	std::erase_if(pendingReadbacks, [this](PendingScreenshot& screenshot) {
		const HRESULT hr = PopulateScratchImageFromStagingTexture(
			globals::d3d::context,
			screenshot.stagingTexture.get(),
			screenshot.format,
			screenshot.width,
			screenshot.height,
			screenshot.image);
		if (hr == DXGI_ERROR_WAS_STILL_DRAWING) {
			return false;
		}
		screenshot.stagingTexture = nullptr;
		if (FAILED(hr)) {
			logger::error("Failed to map screenshot staging texture.");
		} else {
			// Throwing out of the predicate would leave moved-from entries in pendingReadbacks.
			try {
				EnsureWorkerThread();
				EnqueueScreenshot(std::move(screenshot));
			} catch (const std::exception& e) {
				logger::error("Screenshot failed: could not hand off to worker thread: {}", e.what());
			}
		}
		return true;
	});
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

		Util::FileHelpers::EnsureDirectoryExists(screenshot.outputPath.parent_path());

		const bool saveOk = SaveScreenshotToDisk(
			screenshot.image,
			screenshot.outputPath,
			screenshot.format,
			screenshot.hdrPngBitDepth,
			screenshot.saveAsHdrPng,
			screenshot.saveAsHdrAvif,
			screenshot.saveAsSdrPng);
		if (!saveOk) {
			logger::error(
				"Failed to save {} screenshot.",
				screenshot.saveAsHdrAvif ? "HDR AVIF" : (screenshot.saveAsHdrPng ? "HDR PNG" : "SDR"));
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

	CaptureStateGuard guard(context);
	winrt::com_ptr<ID3D11Texture2D> sourceTextureKeepAlive;
	const auto src = SelectCaptureSource(sourceTextureKeepAlive, /*forCapture=*/true);
	logger::debug("Capturing from {}", src.description);

	if (!src.texture) {
		logger::error("Failed to acquire screenshot source texture ({}).", src.description);
		return;
	}
	ID3D11Texture2D* sourceTexture = src.uiSRV ? CompositeCapture(src.texture, src.srv, src.uiSRV) : src.texture;
	if (!sourceTexture) {
		logger::error("Failed to composite screenshot UI.");
		return;
	}

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
	Util::SetResourceName(stagingTexture.get(), "Screenshot::Staging");

	D3D11_BOX sourceRegion{};
	sourceRegion.left = copyX;
	sourceRegion.top = copyY;
	sourceRegion.front = 0;
	sourceRegion.right = copyX + copyW;
	sourceRegion.bottom = copyY + copyH;
	sourceRegion.back = 1;

	context->CopySubresourceRegion(stagingTexture.get(), 0, 0, 0, 0, sourceTexture, 0, &sourceRegion);

	// Match SelectCaptureSource: only the flat HDR back-buffer path uses HDR encoding.
	// Do not key off DXGI format alone — kFRAMEBUFFER can be float/HDR-sized in SDR mode.
	const bool flatHdrCapture = IsFlatHdrScreenshotCapture();
	if (flatHdrCapture && !IsHdrCaptureFormat(srcDesc.Format)) {
		logger::error("Unsupported HDR screenshot format: {}", static_cast<uint32_t>(srcDesc.Format));
		return;
	}
	const bool saveAsHdrPng = flatHdrCapture && !hdrUseAvif;
	const bool saveAsHdrAvif = flatHdrCapture && hdrUseAvif;
	const bool saveAsSdrPng = !flatHdrCapture && sdrUsePng;

	PendingScreenshot screenshot;
	screenshot.stagingTexture = std::move(stagingTexture);
	screenshot.format = srcDesc.Format;
	screenshot.width = copyW;
	screenshot.height = copyH;
	screenshot.saveAsHdrPng = saveAsHdrPng;
	screenshot.saveAsHdrAvif = saveAsHdrAvif;
	screenshot.saveAsSdrPng = saveAsSdrPng;
	screenshot.hdrPngBitDepth = static_cast<int>(hdrPngBitDepth);
	screenshot.outputPath = BuildScreenshotPath(screenshotPath, saveAsHdrAvif ? ".avif" : (saveAsHdrPng || saveAsSdrPng ? ".png" : ".bmp"));
	screenshot.copyToClipboard = copyToClipboard;
	pendingReadbacks.push_back(std::move(screenshot));
}
#undef I18N_KEY_PREFIX
