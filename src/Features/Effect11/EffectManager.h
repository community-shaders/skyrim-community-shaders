#pragma once

#include "Effects/ENBAdaptation.h"
#include "Effects/ENBBloom.h"
#include "Effects/ENBEffect.h"
#include "Effects/ENBEffectPostPass.h"
#include "Effects/ENBLens.h"

enum class TimeOfDay1Index : int
{
	Dawn,
	Sunrise,
	Day,
	Sunset
};

enum class TimeOfDay2Index : int
{
	Dusk,
	Night,
	InteriorDay,
	InteriorNight
};

enum class TimeOfDayFactorIndex : int
{
	Dawn,
	Sunrise,
	Day,
	Sunset,
	Dusk,
	Night,
	Count
};

/**
 * Return the singleton instance of EffectManager.
 * @returns Reference to the global EffectManager instance.
 */

/**
 * Execute the full post-processing effect pipeline for all configured effects.
 */

/**
 * Initialize the EffectManager and create any required resources.
 */

/**
 * Apply current settings to all managed effects and resources.
 */

/**
 * Load persisted EffectManager configuration and state.
 */

/**
 * Save current EffectManager configuration and state to persistent storage.
 */

/**
 * Register configurable settings used by the manager and its effects.
 */

/**
 * Update effect-specific common variables on the provided D3D11 effect object.
 * @param effect Pointer to an ID3DX11Effect to receive updated common variables.
 */

/**
 * Create resources shared across multiple effects (geometry, states, shaders).
 */

/**
 * Create shared quad geometry used for full-screen passes.
 */

/**
 * Create shared rasterizer, blend and other render state objects.
 */

/**
 * Create vertex/pixel shaders and related resources used for texture copy passes.
 */

/**
 * Create the compute shader and its constant buffer used for color correction.
 */

/**
 * Render a list or ordering view of managed effects (for UI or debug visualization).
 */

/**
 * Update the cached CommonVariableData values (timer, weather, time-of-day factors).
 */

/**
 * Container for common variables applied to effects: timer, weather, time-of-day factors,
 * and two scalar factors for night/day and interior blending.
 */

/**
 * Setting identifiers for runtime-configurable toggles and parameters.
 */

/**
 * Get the current set of common variable values prepared for application to effects.
 * @returns Reference to the current CommonVariableData.
 */

/**
 * Query whether the EffectManager has completed initialization.
 * @returns `true` if initialized, `false` otherwise.
 */

/**
 * Execute a single effect, performing common-variable setup and gating execution using an optional enable-setting ID.
 * @param effect Effect instance to execute.
 * @param enableSettingID Optional setting identifier that, if not equal to 0xFFFFFFFF, controls whether the effect runs.
 */

/**
 * Copy a source shader resource view into a render target using the manager's copy shader pass.
 * @param source Shader resource view providing the source texture.
 * @param destination Render target view to receive the copied texture.
 */

/**
 * Apply color correction to a texture via the manager's compute shader.
 * @param textureUAV Unordered access view of the texture to be modified in place.
 */
class EffectManager
{
public:
	static EffectManager& GetSingleton();

	// Effect execution
	void ExecuteEffects();

	// Lifecycle
	void Initialize();

	void Apply();
	void Load();
	void Save();

	void RegisterSettings();

	// Common variable management
	void UpdateCommonVariablesForEffect(ID3DX11Effect* effect);

public:
	ENBBloom enbBloom;
	ENBLens enbLens;
	ENBAdaptation enbAdaptation;
	ENBEffect enbEffect;
	ENBEffectPostPass enbEffectPostPass;

	// Common resources shared across effects
	void CreateCommonResources();

	// Shared D3D resources
	winrt::com_ptr<ID3D11Buffer> quadVertexBuffer;
	winrt::com_ptr<ID3D11InputLayout> inputLayout;
	winrt::com_ptr<ID3D11RasterizerState> rasterizerState;
	winrt::com_ptr<ID3D11BlendState> blendState;

	// Copy shader resources
	winrt::com_ptr<ID3D11VertexShader> copyVertexShader;
	winrt::com_ptr<ID3D11PixelShader> copyPixelShader;

	// Color correction compute shader resources
	winrt::com_ptr<ID3D11ComputeShader> colorCorrectionComputeShader;
	winrt::com_ptr<ID3D11Buffer> colorCorrectionConstantBuffer;

	void CreateQuadGeometry();
	void CreateRenderStates();
	void CreateCopyShaders();
	void CreateColorCorrectionShader();

	void RenderEffectsList();

	// Common variable data (updated once, applied to all effects)
	struct CommonVariableData
	{
		float timer[4];
		float weather[4];
		float timeOfDay1[4];
		float timeOfDay2[4];
		float eNightDayFactor;
		float eInteriorFactor;
	} commonData;

	void UpdateCommonData();

	struct SettingIDs
	{
		uint32_t useBloom = 0xFFFFFFFF;
		uint32_t useLens = 0xFFFFFFFF;
		uint32_t useAdaptation = 0xFFFFFFFF;
		uint32_t usePostPass = 0xFFFFFFFF;

		uint32_t enableMultipleWeathers = 0xFFFFFFFF;
		uint32_t enableLocationWeather = 0xFFFFFFFF;

		uint32_t nightTime = 0xFFFFFFFF;
		uint32_t sunriseTime = 0xFFFFFFFF;
		uint32_t dawnDuration = 0xFFFFFFFF;
		uint32_t dayTime = 0xFFFFFFFF;
		uint32_t sunsetTime = 0xFFFFFFFF;
		uint32_t duskDuration = 0xFFFFFFFF;

		uint32_t brightness = 0xFFFFFFFF;
		uint32_t gammaCurve = 0xFFFFFFFF;
	} ids;

	const CommonVariableData& GetCommonData() const { return commonData; }

	bool IsInitialized() const { return initialized; }

	// Execute a single effect with perf events and common variable setup
	void ExecuteEffect(Effect& effect, uint32_t enableSettingID = 0xFFFFFFFF);

	// Texture copy using pixel shader
	void CopyTexture(ID3D11ShaderResourceView* source, ID3D11RenderTargetView* destination);

	// Color correction using compute shader
	void ApplyColorCorrection(ID3D11UnorderedAccessView* textureUAV);

private:
	bool initialized = false;
};