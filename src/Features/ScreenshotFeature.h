#pragma once

#include "Feature.h"
#include "Features/Subrect/Subrect.h"
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
	virtual bool IsInMenu() const override { return REL::Module::IsVR(); }

	virtual void DrawSettings() override;
	virtual void LoadSettings(json& a_json) override;
	virtual void SaveSettings(json& a_json) override;

	void ProcessInput(RE::InputEvent* const* a_events);
	void ProcessPendingCapture();
	void Capture();
	bool applyCropToScreenshot = true;

	// Settings
	int screenshotKey = 0x2C; // VK_PRINT_SCREEN
	std::string screenshotPath = "Screenshots";

	bool justPressed = false;
	bool waitingForHotkey = false;

private:
	struct PendingScreenshot {
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
	Subrect::Controller subrect;
	bool captureRequested = false;
	winrt::com_ptr<ID3D11ShaderResourceView> previewSRV;
	ID3D11Texture2D* previewTexture = nullptr;

	void EnsureWorkerThread();
	void StopWorkerThread();
	void EnqueueScreenshot(PendingScreenshot&& screenshot);
	void ScreenshotWorkerLoop();
};
