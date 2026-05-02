#pragma once

#include "VrsLutManager.h"
#include "VrsSrsBuilder.h"

#include <cstdint>
#include <d3d11.h>
#include <vector>
#include <wrl/client.h>

#include <nvapi.h>

/// NvVrsController — manages the NVAPI VRS pipeline for DX11.
class NvVrsController
{
public:
	/// Reason VRS was disabled this frame (sticky for diagnostics).
	enum class DisableReason : uint32_t
	{
		None = 0,
		SettingsDisabled,
		InvalidContext,
		InitFailed,
		SurfaceFailed,
		BindSurfaceFailed,
		BindRateTableFailed,
		UIPass,
		TerrainBlending,
	};

	enum class RebuildReason : uint32_t
	{
		None = 0,
		FirstCreate,
		ResolutionChanged,
	};

	/// Per-frame parameters assembled by VRS::UpdateVRShadingRateState.
	struct Settings
	{
		bool enable = false;
		uint32_t srsPreset = 0;  // 0=Default, 1=Faster, 2=Extreme
		uint32_t lutPreset = 0;  // 0=Default, 1=Full 1×1, 2=Full 4×4
		VrsSrsBuilder::EyeSubrectUV leftSubrectUV{};
		VrsSrsBuilder::EyeSubrectUV rightSubrectUV{};
		float ringGrowthRate = 0.25f;
		bool enableDirectionalRates = true;
		bool enableBoundaryDither = true;
		bool enableDiagnostics = false;
	};

	/// display = backbuffer size (tile grid basis), render = after DRS scaling.
	struct FrameInfo
	{
		int displayWidth = 0;
		int displayHeight = 0;
		int renderWidth = 0;
		int renderHeight = 0;
	};

	/// Diagnostics snapshot exposed to the settings UI.
	struct DebugState
	{
		bool initialized = false;
		bool supported = false;
		bool active = false;
		int lastNvapiStatus = 0;
		uint32_t tileWidth = 0;
		uint32_t tileHeight = 0;
		uint64_t patternRebuildCount = 0;
		uint64_t patternReuseCount = 0;

		uint64_t lastTileLevelCount[SrsLevel::kCount] = {};
		uint32_t lastViewportCount = 0;
		DisableReason lastDisableReason = DisableReason::None;
		RebuildReason lastRebuildReason = RebuildReason::None;
		uint64_t failureCount = 0;
		const char* lastFailureSite = "None";
	};

	NvVrsController() = default;
	~NvVrsController();

	void Update(const Settings& settings, const FrameInfo& frameInfo, ID3D11Device* device, ID3D11DeviceContext* context);
	void Disable(ID3D11DeviceContext* context);
	void Suspend(ID3D11DeviceContext* context);
	void Resume(ID3D11DeviceContext* context);
	void SetLastDisableReason(DisableReason reason);

	const DebugState& GetDebugState() const { return debugState; }
	ID3D11ShaderResourceView* GetDebugVisualizationSRV() const;

private:
	DebugState debugState{};

	Settings currentSettings{};
	FrameInfo currentFrame{};

	Microsoft::WRL::ComPtr<ID3D11Texture2D> shadingRateSurface;
	ID3D11NvShadingRateResourceView* shadingRateResourceView = nullptr;
	std::vector<uint8_t> patternBuffer;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> debugVisualizationTexture;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> debugVisualizationSRV;
	std::vector<uint32_t> debugVisualizationBuffer;

	VrsSrsBuilder srsBuilder;

	uint32_t lastSurfaceWidth = 0;
	uint32_t lastSurfaceHeight = 0;
	bool surfaceDirty = false;
	bool rateTableDirty = true;
	uint32_t lastLutPreset = UINT32_MAX;  // force first-frame rebind
	bool EnsureInitialized(ID3D11Device* device);
	bool EnsureSurface(ID3D11Device* device);
	void UpdateSurfaceData(ID3D11DeviceContext* context);

	bool BindRateTable(ID3D11DeviceContext* context);
	bool BindSurface(ID3D11DeviceContext* context);
	uint32_t QueryViewportCount(ID3D11DeviceContext* context, uint32_t fallback) const;
	void ReleaseResources();
	void MarkError(int status, const char* site);
};
