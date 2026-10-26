#include "RainRefraction.h"

#include <DirectXTex.h>

#include "Effects11.h"
#include "Effects11/EffectManager.h"
#include "Effects11/SettingManager.h"

#include "I18n/I18n.h"
#include "Menu.h"
#include "ShaderCache.h"
#include "State.h"
#include "Utils/D3D.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	RainRefraction::Settings,
	Enable,
	MotionStretch,
	MotionTransparency)

namespace
{
	constexpr auto kDefaultRaindropsPath = L"Data\\Shaders\\RainRefraction\\Raindrops.png";

	/** @brief Loads a raindrop normal map (RGB normal, A coverage) as a mipmapped BC7 texture named a_name. */
	RainRefraction::RaindropTexture LoadRaindropTexture(const std::filesystem::path& a_path, const char* a_name)
	{
		RainRefraction::RaindropTexture result;
		const auto fail = [&](std::string a_status) {
			logger::warn("[Rain Refraction] {}: {}", a_path.string(), a_status);
			result.status = std::move(a_status);
			return std::move(result);
		};

		// Most presets ship without enbraindrops.png, so a missing file is not worth a warning
		if (!std::filesystem::exists(a_path)) {
			logger::debug("[Rain Refraction] Raindrop texture not found: {}", a_path.string());
			result.status = std::format("Texture not found: {}", a_path.filename().string());
			return result;
		}

		DirectX::ScratchImage image;
		HRESULT hr = DirectX::LoadFromWICFile(a_path.c_str(), DirectX::WIC_FLAGS_IGNORE_SRGB, nullptr, image);
		if (FAILED(hr))
			return fail(std::format("Failed to load texture (invalid image, HRESULT 0x{:08X})", static_cast<uint32_t>(hr)));

		DirectX::ScratchImage mipImage;
		hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_DEFAULT, 0, mipImage);
		if (FAILED(hr))
			return fail(std::format("Failed to generate mipmaps (HRESULT 0x{:08X})", static_cast<uint32_t>(hr)));

		DirectX::ScratchImage bc7Image;
		hr = DirectX::Compress(mipImage.GetImages(), mipImage.GetImageCount(), mipImage.GetMetadata(),
			DXGI_FORMAT_BC7_UNORM, DirectX::TEX_COMPRESS_BC7_QUICK, 1.0f, bc7Image);
		if (FAILED(hr))
			return fail(std::format("Failed to compress texture (HRESULT 0x{:08X})", static_cast<uint32_t>(hr)));

		const auto& metadata = bc7Image.GetMetadata();
		winrt::com_ptr<ID3D11Texture2D> texture;
		hr = DirectX::CreateTexture(globals::d3d::device, bc7Image.GetImages(), bc7Image.GetImageCount(), metadata,
			reinterpret_cast<ID3D11Resource**>(texture.put()));
		if (FAILED(hr))
			return fail(std::format("Failed to create GPU texture (HRESULT 0x{:08X})", static_cast<uint32_t>(hr)));
		Util::SetResourceName(texture.get(), a_name);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{
			.Format = DXGI_FORMAT_BC7_UNORM,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = static_cast<UINT>(metadata.mipLevels) }
		};
		hr = globals::d3d::device->CreateShaderResourceView(texture.get(), &srvDesc, result.srv.put());
		if (FAILED(hr))
			return fail(std::format("Failed to create shader resource view (HRESULT 0x{:08X})", static_cast<uint32_t>(hr)));
		Util::SetResourceName(result.srv.get(), "%s SRV", a_name);

		logger::info("[Rain Refraction] Loaded raindrop texture: {} ({}x{}, BC7, {} mips)", a_path.string(), metadata.width, metadata.height, metadata.mipLevels);
		return result;
	}
}

#define I18N_KEY_PREFIX "feature.rain_refraction."

void RainRefraction::DrawSettings()
{
	const Util::LockedSection enbLock(IsManagedByENB(),
		T("common.settings_managed_by_enb", "This setting is managed by Effects 11."));

	if (!defaultRaindrops.status.empty())
		Util::Text::WrappedWarning("%s", defaultRaindrops.status.c_str());

	ImGui::Checkbox(T(TKEY("enable"), "Enable"), &settings.Enable);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("enable_tooltip"), "Renders rain as refractive drops. Requires Dynamic Cubemaps."));
	}
	ImGui::SliderFloat(T(TKEY("motion_stretch"), "Motion Stretch"), &settings.MotionStretch, kMinMotionStretch, kMaxMotionStretch, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("motion_stretch_tooltip"), "How far drops stretch along their fall velocity.\nLow values give round drops, 1 = vanilla streak length, higher values stretch further."));
	}
	ImGui::SliderFloat(T(TKEY("motion_transparency"), "Motion Transparency"), &settings.MotionTransparency, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("motion_transparency_tooltip"), "Fades the drops; higher values make rain more transparent."));
	}
}

