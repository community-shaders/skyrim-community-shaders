#include "LensEffects.h"
#include "State.h"
#include "Upscaling.h"
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
	GL_Intensity, GL_Scale, GL_XAxisOffset, GL_YAxisOffset,
	GL_MaxRotation, GL_CutDepth, GL_Radius, GL_TipFade,
	HL_Scale, HL_Intensity, HL_EnableExp,
	HL_FlipExpOffset, HL_ExpMinSize, HL_ExpMaxSize,
	HL_RotationSpeed, HL_LineVolume, HL_LineLength,
	HL_LineWidth, HL_LineTaper, HL_ColorShift,
	CA_Intensity, CA_Threshold, CA_MaxOffset)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LensEffects::ColdSettings,
	GH_Params, GH_Params_2, GH_Color, GH_Atlas,
	GH_InInt, SB_Color, HL_Color, LI_Color,
	LI_FadeIn, LI_FadeOut, LI_FadeDuration, useOldSchoolCA)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LensEffects::Settings,
	mainsettings, coldsettings, EnableStarburst,
	EnableLensGlare, EnableHalo, EnableGhosts,
	EnableIce, EnableSunGlare, EnableCA, useCustomPreset)

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

	D3D11_SAMPLER_DESC pointMirSamplerDesc{ linearSamplerDesc };
	pointMirSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	pointMirSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_MIRROR;
	pointMirSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_MIRROR;
	pointMirSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_MIRROR;

	D3D11_SAMPLER_DESC pointSamplerDesc{ linearSamplerDesc };
	pointSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;

	D3D11_SAMPLER_DESC depthSamplerDesc{ linearSamplerDesc };
	depthSamplerDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
	depthSamplerDesc.ComparisonFunc = D3D11_COMPARISON_EQUAL;

	DX::ThrowIfFailed(device->CreateSamplerState(&linearSamplerDesc, &LinearSampler));
	DX::ThrowIfFailed(device->CreateSamplerState(&pointSamplerDesc, &PointSampler));
	DX::ThrowIfFailed(device->CreateSamplerState(&pointMirSamplerDesc, &PointMirrorSampler));
	DX::ThrowIfFailed(device->CreateSamplerState(&depthSamplerDesc, &DepthSampler));

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

	std::sort(weatherDisables.begin(), weatherDisables.end());

	skyrim_FlareData = reinterpret_cast<uintptr_t*>(REL::RelocationID(527915, 414867).address());
	skyrim_RunFlarePtr = reinterpret_cast<uint32_t*>(REL::RelocationID(527916, 414862).address());

	skyrim_SunPosition = reinterpret_cast<RE::NiPoint3*>(REL::RelocationID(527924, 414871).address());
	skyrim_SunGlareScale = reinterpret_cast<float*>(REL::RelocationID(502611, 370235).address());

	renderdata = new Setup::LF_RenderData;

	if (!settingsLoaded || !settings) {
		stdSettings = {};
		settings = &stdSettings;
	}

	renderdata->SetupPass(Shaders::LensCA, settings->EnableCA, 1, { .uncond_pass = true });
	renderdata->SetupPass(Shaders::LensIce, settings->EnableIce, 1, { .weather_pass = true });
	renderdata->SetupPass(Shaders::LensBurst, settings->EnableStarburst, 1);
	renderdata->SetupPass(Shaders::LensSunGlare, settings->EnableSunGlare, 1);
	renderdata->SetupPass(Shaders::LensGlare, settings->EnableLensGlare, 1);
	renderdata->SetupPass(Shaders::LensHalo, settings->EnableHalo, 1);
	renderdata->SetupPass(Shaders::LensGhosts, settings->EnableGhosts, ghostpasses);

	renderdata->SetupRenderData();

	CompileShaders();
}

void LensEffects::CheckOverride()
{
	static Util::FrameChecker frame_checker;

	if (overrideShader) {
		if (frame_checker.IsNewFrame()) {
			SettingsCB->Update(UpdateBufferValues());
			UpdateWeatherBasedDisable();
			UpdateUpscalingBasedDisable();
			frameIdx = (frameIdx < 16) ? ++frameIdx : 5;
		}
		LookupShader(shaderdesc);
	}
}

