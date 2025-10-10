#pragma once

#include <d3d11.h>
#include <windows.h>
#include <string>

#include <Renderdoc/renderdoc_app.h>

class RenderDoc
{
public:
	static RenderDoc* GetSingleton()
	{
		static RenderDoc singleton;
		return &singleton;
	}

	// Initialize RenderDoc API
	bool Initialize();

	// Check if RenderDoc is available
	bool IsAvailable() const { return renderDocApi != nullptr; }

	// Trigger a frame capture
	void TriggerCapture();

	// Check if a capture is currently in progress
	bool IsCapturing() const;

	// Get capture path for current index
	std::string GetCapturePath(uint32_t a_index);

	// Get the number of captures made
	uint32_t GetNumCaptures() const;

	uint32_t CalculateCapturesDiskUsage();

	void ClearFrameCaptures();

private:
	RenderDoc() = default;
	~RenderDoc() = default;

	// Delete copy/move operations
	RenderDoc(const RenderDoc&) = delete;
	RenderDoc& operator=(const RenderDoc&) = delete;
	RenderDoc(RenderDoc&&) = delete;
	RenderDoc& operator=(RenderDoc&&) = delete;

	// RenderDoc library and API
	HMODULE renderDocModule = nullptr;
	RENDERDOC_API_1_6_0* renderDocApi = nullptr;
};
