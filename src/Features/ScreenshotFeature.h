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
	/** @brief Stops the background screenshot worker thread on destruction. */
	virtual ~ScreenshotFeature();
	virtual std::string GetName() override { return "Screenshot"; }
	virtual std::string GetDisplayName() override { return T("feature.screenshot.name", "Screenshot"); }
	virtual std::string GetShortName() override { return "Screenshot"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kUtility; }

	/** @brief Returns true, indicating this feature's settings are always visible in the menu. */
	virtual bool IsInMenu() const override;

	/** @brief Draws the ImGui settings UI for screenshot path, format, crop, and hotkey configuration. */
	virtual void DrawSettings() override;
	virtual void LoadSettings(json& a_json) override;
	virtual void SaveSettings(json& a_json) override;
	/** @brief Resets transient state (no-op for this feature). */
	virtual void Reset() override;
	/** @brief Called after all features are loaded (no-op for this feature). */
	virtual void PostPostLoad() override;

	/** @brief Captures a screenshot from the current back buffer and enqueues it for async encoding and save. */
	void Capture();
	/** @brief Checks for a pending capture request and executes Capture() if one is pending. Called after HDR Present processing. */
	void ProcessCaptureRequest();
	bool applyCropToScreenshot = true;

	// Settings
	std::string screenshotPath = "Screenshots";
	// HDR PNG quantization (7-16); used when HDR Display captures the back buffer.
	unsigned int hdrPngBitDepth = 11;
	// SDR output (HDR captures always use PNG).
	bool sdrUsePng = true;
	// After save, put the file path on the clipboard (CF_HDROP).
	bool copyToClipboard = false;

	std::atomic<bool> captureRequested{ false };

private:
	struct PendingScreenshot
	{
		winrt::com_ptr<ID3D11Texture2D> stagingTexture;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		uint32_t width = 0;
		uint32_t height = 0;
		std::filesystem::path outputPath;
		bool saveAsHdrPng = false;
		bool saveAsSdrPng = false;
		int hdrPngBitDepth = 11;
		bool copyToClipboard = false;
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
