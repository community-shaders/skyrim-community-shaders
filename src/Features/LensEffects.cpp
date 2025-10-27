#include "LensEffects.h"
#include "State.h"
#include "Upscaling.h"
#include "Upscaling/DX12SwapChain.h"
#include "Util.h"
#include <DDSTextureLoader.h>
#include <DirectXTex.h>
#include <REX/W32/COMPTR.h>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LensEffects::MainSettings,
	SB_Intensity, SB_Scale, SB_EnableBlades,
	SB_BladeInt, SB_BladeVertices, SB_BladeSplay,
	SB_BladeRotation, SB_BladeLength, SB_BladeBaseWidth,
	SB_BladeWidth, SB_BladeTaper, SB_BladeFeather,
	SB_EnableRays, SB_RandomRaysInt, SB_RandomRaysVolume,
	SB_RandomRaysLength, SB_RandomRaysWidth,
	SG_Scale, SG_Intensity, SG_OuterInt, SG_OuterFade,
	GH_Intensity, GH_Scale, GH_Saturation, LI_Intensity,
	GH_EnableClampOffset, GH_ClampOffset,
	GL_Intensity, GL_Scale, GL_DynPosition, GL_XAxisOffset, GL_YAxisOffset,
	GL_MaxRotation, GL_CutDepth, GL_Radius, GL_TipFade,
	HL_Scale, HL_Intensity, HL_EnableExp,
	HL_FlipExpOffset, HL_ExpMinSize, HL_ExpMaxSize,
	HL_RotationSpeed, HL_LineVolume, HL_LineLength,
	HL_LineWidth, HL_LineTaper, HL_ColorShift,
	CA_Intensity, CA_Threshold, CA_MaxOffset, CA_RChannelOnly)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LensEffects::ColdSettings,
	GH_Params, GH_Params_2, GH_Color, GH_Atlas,
	SB_Color, HL_Color, LI_Color, LI_FadeIn,
	LI_FadeOut, LI_FadeDuration)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LensEffects::Settings,
	mainsettings, coldsettings, EnableStarburst,
	EnableLensGlare, EnableHalo, EnableGhosts,
	EnableIce, EnableSunGlare, EnableCA)

void LensEffects::CompileShaders()
{
	SunOcclusionMaskPixelShader = (ID3D11PixelShader*)Util::CompileShader(L"Data\\Shaders\\LensEffects\\LensEffects.hlsl", { { "OCCLUSION_PIXEL_SHADER", "" } }, "ps_5_0");
	ChromaticAberrationPixelShader = (ID3D11PixelShader*)Util::CompileShader(L"Data\\Shaders\\LensEffects\\LensEffects.hlsl", { { "CHROMATIC_ABERRATION_PIXEL_SHADER", "" } }, "ps_5_0");
	BypassVertexShader = (ID3D11VertexShader*)Util::CompileShader(L"Data\\Shaders\\LensEffects\\LensEffects.hlsl", { { "BYPASS_VERTEX_SHADER", "" } }, "vs_5_0");

	BurstVertexShader = (ID3D11VertexShader*)Util::CompileShader(L"Data\\Shaders\\LensEffects\\LensEffects.hlsl", { { "STARBURST_VERTEX_SHADER", "" } }, "vs_5_0");
	BurstPixelShader = (ID3D11PixelShader*)Util::CompileShader(L"Data\\Shaders\\LensEffects\\LensEffects.hlsl", { { "STARBURST_PIXEL_SHADER", "" } }, "ps_5_0");

	GhostVertexShader = (ID3D11VertexShader*)Util::CompileShader(L"Data\\Shaders\\LensEffects\\LensEffects.hlsl", { { "GHOST_VERTEX_SHADER", "" } }, "vs_5_0");
	GhostPixelShader = (ID3D11PixelShader*)Util::CompileShader(L"Data\\Shaders\\LensEffects\\LensEffects.hlsl", { { "GHOST_PIXEL_SHADER", "" } }, "ps_5_0");

	SunGlareVertexShader = (ID3D11VertexShader*)Util::CompileShader(L"Data\\Shaders\\LensEffects\\LensEffects.hlsl", { { "SUNGLARE_VERTEX_SHADER", "" } }, "vs_5_0");
	SunGlarePixelShader = (ID3D11PixelShader*)Util::CompileShader(L"Data\\Shaders\\LensEffects\\LensEffects.hlsl", { { "SUNGLARE_PIXEL_SHADER", "" } }, "ps_5_0");

	HaloVertexShader = (ID3D11VertexShader*)Util::CompileShader(L"Data\\Shaders\\LensEffects\\LensEffects.hlsl", { { "HALO_VERTEX_SHADER", "" } }, "vs_5_0");
	HaloPixelShader = (ID3D11PixelShader*)Util::CompileShader(L"Data\\Shaders\\LensEffects\\LensEffects.hlsl", { { "HALO_PIXEL_SHADER", "" } }, "ps_5_0");

	LensGlareVertexShader = (ID3D11VertexShader*)Util::CompileShader(L"Data\\Shaders\\LensEffects\\LensEffects.hlsl", { { "LENSGLARE_VERTEX_SHADER", "" } }, "vs_5_0");
	LensGlarePixelShader = (ID3D11PixelShader*)Util::CompileShader(L"Data\\Shaders\\LensEffects\\LensEffects.hlsl", { { "LENSGLARE_PIXEL_SHADER", "" } }, "ps_5_0");

	IceVertexShader = (ID3D11VertexShader*)Util::CompileShader(L"Data\\Shaders\\LensEffects\\LensEffects.hlsl", { { "ICE_VERTEX_SHADER", "" } }, "vs_5_0");
	IcePixelShader = (ID3D11PixelShader*)Util::CompileShader(L"Data\\Shaders\\LensEffects\\LensEffects.hlsl", { { "ICE_PIXEL_SHADER", "" } }, "ps_5_0");
}

