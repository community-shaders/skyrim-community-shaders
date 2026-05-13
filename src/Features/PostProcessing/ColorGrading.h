#pragma once
#include "PostProcessFeature.h"

#include "Buffer.h"

struct ColorGrading : public PostProcessFeature
{
	virtual inline std::string GetType() const override { return "Color Grading and Tone Mapping"; }
	virtual inline std::string GetDesc() const override { return "Color grading operations and multiple tone mapping options."; }
	virtual inline bool DisableInMainLoadingMenu() const override { return true; }

	virtual bool SupportsVR() const override { return true; }

	template <size_t N, typename T>
	constexpr auto make_array(T value) -> std::array<T, N>
	{
		std::array<T, N> a{};
		for (auto& x : a)
			x = value;
		return a;
	}

	const std::string outputPath = "SKSE\\Plugins\\CommunityShaders\\PostProcessing\\ColorGrading";

	struct Settings
	{
		bool skipLDR = false;
		bool skipLUT = false;

		// ASC CDL
		float4 slope = { 1.f, 1.f, 1.f, 0.f };
		float4 power = { 1.f, 1.f, 1.f, 0.f };
		float4 cdlOffset = { 0.f, 0.f, 0.f, 0.f };

		// Lift Gamma Gain
		float4 lift = { 0.f, 0.f, 0.f, 0.f };
		float4 gamma = { 0.f, 0.f, 0.f, 0.f };
		float4 gain = { 1.f, 1.f, 1.f, 1.f };

		// Input/Output Gamma
		float4 inOutGamma = { 1.f, 1.f, 1.f, 1.f };

		// OKLCH
		float4 oklchSaturation = { 1.f, 1.f, 0.f, 0.f };
		std::array<float4, 7> oklchColorMixer = {
			float4{ 0.f, 1.f, 0.f, 0.f }, float4{ 0.f, 1.f, 0.f, 0.f }, float4{ 0.f, 1.f, 0.f, 0.f },
			float4{ 0.f, 1.f, 0.f, 0.f }, float4{ 0.f, 1.f, 0.f, 0.f }, float4{ 0.f, 1.f, 0.f, 0.f },
			float4{ 0.f, 1.f, 0.f, 0.f }
		};

		// Contrast
		float4 contrast = { 1.f, 1.f, 1.f, 0.f };
		float4 pivot = { 0.18f, 0.18f, 0.18f, 0.f };

		// Exposure/Temperature/Tint
		float4 exposureTemperatureTint = { 1.f, 65.f, 0.f, 0.f };

		// Shadows/Midtones/Highlights
		float4 shadowsGain = { 1.f, 1.f, 1.f, 0.f };
		float4 midtonesGain = { 1.f, 1.f, 1.f, 0.f };
		float4 highlightsGain = { 1.f, 1.f, 1.f, 0.f };
		float4 shadowsHighlightsRange = { 0.f, 0.3f, 0.55f, 1.f };

		// SMH color offsets
		float4 shadowsOffset = { 0.f, 0.f, 0.f, 0.f };
		float4 midtonesOffset = { 0.f, 0.f, 0.f, 0.f };
		float4 highlightsOffset = { 0.f, 0.f, 0.f, 0.f };

		std::string currentTonemapper = "GT7";
		std::array<float4, 2> tonemapParams = { float4{ 1.f, 2.f, 0.f, 0.f }, float4{ 0.f, 0.f, 0.f, 0.f } };
		float3 gameCinematicBlend = { 1.0f, 1.0f, 1.0f };
		float gameFadeBlend = 1.0f;
		float gameTintBlend = 1.0f;
		bool useLog = false;
		uint logType = 0;
		bool invertLog = false;
		bool enableTonemap = true;
		int processColorSpace = 0;
	} settings;

	// Computed matrices (not serialized)
	std::array<float3, 3> inputToWorkingMatrix = { float3{ 1.0f, 0.0f, 0.0f }, float3{ 0.0f, 1.0f, 0.0f }, float3{ 0.0f, 0.0f, 1.0f } };
	std::array<float3, 3> workingToTonemapMatrix = { float3{ 1.0f, 0.0f, 0.0f }, float3{ 0.0f, 1.0f, 0.0f }, float3{ 0.0f, 0.0f, 1.0f } };
	std::array<float3, 3> tonemapToOutputMatrix = { float3{ 1.0f, 0.0f, 0.0f }, float3{ 0.0f, 1.0f, 0.0f }, float3{ 0.0f, 0.0f, 1.0f } };
	std::array<float3, 3> workingToXYZMatrix = { float3{ 1.0f, 0.0f, 0.0f }, float3{ 0.0f, 1.0f, 0.0f }, float3{ 0.0f, 0.0f, 1.0f } };
	std::array<float3, 3> xyzToWorkingMatrix = { float3{ 1.0f, 0.0f, 0.0f }, float3{ 0.0f, 1.0f, 0.0f }, float3{ 0.0f, 0.0f, 1.0f } };

	int tonemapperType = 10;

	enum class LogType : uint32_t
	{
		ACEScct = 1 << 0,
		ARRIlogC4 = 1 << 1,
		SonySLog3 = 1 << 2,
		Invert = 1 << 3
	};

	struct alignas(16) ColorCB
	{
		float4 asccdl[3];
		float4 liftgammagain[3];  // lift，gamma，gain
		float4 inOutGamma;        // .z = input gamma, .w = output gamma
		float4 oklchSaturation;
		float4 oklchColorMixer[7];
		float4 contrast;
		float4 pivot;
		float4 exposureTemperatureTint;
		float4 shadows;
		float4 midtones;
		float4 highlights;
		float4 shadowsHighlightsRange;  // shadowBegin, shadowEnd, highlightBegin, highlightEnd

		float4 tonemapParams[2];
		float4 inputToWorking[3];    // sRGB → working color space
		float4 workingToTonemap[3];  // working → tonemapper native space
		float4 tonemapToOutput[3];   // tonemapper native → output space

		float4 workingToXYZ[3];  // working → CIE XYZ (for white balance)
		float4 xyzToWorking[3];  // CIE XYZ → working (for white balance)

		float4 workingWhitePoint;  // .xy = native white chromaticity of working space

		float4 shadowsOffset;  // SMH color offsets
		float4 midtonesOffset;
		float4 highlightsOffset;

		// game value
		float4 cinematic;  // saturation, brightness, contrast
		float4 fade;       // color
		float4 tint;       // color

		uint logType;
		uint skipLDR;
		uint skipLUT;
		uint enableTonemap;
		uint enableColorSpaceTransform;
		uint enableHDR;     // HDR display is enabled (auto-set from HDR feature)
		float hdrPeakNits;  // Maximum display brightness in nits for HDR
		uint pad;
	};
	std::unique_ptr<ConstantBuffer> colorCB = nullptr;

	std::unique_ptr<Texture2D> texColor = nullptr;
	std::unique_ptr<Texture3D> texLUT = nullptr;

	static constexpr int LUTDim = 64;

	bool recompileFlag = true;
	bool saveImagesFlag = false;
	winrt::com_ptr<ID3D11ComputeShader> colorgradingCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> lutgenCS = nullptr;

	winrt::com_ptr<ID3D11SamplerState> linearSampler = nullptr;

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileComputeShaders();

	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json&) override;
	virtual void SaveSettings(json&) override;

	virtual void DrawSettings() override;

	virtual void Draw(TextureInfo&) override;
	void UpdateColorSpaceTransforms(bool hdrEnabled = false);

	void OutputTextures();
};