void LensEffects::LookupShader(int desc)
{
	static const std::unordered_map<int, void (LensEffects::*)()> effects{
		{ Shaders::Bypass, &LensEffects::BypassShader },
		{ Shaders::AttachLUT, &LensEffects::AppendOcclusionLUT },
		{ Shaders::OcculsionMask, &LensEffects::SetupOcclusionMask },
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
	context->PSSetSamplers(12, 1, &PointMirrorSampler);
	context->PSSetSamplers(13, 1, &DepthSampler);

	if (!BlendState[1]) {
		context->OMGetBlendState(&BlendState[1], nullptr, nullptr);
		if (BlendState[1]) {
			D3D11_BLEND_DESC SrcBlendDesc = {};
			BlendState[1]->GetDesc(&SrcBlendDesc);
			auto& blendState = SrcBlendDesc.RenderTarget[0];
			blendState.BlendEnable = TRUE;
			blendState.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			globals::d3d::device->CreateBlendState(&SrcBlendDesc, &BlendState[0]);
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

	auto& mainCopySRV = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].SRVCopy;

	context->VSSetShader(SunGlareVertexShader, NULL, NULL);
	context->PSSetShader(SunGlarePixelShader, NULL, NULL);

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

	auto it = renderdata->GetCurrentEffect().passesdone - 1;
	float4& params = settings->coldsettings.GH_Params[it];
	float4& params_2 = settings->coldsettings.GH_Params_2[it];

	ConstBuffer data = UpdateBufferValues();
	data.shadersettings.GH_Size = params.x;
	data.shadersettings.GH_Offset = params.y;
	data.shadersettings.GH_Shape = params.z;
	data.shadersettings.GH_Roundness = params.w;
	data.shadersettings.GH_Rotation = params_2.x;
	data.shadersettings.GH_Feather = params_2.y;
	data.shadersettings.GH_CAScale = params_2.z;
	data.shadersettings.GH_MoveCurve = params_2.w;
	data.shadersettings.GH_InnerInt = settings->coldsettings.GH_InInt[it];
	data.shadersettings.GH_Color = VectorToXMFloat(settings->coldsettings.GH_Color[it]);
	data.shadersettings.GH_Atlas = VectorToXMFloat(settings->coldsettings.GH_Atlas[it]);

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

	auto& mainCopy = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].SRVCopy;
	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR].SRV;

	if (settings->coldsettings.useOldSchoolCA) {
		context->OMSetBlendState(BlendState[1], nullptr, 0xFFFFFFFF);
	} else
		context->OMSetBlendState(BlendState[0], nullptr, 0xFFFFFFFF);

	context->VSSetShader(BypassVertexShader, NULL, NULL);
	context->PSSetShader(ChromaticAberrationPixelShader, NULL, NULL);

	context->PSSetShaderResources(1, 1, &mainCopy);
	context->PSSetShaderResources(2, 1, &motionVector);

	overrideShader = false;
}

