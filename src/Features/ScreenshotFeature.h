#pragma once

#include "Feature.h"
#include "Utils/Subrect.h"
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
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
	virtual void PostPostLoad() override;

	void Capture();
	bool applyCropToScreenshot = true;

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

	// SRV-readable copy used when the capture source's own SRV can't be sampled
	// directly (kFRAMEBUFFER on flat aliases the swap-chain backbuffer).
	winrt::com_ptr<ID3D11Texture2D> previewCacheTexture;
	winrt::com_ptr<ID3D11ShaderResourceView> previewCacheSRV;

	void EnsureWorkerThread();
	void StopWorkerThread();
	void EnqueueScreenshot(PendingScreenshot&& screenshot);
	void ScreenshotWorkerLoop();
	void EnsurePreviewCache(ID3D11Texture2D* sourceTexture);
	static void ShowInGameNotification(std::string message);
};
