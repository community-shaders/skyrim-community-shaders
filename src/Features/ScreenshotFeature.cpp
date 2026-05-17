// Screenshot Feature
// Non-blocking screenshot tool for flat (SE/AE) and VR. GPU copy runs on the
// render thread; encoding and disk I/O run on a dedicated worker thread so
// capture does not stall the frame.

#include "Features/ScreenshotFeature.h"
#include "Features/HDRDisplay.h"
#include "Globals.h"
#include "Menu.h"
#include "Utils/FileSystem.h"
#include <DirectXTex.h>
#include <PCH.h>
#include <cstring>
#include <filesystem>
#include <format>
#include <imgui.h>
#include <thread>

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

		auto* destPixels = image.GetPixels();
		for (size_t row = 0; row < destImage->height; ++row) {
			memcpy(
				destPixels + row * destImage->rowPitch,
				static_cast<const uint8_t*>(mapped.pData) + row * mapped.RowPitch,
				destImage->rowPitch);
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

	// Tonemaps an FP16 linear scene-referred ScratchImage in-place: Reinhard
	// c / (1 + c) for the luminance map, then gamma-2.2 for sRGB encoding.
	// Approximates HDRDisplay's on-screen tonemap closely enough for SDR save.
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
					// Clamp negatives - some shaders emit tiny sub-zero values pow() would NaN on.
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
		// FP16 sources carry HDR scene-referred values (peak >> 1.0) that BMP
		// can't represent. Tonemap + gamma-encode before the 8-bit conversion.
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

	// Picks the capture source by where ISHDR wrote the scene this frame:
	//   VR              -> RE::RENDER_TARGETS::kVR_FRAMEBUFFER (SBS).
	//   HDR enabled     -> HDR::HdrTexture (FP16 linear; PrepareBmpImage tonemaps).
	//   otherwise       -> kFRAMEBUFFER (already tonemapped UNORM).
	//
	// HDR::OutputTexture is intentionally not used: on HDR10 swap chains it
	// holds PQ-encoded values regardless of the enableHDR toggle, which save
	// as washed-out BMPs without a color transform.
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

		auto& hdr = globals::features::hdrDisplay;
		if (hdr.loaded && hdr.settings.enableHDR && hdr.hdrTexture && hdr.hdrTexture->resource) {
			src.texture = hdr.hdrTexture->resource.get();
			src.srv = hdr.hdrTexture->srv.get();
			src.description = "HDR::HdrTexture (FP16 linear, will tonemap)";
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

	std::filesystem::path BuildScreenshotPath(const std::string& screenshotPath)
	{
		SYSTEMTIME st;
		GetLocalTime(&st);
		char buf[80];
		snprintf(buf, sizeof(buf), "CS_%04d-%02d-%02d_%02d-%02d-%02d_%03d.bmp",
			st.wYear, st.wMonth, st.wDay,
			st.wHour, st.wMinute, st.wSecond,
			st.wMilliseconds);
		return std::filesystem::path(screenshotPath) / buf;
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

	subrect.LoadSettings(a_json);
}

void ScreenshotFeature::SaveSettings(json& a_json)
{
	a_json["ScreenshotPath"] = screenshotPath;
	a_json["ApplyCropToScreenshot"] = applyCropToScreenshot;
	subrect.SaveSettings(a_json);
}

void ScreenshotFeature::DrawSettings()
{
	Util::Text::Disabled("Capture and save run asynchronously - no frame stall.");
	Util::Text::Disabled(
		"Saves SDR .bmp files. HDR scenes are tonemapped (Reinhard) so the saved\n"
		"image matches what's on screen. For true HDR files with HDR10 metadata,\n"
		"use Xbox Game Bar (Win+G) or your GPU vendor's overlay (saves .jxr).");

	if (ImGui::Button("Take Screenshot Now")) {
		Capture();
	}
	ImGui::SameLine();
	ImGui::Checkbox("Apply crop", &applyCropToScreenshot);

	ImGui::SeparatorText("Output");

	char buf[260];
	strncpy_s(buf, sizeof(buf), screenshotPath.c_str(), _TRUNCATE);
	ImGui::PushItemWidth(-FLT_MIN - 120.0f);  // leave room for Open button + label
	if (ImGui::InputText("##ScreenshotFolder", buf, sizeof(buf))) {
		screenshotPath = buf;
	}
	ImGui::PopItemWidth();
	ImGui::SameLine();
	const bool canOpen = !screenshotPath.empty();
	ImGui::BeginDisabled(!canOpen);
	if (ImGui::Button("Open")) {
		std::error_code ec;
		std::filesystem::create_directories(screenshotPath, ec);
		ShellExecuteA(nullptr, "open", screenshotPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::Text("Folder");
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip(
			"Relative paths resolve against the Skyrim install dir.\n"
			"Absolute paths (e.g. D:\\Captures) save there directly.");
	}

	auto& menuSettings = Menu::GetSingleton()->GetSettings();
	Util::InputComboWidget(
		"Hotkey",
		menuSettings.ScreenshotKey,
		Menu::GetSingleton()->settingScreenshotKey,
		"Change##ScreenshotFeature");

	if (HotkeyCollidesWithVanilla()) {
		Util::Text::Disabled(
			"This hotkey collides with vanilla PrintScreen; both saves will fire.\n"
			"Set bAllowScreenShot=0 in Skyrim.ini to suppress vanilla, or pick a\n"
			"different hotkey above.");
	}

	ImGui::SeparatorText("Crop");

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

		StripAlphaForBmp(image);
		DirectX::ScratchImage convertedImage;
		const DirectX::Image* saveImage = PrepareBmpImage(image, convertedImage);
		if (!saveImage) {
			logger::error("Failed to prepare screenshot image for BMP output.");
			continue;
		}

		Util::FileHelpers::EnsureDirectoryExists(screenshot.outputPath.parent_path());

		HRESULT hr = DirectX::SaveToWICFile(
			*saveImage,
			DirectX::WIC_FLAGS_NONE,
			DirectX::GetWICCodec(DirectX::WIC_CODEC_BMP),
			screenshot.outputPath.c_str());

		if (FAILED(hr)) {
			logger::error("Failed to save screenshot: {:x}", static_cast<unsigned int>(hr));
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
	// ShowHUDMessage must run on the game's main thread; marshall via SKSE's
	// task interface. Third arg dedupes spam-clicks - one toast at a time.
	if (auto* taskInterface = SKSE::GetTaskInterface()) {
		taskInterface->AddTask([msg = std::move(message)]() {
			RE::SendHUDMessage::ShowHUDMessage(msg.c_str(), nullptr, true);
		});
	}
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

	EnsureWorkerThread();
	PendingScreenshot screenshot;
	screenshot.stagingTexture = std::move(stagingTexture);
	screenshot.format = srcDesc.Format;
	screenshot.width = copyW;
	screenshot.height = copyH;
	screenshot.outputPath = BuildScreenshotPath(screenshotPath);
	EnqueueScreenshot(std::move(screenshot));
}