void LensEffects::SetupResources()
{
	auto device = globals::d3d::device;

	D3D11_SAMPLER_DESC linearSamplerDesc{};
	linearSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
	linearSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	linearSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	linearSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	linearSamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	linearSamplerDesc.MinLOD = 0;
	linearSamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

	D3D11_SAMPLER_DESC pointSamplerDesc{ linearSamplerDesc };
	pointSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;

	D3D11_SAMPLER_DESC depthSamplerDesc{ linearSamplerDesc };
	depthSamplerDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
	depthSamplerDesc.ComparisonFunc = D3D11_COMPARISON_EQUAL;

	DX::ThrowIfFailed(device->CreateSamplerState(&linearSamplerDesc, &LinearSampler));
	DX::ThrowIfFailed(device->CreateSamplerState(&pointSamplerDesc, &PointSampler));
	DX::ThrowIfFailed(device->CreateSamplerState(&depthSamplerDesc, &DepthSampler));

	CD3D11_BLEND_DESC blendDesc(D3D11_DEFAULT);
	blendDesc.RenderTarget[0].BlendEnable = TRUE;

	DX::ThrowIfFailed(device->CreateBlendState(&blendDesc, &BlendState[0]));
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
	DX::ThrowIfFailed(device->CreateBlendState(&blendDesc, &BlendState[1]));

	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = 16.0f;
	viewport.Height = 1.0f;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	D3D11_TEXTURE2D_DESC OcclusionTexDesc{};
	OcclusionTexDesc.Width = 16;
	OcclusionTexDesc.Height = 1;
	OcclusionTexDesc.MipLevels = 1;
	OcclusionTexDesc.ArraySize = 1;
	OcclusionTexDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	OcclusionTexDesc.Usage = D3D11_USAGE_DEFAULT;
	OcclusionTexDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	OcclusionTexDesc.SampleDesc.Count = 1;
	OcclusionTexDesc.SampleDesc.Quality = 0;
	OcclusionTexDesc.CPUAccessFlags = 0;
	OcclusionTexDesc.MiscFlags = 0;

	D3D11_TEXTURE2D_DESC OcclusionTexDescAT{ OcclusionTexDesc };
	OcclusionTexDescAT.Format = DXGI_FORMAT_R16_UINT;

	D3D11_UNORDERED_ACCESS_VIEW_DESC OcclusionUAVDesc{};
	OcclusionUAVDesc.Format = OcclusionTexDesc.Format;
	OcclusionUAVDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
	OcclusionUAVDesc.Texture2D.MipSlice = 0;

	D3D11_UNORDERED_ACCESS_VIEW_DESC OcclusionUAVDescAT{ OcclusionUAVDesc };
	OcclusionUAVDescAT.Format = DXGI_FORMAT_R16_UINT;

	DX::ThrowIfFailed(device->CreateTexture2D(&OcclusionTexDesc, nullptr, &SunOcclusionLUT));
	DX::ThrowIfFailed(device->CreateUnorderedAccessView(SunOcclusionLUT, &OcclusionUAVDesc, &SunOcclusionUAV));
	DX::ThrowIfFailed(device->CreateShaderResourceView(SunOcclusionLUT, nullptr, &SunOcclusionSRV));

	DX::ThrowIfFailed(device->CreateTexture2D(&OcclusionTexDescAT, nullptr, &SunOcclusionLUT_AT));
	DX::ThrowIfFailed(device->CreateUnorderedAccessView(SunOcclusionLUT_AT, &OcclusionUAVDescAT, &SunOcclusionUAV_AT));

	DX::ThrowIfFailed(DirectX::CreateDDSTextureFromFile(device, L"Data\\Shaders\\LensEffects\\Textures\\Atlas.dds", nullptr, &AtlasTexSRV));
	DX::ThrowIfFailed(DirectX::CreateDDSTextureFromFile(device, L"Data\\Shaders\\LensEffects\\Textures\\Frost.dds", nullptr, &IceTexSRV));

	SettingsCB = new ConstantBuffer(ConstantBufferDesc<ConstBuffer>());

	std::sort(weatherDisablesSun.begin(), weatherDisablesSun.end());
	std::sort(weatherDisablesSnow.begin(), weatherDisablesSnow.end());

	skyrim_FlareData = reinterpret_cast<uintptr_t*>(REL::RelocationID(527915, 414867).address());
	skyrim_RunFlarePtr = reinterpret_cast<uint32_t*>(REL::RelocationID(527916, 414862).address());

	skyrim_SunPosition = reinterpret_cast<RE::NiPoint3*>(REL::RelocationID(527924, 414871).address());
	skyrim_SunGlareScale = reinterpret_cast<float*>(REL::RelocationID(502611, 370235).address());

	gFlareApplyFunc = reinterpret_cast<decltype(gFlareApplyFunc)>(REL::RelocationID(100281, 106995).address());

	renderdata = new Setup::LF_RenderData;

	renderdata->AddEffect(Shaders::LensCA, settings.EnableCA, 1, { .uncond = true });
	renderdata->AddEffect(Shaders::LensIce, settings.EnableIce, 1, { .weather = true, .deferred = true });
	renderdata->AddEffect(Shaders::LensBurst, settings.EnableStarburst, 1);
	renderdata->AddEffect(Shaders::LensSunGlare, settings.EnableSunGlare, 1);
	renderdata->AddEffect(Shaders::LensGlare, settings.EnableLensGlare, 1);
	renderdata->AddEffect(Shaders::LensHalo, settings.EnableHalo, 1);
	renderdata->AddEffect(Shaders::LensGhosts, settings.EnableGhosts, ghostpasses);

	renderdata->SetRenderData();

	CompileShaders();
}

void LensEffects::CheckOverride()
{
	static Util::FrameChecker frame_checker;

	if (overrideShader) {
		if (frame_checker.IsNewFrame()) {
			SettingsCB->Update(UpdateBufferValues());
			UpdateWeatherBasedDisable();
			frameIdx = (frameIdx < 16) ? ++frameIdx : 5;
		}
		LookupShader(shaderdesc);
	}
}

void LensEffects::LookupShader(int desc)
{
	static const std::unordered_map<int, void (LensEffects::*)()> effects{
		{ Shaders::Bypass, &LensEffects::BypassShader },
		{ Shaders::AttachOcclusionLUT, &LensEffects::AppendOcclusionLUT },
		{ Shaders::OcclusionLUT, &LensEffects::SetupOcclusionMask },
		{ Shaders::LensIce, &LensEffects::SetupIceEffect },
		{ Shaders::LensBurst, &LensEffects::SetupBurstEffect },
		{ Shaders::LensSunGlare, &LensEffects::SetupSunGlareEffect },
		{ Shaders::LensGlare, &LensEffects::SetupLensGlareEffect },
		{ Shaders::LensHalo, &LensEffects::SetupHaloEffect },
		{ Shaders::LensGhosts, &LensEffects::SetupGhostEffect },
		{ Shaders::LensCA, &LensEffects::SetupCAEffect },
	};
	auto it = effects.find(desc);
	if (it != effects.cend())
		(this->*(it->second))();
}

