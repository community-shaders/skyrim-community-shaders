#pragma once

#include "Feature.h"
#include "Utils/Subrect.h"
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>

struct ScreenshotFeature : public Feature
{
	virtual ~ScreenshotFeature();
	virtual std::string GetName() override { return "Screenshot"; }
	virtual std::string GetShortName() override { return "Screenshot"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kUtility; }

	virtual bool SupportsVR() override { return true; }
	virtual bool IsInMenu() const override;

	virtual void DrawSettings() override;
	virtual void LoadSettings(json& a_json) override;
	virtual void SaveSettings(json& a_json) override;
	virtual void Reset() override;
	virtual void PostPostLoad() override;  // installs vanilla-screenshot detour

	void Capture();
	bool applyCropToScreenshot = true;

	// When true, suppress Skyrim's built-in default-path screenshot save (the keypress
	// path that writes Screenshot<N>.png into the game install directory). Explicit-path
	// callers (Papyrus Debug.TakeScreenshot, modder code) still pass through.
	bool suppressVanillaScreenshot = true;

	// Settings
	std::string screenshotPath = "Screenshots";

	std::atomic<bool> captureRequested{ false };

private:
	struct PendingScreenshot
	{
		winrt::com_ptr<ID3D11Texture2D> stagingTexture;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		uint32_t width = 0;
		uint32_t height = 0;
		std::filesystem::path outputPath;
	};

	std::mutex screenshotQueueMutex;
	std::condition_variable screenshotQueueCV;
	std::queue<PendingScreenshot> screenshotQueue;
	std::thread screenshotWorker;
	bool screenshotWorkerRunning = false;
	Util::Subrect::Controller subrect;

	// Lazy SRV-readable copy of the capture source, kept alive across frames.
	// Used when the source slot exposes no native SRV that ImGui can sample - in
	// particular kFRAMEBUFFER on flat with HDRDisplay not loaded, where the
	// underlying swap-chain backbuffer lacks D3D11_BIND_SHADER_RESOURCE.
	winrt::com_ptr<ID3D11Texture2D> previewCacheTexture;
	winrt::com_ptr<ID3D11ShaderResourceView> previewCacheSRV;

	void EnsureWorkerThread();
	void StopWorkerThread();
	void EnqueueScreenshot(PendingScreenshot&& screenshot);
	void ScreenshotWorkerLoop();
	// (Re)allocates the SRV-readable preview cache to match the given source texture's
	// dimensions and format. Used to give the live preview a sampleable view when the
	// chosen capture source's slot doesn't expose one (kFRAMEBUFFER on flat, etc.).
	void EnsurePreviewCache(ID3D11Texture2D* sourceTexture);
	// Posts a non-modal in-game HUD toast via SKSE's task interface so the call
	// is marshalled to the game's main thread.
	static void ShowInGameNotification(std::string message);
};
