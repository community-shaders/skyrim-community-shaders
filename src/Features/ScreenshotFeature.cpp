// Screenshot Feature
// Lossless, non-blocking screenshot tool for both flat (SE/AE) and VR.
// GPU copy is issued on the render thread; encoding and disk I/O run on a
// dedicated worker thread, so capturing does not stall the frame.
// Lossless recording support is planned.

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
	// VR analog of kFRAMEBUFFER. CommonLib reuses RE::RENDER_TARGETS::kTOTAL (==114) as the index
	// of this slot in the renderTargets array — confusing because kTOTAL primarily means "count of
	// standard targets". RenderDoc capture audit (2026-05-09) shows this slot:
	//   - Receives Skyrim's final ISCopy draw (input: kMENUBG, the post-UI SBS composite).
	//   - Is then CopySubresourceRegion'd into two DXGI-shared per-eye R8G8B8A8_SRGB textures
	//     that SteamVR/OpenVR's compositor consumes.
	// So functionally it is the side-by-side framebuffer headed to the HMD — the VR equivalent of
	// the swapchain backbuffer that kFRAMEBUFFER aliases on flat.
	// TODO: replace with a properly named CommonLibSSE-NG enumerator (e.g. kFRAMEBUFFER_VR) once
	// upstream lands; this constant is the local stopgap.
	constexpr auto kVRFramebufferTarget = RE::RENDER_TARGETS::kTOTAL;

	// Describes the chosen capture source. SRV is non-owning; texture lifetime is
	// either tied to the renderer slot it lives in, or to a com_ptr the caller holds.
	struct CaptureSource
	{
		ID3D11Texture2D* texture = nullptr;
		ID3D11ShaderResourceView* srv = nullptr;
		// When true, the preview path must copy through the SRV-readable cache instead of
		// using slot.SRV directly. The kFRAMEBUFFER fallback sets this because the slot's
		// SRV (when one exists) is for the swap-chain backbuffer, which ImGui's DX11
		// backend can't sample correctly even when the pointer is non-null. VR's SBS slot
		// and HDRDisplay::OutputTexture have known-good shader-readable SRVs and don't.
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

	// Tonemaps an FP16 linear scene-referred ScratchImage in-place using Reinhard
	// (c / (1 + c)) plus an sRGB-curve encode. Reinhard is the cheap-and-correct
	// fit here: it always lands in [0,1] regardless of input peak luminance, runs
	// in three FLOPs per channel, and gives a result close enough to HDRDisplay's
	// on-screen output for screenshot purposes. Caller passes the result of
	// PopulateScratchImageFromStagingTexture; we overwrite the pixels in-place.
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
				// Standard sRGB encode: linear -> 1.055 * pow(c, 1/2.4) - 0.055 above
				// the linear segment threshold of 0.0031308. We use the gamma-2.2
				// approximation (XMVectorPow with 1/2.2) - visually indistinguishable
				// for screenshot use and avoids the per-channel branch.
				const XMVECTOR invGamma = XMVectorReplicate(1.0f / 2.2f);
				for (size_t i = 0; i < width; ++i) {
					XMVECTOR c = inPixels[i];
					// Clamp negatives to zero - some shaders write tiny sub-zero values
					// from float math that pow() would NaN on.
					c = XMVectorMax(c, XMVectorZero());
					// Reinhard tonemap on RGB; preserve alpha.
					const XMVECTOR rgb = XMVectorDivide(c, XMVectorAdd(c, one));
					const XMVECTOR gammaCorrected = XMVectorPow(rgb, invGamma);
					// Re-pack with original alpha.
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
		// HDR display mode produces FP16 linear scene-referred values (peak >> 1.0)
		// that BMP can't represent. Tonemap to [0,1] and gamma-encode before the
		// 8-bit format conversion. SDR captures (8-bit sRGB / 10-bit PQ outputs we
		// no longer source) skip this step entirely.
		const auto metadata = sourceImage.GetMetadata();
		if (metadata.format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
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

	// Resolves the underlying ID3D11Texture2D from a renderer slot. Some slots (notably
	// kFRAMEBUFFER on flat, which aliases the swapchain backbuffer) populate the view
	// pointers but leave .texture null - HDRDisplay accesses the slot via .SRV/.RTV.
	// Pull the texture out of the view so a single capture path works for every slot.
	// The com_ptr `holder` keeps the QueryInterface refcount alive across the call site.
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

	// Picks the best capture source for the current runtime. The decision tree
	// follows where ISHDR actually wrote the scene this frame, NOT where it would
	// be displayed:
	//
	//  - VR:                              SBS framebuffer (slot 114, kVRFramebufferTarget).
	//  - Flat + HDRDisplay::enableHDR=ON: HDR::HdrTexture - ISHDR was redirected here
	//                                     and wrote FP16 linear scene-referred values
	//                                     (peak >> 1.0). PrepareBmpImage runs a Reinhard
	//                                     tonemap + gamma encode so the BMP matches what
	//                                     the user sees on their HDR monitor.
	//  - Flat + HDRDisplay loaded but
	//    enableHDR=OFF, OR HDRDisplay
	//    not loaded:                      vanilla kFRAMEBUFFER - ISHDR wrote tonemapped
	//                                     UNORM values 0-1 directly here. Already SDR,
	//                                     BMPs cleanly, no tonemap needed.
	//
	// We deliberately never source HDR::OutputTexture: when the swap chain is HDR10
	// (R10G10B10A2 PQ) and the monitor is HDR-capable, OutputTexture holds PQ-encoded
	// values regardless of the enableHDR toggle - HDRDisplay's compute shader still
	// runs and PQ-encodes the SDR scene for the swap chain. Saving those PQ bytes as
	// BMP without a color transform produces washed-out files.
	//
	// `holder` is a keep-alive com_ptr for the kFRAMEBUFFER fallback path (where we
	// QueryInterface the texture out of an SRV) - it must outlive any use of the returned
	// texture. For VR/HdrTexture paths the texture is owned elsewhere and `holder` stays
	// empty.
	CaptureSource SelectCaptureSource(winrt::com_ptr<ID3D11Texture2D>& holder)
	{
		CaptureSource src;
		auto* renderer = globals::game::renderer;
		if (!renderer) {
			return src;
		}

		if (globals::game::isVR) {
			auto& slot = renderer->GetRuntimeData().renderTargets[kVRFramebufferTarget];
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

		// Either HDRDisplay isn't loaded, or it's loaded but enableHDR is off. In
		// both cases ISHDR wrote tonemapped UNORM values to kFRAMEBUFFER, so saving
		// it directly produces a correct SDR BMP without any color transform.
		auto& slot = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER];
		src.texture = ResolveSlotTexture(slot, holder);
		src.srv = slot.SRV;
		src.needsPreviewCache = true;
		src.description = "kFRAMEBUFFER";
		return src;
	}

	// Detour for Skyrim's built-in screenshot save function. Symbol is named
	// "po3tweaks::ScreenshotToConsole::DebugNotification" in the Address Library
	// (despite the name, this is the function the keypress handler and Papyrus
	// Debug.TakeScreenshot call to actually write Screenshot<N>.png to the game
	// directory and post the in-game "ScreenShot: File '%s' created." toast).
	//
	// Address Library ids covering all editions:
	//   SE 1.5.97    id 35882   sse_addr 0x1405c0610
	//   AE 1.6.1170  id 36853   RVA      0x653240
	//   VR           via SE id  vr_addr  0x1405c8b30 (status 3 in database.csv)
	//
	// True when the user's screenshot hotkey is the single key vanilla Skyrim binds
	// to its built-in PrintScreen save. Anything else (a different key, a chord,
	// an unbound combo) means the user can have both: ours on their hotkey,
	// vanilla on PrintScreen.
	bool HotkeyCollidesWithVanilla()
	{
		const auto& combo = Menu::GetSingleton()->GetSettings().ScreenshotKey;
		return combo.size() == 1 &&
		       combo[0].GetDevice() == InputDeviceType::Keyboard &&
		       combo[0].GetKey() == VK_SNAPSHOT;
	}

	// Pass through whenever an explicit path is provided (Papyrus's
	// Debug.TakeScreenshot(filename) and modder code) so scripts and mods that
	// take programmatic screenshots keep working. The keypress-driven default-
	// path save is suppressed only when our hotkey collides with vanilla's
	// PrintScreen - otherwise the user gets both saves on the same key, which
	// is just clutter. With separate hotkeys both paths run independently.
	struct VanillaScreenshotHook
	{
		static void thunk(char* a_explicitPath, int a_format)
		{
			if (a_explicitPath == nullptr && HotkeyCollidesWithVanilla()) {
				return;
			}
			func(a_explicitPath, a_format);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

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
	// Address ids and pass-through behavior documented on VanillaScreenshotHook.
	stl::write_thunk_jmp<VanillaScreenshotHook>(
		REL::RelocationID(35882, 36853).address());
	logger::info("Installed vanilla screenshot detour");
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
	// Description first so users know what the feature does before they hit the button.
	Util::Text::Disabled("Capture and save run asynchronously - no frame stall.");
	Util::Text::Disabled(
		"Saves SDR .bmp files. HDR scenes are tonemapped (Reinhard) so the saved\n"
		"image matches what's on screen. For true HDR files with HDR10 metadata,\n"
		"use Xbox Game Bar (Win+G) or your GPU vendor's overlay (saves .jxr).");

	// Top-level actions
	if (ImGui::Button("Take Screenshot Now")) {
		Capture();
	}
	ImGui::SameLine();
	ImGui::Checkbox("Apply crop", &applyCropToScreenshot);

	// Output section
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

	// Status: explain what happens to the vanilla PrintScreen save based on
	// whether the user's hotkey collides with it. Mirrors VanillaScreenshotHook.
	if (HotkeyCollidesWithVanilla()) {
		Util::Text::Disabled("Vanilla PrintScreen save suppressed (this hotkey owns it).");
	} else {
		Util::Text::Disabled("Vanilla PrintScreen save still works alongside this hotkey.");
	}

	// Crop section (Subrect renders preset combo, Save/Delete/Reset buttons,
	// UV sliders, and the live drag-crop preview).
	ImGui::SeparatorText("Crop");

	// VR's source target packs both eyes side-by-side; only show the left half in the preview.
	const float previewVisibleWidth = globals::game::isVR ? 0.5f : 1.0f;

	// Same selection logic as Capture(); preview always reflects what would be saved.
	// kFRAMEBUFFER's slot.SRV (when present) is for the swap-chain backbuffer and ImGui
	// can't sample it correctly, so SelectCaptureSource flags that path as
	// needsPreviewCache=true and we copy through an SRV-readable cache instead.
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

	subrect.DrawEditor(previewView, src.texture, previewVisibleWidth);
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
	// RE::SendHUDMessage::ShowHUDMessage must run on the game's main thread; the
	// worker thread dispatches via SKSE's task interface. Capture by value because
	// the worker has long since moved past the queued message by the time the task
	// fires. Third arg `cancelIfAlreadyQueued` true dedupes if the player spam-takes
	// captures - they get one toast at a time, not a stack.
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

	// VR's side-by-side source packs both eyes; flat is a single image.
	// Treating one eye's pixel width as the canonical crop space unifies both paths.
	const uint32_t eyeWidth = globals::game::isVR ? srcDesc.Width / 2 : srcDesc.Width;
	const uint32_t eyeHeight = srcDesc.Height;

	uint32_t copyX = 0;
	uint32_t copyY = 0;
	uint32_t copyW = eyeWidth;
	uint32_t copyH = eyeHeight;

	if (applyCropToScreenshot) {
		auto region = subrect.GetPixelRegion(eyeWidth, eyeHeight);
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