LensEffects::ConstBuffer LensEffects::UpdateBufferValues()
{
	float2 screenSize = Util::ConvertToDynamic(globals::state->screenSize);
	float4 screenParams = { screenSize.x, screenSize.y, screenSize.x / screenSize.y, 0.0f };
	ConstBuffer data{};
	data.screensize = VectorToXMFloat(screenParams);
	data.frame = frameIdx;
	data.precip = GetWeatherPrecip();
	data.sunFXFade = weatherFadeout;
	data.SunParams = GetSunPosition();
	data.suncolor = GetSunColor();
	data.shadersettings = UpdateSettings();

	return data;
}

void LensEffects::SetupOcclusionMask()
{
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;
	auto buffer = SettingsCB->CB();

	auto& mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].texture;
	auto& mainTexCopy = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].textureCopy;
	context->CopyResource(mainTexCopy, mainTex);

	auto& mainSRV = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].SRV;
	auto& motionVectorSRV = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR].SRV;

	ID3D11UnorderedAccessView* OcclusionLUTs[2] = { SunOcclusionUAV, SunOcclusionUAV_AT };
	context->OMSetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, 2, OcclusionLUTs, nullptr);

	context->RSSetViewports(1, &viewport);

	context->PSSetShader(SunOcclusionMaskPixelShader, NULL, NULL);

	context->PSSetConstantBuffers(1, 1, &buffer);
	context->VSSetConstantBuffers(1, 1, &buffer);

	context->PSSetShaderResources(1, 1, &mainSRV);
	context->PSSetShaderResources(2, 1, &motionVectorSRV);

	context->PSSetSamplers(10, 1, &LinearSampler);
	context->PSSetSamplers(11, 1, &PointSampler);
	context->PSSetSamplers(13, 1, &DepthSampler);

	if (!Raster) {
		context->RSGetState(&Raster);
		if (Raster) {
			D3D11_RASTERIZER_DESC rastDesc = {};
			Raster->GetDesc(&rastDesc);
			rastDesc.CullMode = D3D11_CULL_NONE;
			DX::ThrowIfFailed(globals::d3d::device->CreateRasterizerState(&rastDesc, &Raster));
		}
	}

	overrideShader = false;
}

void LensEffects::SetupBurstEffect()
{
	auto context = globals::d3d::context;

	context->VSSetShader(BurstVertexShader, NULL, NULL);
	context->PSSetShader(BurstPixelShader, NULL, NULL);

	context->VSSetShaderResources(0, 1, &SunOcclusionSRV);

	overrideShader = false;
}

void LensEffects::SetupSunGlareEffect()
{
	auto context = globals::d3d::context;

	context->VSSetShader(SunGlareVertexShader, NULL, NULL);
	context->PSSetShader(SunGlarePixelShader, NULL, NULL);

	auto& mainCopySRV = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].SRVCopy;
	context->VSSetShaderResources(0, 1, &SunOcclusionSRV);
	context->PSSetShaderResources(1, 1, &mainCopySRV);

	overrideShader = false;
}

void LensEffects::SetupLensGlareEffect()
{
	auto context = globals::d3d::context;

	context->VSSetShader(LensGlareVertexShader, NULL, NULL);
	context->PSSetShader(LensGlarePixelShader, NULL, NULL);

	context->VSSetShaderResources(0, 1, &SunOcclusionSRV);

	overrideShader = false;
}

void LensEffects::SetupHaloEffect()
{
	auto context = globals::d3d::context;

	context->VSSetShader(HaloVertexShader, NULL, NULL);
	context->PSSetShader(HaloPixelShader, NULL, NULL);

	context->VSSetShaderResources(0, 1, &SunOcclusionSRV);

	overrideShader = false;
}

void LensEffects::SetupGhostEffect()
{
	auto context = globals::d3d::context;

	auto it = renderdata->GetEffect(Shaders::LensGhosts).passesdone - 1;

	float4& params = settings.coldsettings.GH_Params[it];
	float4& params_2 = settings.coldsettings.GH_Params_2[it];

	ConstBuffer data = UpdateBufferValues();
	data.shadersettings.GH_Size = params.x;
	data.shadersettings.GH_Offset = params.y;
	data.shadersettings.GH_Shape = params.z;
	data.shadersettings.GH_Roundness = params.w;
	data.shadersettings.GH_Rotation = params_2.x;
	data.shadersettings.GH_Feather = params_2.y;
	data.shadersettings.GH_CAScale = params_2.z;
	data.shadersettings.GH_InnerInt = params_2.w;
	data.shadersettings.GH_Color = VectorToXMFloat(settings.coldsettings.GH_Color[it]);
	data.shadersettings.GH_Atlas = VectorToXMFloat(settings.coldsettings.GH_Atlas[it]);

	SettingsCB->Update(data);

	context->VSSetShader(GhostVertexShader, NULL, NULL);
	context->PSSetShader(GhostPixelShader, NULL, NULL);

	context->VSSetShaderResources(0, 1, &SunOcclusionSRV);
	context->PSSetShaderResources(1, 1, &AtlasTexSRV);

	overrideShader = false;
}

void LensEffects::SetupCAEffect()
{
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;

	context->OMSetBlendState(BlendState[0], nullptr, 0xFFFFFFFF);

	context->VSSetShader(BypassVertexShader, NULL, NULL);
	context->PSSetShader(ChromaticAberrationPixelShader, NULL, NULL);

	auto& mainCopy = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].SRVCopy;
	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR].SRV;
	context->PSSetShaderResources(1, 1, &mainCopy);
	context->PSSetShaderResources(2, 1, &motionVector);

	overrideShader = false;
}

void LensEffects::SetupIceEffect()
{
	auto context = globals::d3d::context;

	if (auto& UIBuffer = globals::features::upscaling.dx12SwapChain.uiBufferWrapped) {
		auto UIRTV = UIBuffer->rtv;
		context->OMSetRenderTargets(1, &UIRTV, nullptr);
	}

	context->RSSetState(Raster);
	context->OMSetBlendState(BlendState[1], nullptr, 0xFFFFFFFF);

	auto& mainCopy = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].SRVCopy;

	context->VSSetShader(IceVertexShader, NULL, NULL);
	context->PSSetShader(IcePixelShader, NULL, NULL);

	context->PSSetShaderResources(0, 1, &IceTexSRV);
	context->PSSetShaderResources(1, 1, &mainCopy);

	overrideShader = false;
}

