#pragma once

#include "Buffer.h"
#include "CinematicCamera.h"
#include "PhysicalLensCamera.h"
#include "PhysicalLensGhostWave.h"
#include "PhysicalLensImagingGPU.h"
#include "PhysicalLensPresets.h"
#include "PhysicalLensZoom.h"
#include "PostProcessFeature.h"

struct PhysicalLens : PostProcessFeature
{
	static constexpr unsigned LensPresetCount = PhysicalLensOptics::PresetCount + 1;
	struct Settings
	{
		int LensPreset = 0;
		int MaxSources = 8;
		float Intensity = 1.f;
		float ThresholdEV = 3.f;
		float FNumber = 5.6f;
		int ApertureBlades = 0;  // Zero follows the lens preset; existing manual values remain explicit.
		float ApertureRotation = 0.f;
		float Coating = 1.f;
		float FocusDistanceM = 0.f;  // Zero denotes infinity.
		float ZoomPosition = 0.f;
		bool ImageEffects = true;
		bool Diffraction = true;
		float StarburstSize = 10.f;
		float StarburstIntensity = .02f;
		float DirtOpacity = 1.f;
		float DirtDensity = 1.f;
		float DirtSize = 1.f;
		float ScratchLength = 1.f;
		float ScratchSpread = 180.f;
		float SurfaceScatter = .25f;
		float GhostDiffraction = .6f;
		float AlignmentVariation = .2f;
	} settings;

	std::string GetType() const override { return "Physical Lens"; }
	std::string GetDisplayName() const override { return T("feature.post_processing.physical_lens.name", "Physical Lens"); }
	std::string GetDesc() const override { return T("feature.post_processing.physical_lens.description", "Lens ghosts, starbursts, distortion, vignetting and chromatic aberration from highlights visible on screen."); }
	bool WritesToMainTexture() const override { return false; }
	bool DisableInMainLoadingMenu() const override { return true; }
	void SetupResources() override;
	void ClearShaderCache() override;
	void RestoreDefaultSettings() override { settings = {}; }
	void LoadSettings(json&) override;
	void SaveSettings(json&) override;
	void DrawSettings() override;
	/// Resolve preset iris geometry or the user's explicit blade-count override.
	unsigned ApertureBladeCount() const { return PhysicalLensOptics::ApertureBladeCount(settings.LensPreset, settings.ApertureBlades); }
	void Draw(TextureInfo&) override;
	/// Half-resolution pre-exposed ghost radiance for the shared composite.
	TextureInfo GetOutput() const { return outputReady ? TextureInfo{ output->resource.get(), output->srv.get() } : TextureInfo{}; }
	/// Current lens mapping, valid even when the ghost intensity is zero.
	ID3D11ShaderResourceView* GetCameraMapSRV() const { return cameraReady && settings.ImageEffects ? cameraMap->srv.get() : nullptr; }
	/// Diffraction resources for the shared composite; null until this frame's sources are ready.
	ID3D11Buffer* GetDiffractionCB() const { return waveReady ? waveParameters->CB() : nullptr; }
	std::array<ID3D11ShaderResourceView*, 5> GetDiffractionSRVs() const
	{
		return waveReady ? std::array{ waveKernel->SRV(), regions->SRV(), waveSources->SRV(), waveTiles->SRV(), waveRadiance->srv.get() } : std::array<ID3D11ShaderResourceView*, 5>{ nullptr, nullptr, nullptr, outputReady ? waveTiles->SRV() : nullptr, nullptr };
	}

