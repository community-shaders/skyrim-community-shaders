#pragma once

#include "Utils/Game.h"

struct CloudShadows : Feature
{
private:
	static constexpr std::string_view MOD_ID = "139185";

public:
	static constexpr int kMaxCloudLayers = 32;

	struct alignas(16) Settings
	{
		float Opacity = 0.5f;
		float pad[3];
	};

	Settings settings;

	virtual inline std::string GetName() override { return "Cloud Shadows"; }
	virtual std::string GetDisplayName() override { return T("feature.cloud_shadows.name", "Cloud Shadows"); }
	virtual inline std::string GetShortName() override { return "CloudShadows"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kSky; }
	virtual inline std::string_view GetShaderDefineName() override { return "CLOUD_SHADOWS"; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.cloud_shadows.description", "Adds realistic cloud shadows that move across the landscape, creating dynamic lighting changes as clouds pass overhead, enhancing atmospheric immersion."),
			{ T("feature.cloud_shadows.key_feature_1", "Dynamic cloud shadow projection on terrain and objects"),
				T("feature.cloud_shadows.key_feature_2", "Configurable shadow opacity for artistic control"),
				T("feature.cloud_shadows.key_feature_3", "Real-time shadow movement synchronized with cloud motion"),
				T("feature.cloud_shadows.key_feature_4", "Cubemap-based shadow calculation for accurate projection"),
				T("feature.cloud_shadows.key_feature_5", "Enhanced sky rendering integration") } };
	};

	virtual inline bool HasShaderDefine(RE::BSShader::Type) override { return true; }

	bool overrideSky = false;
	/** @brief Set for a cloud layer drawn outside the reflections capture, which binds that layer's occlusion at t26. */
	bool bindLayerSelfShadow = false;
	/** @brief Set for a main-view cloud layer while the reflections camera has not captured this frame; the layer is redrawn into the capture faces. */
	bool captureLayerFromView = false;
	RE::BSGeometry* captureGeometry = nullptr;
	/**
	 * @brief Applies sky shader render state overrides for cloud shadow capture.
	 *
	 * When overrideSky is set, redirects rendering to the cloud occlusion cubemap
	 * and configures the appropriate blend state and depth resources. For every
	 * cloud layer it also binds the cloud occlusion read by cloud self shadowing (t26).
	 */
	void SkyShaderHacks();

	Texture2D* texCloudShadowLayers[kMaxCloudLayers] = {};
	ID3D11RenderTargetView* cloudShadowLayerRTVs[kMaxCloudLayers][6] = {};
	Texture2D* texCubemapCloudOccCopy = nullptr;

	UINT cubemapMipLevels = 1;
	int currentLayerForDraw = 0;

	uint32_t renderedLayersMask[6] = {};
	uint32_t globalRenderedMask = 0;
	int previouslyRenderedSide = -1;
	/** @brief Set once the occlusion has been cleared for a weather without cloud layers. */
	bool occlusionClearedWithoutClouds = false;

	ID3D11BlendState* cloudShadowBlendState = nullptr;

	/** @brief Faces captured per frame and frames per full cube, matching the reflections camera's cadence. */
	static constexpr uint32_t kCaptureFacesPerFrame = 2;
	static constexpr uint32_t kCaptureFramesPerCube = 6 / kCaptureFacesPerFrame;
	/** @brief Vertex constant buffer slot of the capture face index, CloudShadowsCapture in CloudShadows.hlsli. */
	static constexpr UINT kCaptureFaceSlot = 7;

	/** @brief Frame of the last capture from either source; the occlusion is bound only while it is at most one cube old. */
	uint32_t lastCaptureFrame = 0;
	uint32_t lastReflectionsCaptureFrame = UINT32_MAX;
	uint32_t viewCaptureFrame = UINT32_MAX;
	uint32_t viewCaptureFaces[kCaptureFacesPerFrame] = {};
	bool viewCapturePending = false;

	winrt::com_ptr<ID3D11RasterizerState> captureRasterizerState;
	winrt::com_ptr<ID3D11Buffer> captureFaceCBs[6];

	struct CaptureVertexShader
	{
		std::string defines;
		uint32_t checkedFrame = UINT32_MAX;
		winrt::com_ptr<ID3D11VertexShader> shader;
	};
	std::unordered_map<uint32_t, CaptureVertexShader> captureVertexShaders;

private:
	Util::FrameChecker frameChecker;
	Util::FrameChecker frame_checker[6];

public:
	/** @brief Creates cubemap textures, SRVs, RTVs, and blend state for cloud shadow rendering. */
	virtual void SetupResources() override;

	/** @brief Draws the ImGui settings UI for cloud shadow opacity. */
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	Settings GetCommonBufferData();

	/**
	 * @brief Clears the cloud occlusion render target for a given cubemap face if not yet cleared this frame.
	 * @param side Cubemap face index (0-5).
	 */
	void CheckResourcesSide(int side);
	void PropagateToCompletion(int side);
	/**
	 * @brief Checks if the current sky render pass is rendering clouds to the reflections cubemap and flags it for override.
	 * @param Pass The BSRenderPass being set up for rendering.
	 */
	int FindCloudLayer(RE::BSRenderPass* Pass);
	void ModifySky(RE::BSRenderPass* Pass);

	/** @brief Redraws the current main-view cloud layer into this frame's capture faces with the engine's bound state, then restores that state. */
	void CaptureLayerFromView();
	/** @brief Completes the faces captured from the main view before another capture reuses the layer chain. */
	void PropagateViewCapture();
	/** @brief Returns the capture variant of the bound sky vertex shader, compiled once per descriptor and define set. */
	ID3D11VertexShader* GetCaptureVertexShader();

	/** @brief Copies the cloud occlusion cubemap and binds it as a shader resource for the reflections prepass. */
	virtual void ReflectionsPrepass() override;
	/** @brief Completes the captured faces and binds the cloud occlusion for this frame's draws while a capture is current. */
	virtual void EarlyPrepass() override;

	/** @brief Installs the BSSkyShader hooks after all plugins have loaded. */
	virtual inline void PostPostLoad() override { Hooks::Install(); }

	struct Hooks
	{
		struct BSSkyShader_SetupMaterial
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			stl::write_vfunc<0x6, BSSkyShader_SetupMaterial>(RE::VTABLE_BSSkyShader[0]);
			logger::info("[Cloud Shadows] Installed hooks");
		}
	};
};