void LensEffects::AppendOcclusionLUT()
{
	auto context = globals::d3d::context;

	ID3D11RenderTargetView* rtvs[8] = {};
	ID3D11DepthStencilView* dsv = nullptr;

	context->OMGetRenderTargets(8, rtvs, &dsv);

	UINT numRTs = 0;
	for (int i = 0; i < 8; ++i)
		if (rtvs[i])
			numRTs++;

	if (numRTs < 8) {
		if (!useCloudLUT) {
			context->OMSetRenderTargetsAndUnorderedAccessViews(numRTs, rtvs, dsv, 7, 1, &SunOcclusionUAV, nullptr);
		} else {
			context->OMSetRenderTargetsAndUnorderedAccessViews(numRTs, rtvs, dsv, 7, 1, &SunOcclusionUAV_AT, nullptr);
			context->PSSetShaderResources(30, 1, &SunOcclusionSRV);
			useCloudLUT = false;
		}
	} else {
		logger::error("[Lens Effects] failed to bind UAV");
	}

	for (int i = 0; i < 8; ++i)
		if (rtvs[i])
			rtvs[i]->Release();
	if (dsv)
		dsv->Release();

	overrideShader = false;
}

void LensEffects::BypassShader()
{
	auto context = globals::d3d::context;

	context->VSSetShader(BypassVertexShader, NULL, NULL);
	context->PSSetShader(NULL, NULL, NULL);

	overrideShader = false;
}

void LensEffects::GetWeatherShader()
{
	if (shaderdesc == Shaders::LensIce) {
		if (auto sky = globals::game::sky) {
			float lastUpdate = sky->lastWeatherUpdate / 24.0f;
			static float transEpoch = lastUpdate;
			static float epochDelta = 0;
			static int weatherRole = 0;
			static float fade;
			static float* UIFadeSetting = nullptr;

			if (CheckWeatherChange()) {
				transEpoch = lastUpdate;
				weatherRole = false;
				if (auto currWeather = sky->currentWeather; currWeather && currWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kSnow)) {
					if (!std::binary_search(weatherDisablesSnow.begin(), weatherDisablesSnow.end(), WeatherID)) {
						fade = std::max(sky->currentWeather->data.precipitationBeginFadeIn / 255.0f / 24.0f / 7.0f, 0.0018f);
						UIFadeSetting = &settings.coldsettings.LI_FadeIn;
						weatherRole = 1;
						epochDelta = 0;
					}
				} else if (auto prevWeather = sky->lastWeather; prevWeather && prevWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kSnow)) {
					if (!std::binary_search(weatherDisablesSnow.begin(), weatherDisablesSnow.end(), PrevWeatherID)) {
						fade = std::max(sky->lastWeather->data.precipitationEndFadeOut / 255.0f / 24.0f / 7.0f, 0.00025f);
						UIFadeSetting = &settings.coldsettings.LI_FadeOut;
						weatherRole = 2;
						epochDelta = snowPrecipValue;
					}
				}
			}

			if (weatherRole) {
				float time = RE::Calendar::GetSingleton()->GetCurrentGameTime();
				bool IsInside = RE::PlayerCharacter::GetSingleton()->GetParentCell()->IsInteriorCell();
				static bool lastIsInside = IsInside;
				if (IsInside != lastIsInside) {
					lastIsInside = IsInside;
					epochDelta = snowPrecipValue;
					transEpoch = time;
					UIFadeSetting = (IsInside) ? &settings.coldsettings.LI_FadeOut : &settings.coldsettings.LI_FadeIn;
					fade = 0;
				}
				float duration = settings.coldsettings.LI_FadeDuration / 24.0f / 4.0f;
				duration = (IsInside) ? duration * 0.5f : duration;

				float UIFadeFactor = *UIFadeSetting / 24.0f / 4.0f;
				float fadeValue = std::clamp((time - (transEpoch + (fade + UIFadeFactor))) / duration, 0.0f, 1.0f);

				if (!IsInside && weatherRole == 1)
					snowPrecipValue = std::clamp(epochDelta + fadeValue, 0.0f, 1.0f);
				else
					snowPrecipValue = std::clamp(epochDelta - fadeValue, 0.0f, 1.0f);
			} else {
				snowPrecipValue = 0.0f;
				shaderdesc = Shaders::Bypass;
			}
		} else {
			shaderdesc = Shaders::Bypass;
		}
	}
}

float LensEffects::GetWeatherPrecip()
{
	float precip = 0.0f;
	if (auto sky = globals::game::sky; sky && sky->mode.get() == RE::Sky::Mode::kFull) {
		if (auto prevweather = sky->lastWeather; prevweather && prevweather->precipitationData)
			precip = 1.0f - sky->currentWeatherPct;
		if (auto currweather = sky->currentWeather; currweather && currweather->precipitationData)
			precip = (precip) ? 1.0f : sky->currentWeatherPct;
	}
	return precip;
}

bool LensEffects::CheckWeatherChange()
{
	if (auto sky = globals::game::sky) {
		if (!WeatherID || std::floor(sky->lastWeatherUpdate * 60) == std::floor(sky->currentGameHour * 60)) {
			WeatherID = (sky->currentWeather) ? sky->currentWeather->formID : 0;
			PrevWeatherID = (sky->lastWeather) ? sky->lastWeather->formID : 0;
			return true;
		}
	}
	return false;
}

void LensEffects::UpdateWeatherBasedDisable()
{
	static bool currWeatherRole = false;
	static bool prevWeatherRole = false;

	if (CheckWeatherChange()) {
		disableSunFX = false;
		weatherFadeout = 0.0f;
		currWeatherRole = std::binary_search(weatherDisablesSun.begin(), weatherDisablesSun.end(), WeatherID);
		prevWeatherRole = std::binary_search(weatherDisablesSun.begin(), weatherDisablesSun.end(), PrevWeatherID);
	}

	if (auto sky = globals::game::sky; sky && (currWeatherRole || prevWeatherRole)) {
		auto weatherTransition = LinearStep(0.3, 1.0, sky->currentWeatherPct);
		if ((weatherTransition == 1.0f && currWeatherRole) || (currWeatherRole && prevWeatherRole))
			disableSunFX = true;
		weatherFadeout = (currWeatherRole) ? weatherTransition : 1.0f - weatherTransition;
	}
}

DirectX::XMFLOAT4A LensEffects::GetSunPosition()
{
	static float sunRadius = 0.0f;
	float2 screenSize = Util::ConvertToDynamic(globals::state->screenSize);
	auto sunWSPos = *skyrim_SunPosition;
	float4 sunWorldPos = { sunWSPos.x, sunWSPos.y, sunWSPos.z, 1.0f };

	Matrix viewMatrix = Util::GetCameraData(0).viewMat;
	Matrix projMatrix = Util::GetCameraData(0).projMat;

	float4 sunViewPos = float4::Transform(sunWorldPos, viewMatrix);
	float SunSSRad = (*skyrim_SunGlareScale * SunScale) * (projMatrix._22 / sunViewPos.z) * (screenSize.y * 0.5f);
	sunViewPos.w = sunRadius = (SunSSRad > 10.0f && SunSSRad < 100.0f) ? SunSSRad : sunRadius;

	return VectorToXMFloat(sunViewPos);
}