	winrt::com_ptr<ID3D11SamplerState> sampler;

private:
	static constexpr unsigned SourceCount = PhysicalLensOptics::SourceCount, TileCount = 64 * 36, CandidateCount = 2 * TileCount;
	using Parameters = PhysicalLensOptics::Parameters;
	struct OpticalState
	{
		float fNumber = 5.6f, focusM = 0, rotation = 0, roundness = 0;
		float focalMM = 0, gateWidthMM = 0;
		unsigned blades = 9;
		bool linked = false;
	} optical;
	float BladeCurvature() const { return optical.linked ? -1.f - optical.roundness : float(cachedImagingModel.minimumFNumber) / optical.fNumber; }
	PhysicalLensImaging::GPUParameters OpticalParameters() const;
	float ResolveCameraFocus(const CinematicCamera::PhysicalCameraState& camera);
	winrt::com_ptr<ID3D11ComputeShader> focusCS;
	std::unique_ptr<Texture2D> focusTexture, previousFocus;
	std::unique_ptr<ConstantBuffer> focusParameters;
	std::array<winrt::com_ptr<ID3D11Texture2D>, 3> focusReadback;
	std::array<bool, 3> focusPending{};
	std::array<unsigned, 3> focusFrames{};
	unsigned lastFocusFrame = ~0u, completedFocusFrame = 0;
	float resolvedFocusM = 10;
	PhysicalLensOptics::Model cachedLens{};
	int cachedPreset = -1;
	float cachedFNumber = 0;
	float cachedCoating = -1;
	float cachedFocusDistance = -1, cachedZoom = -1, cachedFocalMM = -1;
	bool cachedLinked = false, configurationValid = true;
	unsigned cachedCameraBladeCount = 0;
	float cachedCameraRoundness = -1, cameraApertureExtent = 1;
	PhysicalLensImaging::Model cachedImagingModel{}, cachedCameraInfinity{};
	double projectionFocal = 0;
	unsigned cachedPathCount = 0;
	std::unique_ptr<ConstantBuffer> parameters;
	std::unique_ptr<StructuredBuffer> candidates, regions, sources, rays, paths, jobs, bundles, pupils;
	std::unique_ptr<StructuredBuffer> planTable, planStatus, planRequests;
	winrt::com_ptr<ID3D11Buffer> planArguments;
	winrt::com_ptr<ID3D11UnorderedAccessView> planArgumentsUAV;
	winrt::com_ptr<ID3D11ComputeShader> requestPlansCS, bakePlansCS;
	bool planningValid = false;
	float plannedSensorWidth = 0, plannedSensorHeight = 0;
	winrt::com_ptr<ID3D11Buffer> indirect, splatIndices;
	winrt::com_ptr<ID3D11UnorderedAccessView> indirectUAV;
	winrt::com_ptr<ID3D11ShaderResourceView> indirectSRV;
	std::unique_ptr<Texture2D> output, blueViolet;
	std::array<std::unique_ptr<Texture2D>, 3> areaLayers;
	std::array<std::unique_ptr<ConstantBuffer>, 9> areaParameters;
	std::unique_ptr<Texture2D> ghostDiffraction, ghostMaterial;
	std::unique_ptr<ConstantBuffer> ghostWaveParameters;
	std::array<winrt::com_ptr<ID3D11ComputeShader>, 4> ghostWaveShaders;
	PhysicalLensGhostWave::Parameters cachedGhostWave{};
	bool ghostWaveValid = false;
	unsigned lastGhostFrame = ~0u;
	bool UpdateGhostWave();
	std::array<winrt::com_ptr<ID3D11RenderTargetView>, 2> channelTargets;
	std::array<std::unique_ptr<ConstantBuffer>, 3> channelParameters;
	winrt::com_ptr<ID3D11ComputeShader> extractCS, prepareCS, splitCS, selectCS, jobsCS, expandCS, traceCS, shadeCS, reduceCS, resolveCS;
	winrt::com_ptr<ID3D11VertexShader> ghostVS;
	winrt::com_ptr<ID3D11PixelShader> ghostPS, ghostVioletPS, ghostAreaPS;
	winrt::com_ptr<ID3D11BlendState> additive;
	winrt::com_ptr<ID3D11RasterizerState> twoSided;
	bool outputReady = false;
	bool cameraReady = false, cameraValid = false;
	std::unique_ptr<Texture2D> cameraMap;
	std::unique_ptr<ConstantBuffer> cameraParameters, cameraTableParameters;
	winrt::com_ptr<ID3D11ComputeShader> cameraCS;
	PhysicalLensImaging::GPUParameters cachedCameraParameters{};
	PhysicalLensImaging::CameraTableParameters cachedCameraTable{};
	void UpdateCameraMapping(float aspect);
	bool waveReady = false, waveValid = false;
	std::array<std::unique_ptr<Texture2D>, 3> waveTextures;
	std::unique_ptr<Texture2D> waveMax, waveRadiance;
	std::unique_ptr<StructuredBuffer> waveWorkTiles;
	winrt::com_ptr<ID3D11Buffer> waveDispatch;
	winrt::com_ptr<ID3D11UnorderedAccessView> waveDispatchUAV;
	winrt::com_ptr<ID3D11ShaderResourceView> waveDispatchSRV;
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 7> waveMaxMipSRVs;
	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 7> waveMaxMipUAVs;
	std::unique_ptr<StructuredBuffer> waveKernel, waveSources, waveTiles;
	std::unique_ptr<ConstantBuffer> waveParameters;
	std::array<winrt::com_ptr<ID3D11ComputeShader>, 9> waveShaders;
	winrt::com_ptr<ID3D11ComputeShader> waveSourcesCS, waveTilesCS, waveTailCS;
	PhysicalLensImaging::GPUParameters cachedWaveOptics{};
	PhysicalLensImaging::CameraTableParameters cachedWaveTable{};
	PhysicalLensImaging::DiffractionParameters cachedWaveParameters{};
	bool UpdateDiffraction(unsigned width, unsigned height);
	void CompileShaders();
	void SanitizeSettings();
};
