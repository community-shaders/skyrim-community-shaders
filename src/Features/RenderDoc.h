#pragma once

#include <chrono>
#include <filesystem>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

using json = nlohmann::json;

// Forward declarations
struct ID3D11Device;
struct RENDERDOC_API_1_7_0;

// Structure to hold capture file information for UI display
struct CaptureFileInfo
{
	std::string filename;
	std::string sizeStr;
	uint64_t fileSize;
	std::filesystem::file_time_type lastWriteTime;
	std::filesystem::path fullPath;
	bool deletionFailed = false;
	std::string deletionErrorMessage;
};

/**
 * @brief In-application RenderDoc capture support. A component of the Developer Tools feature, not a feature itself.
 */
class RenderDoc
{
public:
	static RenderDoc* GetSingleton();

	// Core RenderDoc functionality
	bool IsAvailable() const { return renderDocApi != nullptr; }
	/** @brief True when renderdoc.dll is in the process, whether loaded by CS or injected by the RenderDoc UI. */
	static bool IsLoaded();
	void TriggerCapture();
	/** @brief Captures with the configured frame count after checking disk space, as the panel's button does. @return False when it could not start. */
	bool CaptureNow() { return TriggerConfiguredCapture(); }
	/** @brief Turns capture on or off for the next start, forcing frame annotations on while it is on and restoring them after. */
	void SetCaptureEnabled(bool a_enabled);
	void TriggerMultiFrameCapture(uint32_t a_frameCount);
	bool HandleCaptureHotkey(uint32_t a_vkKey);
	void SetCaptureFilePathTemplate(const std::string& a_template);
	std::string GetCapturesDirectory() const;
	std::string GetCapturePath(uint32_t a_index);
	uint32_t GetNumCaptures() const;
	uint32_t CalculateCapturesDiskUsage();
	void ClearFrameCaptures();
	void SetPendingCaptureComments(std::string a_comments);
	void ApplyPendingCaptureComments(uint32_t a_index);

	// Lifecycle and UI, driven by DeveloperTools
	/** @brief Renders the RenderDoc tab of the Developer Tools panel. */
	void DrawSettings();
	void RestoreDefaultSettings();
	void LoadSettings(json& o_json);
	void SaveSettings(json& o_json);
	/** @brief Loads renderdoc.dll when capture is enabled. Called from DeveloperTools::Load. */
	void Load();

	// Helper methods
	std::filesystem::path GetCapturesPath() const;
	std::filesystem::path GetRenderDocDllPath() const;
	std::string BuildAutomaticCaptureComments(const std::string& userComments) const;
	void ApplyAutomaticCommentsToNewCaptures();
	void ClearFailedDeletions();

	/**
	 * Gets the warning message to display when RenderDoc capture is active
	 * @return Formatted warning message for overlay display
	 */
	std::string GetOverlayWarningMessage() const;

private:
	bool TriggerConfiguredCapture(bool a_checkDiskSpace = true);
	uint32_t GetCaptureFrameCount() const;
	void SetCaptureFrameCount(uint32_t a_frameCount);
	uint64_t GetRequiredCaptureSpaceBytes() const;
	bool HasSufficientDiskSpaceForConfiguredCapture(uint64_t* a_requiredSpaceBytes = nullptr) const;

	// Cache management for capture files
	void RefreshCaptureFileCache();
	const std::vector<CaptureFileInfo>& GetCachedCaptureFiles();
	void InvalidateCaptureCache();  // Force cache refresh

public:
	RenderDoc() = default;
	~RenderDoc() = default;

	// Delete copy/move operations
	RenderDoc(const RenderDoc&) = delete;
	RenderDoc& operator=(const RenderDoc&) = delete;
	RenderDoc(RenderDoc&&) = delete;
	RenderDoc& operator=(RenderDoc&&) = delete;

	// RenderDoc library and API
	void* renderDocModule = nullptr;
	RENDERDOC_API_1_7_0* renderDocApi = nullptr;

	// Pending comments to attach to the next capture (applied when a new capture is detected)
	std::string pendingCaptureComments;
	mutable std::mutex pendingCommentsMutex;

	// RenderDoc capture enable setting
	bool enableRenderDocCapture = false;
	uint32_t captureFrameCount = 1;

	// Track the last capture count we've processed for automatic comments
	uint32_t lastCaptureCount = 0;

	// Cache for capture file information to avoid frequent filesystem access
	std::vector<CaptureFileInfo> cachedCaptureFiles;
	std::chrono::steady_clock::time_point cacheLastUpdate;
	bool cacheValid = false;
	bool wasSectionVisible = false;  // Track if RenderDoc section was visible last frame

	// Track files that failed to delete for UI feedback
	std::unordered_map<std::filesystem::path, std::string> failedDeletions;

	static constexpr uint64_t kMinCaptureSpaceBytes = 100ULL * 1024ULL * 1024ULL;   // 100 MB minimum free space floor
	static constexpr uint64_t kObservedPerFrameBytes = 256ULL * 1024ULL * 1024ULL;  // ~256 MB per frame observed for multi-frame captures
	static constexpr uint32_t kCacheRefreshIntervalSeconds = 5;                     // Cache refresh interval
	static constexpr size_t kCommentsBufferSize = 1024;                             // Size of comments input buffer
	static constexpr uint32_t kMinCaptureFrameCount = 1;
	static constexpr uint32_t kMaxCaptureFrameCount = 120;
};