#undef I18N_KEY_PREFIX

void RainRefraction::LoadSettings(json& o_json)
{
	settings = o_json;
	settings.MotionStretch = std::clamp(settings.MotionStretch, kMinMotionStretch, kMaxMotionStretch);
	settings.MotionTransparency = std::clamp(settings.MotionTransparency, 0.0f, 1.0f);
}

void RainRefraction::SaveSettings(json& o_json)
{
	o_json = settings;
}

void RainRefraction::RestoreDefaultSettings()
{
	settings = {};
}

void RainRefraction::SetupResources()
{
	defaultRaindrops = LoadRaindropTexture(kDefaultRaindropsPath, "RainRefraction::DefaultRaindrops");

	D3D11_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	if (FAILED(globals::d3d::device->CreateBlendState(&blendDesc, alphaBlendState.put()))) {
		logger::error("[Rain Refraction] Failed to create alpha blend state, refractive rain disabled");
		return;
	}
	Util::SetResourceName(alphaBlendState.get(), "RainRefraction::AlphaBlendState");
}

void RainRefraction::LoadPresetRaindrops(const std::filesystem::path& a_presetPath)
{
	presetRaindrops = LoadRaindropTexture(a_presetPath / "enbraindrops.png", "RainRefraction::PresetRaindrops");
}

bool RainRefraction::IsManagedByENB() const
{
	const auto& effects11 = globals::features::effects11;
	return effects11.loaded && effects11.enableEffect;
}

ID3D11ShaderResourceView* RainRefraction::GetRaindropSRV() const
{
	if (IsManagedByENB() && presetRaindrops.srv)
		return presetRaindrops.srv.get();
	return defaultRaindrops.srv.get();
}

RainRefraction::PerFrame RainRefraction::GetCommonBufferData()
{
	// Effects 11 resolves its active state and time of day on first use each frame
	auto& effects11 = globals::features::effects11;
	if (effects11.loaded)
		effects11.CheckCommonData();

	if (!perFrameCacheChecker.IsNewFrame())
		return perFrameCache;

	PerFrame data{
		.Enable = settings.Enable,
		.MotionStretch = settings.MotionStretch,
		.MotionTransparency = settings.MotionTransparency
	};

	if (IsManagedByENB()) {
		auto& settingManager = SettingManager::GetSingleton();
		data.Enable = settingManager.GetValue<bool>(EffectManager::GetSingleton().ids.enableRain);
		data.MotionStretch = settingManager.GetInterpolatedTimeOfDayValue("MotionStretch", "RAIN");
		data.MotionTransparency = settingManager.GetInterpolatedTimeOfDayValue("MotionTransparency", "RAIN");
	}
	data.Enable = data.Enable && GetRaindropSRV() && alphaBlendState;

	perFrameCache = data;
	return data;
}

bool RainRefraction::IsRefractiveRainPass()
{
	auto state = globals::state;
	// Runs per draw, so reuse the last feature buffer resolve instead of re-querying settings
	return state->currentPixelDescriptor == static_cast<uint32_t>(SIE::ShaderCache::ParticleShaderTechniques::EnvCubeRain) &&
	       perFrameCache.Enable;
}

void RainRefraction::ModifyParticle()
{
	if (!IsRefractiveRainPass())
		return;

	auto context = globals::d3d::context;
	ID3D11ShaderResourceView* srv = GetRaindropSRV();
	context->PSSetShaderResources(80, 1, &srv);

	ID3D11Buffer* cbs[] = { globals::state->sharedDataCB->CB(), globals::state->featureDataCB->CB() };
	context->VSSetConstantBuffers(5, 2, cbs);
}

void RainRefraction::ParticleShaderHacks()
{
	auto state = globals::state;
	if (!state->currentShader || state->currentShader->shaderType.get() != RE::BSShader::Type::Particle)
		return;
	if (!IsRefractiveRainPass())
		return;

	float blendFactor[4] = { 0, 0, 0, 0 };
	globals::d3d::context->OMSetBlendState(alphaBlendState.get(), blendFactor, 0xFFFFFFFF);
	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_ALPHA_BLEND);
}