void LensEffects::SetupIceEffect()
{
	auto context = globals::d3d::context;

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

	REX::W32::ComPtr<ID3D11RenderTargetView> oldRTVs[8];
	REX::W32::ComPtr<ID3D11DepthStencilView> oldDSV;
	UINT numRTs = 0;

	context->OMGetRenderTargets(8, oldRTVs[0].GetAddressOf(), oldDSV.GetAddressOf());

	for (UINT i = 0; i < 8; ++i)
		if (oldRTVs[i].Get())
			numRTs++;

	if (!useCloudLUT) {
		context->OMSetRenderTargetsAndUnorderedAccessViews(numRTs, reinterpret_cast<ID3D11RenderTargetView* const*>(oldRTVs), oldDSV.Get(), numRTs, 1, &SunOcclusionUAV, nullptr);
	} else {
		context->OMSetRenderTargetsAndUnorderedAccessViews(numRTs, reinterpret_cast<ID3D11RenderTargetView* const*>(oldRTVs), oldDSV.Get(), numRTs, 1, &SunOcclusionUAV_AT, nullptr);
		context->PSSetShaderResources(30, 1, &SunOcclusionSRV);
		useCloudLUT = false;
	}

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
			static float fadeMod;

			if (CheckWeatherChange()) {
				transEpoch = lastUpdate;
				if (auto currWeather = sky->currentWeather; currWeather && currWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kSnow)) {
					fade = std::max(sky->currentWeather->data.precipitationBeginFadeIn / 255.0f / 24.0f / 7.0f, 0.0018f);
					fadeMod = settings->coldsettings.LI_FadeIn / 24.0f / 4.0f;
					weatherRole = 1;
					epochDelta = 0;
				} else if (auto prevWeather = sky->lastWeather; prevWeather && prevWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kSnow)) {
					fade = std::max(sky->lastWeather->data.precipitationEndFadeOut / 255.0f / 24.0f / 7.0f, 0.00025f);
					fadeMod = settings->coldsettings.LI_FadeOut / 24.0f / 4.0f;
					weatherRole = 2;
					epochDelta = snowPrecipValue;
				} else {
					weatherRole = false;
					return;
				}
			}

			if (weatherRole) {
				float duration = settings->coldsettings.LI_FadeDuration / 24.0f / 4.0f;
				float time = RE::Calendar::GetSingleton()->GetCurrentGameTime();
				bool IsInside = RE::PlayerCharacter::GetSingleton()->GetParentCell()->IsInteriorCell();
				static bool lastIsInside = IsInside;
				if (IsInside != lastIsInside) {
					lastIsInside = IsInside;
					epochDelta = snowPrecipValue;
					transEpoch = time;
					fadeMod = (IsInside) ? settings->coldsettings.LI_FadeOut : settings->coldsettings.LI_FadeIn;
					fadeMod = fadeMod / 24.0f / 4.0f;
					duration = (IsInside) ? duration * 0.5f : duration;
					fade = 0;
				}

				float fadeValue = std::clamp((time - (transEpoch + (fade + fadeMod))) / duration, 0.0f, 1.0f);

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
		auto update = [&]() {
			WeatherID = (sky->currentWeather) ? sky->currentWeather->formID : 0;
			PrevWeatherID = (sky->lastWeather) ? sky->lastWeather->formID : 0; };

		if (!WeatherID || std::floor(sky->lastWeatherUpdate * 60) == std::floor(sky->currentGameHour * 60)) {
			update();
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
		currWeatherRole = std::binary_search(weatherDisables.begin(), weatherDisables.end(), WeatherID);
		prevWeatherRole = std::binary_search(weatherDisables.begin(), weatherDisables.end(), PrevWeatherID);
	}

	if (auto sky = globals::game::sky; sky && (currWeatherRole || prevWeatherRole)) {
		auto weatherTransition = LinearStep(0.5, 1.0, sky->currentWeatherPct);
		if ((weatherTransition == 1.0f && currWeatherRole) || (currWeatherRole && prevWeatherRole))
			disableSunFX = true;
		weatherFadeout = (currWeatherRole) ? weatherTransition : 1.0f - weatherTransition;
	}
}

void LensEffects::UpdateUpscalingBasedDisable()
{
	auto& upscaling = globals::features::upscaling;
	auto method = globals::features::upscaling.GetUpscaleMethod();
	if (upscaling.IsFrameGenerationActive() || method == Upscaling::UpscaleMethod::kDLSS || method == Upscaling::UpscaleMethod::kFSR || method == Upscaling::UpscaleMethod::kXESS) {
		renderdata->GetEffect(Shaders::LensIce).Toggle(false);
		upscalingActive = true;
	} else if (upscalingActive) {
		renderdata->GetEffect(Shaders::LensIce).Toggle(settings->EnableIce);
		upscalingActive = false;
	}
}

