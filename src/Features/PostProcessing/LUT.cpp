#include "LUT.h"

#include "State.h"
#include "Util.h"

#include <DDSTextureLoader.h>
#include <DirectXTex.h>

#include <imgui_stdlib.h>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LUT::Settings,
	LutPath,
	InputMin,
	InputMax)

void LUT::DrawSettings()
{
	ImGui::TextWrapped("Relative path starts from game executable directory. Supports dds/bmp/png format.");
	ImGui::BulletText("1D LUT: N x 1 sized images.");
	ImGui::BulletText("3D LUT in 2D format: N (R) x N (G) sized images, stacked horizontally along blue axis.");
	ImGui::BulletText("3D LUT: 3D dds only.");

	ImGui::InputText("LUT Texture Path", &tempPath);

	if (ImGui::Button("Load"))
		ReadTexture(tempPath);
	ImGui::SameLine();
	if (ImGui::Button("Clear")) {
		Clear();
		tempPath = "";
	}
	ImGui::SameLine();
	ImGui::TextColored({ 1.f, 0.f, 0.f, 1.f }, errMsg.c_str());

	if (LutType == -1)
		ImGui::Text("Loaded Texture: None");
	else
		ImGui::Text("Loaded Texture: %s", settings.LutPath.c_str());

	ImGui::Separator();

	if (LutType == 0 || LutType == 1)
		if (ImGui::BeginTable("##1d", 2)) {
			ImGui::TableNextColumn();
			ImGui::RadioButton("Map Luma", &LutType, 0);
			ImGui::TableNextColumn();
			ImGui::RadioButton("Map Per Channel", &LutType, 1);
			ImGui::EndTable();
		}
	ImGui::InputFloat3("Input Min", &settings.InputMin.x);
	ImGui::InputFloat3("Input Max", &settings.InputMax.x);
}

void LUT::RestoreDefaultSettings()
{
	settings = {};
}

void LUT::LoadSettings(json& o_json)
{
	settings = o_json;

	tempPath = settings.LutPath;
	logger::info("Loading LUT settings, LUT Path: {}", settings.LutPath);

	try {
		if (!tempPath.empty() && !firstLoad)
			ReadTexture(tempPath);
		else if (firstLoad)
			firstLoad = false;
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

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		texDesc.MipLevels = srvDesc.Texture2D.MipLevels = 1;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texDesc.MiscFlags = 0;

		texOutput = eastl::make_unique<Texture2D>(texDesc);
		texOutput->CreateSRV(srvDesc);
		texOutput->CreateUAV(uavDesc);
	}

	CompileComputeShaders();
}

void LUT::ReadTexture(std::filesystem::path path)
{
	constexpr auto comErrMsg = "Failed to create texture! Error: {}";

	auto device = globals::d3d::device;

	Clear();

	if (path.extension() != ".dds" && path.extension() != ".png" && path.extension() != ".bmp") {
		errMsg = std::format("Invalid extension: {}! Only dds/png/bmp are supported.", path.extension().string());
		logger::warn("Invalid extension: {}! Only dds/png/bmp are supported.", path.extension().string());
		return;
	}
	if (!std::filesystem::exists(path)) {
		errMsg = "The file does not exist.";
		logger::warn("The file does not exist.");
		return;
	}

	if (path.extension() == ".dds") {
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

	settings.LutPath = path.string();
}

void LUT::ClearShaderCache()
{
	const auto shaderPtrs = std::array{
		&lutCS
	};

	for (auto shader : shaderPtrs)
		if ((*shader)) {
			(*shader)->Release();
			shader->detach();
		}

	CompileComputeShaders();
}

void LUT::CompileComputeShaders()
{
	struct ShaderCompileInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* programPtr;
		std::string_view filename;
		std::vector<std::pair<const char*, const char*>> defines = {};
		std::string entry = "main";
	};

	std::vector<ShaderCompileInfo>
		shaderInfos = {
			{ &lutCS, "lut.cs.hlsl" },
		};

	for (auto& info : shaderInfos) {
		auto path = std::filesystem::path("Data\\Shaders\\PostProcessing\\LUT") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0", info.entry.c_str())))
			info.programPtr->attach(rawPtr);
	}
}

void LUT::Draw(TextureInfo& inout_tex)
{
	if (LutType == -1)
		return;

	auto context = globals::d3d::context;

	float2 res = { (float)texOutput->desc.Width, (float)texOutput->desc.Height };
	res = Util::ConvertToDynamic(res);

	LUTCB data = {
		.InputMin = settings.InputMin,
		.InputMax = settings.InputMax,
		.LutType = LutType
	};
	lutCB->Update(data);

	ID3D11ShaderResourceView* srv[3] = {
		inout_tex.srv,
		LutType == 3 ? nullptr : texLUT2D->srv.get(),
		LutType == 3 ? texLUT3D->srv.get() : nullptr
	};

	ID3D11UnorderedAccessView* uav = texOutput->uav.get();
	ID3D11Buffer* cb = lutCB->CB();

	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	context->CSSetShaderResources(0, 3, srv);
	context->CSSetShader(lutCS.get(), nullptr, 0);

	context->Dispatch(((uint)res.x + 7) >> 3, ((uint)res.y + 7) >> 3, 1);

	// clean up
	std::fill(srv, srv + 3, nullptr);
	uav = nullptr;
	cb = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	context->CSSetShaderResources(0, 3, srv);
	context->CSSetConstantBuffers(0, 1, &cb);
	context->CSSetShader(nullptr, nullptr, 0);

	inout_tex = { texOutput->resource.get(), texOutput->srv.get() };
}