#pragma once
#include "Feature.h"

struct LensEffects : Feature
{
	static LensEffects* GetSingleton()
	{
		static LensEffects singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "Lens Effects"; }
	virtual inline std::string GetShortName() override { return "LensEffects"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type) override { return true; }
	virtual inline std::string_view GetShaderDefineName() override { return "LENS_EFFECTS"; }
	virtual std::string_view GetCategory() const override { return "Post-Processing"; }
	virtual inline bool SupportsVR() override { return false; };  //
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Lens effects mimic how camera lenses respond to different levels of light and environmental factors. Artists can tune intensity, color, shape, and placement to fit their intended style.",
			{ "Motion based chromatic aberration",
				"Starburst lens flare",
				"Lens ghosting",
				"Lens halo effect",
				"Lens glare",
				"Adjustable sun glare",
				"Weather based frost vignette" }
		};
	}

	virtual inline void DataLoaded() override { RE::GetINISetting("bLensFlare:Imagespace")->data.b = true; }
	virtual inline void PostPostLoad() override { Hooks::Install(); }
	virtual void SetupResources() override;
	void CompileShaders();

	virtual void CheckOverride();
	void LookupShader(int desc);

	void AppendOcclusionLUT();
	void SetupOcclusionMask();
	void SetupBurstEffect();
	void SetupSunGlareEffect();
	void SetupLensGlareEffect();
	void SetupHaloEffect();
	void SetupGhostEffect();
	void SetupIceEffect();
	void BypassShader();
	void SetupCAEffect();

	ConstantBuffer* SettingsCB = nullptr;
	ID3D11BlendState* BlendState[3] = {};
	ID3D11RasterizerState* Raster = nullptr;
	D3D11_VIEWPORT viewport{};

	ID3D11SamplerState* LinearSampler = nullptr;
	ID3D11SamplerState* PointSampler = nullptr;
	ID3D11SamplerState* DepthSampler = nullptr;
	ID3D11SamplerState* PointMirrorSampler = nullptr;

	ID3D11ShaderResourceView* AtlasTexSRV = nullptr;
	ID3D11ShaderResourceView* IceTexSRV = nullptr;
	ID3D11ShaderResourceView* SunTexSRV = nullptr;

	ID3D11Texture2D* SunOcclusionLUT = nullptr;
	ID3D11Texture2D* SunOcclusionLUT_AT = nullptr;
	ID3D11UnorderedAccessView* SunOcclusionUAV = nullptr;
	ID3D11UnorderedAccessView* SunOcclusionUAV_AT = nullptr;
	ID3D11ShaderResourceView* SunOcclusionSRV = nullptr;

	ID3D11PixelShader* SunOcclusionMaskPixelShader = nullptr;
	ID3D11PixelShader* ChromaticAberrationPixelShader = nullptr;
	ID3D11VertexShader* BypassVertexShader = nullptr;

	ID3D11VertexShader* BurstVertexShader = nullptr;
	ID3D11PixelShader* BurstPixelShader = nullptr;

	ID3D11VertexShader* SunGlareVertexShader = nullptr;
	ID3D11PixelShader* SunGlarePixelShader = nullptr;

	ID3D11VertexShader* LensGlareVertexShader = nullptr;
	ID3D11PixelShader* LensGlarePixelShader = nullptr;

	ID3D11VertexShader* HaloVertexShader = nullptr;
	ID3D11PixelShader* HaloPixelShader = nullptr;

	ID3D11VertexShader* GhostVertexShader = nullptr;
	ID3D11PixelShader* GhostPixelShader = nullptr;

	ID3D11VertexShader* IceVertexShader = nullptr;
	ID3D11PixelShader* IcePixelShader = nullptr;

	uintptr_t* skyrim_FlareData = nullptr;
	uint32_t* skyrim_RunFlarePtr = nullptr;

	RE::NiPoint3* skyrim_SunPosition = nullptr;
	float* skyrim_SunGlareScale = nullptr;
	bool sunVisble;

	void(__fastcall* gFlareApplyFunc)(RE::NiCamera*, void*, uint64_t) = nullptr;
	void* gFlareShader = nullptr;

	static constexpr int ghostpasses = 20;

	bool overrideShader = false;
	bool useCloudLUT = false;
	bool upscalingActive = false;
	uint frameIdx = 5;

	DirectX::XMFLOAT4A GetSunPosition();
	DirectX::XMFLOAT4A GetSunColor();
	void GetWeatherShader();
	float GetWeatherPrecip();
	bool CheckWeatherChange();
	void UpdateWeatherBasedDisable();

	bool disableSunFX = false;
	float weatherFadeout = 0.0f;
	float snowPrecipValue = 0.0f;
	uint32_t PrevWeatherID = 0;
	uint32_t WeatherID = 0;
	float SunScale;
	static inline std::array<uint32_t, 31> weatherDisables = { { 0x00D299E, 0x02006AEC, 0x02001407, 0x02018DBB, 0x02018DBC, 0x02018DBD,
		0x02001407, 0x000D9329, 0x0200959F, 0x00105941, 0x000923FD, 0x00048C14,
		0x0010FEF8, 0x0010D9EC, 0x000923FD, 0x00105941, 0x0010199F, 0x000C821E,
		0x0010FE7E, 0x0010A7A7, 0x0010A23E, 0x0010A239, 0x0010A235, 0x0010A232,
		0x00106635, 0x0010A242, 0x00105945, 0x00105944, 0x00105943, 0x00105942,
		0x000c8221 } };

	static const inline std::string customSettingsPath = "Data\\SKSE\\Plugins\\CommunityShaders\\Overrides\\DEFAULT_LensEffects.json";
	bool presetLoaded = false;
	bool settingsLoaded = false;

	void RefreshToggles();
	virtual void RestoreDefaultSettings() override;
	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	bool PresetFileExists();
	void ExportAsPreset();

	struct MainSettings
	{
		//Starburst
		float SB_Scale = 0.25f;
		float SB_Intensity = 0.5f;

		uint SB_EnableBlades = false;
		float SB_BladeInt = 0.3f;
		float SB_BladeVertices = 6.0f;
		float SB_BladeSplay = 0.0f;
		float SB_BladeRotation = 180.0f;
		float SB_BladeLength = 0.8f;
		float SB_BladeBaseWidth = 1.5f;
		float SB_BladeWidth = 1.1f;
		float SB_BladeTaper = 1.0f;
		float SB_BladeFeather = 35.0f;
		float SB_BladeFadePow = 0.5f;
		float SB_BladeFadeDist = 3.0f;
		float SBEX_BladeSplayLen = 0.0f;

		uint SB_EnableRays = true;
		float SB_RandomRaysInt = 1.2f;
		float SB_RandomRaysVolume = 0.3f;
		float SB_RandomRaysLength = 1.0f;
		float SB_RandomRaysWidth = 0.073f;

		//Ghosts
		float GH_Scale = 0.40f;
		float GH_Intensity = 0.4f;
		float GH_Saturation = 1.0f;
		uint GH_EnableClampOffset = true;
		float GH_ClampOffset = 0.5f;

		float GH_Size = 0.0f;
		float GH_Offset = 0.0f;
		float GH_Shape = 0.0f;
		float GH_Roundness = 0.0f;
		float GH_Rotation = 0.0f;
		float GH_Feather = 0.0f;
		float GH_CAScale = 0.0f;
		float GH_MoveCurve = 0.0f;
		float GH_InnerInt = 0.0f;

		//Lens Glare
		float GL_Scale = 0.35f;
		float GL_Intensity = 0.3f;
		uint GL_DynPosition = false;
		float GL_XAxisOffset = 0.5f;
		float GL_YAxisOffset = 0.1f;
		float GL_MaxRotation = 50.0f;
		float GL_CutDepth = 0.86f;
		float GL_Radius = 0.88f;
		float GL_TipFade = 1.0f;

		//Halo
		float HL_Scale = 0.45f;
		float HL_Intensity = 0.16f;
		uint HL_EnableExp = true;
		uint HL_FlipExpOffset = false;
		float HL_ExpMinSize = 0.46f;
		float HL_ExpMaxSize = 0.4f;
		float HL_RotationSpeed = 0.22f;
		float HL_LineVolume = 5.0f;
		float HL_LineLength = 0.11f;
		float HL_LineWidth = 0.085f;
		float HL_LineTaper = 0.15f;
		float HL_ColorShift = 0.52f;

		//Sun Glare
		float SG_Scale = 0.5f;
		float SG_Intensity = 1.0f;
		float SG_OuterInt = 1.0f;
		float SG_OuterFade = 0.8f;

		//LensCA
		float CA_Intensity = 0.25f;
		float CA_Threshold = 0.015f;
		float CA_MaxOffset = 0.003f;

		//LensIce
		float LI_Intensity = 0.50f;
		float LIEX_FadeFactor = 0.0f;
		//64  256

		DirectX::XMFLOAT4A SB_Color = DirectX::XMFLOAT4A(0.0f, 0.0f, 0.0f, 0.0f);
		DirectX::XMFLOAT4A SG_Color = DirectX::XMFLOAT4A(0.0f, 0.0f, 0.0f, 0.0f);
		DirectX::XMFLOAT4A HL_Color = DirectX::XMFLOAT4A(0.0f, 0.0f, 0.0f, 0.0f);
		DirectX::XMFLOAT4A GH_Color = DirectX::XMFLOAT4A(0.0f, 0.0f, 0.0f, 0.0f);
		DirectX::XMFLOAT4A GH_Atlas = DirectX::XMFLOAT4A(0.0f, 0.0f, 0.0f, 0.0f);
		DirectX::XMFLOAT4A LI_Color = DirectX::XMFLOAT4A(0.0f, 0.0f, 0.0f, 0.0f);
	};

	struct ColdSettings
	{
		//Size, Offset, Shape, Roundness
		std::array<float4, 20> GH_Params = {
			float4(0.19f, 1.00f, 7.00f, 0.15f),
			float4(0.08f, 0.91f, 6.00f, 0.67f),
			float4(0.42f, 0.78f, 6.00f, 0.05f),
			float4(0.14f, 0.64f, 6.00f, 0.50f),
			float4(0.12f, 0.54f, 6.00f, 0.35f),
			float4(0.13f, 0.43f, 5.00f, 0.42f),
			float4(0.20f, 0.43f, 7.00f, 1.00f),
			float4(0.22f, 0.43f, 6.00f, 0.30f),
			float4(0.14f, 0.30f, 9.00f, 0.25f),
			float4(0.13f, 0.20f, 6.00f, 0.60f),
			float4(0.1f, 1.0f, 6.0f, 0.0f),
			float4(0.1f, 1.0f, 6.0f, 0.0f),
			float4(0.1f, 1.0f, 6.0f, 0.0f),
			float4(0.1f, 1.0f, 6.0f, 0.0f),
			float4(0.1f, 1.0f, 6.0f, 0.0f),
			float4(0.1f, 1.0f, 6.0f, 0.0f),
			float4(0.1f, 1.0f, 6.0f, 0.0f),
			float4(0.1f, 1.0f, 6.0f, 0.0f),
			float4(0.1f, 1.0f, 6.0f, 0.0f),
			float4(0.1f, 1.0f, 6.0f, 0.0f)
		};
		//Rotation, Feather, CA Scale, Motion
		std::array<float4, 20> GH_Params_2 = {
			float4(0.00f, 0.03f, 1.11f, 1.00f),
			float4(215.0f, 0.39f, 2.00f, 1.00f),
			float4(45.0f, 0.16f, 1.00f, 1.00f),
			float4(40.0f, 0.13f, 1.27f, 1.00f),
			float4(63.0f, 0.34f, 1.00f, 1.00f),
			float4(0.00f, 1.00f, 1.00f, 1.00f),
			float4(31.0f, 0.32f, 1.00f, 1.00f),
			float4(60.0f, 0.16f, 1.00f, 1.00f),
			float4(90.0f, 0.10f, 1.00f, 1.00f),
			float4(75.0f, 0.73f, 1.00f, 1.00f),
			float4(0.0f, 0.0f, 1.0f, 1.00f),
			float4(0.0f, 0.0f, 1.0f, 1.00f),
			float4(0.0f, 0.0f, 1.0f, 1.00f),
			float4(0.0f, 0.0f, 1.0f, 1.00f),
			float4(0.0f, 0.0f, 1.0f, 1.00f),
			float4(0.0f, 0.0f, 1.0f, 1.00f),
			float4(0.0f, 0.0f, 1.0f, 1.00f),
			float4(0.0f, 0.0f, 1.0f, 1.00f),
			float4(0.0f, 0.0f, 1.0f, 1.00f),
			float4(0.0f, 0.0f, 1.0f, 1.00f)
		};
		//Guess
		std::array<float4, 20> GH_Color = {
			float4(0.48f, 0.00f, 0.58f, 0.60f),
			float4(1.00f, 1.00f, 1.00f, 0.55f),
			float4(0.20f, 0.00f, 0.25f, 0.98f),
			float4(1.00f, 0.80f, 0.80f, 0.41f),
			float4(0.17f, 0.24f, 0.86f, 0.59f),
			float4(0.85f, 0.77f, 0.37f, 0.67f),
			float4(1.00f, 0.00f, 0.00f, 0.25f),
			float4(0.00f, 0.46f, 0.82f, 0.30f),
			float4(1.00f, 0.00f, 0.00f, 0.38f),
			float4(0.99f, 0.98f, 0.42f, 0.35f),
			float4(1.0f, 1.0f, 1.0f, 0.0f),
			float4(1.0f, 1.0f, 1.0f, 0.0f),
			float4(1.0f, 1.0f, 1.0f, 0.0f),
			float4(1.0f, 1.0f, 1.0f, 0.0f),
			float4(1.0f, 1.0f, 1.0f, 0.0f),
			float4(1.0f, 1.0f, 1.0f, 0.0f),
			float4(1.0f, 1.0f, 1.0f, 0.0f),
			float4(1.0f, 1.0f, 1.0f, 0.0f),
			float4(1.0f, 1.0f, 1.0f, 0.0f),
			float4(1.0f, 1.0f, 1.0f, 0.0f)
		};
		//Tex, Vis, Scale, NA
		std::array<float4, 20> GH_Atlas = {
			float4(1.00f, 0.42f, 0.50f, 0.00f),
			float4(1.00f, 0.00f, 1.00f, 0.00f),
			float4(2.00f, 0.11f, 0.77f, 0.00f),
			float4(1.00f, 0.28f, 1.00f, 0.00f),
			float4(1.00f, 0.14f, 0.55f, 0.00f),
			float4(1.00f, 0.00f, 1.00f, 0.00f),
			float4(3.00f, 0.24f, 0.50f, 0.00f),
			float4(4.00f, 0.00f, 1.00f, 0.00f),
			float4(4.00f, 0.61f, 1.00f, 0.00f),
			float4(2.00f, 1.00f, 1.00f, 0.00f),
			float4(1.0f, 0.0f, 0.0f, 0.0f),
			float4(1.0f, 0.0f, 0.0f, 0.0f),
			float4(1.0f, 0.0f, 0.0f, 0.0f),
			float4(1.0f, 0.0f, 0.0f, 0.0f),
			float4(1.0f, 0.0f, 0.0f, 0.0f),
			float4(1.0f, 0.0f, 0.0f, 0.0f),
			float4(1.0f, 0.0f, 0.0f, 0.0f),
			float4(1.0f, 0.0f, 0.0f, 0.0f),
			float4(1.0f, 0.0f, 0.0f, 0.0f),
			float4(1.0f, 0.0f, 0.0f, 0.0f)
		};
		std::array<float, 20> GH_InInt = {
			float(0.11f), float(0.64f),
			float(0.20f), float(0.34f),
			float(0.26f), float(0.74f),
			float(0.40f), float(0.15f),
			float(0.11f), float(0.67f),
			float(1.0f), float(1.0f),
			float(1.0f), float(1.0f),
			float(1.0f), float(1.0f),
			float(1.0f), float(1.0f),
			float(1.0f), float(1.0f)
		};

		float4 SB_Color = float4(1.0f, 1.0f, 1.0f, 1.0f);
		float4 SG_Color = float4(1.0f, 0.9f, 0.7f, 1.0f);
		float4 HL_Color = float4(1.0f, 0.0f, 0.0f, 1.0f);
		float4 LI_Color = float4(0.29f, 0.69f, 0.8f, 1.0f);

		float LI_FadeDuration = 1.0f;
		float LI_FadeIn = 0.35f;
		float LI_FadeOut = 0.01f;
		bool CA_RChannelOnly = false;
	};

	struct Settings
	{
		MainSettings mainsettings;
		ColdSettings coldsettings;
		bool EnableStarburst = true;
		bool EnableSunGlare = false;
		bool EnableLensGlare = true;
		bool EnableHalo = true;
		bool EnableGhosts = true;
		bool EnableCA = true;
		bool EnableIce = true;

		bool useCustomPreset = true;
	};
	Settings settings;

	struct alignas(16) ConstBuffer
	{
		DirectX::XMFLOAT4A screensize;
		uint frame;
		float precip;
		float sunFXFade;
		float _pad[1];
		DirectX::XMFLOAT4A SunParams;
		DirectX::XMFLOAT4A suncolor;
		MainSettings shadersettings;
	};
	ConstBuffer UpdateBufferValues();

	inline DirectX::XMFLOAT4A VectorToXMFloat(float4& value) { return DirectX::XMFLOAT4A(value.x, value.y, value.z, value.w); }
	inline float LinearStep(float edge0, float edge1, float x) { return std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f); }

	inline MainSettings UpdateSettings()
	{
		auto mSettings = settings->mainsettings;
		auto& cSettings = settings->coldsettings;
		mSettings.SB_BladeTaper = mSettings.SB_BladeTaper + ((mSettings.SB_BladeSplay > 0.01f) * 80);
		mSettings.SB_BladeWidth = (1.0f + (mSettings.SB_BladeWidth - 1.0f) * 0.25f) + mSettings.SB_BladeSplay;
		mSettings.SB_BladeBaseWidth = 1.0f + (mSettings.SB_BladeBaseWidth - 1.0f) * 0.25f;
		mSettings.SB_RandomRaysWidth = std::lerp(11.0f, 7.0f, mSettings.SB_RandomRaysWidth);
		mSettings.SBEX_BladeSplayLen = (1 / mSettings.SB_BladeLength - 0.95f) * (mSettings.SB_BladeSplay > 0.01f);
		mSettings.SG_Scale = mSettings.SG_Scale * 0.75f;
		mSettings.GH_Scale = mSettings.GH_Scale * 0.5f;
		mSettings.HL_LineWidth = mSettings.HL_LineWidth * 0.1f;
		mSettings.LIEX_FadeFactor = snowPrecipValue;
		mSettings.SB_Color = VectorToXMFloat(cSettings.SB_Color);
		mSettings.SG_Color = VectorToXMFloat(cSettings.SG_Color);
		mSettings.HL_Color = VectorToXMFloat(cSettings.HL_Color);
		mSettings.LI_Color = VectorToXMFloat(cSettings.LI_Color);
		return mSettings;
	}

	struct Shaders
	{
		enum Enum
		{
			Bypass = 0,
			OcclusionLUT = 1,
			AttachOcclusionLUT = 2,

			LensIce = 3,
			LensCA = 4,
			LensBurst = 5,
			LensGlare = 6,
			LensHalo = 7,
			LensGhosts = 8,
			LensSunGlare = 9,
		};
	};
	Shaders::Enum shaderdesc;

	struct Setup  //expanded version of BGS lens flare system
	{
		class LF_PassData
		{
		private:
			const uint64_t shaderdesc;
			uint64_t active;
			const uint64_t numpasses;
			const uint64_t weatherpass;
			const uint64_t uncondpass;
			const uint64_t deferred;
			const uint64_t pad[2] = {};
			uint64_t* enginerefs_ptr = nullptr;

			std::array<uint64_t, 4> enginerefs{ 1, 8, 0, 0 };

		public:
			LF_PassData(uint64_t desc, uint64_t active, uint64_t numpasses, uint64_t weather, uint64_t nocond, uint64_t defer) :
				shaderdesc(desc), active(active), numpasses(numpasses), weatherpass(weather), uncondpass(nocond), deferred(defer)
			{
				enginerefs_ptr = enginerefs.data();
			}

			int passesdone = 0;

			Shaders::Enum GetDesc() const { return (Shaders::Enum)shaderdesc; }
			int PassesRemaining() const { return (int)numpasses - (int)passesdone; }
			int PassesTotal() const { return (int)numpasses; }

			void Toggle(bool value) { active = (uint64_t)value; }
			bool IsActive() const { return (bool)active; }
			bool IsWeatherShader() const { return (bool)weatherpass; }
			bool IsUncond() const { return (bool)uncondpass; }
			bool IsDeferred() const { return (bool)deferred; }  //renders between upscaling and refraction

			void CheckRefs()
			{
				enginerefs = { 1, 8, 0, 0 };
				enginerefs_ptr = enginerefs.data();
			}
		};

		class LF_RenderData
		{
		private:
			const uint64_t head = 0x3F800000;
			std::unique_ptr<LF_PassData>* passarray_ptr = nullptr;
			const uint64_t _pad[1] = {};
			uint64_t passcount = 0;
			const uint64_t pad[2] = {};

			std::vector<std::unique_ptr<LF_PassData>> Effects;
			std::vector<std::unique_ptr<LF_PassData>> RenderPassList;  //engine loops via passarray_ptr and renders for each
			size_t currentPass = 0;

		public:
			LF_RenderData()
			{
				RenderPassList.reserve(100);
			}

			struct Type
			{
				bool weather = false;
				bool uncond = false;
				bool deferred = false;
			};

			void AddPasses(LF_PassData& effect)
			{
				for (int i = 0; i < effect.PassesTotal(); i++) {
					RenderPassList.push_back(std::make_unique<LF_PassData>(effect));
				}
			}

			void AddEffect(int desc, bool active, int passes, Type type = {})
			{
				Effects.push_back(std::make_unique<LF_PassData>(desc, active, passes, type.weather, type.uncond, type.deferred));
			}

			void SetRenderData()
			{
				passcount = RenderPassList.size();
				passarray_ptr = RenderPassList.data();
			}

			void CreateRenderList(bool sunVisible, bool deferred = false)
			{
				RenderPassList.clear();
				currentPass = 0;

				for (auto& effect : Effects) {
					effect->passesdone = 0;
					if (effect->IsActive()) {
						if (sunVisible || effect->IsUncond() || effect->IsWeatherShader())
							if (deferred == effect->IsDeferred())
								AddPasses(*effect);
					}
				}
				SetRenderData();
			}

			LF_PassData& GetEffect(int desc)
			{
				for (auto& effect : Effects) {
					if (effect->GetDesc() == desc)
						return *effect.get();
				}
				throw std::out_of_range("");
			}

			Shaders::Enum UpdateCurrentEffect()
			{
				auto desc = RenderPassList[0]->GetDesc();
				if (currentPass < RenderPassList.size()) {
					desc = RenderPassList[currentPass]->GetDesc();
					GetEffect(desc).passesdone++;
					currentPass++;
				}
				return desc;
			}

			void CheckRefData()
			{
				for (auto& pass : RenderPassList) pass->CheckRefs();
			}
		};
	};
	Setup::LF_RenderData* renderdata = nullptr;

	struct Hooks
	{
		struct LensFlare_CheckResources  //override main init/integ
		{
			static void thunk();
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct LensFlareVisibility_CheckRenderCondition
		{
			static void thunk(RE::NiCamera* camera, void* unk);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct LensFlare_CheckRenderCondition  //setup buffers etc, if sun on screen return true (override for non sun FX)
		{
			static bool thunk(void* shader, RE::NiCamera* camera, uint64_t unk);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		template <RE::ImageSpaceManager::ImageSpaceEffectEnum EffectType>
		struct BSImagespaceShader_Render
		{
			static void thunk(void* imageSpaceShader, RE::BSTriShape* shape, RE::ImageSpaceEffectParam* param);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct LensFlare_AssignTexture  //remove lensflare texture init/assignment
		{
			static void thunk(void* previous, uint64_t current)
			{
				current = 0;
				func(previous = &current, current);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_PostProcessing  //run post upscale effects
		{
			static void thunk(RE::ImageSpaceManager* a1, uint32_t a3, uint32_t er8_);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSSkyShader_SetupMaterial  //override sun glare, fetch sun scale, append occlusion LUT
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			logger::info("[Lens Effects] Installed hooks");

			stl::detour_thunk<LensFlare_CheckResources>(REL::RelocationID(25772, 26327));
			stl::write_thunk_call<LensFlareVisibility_CheckRenderCondition>(REL::RelocationID(100274, 106988).address() + REL::Relocate(0x188, 0x195));
			stl::write_thunk_call<LensFlare_CheckRenderCondition>(REL::RelocationID(100281, 106995).address() + REL::Relocate(0x14, 0x16));
			stl::write_thunk_call<LensFlare_AssignTexture>(REL::RelocationID(100280, 106994).address() + REL::Relocate(0x4B, 0x4B));

			stl::write_thunk_call<BSImagespaceShader_Render<RE::ImageSpaceManager::ISLensFlareVisibility>>(REL::RelocationID(100274, 106988).address() + REL::Relocate(0x1CB, 0x275));
			stl::write_vfunc<0x1, BSImagespaceShader_Render<RE::ImageSpaceManager::ISLensFlare>>(RE::VTABLE_BSImagespaceShaderLensFlare[3]);

			stl::write_vfunc<0x6, BSSkyShader_SetupMaterial>(RE::VTABLE_BSSkyShader[0]);

			stl::detour_thunk<Main_PostProcessing>(REL::RelocationID(99023, 105674));
		}
	};
};
