#include "LUT.h"

#include "Features/PostProcessing.h"
#include "RasterPass.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"

#include "CSEditor/SceneManager/CustomSceneControls.h"
#include "Presets/UnifiedPresetCatalog.h"

#include <DDSTextureLoader.h>
#include <DirectXTex.h>

#include "I18n/I18n.h"
#include <algorithm>
#include <cctype>
#include <imgui_stdlib.h>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LUT::Settings,
	LutPath,
	InputMin,
	InputMax)

namespace
{
	std::string GetLowercaseExtension(const std::filesystem::path& path)
	{
		return Util::ToLower(path.extension().string());
	}

	/** @brief Load/Clear acting on the feature's own LUT, as the main menu shows it. */
	void DrawBaseLutControls(LUT& lut)
	{
		const bool loadClicked = ImGui::Button(T("feature.post_processing.lut.load", "Load"));
		if (loadClicked)
			lut.ReadTexture(lut.tempPath);
		ImGui::SameLine();
		const bool clearClicked = ImGui::Button(T("feature.post_processing.lut.clear", "Clear"));
		if (clearClicked) {
			lut.Clear();
			lut.tempPath = "";
		}
		if (loadClicked || clearClicked)
			CustomSceneControls::RecordLutBaselineEdit(lut);
		if (!lut.errMsg.empty()) {
			ImGui::SameLine();
			Util::Text::Error("%s", lut.errMsg.c_str());
		}

		if (lut.LutType == -1)
			ImGui::Text(T("feature.post_processing.lut.loaded_texture_none", "Loaded Texture: None"));
		else
			ImGui::Text(T("feature.post_processing.lut.loaded_texture", "Loaded Texture: %s"), lut.settings.LutPath.c_str());
	}
}

void LUT::DrawSettings()
{
	ImGui::TextWrapped(T("feature.post_processing.lut.relative_path_starts_from_game_executable_directory_supports", "Relative path starts from game executable directory. Supports dds/bmp/png format."));
	ImGui::BulletText(T("feature.post_processing.lut.1d_lut_n_x_1_sized_images", "1D LUT: N x 1 sized images."));
	ImGui::BulletText(T("feature.post_processing.lut.3d_lut_in_2d_format_n_r_x", "3D LUT in 2D format: N (R) x N (G) sized images, stacked horizontally along blue axis."));
	ImGui::BulletText(T("feature.post_processing.lut.3d_lut_3d_dds_only", "3D LUT: 3D dds only."));

	ImGui::InputText(T("feature.post_processing.lut.lut_texture_path", "LUT Texture Path"), &tempPath);

	if (!CustomSceneControls::DrawLutSceneControls(*this))
		DrawBaseLutControls(*this);

	ImGui::Separator();

	if (LutType == 0 || LutType == 1)
		if (ImGui::BeginTable("##1d", 2)) {
			ImGui::TableNextColumn();
			ImGui::RadioButton(T("feature.post_processing.lut.map_luma", "Map Luma"), &LutType, 0);
			ImGui::TableNextColumn();
			ImGui::RadioButton(T("feature.post_processing.lut.map_per_channel", "Map Per Channel"), &LutType, 1);
			ImGui::EndTable();
		}
	ImGui::InputFloat3(T("feature.post_processing.lut.input_min", "Input Min"), &settings.InputMin.x);
	ImGui::InputFloat3(T("feature.post_processing.lut.input_max", "Input Max"), &settings.InputMax.x);
}

void LUT::RestoreDefaultSettings()
{
	settings = {};
	tempPath = {};
	Clear();
}

void LUT::LoadSettings(json& o_json)
{
	settings = o_json;
	tempPath = settings.LutPath;
}

void LUT::UpdateTexture()
{
	// Scene blends reload every frame, so only hit the disk when the path or the pack it resolves against changes.
	if (settings.LutPath == attemptedPath && UnifiedPresetCatalog::GetSingleton().GetActivePackId() == attemptedPackId)
		return;
	try {
		if (settings.LutPath.empty())
			Clear();
		else
			ReadTexture(settings.LutPath);
	} catch (const std::exception& e) {
		logger::warn("Failed to load LUT settings: {}", e.what());
	}
}

void LUT::SaveSettings(json& o_json)
{
	o_json = settings;
}

void LUT::SetupResources()
{
	auto renderer = globals::game::renderer;

	if (!settings.LutPath.empty())
		ReadTexture(settings.LutPath);

	logger::debug("Creating buffers...");
	{
		lutCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<LUTCB>());
	}

	logger::debug("Creating 2D textures...");
	{
		auto gameTexMainCopy = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_COPY];

		D3D11_TEXTURE2D_DESC texDesc;
		gameTexMainCopy.texture->GetDesc(&texDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};

		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		texDesc.MipLevels = srvDesc.Texture2D.MipLevels = 1;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		texDesc.MiscFlags = 0;

		texOutput = eastl::make_unique<Texture2D>(texDesc);
		texOutput->CreateSRV(srvDesc);
		texOutput->CreateRTV(rtvDesc);
	}

	CompileRasterShaders();
}

std::string LUT::ValidateLutFile(const std::filesystem::path& resolvedPath)
{
	const auto extension = GetLowercaseExtension(resolvedPath);
	if (extension != ".dds" && extension != ".png" && extension != ".bmp")
		return std::format("Invalid extension: {}! Only dds/png/bmp are supported.", resolvedPath.extension().string());
	std::error_code ec;
	if (!std::filesystem::exists(resolvedPath, ec))
		return "The file does not exist.";
	return {};
}

