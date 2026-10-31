#pragma once

#include "Feature.h"
#include "Utils/Subrect.h"
#include <DirectXTex.h>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

class Texture2D;

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
	/** @brief Releases the screenshot compositor shader. */
	virtual void ClearShaderCache() override;
	/** @brief Resets transient state (no-op for this feature). */
	virtual void Reset() override;
	/** @brief Called after all features are loaded (no-op for this feature). */
	virtual void PostPostLoad() override;

	/** @brief Captures a screenshot from the current back buffer and enqueues it for async encoding and save. */
	void Capture();
	/** @brief Hands finished GPU readbacks to the worker, then executes Capture() if one is pending. Render thread only; call before the wrapped buffers are cleared. */
	void ProcessCaptureRequest();
	bool applyCropToScreenshot = true;

	// Settings
	std::string screenshotPath = "Screenshots";
	// HDR PNG quantization (7-16); used when HDR Display captures the back buffer.
	unsigned int hdrPngBitDepth = 11;
	bool hdrUseAvif = false;
	// SDR output.
	bool sdrUsePng = true;
	// After save, put the file path on the clipboard (CF_HDROP).
	bool copyToClipboard = false;
	// Suppress the game's own PrintScreen capture by clearing bAllowScreenShot in memory.
	bool disableVanillaScreenshot = true;

	std::atomic<bool> captureRequested{ false };

private:
	struct PendingScreenshot
	{
		winrt::com_ptr<ID3D11Texture2D> stagingTexture;
		DirectX::ScratchImage image;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		uint32_t width = 0;
		uint32_t height = 0;
		std::filesystem::path outputPath;
		bool saveAsHdrPng = false;
		bool saveAsHdrAvif = false;
		bool saveAsSdrPng = false;
		int hdrPngBitDepth = 11;
		bool copyToClipboard = false;
		// Suppress the game's own PrintScreen capture by clearing bAllowScreenShot in memory.
		bool disableVanillaScreenshot = true;
	};

	std::mutex screenshotQueueMutex;
	std::condition_variable screenshotQueueCV;
	std::queue<PendingScreenshot> screenshotQueue;
	std::thread screenshotWorker;
	bool screenshotWorkerRunning = false;
	// Render thread only: the worker never touches the immediate context.
	std::vector<PendingScreenshot> pendingReadbacks;
	Util::Subrect::Controller subrect;
	// bAllowScreenShot as loaded from Skyrim.ini, captured before we first override it.
	std::optional<bool> vanillaScreenshotDefault;

	// SRV-readable copy used when the capture source's own SRV can't be sampled
	// directly (kFRAMEBUFFER on flat aliases the swap-chain backbuffer).
	winrt::com_ptr<ID3D11Texture2D> previewCacheTexture;
	winrt::com_ptr<ID3D11ShaderResourceView> previewCacheSRV;
	std::unique_ptr<Texture2D> compositeTexture;
	winrt::com_ptr<ID3D11ComputeShader> compositeCS;

	/** @brief Polls in-flight staging copies without stalling and enqueues the finished ones for encoding. */
	void ProcessPendingReadbacks();
	/** @brief Writes bAllowScreenShot:Display in memory from disableVanillaScreenshot, restoring the ini value when off. */
	void ApplyVanillaScreenshotSetting();
	void EnsureWorkerThread();
	void StopWorkerThread();
	void EnqueueScreenshot(PendingScreenshot&& screenshot);
	void ScreenshotWorkerLoop();
	void EnsurePreviewCache(ID3D11Texture2D* sourceTexture);
	ID3D11Texture2D* CompositeCapture(ID3D11Texture2D* sourceTexture, ID3D11ShaderResourceView* sceneSRV, ID3D11ShaderResourceView* uiSRV);
	static void ShowInGameNotification(std::string message);
};
