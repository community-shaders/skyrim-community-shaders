#pragma once

#include "Feature.h"
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

class RenderDoc : public Feature
{
public:
	static RenderDoc* GetSingleton();

	// Core RenderDoc functionality
	bool IsAvailable() const { return renderDocApi != nullptr; }
	void TriggerCapture();
	void TriggerMultiFrameCapture(uint32_t a_frameCount);
	bool HandleCaptureHotkey(uint32_t a_vkKey);
	void SetCaptureFilePathTemplate(const std::string& a_template);
	std::string GetCapturesDirectory() const;
	bool IsCapturing() const;
	std::string GetCapturePath(uint32_t a_index);
	uint32_t GetNumCaptures() const;
	uint32_t CalculateCapturesDiskUsage();
	void ClearFrameCaptures();
	void SetPendingCaptureComments(std::string a_comments);
	void ApplyPendingCaptureComments(uint32_t a_index);

	// Feature overrides
	std::string GetName() override { return "RenderDoc"; }
	virtual std::string GetDisplayName() override { return T("feature.render_doc.name", "RenderDoc"); }
	std::string GetShortName() override { return "RenderDoc"; }
	std::string_view GetCategory() const override { return FeatureCategories::kUtility; }
	bool IsCore() const override { return true; }
	bool IsInMenu() const override { return true; }
	std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.render_doc.description", "In-application RenderDoc capture support and convenience UI."),
			{ T("feature.render_doc.key_feature_1", "Attach comments to captures that appear in RenderDoc UI"),
				T("feature.render_doc.key_feature_2", "Open captures folder"),
				T("feature.render_doc.key_feature_3", "Capture file management") } };
	}
	std::string_view GetShaderDefineName() override { return ""; }
	bool HasShaderDefine(RE::BSShader::Type) override { return false; };

	// Settings & UI
	void DrawSettings() override;
	void RestoreDefaultSettings() override;
	void LoadSettings(json& o_json) override;
	void SaveSettings(json& o_json) override;

	// Resources
	void SetupResources() override;
	void ClearShaderCache() override;

	// Lifecycle
	void Load() override;

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