void LUT::ReadTexture(std::string requestedPath)
{
	constexpr auto comErrMsg = "Failed to create texture! Error: {}";

	auto device = globals::d3d::device;
	auto& catalog = UnifiedPresetCatalog::GetSingleton();

	Clear();
	// Kept even when loading fails, so a scene entry's path still matches what the feature reports.
	settings.LutPath = attemptedPath = requestedPath;
	attemptedPackId = catalog.GetActivePackId();

	const auto path = catalog.ResolveActivePackPath(requestedPath);
	errMsg = ValidateLutFile(path);
	if (!errMsg.empty()) {
		logger::warn("LUT '{}': {}", requestedPath, errMsg);
		return;
	}
	const auto extension = GetLowercaseExtension(path);

	if (extension == ".dds") {
		ID3D11Resource* pRsrc = nullptr;
		ID3D11ShaderResourceView* pSrv = nullptr;
		try {
			DX::ThrowIfFailed(DirectX::CreateDDSTextureFromFile(device, path.c_str(), &pRsrc, &pSrv));
		} catch (std::runtime_error& e) {
			errMsg = std::format(comErrMsg, e.what());
			logger::warn(comErrMsg, e.what());
			return;
		}

		D3D11_RESOURCE_DIMENSION texType;
		pRsrc->GetType(&texType);
		if (texType == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
			texLUT2D = eastl::make_unique<Texture2D>(reinterpret_cast<ID3D11Texture2D*>(pRsrc));
			texLUT2D->srv.attach(pSrv);
			LutType = texLUT2D->desc.Height == 1 ? 0 : 2;
		} else if (texType == D3D11_RESOURCE_DIMENSION_TEXTURE3D) {
			texLUT3D = eastl::make_unique<Texture3D>(reinterpret_cast<ID3D11Texture3D*>(pRsrc));
			texLUT3D->srv.attach(pSrv);
			LutType = 3;
		} else {
			errMsg = std::format("Invalid texture dimension: {}! Only 2D/3D textures are supported.", magic_enum::enum_name(texType));
			logger::warn("Invalid texture dimension: {}! Only 2D/3D textures are supported.", magic_enum::enum_name(texType));
			return;
		}
	} else {
		DirectX::ScratchImage image;
		try {
			DX::ThrowIfFailed(DirectX::LoadFromWICFile(path.c_str(), DirectX::WIC_FLAGS_NONE, nullptr, image));
		} catch (std::runtime_error& e) {
			errMsg = std::format(comErrMsg, e.what());
			logger::warn(comErrMsg, e.what());
			return;
		}

		ID3D11Resource* pRsrc = nullptr;
		try {
			DX::ThrowIfFailed(CreateTexture(device, image.GetImages(), image.GetImageCount(), image.GetMetadata(), &pRsrc));
		} catch (std::runtime_error& e) {
			errMsg = std::format(comErrMsg, e.what());
			logger::warn(comErrMsg, e.what());
			return;
		}

		texLUT2D = eastl::make_unique<Texture2D>(reinterpret_cast<ID3D11Texture2D*>(pRsrc));

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texLUT2D->desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};
		texLUT2D->CreateSRV(srvDesc);

		LutType = texLUT2D->desc.Height == 1 ? 0 : 2;
	}
}

void LUT::ClearShaderCache()
{
	BumpShaderGeneration();
	const auto shaderPtrs = std::array{
		&lutPS
	};

	{
		std::lock_guard lock(shaderMutex);
		for (auto shader : shaderPtrs)
			if ((*shader)) {
				(*shader)->Release();
				shader->detach();
			}
	}

	globals::shaderCache->ClearStandaloneComputeCache(L"PostProcessing/LUT");
	CompileRasterShaders();
}

void LUT::CompileRasterShaders()
{
	const std::vector<PixelShaderCompileInfo> shaderInfos = {
		{ &lutPS, "lut.ps.hlsl" },
	};

	CompileRasterShadersAsync(L"Data\\Shaders\\PostProcessing\\LUT", {}, shaderInfos);
}

void LUT::Draw(TextureInfo& inout_tex)
{
	if (LutType == -1)
		return;

	if (!owner || !owner->GetFullscreenVS())
		return;
	if (!AllShadersReady({ &lutPS }))
		return;

	globals::profiler->BeginPass("PostProcessing::LUT");
	auto context = globals::d3d::context;

	float2 res = { (float)texOutput->desc.Width, (float)texOutput->desc.Height };
	res = Util::ConvertToDynamic(res);

	LUTCB data = {
		.InputMin = settings.InputMin,
		.InputMax = settings.InputMax,
		.LutType = LutType
	};
	lutCB->Update(data);

	{
		PostProcessingRaster::RasterPass pass(context);

		ID3D11ShaderResourceView* srv[3] = {
			inout_tex.srv,
			LutType == 3 ? nullptr : texLUT2D->srv.get(),
			LutType == 3 ? texLUT3D->srv.get() : nullptr
		};

		ID3D11Buffer* cb = lutCB->CB();

		context->PSSetConstantBuffers(1, 1, &cb);
		context->PSSetShaderResources(0, 3, srv);
		pass.SetTargets({ texOutput->rtv.get() }, res.x, res.y);
		pass.SetShaders(owner->GetFullscreenVS(), lutPS.get());
		pass.Draw();

		// clean up
		std::fill(srv, srv + 3, nullptr);
		cb = nullptr;
		context->PSSetShaderResources(0, 3, srv);
		context->PSSetConstantBuffers(1, 1, &cb);
	}

	inout_tex = { texOutput->resource.get(), texOutput->srv.get() };
	globals::profiler->EndPass();
}