DirectX::XMFLOAT4A LensEffects::GetSunColor()
{
	static float4 sunColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	if (auto sky = globals::game::sky; sky && sky->sun && sky->sun->sunBase)
		if (auto property = skyrim_cast<RE::BSSkyShaderProperty*>(sky->sun->sunBase->GetGeometryRuntimeData().properties[1].get()))
			sunColor = { property->kBlendColor.red, property->kBlendColor.green, property->kBlendColor.blue, 1.0f };
	return VectorToXMFloat(sunColor);
}

void LensEffects::Hooks::BSSkyShader_SetupMaterial::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	auto& lens = globals::features::lensEffects;
	auto skyProperty = reinterpret_cast<RE::BSSkyShaderProperty*>(Pass->shaderProperty);

	if (skyProperty) {
		if (skyProperty->uiSkyObjectType == RE::BSSkyShaderProperty::SkyObject::SO_SUN_GLARE) {
			lens.overrideShader = true;
			lens.shaderdesc = Shaders::Bypass;
		}

		else if (skyProperty->uiSkyObjectType == RE::BSSkyShaderProperty::SkyObject::SO_ATMOSPHERE) {
			if (Pass->passEnum == 0x5C00005E && RenderFlags == 65) {
				lens.overrideShader = true;
				lens.shaderdesc = Shaders::AttachOcclusionLUT;
			}
		}

		else if (skyProperty->uiSkyObjectType == RE::BSSkyShaderProperty::SkyObject::SO_CLOUDS) {
			if ((Pass->passEnum == 0x5C000062 || Pass->passEnum == 0x5C000063 || Pass->passEnum == 0x5C000064) && RenderFlags == 65) {
				lens.overrideShader = true;
				lens.shaderdesc = Shaders::AttachOcclusionLUT;
				lens.useCloudLUT = true;
			}
		}

		else if (skyProperty->uiSkyObjectType == RE::BSSkyShaderProperty::SkyObject::SO_SUN) {
			if (lens.SunScale == 0.0f && Pass->passEnum == 0x5C000061 && (RenderFlags == 65 || RenderFlags == 8)) {
				D3D11_TEXTURE2D_DESC sunDesc = {};
				skyProperty->pBaseTexture->rendererTexture->texture->GetDesc(&sunDesc);
				lens.SunScale = 24.0f / sunDesc.Width;
			}
		}
	}
	func(This, Pass, RenderFlags);
}

void LensEffects::Hooks::LensFlare_CheckResources::thunk()
{
	auto& lens = globals::features::lensEffects;

	lens.renderdata->CheckRefData();

	if (!*lens.skyrim_FlareData)
		*lens.skyrim_FlareData = reinterpret_cast<uintptr_t>(lens.renderdata);
}

void LensEffects::Hooks::LensFlareVisibility_CheckRenderCondition::thunk(RE::NiCamera* camera, void* shader)
{
	auto& lens = globals::features::lensEffects;

	func(camera, shader);  //set skyrim_RunFlarePtr

	lens.skyrim_SunVisble = static_cast<bool>(*lens.skyrim_RunFlarePtr);

	if (lens.disableSunFX)
		lens.skyrim_SunVisble = false;

	if (*lens.skyrim_FlareData && !globals::game::ui->GameIsPaused()) {
		lens.renderdata->CreateRenderList(lens.skyrim_SunVisble);
		*lens.skyrim_RunFlarePtr = 1;
	} else
		*lens.skyrim_RunFlarePtr = 0;
}

void LensEffects::Hooks::BSImagespaceShader_Render<RE::ImageSpaceManager::ISLensFlareVisibility>::thunk(void* shader, RE::BSTriShape* shape, RE::ImageSpaceEffectParam* param)
{
	auto& lens = globals::features::lensEffects;

	lens.overrideShader = true;
	lens.shaderdesc = Shaders::OcclusionLUT;

	func(shader, shape, param);
}

#pragma warning(push)
#pragma warning(disable: 4189)
bool LensEffects::Hooks::LensFlare_CheckRenderCondition::thunk(void* shader, RE::NiCamera* camera, uint64_t unk)
{
	auto& lens = globals::features::lensEffects;

	if (!lens.gFlareShader && shader)
		lens.gFlareShader = shader;

	bool result = func(shader, camera, unk);

	return true;
}
#pragma warning(pop)

void LensEffects::Hooks::BSImagespaceShader_Render<RE::ImageSpaceManager::ISLensFlare>::thunk(void* shader, RE::BSTriShape* shape, RE::ImageSpaceEffectParam* param)
{
	auto& lens = globals::features::lensEffects;

	lens.overrideShader = true;
	lens.shaderdesc = lens.renderdata->UpdateCurrentEffect();

	if (lens.renderdata->GetEffect(lens.shaderdesc).IsWeatherShader()) {
		lens.GetWeatherShader();
	}

	func(shader, shape, param);
}

void LensEffects::Hooks::Main_PostProcessing::thunk(RE::ImageSpaceManager* a1, uint32_t a3, uint32_t er8_)
{
	auto& lens = globals::features::lensEffects;

	if (*lens.skyrim_FlareData && lens.gFlareShader && !globals::game::ui->GameIsPaused()) {
		lens.renderdata->CreateRenderList(lens.skyrim_SunVisble, true);
		lens.gFlareApplyFunc(RE::Main::WorldRootCamera(), lens.gFlareShader, 0);
	}

	func(a1, a3, er8_);
}