DirectX::XMFLOAT4A LensEffects::GetSunPosition()
{
	auto sunWSPos = *skyrim_SunPosition;
	float4 sunPosition = { sunWSPos.x, sunWSPos.y, sunWSPos.z, 1.0f };
	Matrix viewMatrix = Util::GetCameraData(0).viewMat;
	float4 sunPosVS = float4::Transform(sunPosition, viewMatrix);
	sunPosVS.w = *skyrim_SunGlareScale * SunScale;

	return VectorToXMFloat(sunPosVS);
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
				lens.shaderdesc = Shaders::AttachLUT;
			}
		}

		else if (skyProperty->uiSkyObjectType == RE::BSSkyShaderProperty::SkyObject::SO_CLOUDS) {
			if ((Pass->passEnum == 0x5C000062 || Pass->passEnum == 0x5C000063 || Pass->passEnum == 0x5C000064) && RenderFlags == 65) {
				lens.overrideShader = true;
				lens.shaderdesc = Shaders::AttachLUT;
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

	bool sunVisble = static_cast<bool>(*lens.skyrim_RunFlarePtr);

	if (lens.disableSunFX)
		sunVisble = false;

	if (*lens.skyrim_FlareData) {
		lens.renderdata->ResetEffects(sunVisble);
		*lens.skyrim_RunFlarePtr = 1;
	} else
		*lens.skyrim_RunFlarePtr = 0;
}

void LensEffects::Hooks::BSImagespaceShader_Render<RE::ImageSpaceManager::ISLensFlareVisibility>::thunk(void* shader, RE::BSTriShape* shape, RE::ImageSpaceEffectParam* param)
{
	auto& lens = globals::features::lensEffects;

	lens.overrideShader = true;
	lens.shaderdesc = Shaders::OcculsionMask;

	func(shader, shape, param);
}

void LensEffects::Hooks::BSImagespaceShader_Render<RE::ImageSpaceManager::ISLensFlare>::thunk(void* shader, RE::BSTriShape* shape, RE::ImageSpaceEffectParam* param)
{
	auto& lens = globals::features::lensEffects;

	lens.overrideShader = true;
	lens.shaderdesc = lens.renderdata->UpdateCurrentEffect();

	if (lens.renderdata->GetCurrentEffect().IsWeatherShader() && lens.shaderdesc != Shaders::Bypass)
		lens.GetWeatherShader();

	func(shader, shape, param);
}

void LensEffects::DrawSettings()
{
	auto& mainSettings = settings->mainsettings;
	auto& coldSettings = settings->coldsettings;

	if (ImGui::TreeNode("Preset Options")) {
		if (PresetFileExists() && presetLoaded) {
			ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Lens preset loaded");
			if (ImGui::Checkbox("Enable  ", &stdSettings.useCustomPreset))
				settings = (stdSettings.useCustomPreset) ? &customSettings : &stdSettings;
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("When enabled any changes made to settings are applied to the preset");

			ImGui::SameLine();
			if (ImGui::Button("Delete Preset")) {
				DeletePresetFile();
				presetLoaded = false;
				settings = &stdSettings;
			}
		} else {
			if (ImGui::Button("Export As Preset")) {
				customSettings = *settings;
				ExportAsPreset();
				presetLoaded = true;
				if (stdSettings.useCustomPreset)
					settings = &customSettings;
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Saves the current settings to Data/Shaders/LensEffects/Lens Settings.json");
				ImGui::Text("This is meant for modders who want to package a custom lens preset with their mod");
			}
		}
		ImGui::TreePop();
	}
	ImGui::Spacing();

	if (ImGui::Checkbox("Enable Sun Glare", &settings->EnableSunGlare))
		renderdata->GetEffect(Shaders::LensSunGlare).Toggle(settings->EnableSunGlare);

	if (ImGui::Checkbox("Enable Starburst", &settings->EnableStarburst))
		renderdata->GetEffect(Shaders::LensBurst).Toggle(settings->EnableStarburst);

	if (ImGui::Checkbox("Enable Ghosts", &settings->EnableGhosts))
		renderdata->GetEffect(Shaders::LensGhosts).Toggle(settings->EnableGhosts);

	if (ImGui::Checkbox("Enable Lens Glare", &settings->EnableLensGlare))
		renderdata->GetEffect(Shaders::LensGlare).Toggle(settings->EnableLensGlare);

	if (ImGui::Checkbox("Enable Sun Halo", &settings->EnableHalo))
		renderdata->GetEffect(Shaders::LensHalo).Toggle(settings->EnableHalo);

	if (ImGui::Checkbox("Enable Chromatic Aberration", &settings->EnableCA))
		renderdata->GetEffect(Shaders::LensCA).Toggle(settings->EnableCA);

	if (upscalingActive) {
		ImGui::BeginDisabled();
	}

	ImGui::Checkbox("Enable Lens Ice", &settings->EnableIce);

	if (upscalingActive) {
		ImGui::SameLine();
		ImGui::Text("  (Incompatible with Upscaling Method)");
		ImGui::EndDisabled();
	}

	if (!upscalingActive && ImGui::IsItemEdited())
		renderdata->GetEffect(Shaders::LensIce).Toggle(settings->EnableIce);
	ImGui::Spacing();
	ImGui::Spacing();

	//// Sun Glare ////
	ImGui::SeparatorText("Sun Glare");

	ImGui::Spacing();
	ImGui::SliderFloat("Glare Scale ##sun", &mainSettings.SG_Scale, 0.25f, 1.0f);
	ImGui::SliderFloat("Glare Intensity ##sun", &mainSettings.SG_Intensity, 0.01f, 3.0f);
	ImGui::SliderFloat("Glare Outer Intensity ##sun", &mainSettings.SG_OuterInt, 0.01f, 3.5f);
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
	ImGui::Spacing();
	ImGui::Spacing();
	ImGui::Spacing();

	//// Lens Starburst ////
	ImGui::SeparatorText("Starburst");

	ImGui::Spacing();
	ImGui::SliderFloat("Burst Size", &mainSettings.SB_Scale, 0.1f, 1.0f);
	ImGui::SliderFloat("Burst Intensity", &mainSettings.SB_Intensity, 0.01f, 3.0f);
	ImGui::Spacing();
	ImGui::Spacing();

	if (ImGui::TreeNode("Advanced ##AdvBurst")) {
		ImGui::Spacing();
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

		ImGui::Checkbox("Enable Random Rays", (bool*)&mainSettings.SB_EnableRays);
		ImGui::SliderFloat("Random Rays Intensity", &mainSettings.SB_RandomRaysInt, 0.0f, 10.0f);
		ImGui::SliderFloat("Random Rays Volume", &mainSettings.SB_RandomRaysVolume, 0.0f, 0.6f);
		ImGui::SliderFloat("Random Rays Length", &mainSettings.SB_RandomRaysLength, 0.2f, 1.0f);
		ImGui::SliderFloat("Random Rays Width", &mainSettings.SB_RandomRaysWidth, 0.0f, 1.0f);
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
		ImGui::TreePop();
	}
	ImGui::Spacing();
	ImGui::Spacing();
	ImGui::Spacing();

	//// Lens Ghosts ////
	ImGui::SeparatorText("Ghost Artifacts");

	ImGui::Spacing();
	ImGui::SliderFloat("Ghost Size", &mainSettings.GH_Scale, 0.1f, 1.5f);
	ImGui::SliderFloat("Ghost Intensity", &mainSettings.GH_Intensity, 0.0f, 2.0f);
	ImGui::SliderFloat("Ghost Saturation", &mainSettings.GH_Saturation, 0.0f, 1.0f);
	ImGui::Spacing();
	ImGui::Spacing();

	if (ImGui::TreeNode("Advanced ##AdvGhost")) {
		ImGui::Checkbox("Ghost Enable Clamp", (bool*)&mainSettings.GH_EnableClampOffset);
		ImGui::SliderFloat("Ghost Clamp Offset", &mainSettings.GH_ClampOffset, -1.0f, 1.0f);
		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::Text("Per Ghost Settings");

		for (int i = ghostpasses - 1; i >= 0; --i) {
			if (ImGui::TreeNodeEx(("------------------------ Ghost #" + std::to_string(ghostpasses - (i)) + " ------------------------").c_str())) {
				float4& params = coldSettings.GH_Params[i];
				float4& params_2 = coldSettings.GH_Params_2[i];
				float4& color = coldSettings.GH_Color[i];
				float4& atlas = coldSettings.GH_Atlas[i];
				float& inint = coldSettings.GH_InInt[i];
				ImGui::PushID(i);

				ImGui::Text("General");
				ImGui::SliderFloat("Size", &params.x, 0.02f, 1.0f);
				ImGui::SliderFloat("Intensity", &color.w, 0.1f, 2.0f);
				ImGui::SliderFloat("Inner Intensity", &inint, 0.0f, 1.0f);
				ImGui::SliderFloat("Offset", &params.y, -1.0f, 1.0f);
				ImGui::SliderFloat("Shape", &params.z, 3.0f, 9.0f, "%.0f");
				ImGui::SliderFloat("Roundness", &params.w, 0.0f, 1.0f);
				ImGui::SliderFloat("Rotation", &params_2.x, 0.0f, 360.0f, "%.0f");
				ImGui::SliderFloat("Edge Feather", &params_2.y, 0.03f, 1.0f);
				ImGui::SliderFloat("CA Scale", &params_2.z, 1.0f, 2.0f);
				ImGui::SliderFloat("Movement Curve", &params_2.w, 0.1f, 10.0f, "%.0f");
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
		ImGui::TreePop();
	}
	ImGui::Spacing();
	ImGui::Spacing();
	ImGui::Spacing();

	//// Lens Glare ////
	ImGui::SeparatorText("Lens Glare");

	ImGui::Spacing();
	ImGui::SliderFloat("Glare Scale", &mainSettings.GL_Scale, 0.1f, 1.0f);
	ImGui::SliderFloat("Glare Intensity", &mainSettings.GL_Intensity, 0.1f, 2.0f);
	ImGui::Spacing();
	ImGui::Spacing();

	if (ImGui::TreeNode("Advanced ##AdvGlare")) {
		ImGui::SliderFloat("X Axis Offset", &mainSettings.GL_XAxisOffset, 0.0f, 1.0f);
		ImGui::SliderFloat("Y Axis Offset", &mainSettings.GL_YAxisOffset, 0.0f, 1.0f);
		ImGui::SliderFloat("Max Rotation", &mainSettings.GL_MaxRotation, 0.0f, 180.0f, "%.0f");
		ImGui::SliderFloat("Shape", &mainSettings.GL_CutDepth, 0.5f, 1.0f);
		ImGui::SliderFloat("Radius", &mainSettings.GL_Radius, 0.5f, 1.0f);
		ImGui::SliderFloat("Tip Fade", &mainSettings.GL_TipFade, 0.0f, 1.5f);
		ImGui::TreePop();
	}
	ImGui::Spacing();
	ImGui::Spacing();
	ImGui::Spacing();

	//// Lens Halo ////
	ImGui::SeparatorText("Halo");

	ImGui::Spacing();
	ImGui::SliderFloat("Halo Scale", &mainSettings.HL_Scale, 0.1f, 1.0f);
	ImGui::SliderFloat("Halo Intensity", &mainSettings.HL_Intensity, 0.01f, 1.0f);
	ImGui::Spacing();
	ImGui::Spacing();

	if (ImGui::TreeNode("Advanced ##AdvHalo")) {
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
		ImGui::TreePop();
	}
	ImGui::Spacing();
	ImGui::Spacing();
	ImGui::Spacing();

	//// Lens Ice ////
	ImGui::SeparatorText("Lens Ice");

	if (upscalingActive)
		ImGui::BeginDisabled();

	ImGui::Spacing();
	ImGui::SliderFloat("Ice Intensity", &mainSettings.LI_Intensity, 0.01f, 2.0f);
	ImGui::SliderFloat("Fade Duration", &coldSettings.LI_FadeDuration, 0.01f, 2.0f);
	ImGui::SliderFloat("Fade In Delay", &coldSettings.LI_FadeIn, 0.01f, 1.0f);
	ImGui::SliderFloat("Fade Out Delay", &coldSettings.LI_FadeOut, 0.01f, 1.0f);
	ImGui::Spacing();
	ImGui::Spacing();
	ImGui::Text("Color ");
	ImGui::SameLine();

	float4& Color = coldSettings.LI_Color;
	if (ImGui::ColorButton("Pick ##LI", ImVec4(Color.x, Color.y, Color.z, Color.w)))
		ImGui::OpenPopup("ColorPopup_ ##LI");

	if (ImGui::BeginPopup("ColorPopup_ ##LI", ImGuiWindowFlags_NoMove)) {
		ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() + ImGui::GetStyle().ItemInnerSpacing.x * 2);
		if (ImGui::SmallButton("X"))
			ImGui::CloseCurrentPopup();
		ImGui::ColorPicker4("RGBA ##LI", &Color.x);
		ImGui::EndPopup();
	}
	ImGui::Spacing();
	ImGui::Spacing();
	ImGui::Spacing();

	if (upscalingActive)
		ImGui::EndDisabled();

	//// Chromatic Aberration ////
	ImGui::SeparatorText("Chromatic Aberration");

	ImGui::Spacing();
	ImGui::SliderFloat("CA Intensity", &mainSettings.CA_Intensity, 0.0f, 3.0f);
	ImGui::SliderFloat("CA Motion Threshold", &mainSettings.CA_Threshold, 0.001f, 0.1f);
	ImGui::SliderFloat("CA Max Offset", &mainSettings.CA_MaxOffset, 0.001f, 0.015f);
	ImGui::Checkbox("Use Old School CA", &coldSettings.useOldSchoolCA);
	ImGui::Spacing();
	ImGui::Spacing();
	ImGui::Spacing();
}

void LensEffects::RefreshToggles()
{
	if (renderdata) {
		renderdata->GetEffect(Shaders::LensIce).Toggle(settings->EnableIce);
		renderdata->GetEffect(Shaders::LensCA).Toggle(settings->EnableCA);
		renderdata->GetEffect(Shaders::LensBurst).Toggle(settings->EnableStarburst);
		renderdata->GetEffect(Shaders::LensSunGlare).Toggle(settings->EnableSunGlare);
		renderdata->GetEffect(Shaders::LensGlare).Toggle(settings->EnableLensGlare);
		renderdata->GetEffect(Shaders::LensHalo).Toggle(settings->EnableHalo);
		renderdata->GetEffect(Shaders::LensGhosts).Toggle(settings->EnableGhosts);
	}
}

bool LensEffects::PresetFileExists()
{
	return std::filesystem::exists(customSettingsPath) && std::filesystem::is_regular_file(customSettingsPath);
}

void LensEffects::ExportAsPreset()
{
	if (!PresetFileExists()) {
		try {
			std::filesystem::path path(customSettingsPath);
			if (path.has_parent_path())
				std::filesystem::create_directories(path.parent_path());
			else {
				logger::error("[Lens Effects] Failed to create file {}", customSettingsPath);
				return;
			}
		} catch (const std::filesystem::filesystem_error& e) {
			logger::error("[Lens Effects] Filesystem error when creating {} : {}", customSettingsPath, e.what());
			return;
		} catch (const std::exception& e) {
			logger::error("[Lens Effects] Unexpected error when creating {} : {}", customSettingsPath, e.what());
			return;
		}
	}

	json to_write;
	to_write["Lens Effects"] = (presetLoaded) ? customSettings : *settings;
	std::ofstream outfile(customSettingsPath);

	if (outfile) {
		outfile << to_write.dump(1);
		logger::info("[Lens Effects] Saved preset to {}", customSettingsPath);
	} else {
		logger::error("[Lens Effects] Failed to save {}", customSettingsPath);
	}
}

bool LensEffects::LoadFromPreset()
{
	std::ifstream file(customSettingsPath);
	json custom;

	try {
		file >> custom;
	} catch (const nlohmann::json::parse_error& e) {
		logger::error("[Lens Effects] Failed to parse {}: {}", customSettingsPath, e.what());
		return false;
	}

	auto it = custom.find("Lens Effects");
	if (it != custom.end() && it->is_object()) {
		try {
			logger::info("[Lens Effects] Loading preset from {}", customSettingsPath);
			customSettings = custom["Lens Effects"].get<Settings>();
			return true;
		} catch (const nlohmann::json::type_error& e) {
			logger::error("[Lens Effects] Type error converting settings from {}: {}", customSettingsPath, e.what());
		}
	} else {
		logger::error("[Lens Effects] Failed to load preset from {}", customSettingsPath);
	}

	return false;
}

void LensEffects::DeletePresetFile()
{
	if (PresetFileExists()) {
		std::filesystem::path path(customSettingsPath);
		try {
			if (!std::filesystem::remove(path)) {
				logger::error("[Lens Effects] Failed to delete lens preset file");
			}
		} catch (const std::filesystem::filesystem_error& e) {
			logger::error("[Lens Effects] Filesystem error when deleting {} : {}", customSettingsPath, e.what());
		} catch (const std::exception& e) {
			logger::error("[Lens Effects] Unexpected error when deleting {} : {}", customSettingsPath, e.what());
		}
	}
}

void LensEffects::LoadSettings(json& o_json)
{
	stdSettings = o_json;

	if (PresetFileExists())
		presetLoaded = LoadFromPreset();

	settings = (presetLoaded && stdSettings.useCustomPreset) ? &customSettings : &stdSettings;

	RefreshToggles();

	settingsLoaded = true;
}

void LensEffects::SaveSettings(json& o_json)
{
	o_json = stdSettings;

	if (presetLoaded)
		ExportAsPreset();
}

void LensEffects::RestoreDefaultSettings()
{
	stdSettings = {};

	if (PresetFileExists())
		presetLoaded = LoadFromPreset();

	RefreshToggles();
}
