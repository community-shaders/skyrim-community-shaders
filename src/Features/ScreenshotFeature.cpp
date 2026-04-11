// Screenshot Feature
// Lossless, non-blocking VR screenshot tool.
// GPU copy is issued on the render thread; encoding and disk I/O run on a
// dedicated worker thread, so capturing does not stall the frame.
// Currently VR-only (no SE/AE test client available). Lossless recording
// support is planned.

#include "Features/ScreenshotFeature.h"
#include "Globals.h"
#include <RE/B/ButtonEvent.h>
#include <RE/I/InputEvent.h>
#include <DirectXTex.h>
#include <filesystem>
#include <ctime>
#include <imgui.h>
#include <thread>
#include <cstring>
#include <algorithm>
#include <PCH.h> 

namespace
{
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

	std::filesystem::path BuildScreenshotPath(const std::string& screenshotPath)
	{
		std::time_t t = std::time(nullptr);
		struct tm localTime{};
		localtime_s(&localTime, &t);
		char buf[64];
		std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", &localTime);
		return std::filesystem::path(screenshotPath) / (std::string("CS_") + buf + ".bmp");
	}
}

ScreenshotFeature::~ScreenshotFeature()
{
	StopWorkerThread();
}

void ScreenshotFeature::LoadSettings(json& a_json)
{
	if (a_json.contains("ScreenshotKey")) screenshotKey = a_json["ScreenshotKey"];
	if (a_json.contains("ScreenshotPath")) screenshotPath = a_json["ScreenshotPath"];
	if (a_json.contains("ApplyCropToScreenshot")) applyCropToScreenshot = a_json["ApplyCropToScreenshot"];
	subrect.LoadSettings(a_json);
}

void ScreenshotFeature::SaveSettings(json& a_json)
{
	a_json["ScreenshotKey"] = screenshotKey;
	a_json["ScreenshotPath"] = screenshotPath;
	a_json["ApplyCropToScreenshot"] = applyCropToScreenshot;
	subrect.SaveSettings(a_json);
}

void ScreenshotFeature::DrawSettings()
{
	if (!REL::Module::IsVR()) {
		return;
	}

	ImGui::Text("=== Screenshot Settings ===");

	char buf[256];
	strncpy_s(buf, screenshotPath.c_str(), sizeof(buf));
	if (ImGui::InputText("Screenshot Folder", buf, sizeof(buf))) {
		screenshotPath = buf;
	}

	if (waitingForHotkey) {
		ImGui::Button("Waiting for input... (ESC to cancel)");
	} else {
		const std::string hotkeyLabel = "Bind Screenshot Hotkey (ID: " + std::to_string(screenshotKey) + ")";
		if (ImGui::Button(hotkeyLabel.c_str())) {
			waitingForHotkey = true;
			justPressed = true;  // block until next key-up cycle
		}
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Click and press any key to bind.\nCurrent: %d (ScanCode/ID)", screenshotKey);
	}

	if (ImGui::Button("Take Screenshot Now")) {
		Capture();
	}
	ImGui::SameLine();
	ImGui::Checkbox("Apply Crop to Screenshot", &applyCropToScreenshot);
	ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Capture and saving run asynchronously with no frame stall.");

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	if (renderer) {
		auto& targets = renderer->GetRuntimeData().renderTargets;
		RE::RENDER_TARGETS::RENDER_TARGET targetIndex = RE::RENDER_TARGETS::kTOTAL;
		auto& mainTarget = targets[targetIndex];

		ID3D11ShaderResourceView* previewView = mainTarget.SRV;
		if (mainTarget.texture) {
			if (previewTexture != mainTarget.texture) {
				previewSRV = nullptr;
				previewTexture = mainTarget.texture;
				globals::d3d::device->CreateShaderResourceView(mainTarget.texture, nullptr, previewSRV.put());
			}
			if (previewSRV) {
				previewView = previewSRV.get();
			}
		}

		subrect.DrawEditor(previewView, mainTarget.texture, 0.5f);
	} else {
		subrect.DrawEditor(nullptr, nullptr, 0.5f);
	}
}

void ScreenshotFeature::ProcessInput(RE::InputEvent* const* a_events)
{
	if (!a_events) return;

	for (RE::InputEvent* event = *a_events; event; event = event->next) {
		if (event->GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
			auto buttonEvent = event->AsButtonEvent();
			if (!buttonEvent) continue;
			int key = buttonEvent->GetIDCode();

			if (waitingForHotkey) {
				if (buttonEvent->IsDown()) {
					if (key == 1 /* Esc */) {
						waitingForHotkey = false;
						justPressed = false;
					} else {
						screenshotKey = key;
						waitingForHotkey = false;
						justPressed = false;
						logger::info("Bound screenshot key: {}", key);
					}
				}
				return;
			}
			if (key == screenshotKey) {
				if (buttonEvent->IsDown() && !justPressed) {
					justPressed = true;
				} else if (buttonEvent->IsUp()) {
					if (justPressed) {
						captureRequested = true;
					}
					justPressed = false;
				}
			}
		}
	}
}

void ScreenshotFeature::ProcessPendingCapture()
{
	if (!captureRequested) {
		return;
	}

	captureRequested = false;
	Capture();
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

		try {
			std::filesystem::create_directories(screenshot.outputPath.parent_path());
		} catch (const std::exception& e) {
			logger::error("Failed to create screenshot directory: {}", e.what());
			continue;
		}

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
}

void ScreenshotFeature::Capture()
{
	auto device = globals::d3d::device;
	auto context = globals::d3d::context;

	if (!device || !context)
		return;

	ID3D11Texture2D* sourceTexture = nullptr;

	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	if (renderer) {
		RE::RENDER_TARGETS::RENDER_TARGET targetIndex = RE::RENDER_TARGETS::kTOTAL;
		logger::debug("VR Detected: Selecting Capture Target Index kTOTAL ({})", (int)targetIndex);

		auto& targets = renderer->GetRuntimeData().renderTargets;
		auto& mainTarget = targets[targetIndex];
		
		if (mainTarget.texture) {
			sourceTexture = mainTarget.texture;
			logger::debug("Capturing from BSGraphics::Renderer Index {}", (int)targetIndex);
		}
	}

	if (!sourceTexture) {
		logger::error("Failed to acquire screenshot source texture from kTOTAL.");
		return;
	}

	D3D11_TEXTURE2D_DESC srcDesc{};
	sourceTexture->GetDesc(&srcDesc);

	uint32_t copyX = 0;
	uint32_t copyY = 0;
	uint32_t copyW = srcDesc.Width;
	uint32_t copyH = srcDesc.Height;

	if (applyCropToScreenshot) {
		auto leftEyeRegion = subrect.GetLeftEyePixelRegion(srcDesc.Width, srcDesc.Height);
		copyX = leftEyeRegion.x;
		copyY = leftEyeRegion.y;
		copyW = leftEyeRegion.w;
		copyH = leftEyeRegion.h;
	} else {
		copyW = srcDesc.Width / 2;
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