void LensEffects::DrawSettings()
{
	auto& mainSettings = settings.mainsettings;
	auto& coldSettings = settings.coldsettings;

	if (ImGui::BeginTabBar("Effects")) {
		if (ImGui::BeginTabItem("General")) {
			ImGui::SeparatorText("Quick Enable");
			if (ImGui::Checkbox("Enable Sun Glare", &settings.EnableSunGlare))
				renderdata->GetEffect(Shaders::LensSunGlare).Toggle(settings.EnableSunGlare);
			if (ImGui::Checkbox("Enable Starburst", &settings.EnableStarburst))
				renderdata->GetEffect(Shaders::LensBurst).Toggle(settings.EnableStarburst);
			if (ImGui::Checkbox("Enable Ghosts", &settings.EnableGhosts))
				renderdata->GetEffect(Shaders::LensGhosts).Toggle(settings.EnableGhosts);
			if (ImGui::Checkbox("Enable Lens Glare", &settings.EnableLensGlare))
				renderdata->GetEffect(Shaders::LensGlare).Toggle(settings.EnableLensGlare);
			if (ImGui::Checkbox("Enable Sun Halo", &settings.EnableHalo))
				renderdata->GetEffect(Shaders::LensHalo).Toggle(settings.EnableHalo);
			if (ImGui::Checkbox("Enable Chromatic Aberration", &settings.EnableCA))
				renderdata->GetEffect(Shaders::LensCA).Toggle(settings.EnableCA);
			if (ImGui::Checkbox("Enable Lens Ice", &settings.EnableIce))
				renderdata->GetEffect(Shaders::LensIce).Toggle(settings.EnableIce);

			ImGui::EndTabItem();
		}

		//// Lens Starburst ////
		if (ImGui::BeginTabItem("Starburst Flare")) {
			ImGui::Spacing();
			if (ImGui::CollapsingHeader("Description")) {
				ImGui::Text("Creates diffraction spikes radiating from the sun, mimicking light scattering off the lens aperture.");
				ImGui::Text("Starburst has two adjustable styles: blades and rays. You can use either or both. By default blades are disabled.");
			}
			ImGui::SeparatorText("General");
			ImGui::Spacing();
			if (ImGui::Checkbox("Enable Starburst", &settings.EnableStarburst))
				renderdata->GetEffect(Shaders::LensBurst).Toggle(settings.EnableStarburst);

			ImGui::SliderFloat("Burst Size", &mainSettings.SB_Scale, 0.1f, 1.0f);
			ImGui::SliderFloat("Burst Intensity", &mainSettings.SB_Intensity, 0.01f, 2.0f);
			ImGui::Spacing();
			ImGui::Spacing();
			ImGui::SeparatorText("Advanced");
			ImGui::Spacing();
			ImGui::Checkbox("Enable Blades", (bool*)&mainSettings.SB_EnableBlades);
			ImGui::SliderFloat("Blade Intensity", &mainSettings.SB_BladeInt, 0.0f, 1.0f);
			ImGui::SliderFloat("Blade Points", &mainSettings.SB_BladeVertices, 2.0f, 15.0f, "%.0f");
			ImGui::SliderFloat("Blade Splay", &mainSettings.SB_BladeSplay, 0.0f, 1.0f);
			ImGui::SliderFloat("Blade Rotation", &mainSettings.SB_BladeRotation, 0.0f, 360.0f, "%.0f");
			ImGui::SliderFloat("Blade Length", &mainSettings.SB_BladeLength, 0.4f, 0.9f);
			ImGui::SliderFloat("Blade Base Width", &mainSettings.SB_BladeBaseWidth, 1.0f, 3.0f);
			ImGui::SliderFloat("Blade Width", &mainSettings.SB_BladeWidth, 1.0f, 3.0f);
			ImGui::SliderFloat("Blade Taper", &mainSettings.SB_BladeTaper, 1.0f, 10.0f);
			ImGui::SliderFloat("Blade Sharpness", &mainSettings.SB_BladeFeather, 10.0f, 100.0f, "%.0f");
			ImGui::SliderFloat("Blade Fade Power", &mainSettings.SB_BladeFadePow, 0.0f, 1.0f);
			ImGui::SliderFloat("Blade Fade Distance", &mainSettings.SB_BladeFadeDist, 0.0f, 10.0f);
			ImGui::Spacing();
			ImGui::Spacing();

			ImGui::Checkbox("Enable Rays", (bool*)&mainSettings.SB_EnableRays);
			ImGui::SliderFloat("Rays Intensity", &mainSettings.SB_RandomRaysInt, 0.0f, 5.0f);
			ImGui::SliderFloat("Rays Volume", &mainSettings.SB_RandomRaysVolume, 0.0f, 0.6f);
			ImGui::SliderFloat("Rays Length", &mainSettings.SB_RandomRaysLength, 0.2f, 1.0f);
			ImGui::SliderFloat("Rays Width", &mainSettings.SB_RandomRaysWidth, 0.0f, 1.0f);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Changing this also changes the shape of the effect");
			ImGui::Spacing();
			ImGui::Spacing();

			ImGui::Text("Color ");
			ImGui::SameLine();
			float4& SBcolor = coldSettings.SB_Color;
			if (ImGui::ColorButton("Pick", ImVec4(SBcolor.x, SBcolor.y, SBcolor.z, SBcolor.w)))
				ImGui::OpenPopup("ColorPopup_");

			if (ImGui::BeginPopup("ColorPopup_", ImGuiWindowFlags_NoMove)) {
				ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() + ImGui::GetStyle().ItemInnerSpacing.x * 2);
				if (ImGui::SmallButton("X"))
					ImGui::CloseCurrentPopup();
				ImGui::ColorPicker4("RGBA", &SBcolor.x);
				ImGui::EndPopup();
			}
			ImGui::EndTabItem();
		}

		//// Lens Ghosts ////
		if (ImGui::BeginTabItem("Lens Ghosting")) {
			ImGui::Spacing();
			if (ImGui::CollapsingHeader("Description")) {
				ImGui::Text("Adds a procession of faint flares that move with the camera, simulating reflections bouncing inside the lens stack.");
				ImGui::Text("By default only the first 10 flares are actively used.");
			}
			ImGui::SeparatorText("General");
			ImGui::Spacing();
			if (ImGui::Checkbox("Enable Ghosts", &settings.EnableGhosts))
				renderdata->GetEffect(Shaders::LensGhosts).Toggle(settings.EnableGhosts);

			ImGui::SliderFloat("Ghost Size", &mainSettings.GH_Scale, 0.1f, 1.5f);
			ImGui::SliderFloat("Ghost Intensity", &mainSettings.GH_Intensity, 0.0f, 2.0f);
			ImGui::SliderFloat("Ghost Saturation", &mainSettings.GH_Saturation, 0.0f, 1.0f);
			ImGui::Spacing();
			ImGui::Spacing();
			ImGui::SeparatorText("Advanced");
			ImGui::Spacing();
			ImGui::Checkbox("Ghost Enable Clamp", (bool*)&mainSettings.GH_EnableClampOffset);
			ImGui::SliderFloat("Ghost Clamp Offset", &mainSettings.GH_ClampOffset, -1.0f, 1.0f);
			ImGui::Spacing();
			ImGui::Spacing();
			for (int i = 0; i < ghostpasses; i++) {
				if (ImGui::TreeNodeEx(("------------------------ Ghost #" + std::to_string(i + 1) + " ------------------------").c_str())) {
					float4& params = coldSettings.GH_Params[i];
					float4& params_2 = coldSettings.GH_Params_2[i];
					float4& color = coldSettings.GH_Color[i];
					float4& atlas = coldSettings.GH_Atlas[i];
					ImGui::PushID(i);

					ImGui::Text("General");
					ImGui::SliderFloat("Position", &params.y, -1.0f, 1.0f);
					ImGui::SliderFloat("Size", &params.x, 0.02f, 1.0f);
					ImGui::SliderFloat("Shape", &params.z, 3.0f, 9.0f, "%.0f");
					ImGui::SliderFloat("Roundness", &params.w, 0.0f, 1.0f);
					ImGui::SliderFloat("Rotation", &params_2.x, 0.0f, 360.0f, "%.0f");
					ImGui::SliderFloat("Intensity", &color.w, 0.1f, 2.0f);
					ImGui::SliderFloat("Inner Intensity", &params_2.w, 0.0f, 1.0f);
					ImGui::SliderFloat("Edge Feather", &params_2.y, 0.0f, 1.0f);
					ImGui::SliderFloat("CA Scale", &params_2.z, 1.0f, 2.0f);
					ImGui::Spacing();
					ImGui::Spacing();

					ImGui::Text("Atlas");
					ImGui::SliderFloat("Texture", &atlas.x, 1.0f, 4.0f, "%.0f");
					ImGui::SliderFloat("Visibility", &atlas.y, 0.0f, 1.0f);
					ImGui::SliderFloat("Scale", &atlas.z, 0.5f, 1.0f);
					ImGui::Spacing();
					ImGui::Spacing();

					ImGui::Text("Color ");
					ImGui::SameLine();
					if (ImGui::ColorButton("Pick", ImVec4(color.x, color.y, color.z, color.w)))
						ImGui::OpenPopup("ColorPopup_");

					if (ImGui::BeginPopup("ColorPopup_", ImGuiWindowFlags_NoMove)) {
						ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() + ImGui::GetStyle().ItemInnerSpacing.x * 2);
						if (ImGui::SmallButton("X"))
							ImGui::CloseCurrentPopup();
						ImGui::ColorPicker4("RGB", &color.x);
						ImGui::EndPopup();
					}
					ImGui::Text("");
					ImGui::PopID();
					ImGui::TreePop();
				}
			}
			ImGui::EndTabItem();
		}

		//// Lens Glare ////
		if (ImGui::BeginTabItem("Lens Glare")) {
			ImGui::Spacing();
			if (ImGui::CollapsingHeader("Description")) {
				ImGui::Text("A crescent shaped light leak that forms near the lower edge of the lens, decreasing contrast and giving a washed out look.");
				ImGui::Text("By default glare position is static, when 'dynamic x' is enabled the horizontal position will track with the sun, and 'x axis offset' becomes a base offset.");
			}
			ImGui::SeparatorText("General");
			if (ImGui::Checkbox("Enable Lens Glare", &settings.EnableLensGlare))
				renderdata->GetEffect(Shaders::LensGlare).Toggle(settings.EnableLensGlare);

			ImGui::SliderFloat("Glare Scale", &mainSettings.GL_Scale, 0.1f, 1.0f);
			ImGui::SliderFloat("Glare Intensity", &mainSettings.GL_Intensity, 0.1f, 2.0f);
			ImGui::Spacing();
			ImGui::Spacing();
			ImGui::SeparatorText("Advanced");
			ImGui::Checkbox("Dynamic X", (bool*)&mainSettings.GL_DynPosition);
			ImGui::SliderFloat("X Axis Offset", &mainSettings.GL_XAxisOffset, 0.0f, 1.0f);
			ImGui::SliderFloat("Y Axis Offset", &mainSettings.GL_YAxisOffset, 0.0f, 1.0f);
			ImGui::SliderFloat("Max Rotation", &mainSettings.GL_MaxRotation, 0.0f, 180.0f, "%.0f");
			ImGui::SliderFloat("Shape", &mainSettings.GL_CutDepth, 0.5f, 1.0f);
			ImGui::SliderFloat("Radius", &mainSettings.GL_Radius, 0.5f, 1.0f);
			ImGui::SliderFloat("Tip Fade", &mainSettings.GL_TipFade, 0.0f, 1.5f);

			ImGui::EndTabItem();
		}

		//// Lens Halo ////
		if (ImGui::BeginTabItem("Lens Halo")) {
			ImGui::Spacing();
			if (ImGui::CollapsingHeader("Description")) {
				ImGui::Text("Creates a faint segmented halo around the radius of the sun, which expands and contracts depending on the angle it's viewed from.");
			}
			ImGui::SeparatorText("General");
			if (ImGui::Checkbox("Enable Sun Halo", &settings.EnableHalo))
				renderdata->GetEffect(Shaders::LensHalo).Toggle(settings.EnableHalo);

			ImGui::SliderFloat("Halo Scale", &mainSettings.HL_Scale, 0.1f, 1.0f);
			ImGui::SliderFloat("Halo Intensity", &mainSettings.HL_Intensity, 0.01f, 1.0f);
			ImGui::Spacing();
			ImGui::Spacing();
			ImGui::SeparatorText("Advanced");
			ImGui::Checkbox("Enable Halo Expansion", (bool*)&mainSettings.HL_EnableExp);
			ImGui::Checkbox("Flip Halo Expansion", (bool*)&mainSettings.HL_FlipExpOffset);
			ImGui::SliderFloat("Min Expansion", &mainSettings.HL_ExpMinSize, 0.2f, 1.0f);
			ImGui::SliderFloat("Max Expansion", &mainSettings.HL_ExpMaxSize, 0.2f, 1.0f);
			ImGui::SliderFloat("Rotation Speed", &mainSettings.HL_RotationSpeed, 0.0f, 1.0f);
			ImGui::SliderFloat("Rake Increment (degrees)", &mainSettings.HL_LineVolume, 1.0f, 45.0f, "%.0f");
			ImGui::SliderFloat("Length", &mainSettings.HL_LineLength, 0.05f, 0.8f);
			ImGui::SliderFloat("Width", &mainSettings.HL_LineWidth, 0.03f, 0.5f);
			ImGui::SliderFloat("Taper", &mainSettings.HL_LineTaper, 0.0f, 0.15f);
			ImGui::SliderFloat("Chromatic Shift", &mainSettings.HL_ColorShift, 0.0f, 1.0f);
			ImGui::Spacing();
			ImGui::Spacing();

			ImGui::Text("Color ");
			ImGui::SameLine();
			float4& Color = coldSettings.HL_Color;
			if (ImGui::ColorButton("Pick", ImVec4(Color.x, Color.y, Color.z, Color.w)))
				ImGui::OpenPopup("ColorPopup_");

			if (ImGui::BeginPopup("ColorPopup_", ImGuiWindowFlags_NoMove)) {
				ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() + ImGui::GetStyle().ItemInnerSpacing.x * 2);
				if (ImGui::SmallButton("X"))
					ImGui::CloseCurrentPopup();
				ImGui::ColorPicker4("RGBA", &Color.x);
				ImGui::EndPopup();
			}
			ImGui::EndTabItem();
		}

		//// Lens Ice ////
		if (ImGui::BeginTabItem("Lens Frost")) {
			ImGui::Spacing();
			if (ImGui::CollapsingHeader("Description")) {
				ImGui::Text("A vignette that slowly forms around the edge of the camera lens during snow based weathers.");
			}
			ImGui::SeparatorText("General");
			if (ImGui::Checkbox("Enable Lens Frost", &settings.EnableIce))
				renderdata->GetEffect(Shaders::LensIce).Toggle(settings.EnableIce);

			ImGui::SliderFloat("Frost Intensity", &mainSettings.LI_Intensity, 0.01f, 1.0f);
			ImGui::SliderFloat("Fade Duration", &coldSettings.LI_FadeDuration, 0.01f, 2.0f);
			ImGui::SliderFloat("Fade In Delay", &coldSettings.LI_FadeIn, 0.01f, 1.0f);
			ImGui::SliderFloat("Fade Out Delay", &coldSettings.LI_FadeOut, 0.01f, 1.0f);
			ImGui::Spacing();
			ImGui::Spacing();

			ImGui::Text("Color ");
			ImGui::SameLine();
			float4& Color = coldSettings.LI_Color;
			if (ImGui::ColorButton("Pick", ImVec4(Color.x, Color.y, Color.z, Color.w)))
				ImGui::OpenPopup("ColorPopup_");

			if (ImGui::BeginPopup("ColorPopup_", ImGuiWindowFlags_NoMove)) {
				ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() + ImGui::GetStyle().ItemInnerSpacing.x * 2);
				if (ImGui::SmallButton("X"))
					ImGui::CloseCurrentPopup();
				ImGui::ColorPicker4("RGBA", &Color.x);
				ImGui::EndPopup();
			}
			ImGui::EndTabItem();
		}

		//// Chromatic Aberration ////
		if (ImGui::BeginTabItem("Chromatic Aberration")) {
			ImGui::Spacing();
			if (ImGui::CollapsingHeader("Description")) {
				ImGui::Text("Chromatic Aberration splits the red, green and blue colour channels slightly apart which creates colored fringes on edges and mild image blur.");
				ImGui::Text("By default the amount of chromatic aberration is dependant on camera motion. The motion clamp limits the maximum color offset under motion.");
				ImGui::Text("Note: Setting low motion thresholds without also decreasing intensity/clamp will cause image warping/skipping.");
			}
			ImGui::SeparatorText("General");
			if (ImGui::Checkbox("Enable Chromatic Aberration", &settings.EnableCA))
				renderdata->GetEffect(Shaders::LensCA).Toggle(settings.EnableCA);

			ImGui::SliderFloat("CA Intensity", &mainSettings.CA_Intensity, 0.0f, 2.0f);
			ImGui::SliderFloat("CA Motion Threshold", &mainSettings.CA_Threshold, 0.0f, 0.1f);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Set to 0 for constant CA offset");
			ImGui::SliderFloat("CA Motion Clamp", &mainSettings.CA_MaxOffset, 0.001f, 0.015f);
			ImGui::Checkbox("Offset Red Channel Only", (bool*)&mainSettings.CA_RChannelOnly);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("This will yield better performance");

			ImGui::EndTabItem();
		}

		//// Sun Glare ////
		if (ImGui::BeginTabItem("Sun Glare")) {
			ImGui::Spacing();
			if (ImGui::CollapsingHeader("Description")) {
				ImGui::Text("Allows modification of sun glare.");
				ImGui::Text("Note: This shader is strongly effected by weather mod settings for bloom, tonemapping and color grading and is therefore disabled by default.");
			}
			ImGui::SeparatorText("General");
			if (ImGui::Checkbox("Enable Sun Glare", &settings.EnableSunGlare))
				renderdata->GetEffect(Shaders::LensSunGlare).Toggle(settings.EnableSunGlare);

			ImGui::SliderFloat("Glare Scale ##sun", &mainSettings.SG_Scale, 0.25f, 1.0f);
			ImGui::SliderFloat("Glare Intensity ##sun", &mainSettings.SG_Intensity, 0.0f, 1.0f);
			ImGui::SliderFloat("Glare Outer Fade ##sun", &mainSettings.SG_OuterFade, 0.0f, 1.0f);
			ImGui::Spacing();
			ImGui::Spacing();

			ImGui::Text("Color ");
			ImGui::SameLine();
			float4& SGColor = coldSettings.SG_Color;
			if (ImGui::ColorButton("Pick", ImVec4(SGColor.x, SGColor.y, SGColor.z, SGColor.w)))
				ImGui::OpenPopup("ColorPopup_");

			if (ImGui::BeginPopup("ColorPopup_", ImGuiWindowFlags_NoMove)) {
				ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() + ImGui::GetStyle().ItemInnerSpacing.x * 2);
				if (ImGui::SmallButton("X"))
					ImGui::CloseCurrentPopup();
				ImGui::ColorPicker4("RGBA", &SGColor.x, ImGuiColorEditFlags_AlphaBar);
				ImGui::EndPopup();
			}
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}
}

void LensEffects::RefreshToggles()
{
	if (renderdata) {
		renderdata->GetEffect(Shaders::LensIce).Toggle(settings.EnableIce);
		renderdata->GetEffect(Shaders::LensCA).Toggle(settings.EnableCA);
		renderdata->GetEffect(Shaders::LensBurst).Toggle(settings.EnableStarburst);
		renderdata->GetEffect(Shaders::LensSunGlare).Toggle(settings.EnableSunGlare);
		renderdata->GetEffect(Shaders::LensGlare).Toggle(settings.EnableLensGlare);
		renderdata->GetEffect(Shaders::LensHalo).Toggle(settings.EnableHalo);
		renderdata->GetEffect(Shaders::LensGhosts).Toggle(settings.EnableGhosts);
	}
}

void LensEffects::LoadSettings(json& o_json)
{
	settings = o_json;
	RefreshToggles();
}

void LensEffects::SaveSettings(json& o_json)
{
	o_json = settings;
}

void LensEffects::RestoreDefaultSettings()
{
	settings = {};
	RefreshToggles();
}
