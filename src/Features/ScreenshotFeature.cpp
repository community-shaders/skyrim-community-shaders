// Screenshot Feature
// Lossless, non-blocking VR screenshot tool.
// GPU copy is issued on the render thread; encoding and disk I/O run on a
// dedicated worker thread, so capturing does not stall the frame.
// Currently VR-only (no SE/AE test client available). Lossless recording
// support is planned.

#include "Features/ScreenshotFeature.h"
#include "Globals.h"
#include "Menu.h"
#include "Utils/FileSystem.h"
#include <DirectXTex.h>
#include <PCH.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
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

	// Picks the runtime-appropriate post-composite, post-UI render target.
	// Flat: kFRAMEBUFFER (aliases the swapchain backbuffer).
	// VR:   the SBS framebuffer staged for HMD compositor submission.
	RE::RENDER_TARGETS::RENDER_TARGET CaptureTarget()
	{
		return globals::game::isVR ? kVRFramebufferTarget : RE::RENDER_TARGETS::kFRAMEBUFFER;
	}

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

	const DirectX::Image* PrepareBmpImage(const DirectX::ScratchImage& sourceImage, DirectX::ScratchImage& convertedImage)
	{
		const auto metadata = sourceImage.GetMetadata();
		if (SUCCEEDED(DirectX::Convert(
				sourceImage.GetImages(),
				sourceImage.GetImageCount(),
				metadata,
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
	ImGui::Text("=== Screenshot Settings ===");

	char buf[256];
	strncpy_s(buf, sizeof(buf), screenshotPath.c_str(), _TRUNCATE);
	if (ImGui::InputText("Screenshot Folder", buf, sizeof(buf))) {
		screenshotPath = buf;
	}

	auto& menuSettings = Menu::GetSingleton()->GetSettings();
	Util::InputComboWidget(
		"Screenshot Key:",
		menuSettings.ScreenshotKey,
		Menu::GetSingleton()->settingScreenshotKey,
		"Change##ScreenshotFeature");

	if (ImGui::Button("Take Screenshot Now")) {
		Capture();
	}
	ImGui::SameLine();
	ImGui::Checkbox("Apply Crop to Screenshot", &applyCropToScreenshot);
	Util::Text::Disabled("Capture and saving run asynchronously with no frame stall.");

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	// VR's source target packs both eyes side-by-side; only show the left half in the preview.
	const float previewVisibleWidth = globals::game::isVR ? 0.5f : 1.0f;

	auto renderer = globals::game::renderer;
	if (renderer) {
		auto& targets = renderer->GetRuntimeData().renderTargets;
		auto& mainTarget = targets[CaptureTarget()];

		// Resolve via SRV/RTV when the slot's .texture is null (kFRAMEBUFFER on flat).
		// The keep-alive com_ptr stays in scope until DrawEditor returns, holding the
		// QueryInterface'd texture alive while ImGui samples its SRV.
		winrt::com_ptr<ID3D11Texture2D> previewTextureKeepAlive;
		ID3D11Texture2D* slotTexture = ResolveSlotTexture(mainTarget, previewTextureKeepAlive);

		ID3D11ShaderResourceView* previewView = mainTarget.SRV;
		if (slotTexture) {
			if (previewTexture != slotTexture) {
				previewSRV = nullptr;
				previewTexture = slotTexture;
				globals::d3d::device->CreateShaderResourceView(slotTexture, nullptr, previewSRV.put());
			}
			if (previewSRV) {
				previewView = previewSRV.get();
			}
		}

		subrect.DrawEditor(previewView, slotTexture, previewVisibleWidth);
	} else {
		subrect.DrawEditor(nullptr, nullptr, previewVisibleWidth);
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
		} else {
			logger::info("Saved screenshot to {}", screenshot.outputPath.string());
		}
	}
	CoUninitialize();
}

void ScreenshotFeature::Capture()
{
	auto device = globals::d3d::device;
	auto context = globals::d3d::context;

	if (!device || !context)
		return;

	ID3D11Texture2D* sourceTexture = nullptr;
	winrt::com_ptr<ID3D11Texture2D> sourceTextureKeepAlive;

	const auto targetIndex = CaptureTarget();
	auto renderer = globals::game::renderer;
	if (renderer) {
		logger::debug("Selecting capture target index {}", static_cast<int>(targetIndex));
		auto& targets = renderer->GetRuntimeData().renderTargets;
		auto& mainTarget = targets[targetIndex];
		sourceTexture = ResolveSlotTexture(mainTarget, sourceTextureKeepAlive);
	}

	if (!sourceTexture) {
		logger::error("Failed to acquire screenshot source texture (target {}).", static_cast<int>(targetIndex));
		return;
	}

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
