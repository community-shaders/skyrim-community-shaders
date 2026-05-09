#pragma once

#include "Feature.h"
#include "Utils/Subrect.h"
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <queue>
#include <thread>

struct ScreenshotFeature : public Feature
{
	virtual ~ScreenshotFeature();
	virtual std::string GetName() override { return "Screenshot"; }
	virtual std::string GetShortName() override { return "Screenshot"; }
	virtual std::string_view GetCategory() const override { return "Tools"; }

	virtual bool SupportsVR() override { return true; }
	virtual bool IsInMenu() const override;

	virtual void DrawSettings() override;
	virtual void LoadSettings(json& a_json) override;
	virtual void SaveSettings(json& a_json) override;
	virtual void Reset() override;

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
		// True when source was HDR display output (PQ-encoded R10G10B10A2 or scRGB
		// R16F). The BMP save path doesn't tonemap so the file looks washed out;
		// the worker surfaces this in the in-game toast.
		bool sourceWasHDREncoded = false;
	};

	std::mutex screenshotQueueMutex;
	std::condition_variable screenshotQueueCV;
	std::queue<PendingScreenshot> screenshotQueue;
	std::thread screenshotWorker;
	bool screenshotWorkerRunning = false;
	Util::Subrect::Controller subrect;
	winrt::com_ptr<ID3D11ShaderResourceView> previewSRV;
	ID3D11Texture2D* previewTexture = nullptr;

	void EnsureWorkerThread();
	void StopWorkerThread();
	void EnqueueScreenshot(PendingScreenshot&& screenshot);
	void ScreenshotWorkerLoop();
	// Posts a non-modal in-game HUD toast via SKSE's task interface so the call
	// is marshalled to the game's main thread.
	static void ShowInGameNotification(std::string message);
};
