#include "TerrainShadows.h"

#include <DirectXTex.h>
#include <pystring/pystring.h>

#include "Menu.h"
#include "State.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TerrainShadows::Settings,
	EnableTerrainShadow)

void TerrainShadows::LoadSettings(json& o_json)
{
	settings = o_json;
}

void TerrainShadows::SaveSettings(json& o_json)
{
	o_json = settings;
}

void TerrainShadows::DrawSettings()
{
	ImGui::Checkbox("Enable Terrain Shadow", &settings.EnableTerrainShadow);

	if (ImGui::CollapsingHeader("Debug")) {
		std::string curr_worldspace = "N/A";
		std::string curr_worldspace_name = "N/A";
		auto tes = globals::game::tes;
		if (tes) {
			auto worldspace = tes->GetRuntimeData2().worldSpace;
			if (worldspace) {
				curr_worldspace = worldspace->GetFormEditorID();
				curr_worldspace_name = worldspace->GetName();
			}
		}
		ImGui::Text(fmt::format("Current worldspace: {} ({})", curr_worldspace, curr_worldspace_name).c_str());
		ImGui::Text(fmt::format("Has height map: {}", heightmaps.contains(curr_worldspace)).c_str());

		ImGui::Separator();

		ImGui::BulletText("shadowUpdateCBData");
		ImGui::Indent();
		{
			ImGui::Text(fmt::format("LightPxDir: ({}, {})", shadowUpdateCBData.LightPxDir.x, shadowUpdateCBData.LightPxDir.y).c_str());
			ImGui::Text(fmt::format("LightDeltaZ: ({}, {})", shadowUpdateCBData.LightDeltaZ.x, shadowUpdateCBData.LightDeltaZ.y).c_str());
			ImGui::Text(fmt::format("StartPxCoord: {}", shadowUpdateCBData.StartPxCoord).c_str());
			ImGui::Text(fmt::format("PxSize: ({}, {})", shadowUpdateCBData.PxSize.x, shadowUpdateCBData.PxSize.y).c_str());
		}
		ImGui::Unindent();

		if (ImGui::TreeNode("Buffer Viewer")) {
			static float debugRescale = .1f;
			ImGui::SliderFloat("View Resize", &debugRescale, 0.f, 1.f);

			if (texShadowHeight) {
				BUFFER_VIEWER_NODE_BULLET(texShadowHeight, debugRescale)
			}
			ImGui::TreePop();
		}
	}
}

void TerrainShadows::ClearShaderCache()
{
	if (shadowUpdateProgram) {
		shadowUpdateProgram->Release();
		shadowUpdateProgram = nullptr;
	}

	CompileComputeShaders();
}

void TerrainShadows::ParseHeightmapPath(std::filesystem::path p, bool xlodgen_style)
{
	auto filename = p.filename();
	if (filename.extension() != ".dds")
		return;
	logger::debug("Found dds: {}", filename.string());

	auto splitstr = pystring::split(filename.stem().string(), ".");
	if (splitstr.size() != (xlodgen_style ? 9 : 10)) {
		logger::debug("{} has incorrect number ({}) of fields", filename.string(), splitstr.size());
		return;
	}

	bool middle_check = xlodgen_style ? ((splitstr[1] == "Terrain") && (splitstr[2] == "HeightMap")) : (splitstr[1] == "HeightMap");
	if (middle_check) {
		HeightMapMetadata metadata;
		try {
			if (xlodgen_style) {
				metadata.worldspace = splitstr[0];
				metadata.pos0.x = std::stoi(splitstr[3]) * 4096.f;
				metadata.pos1.y = std::stoi(splitstr[4]) * 4096.f;
				metadata.pos1.x = (std::stoi(splitstr[5]) + 1) * 4096.f;
				metadata.pos0.y = (std::stoi(splitstr[6]) + 1) * 4096.f;
				metadata.pos0.z = -32767 * 8.f;
				metadata.pos1.z = 32767 * 8.f;
				metadata.zRange.x = std::stoi(splitstr[7]) * 8.f;
				metadata.zRange.y = std::stoi(splitstr[8]) * 8.f;
			} else {
				metadata.worldspace = splitstr[0];
				metadata.pos0.x = std::stoi(splitstr[2]) * 4096.f;
				metadata.pos1.y = std::stoi(splitstr[3]) * 4096.f;
				metadata.pos1.x = (std::stoi(splitstr[4]) + 1) * 4096.f;
				metadata.pos0.y = (std::stoi(splitstr[5]) + 1) * 4096.f;
				metadata.pos0.z = std::stoi(splitstr[6]) * 8.f;
				metadata.pos1.z = std::stoi(splitstr[7]) * 8.f;
				metadata.zRange.x = std::stoi(splitstr[8]) * 8.f;
				metadata.zRange.y = std::stoi(splitstr[9]) * 8.f;
			}
		} catch (std::exception& e) {
			logger::debug("Failed to parse {}. Error: {}", filename.string(), e.what());
			return;
		}

		metadata.dir = p.parent_path().wstring();
		metadata.filename = filename.string();

		if (heightmaps.contains(metadata.worldspace))
			logger::warn("{} has more than one height maps!", metadata.worldspace);
		heightmaps[metadata.worldspace] = metadata;

		logger::info("{} loaded.", filename.string());
	} else
		logger::debug("{} has unknown type ({})", filename.string(), splitstr[1]);
}

void TerrainShadows::SetupResources()
{
	logger::debug("Listing xLODGen height maps...");
	{
		std::filesystem::path texture_dir{ L"Data\\textures\\Terrain\\" };
		std::error_code ec;
		for (auto const& dir_entry : std::filesystem::directory_iterator{ texture_dir, ec }) {
			auto dir_path = dir_entry.path();
			if (!std::filesystem::is_directory(dir_path))
				continue;

			for (auto const& sub_dir_entry : std::filesystem::directory_iterator{ dir_path })
				ParseHeightmapPath(sub_dir_entry.path(), true);
		}
	}

	logger::debug("Listing height maps...");
	{
		std::filesystem::path texture_dir{ L"Data\\textures\\heightmaps\\" };
		std::error_code ec;
		for (auto const& dir_entry : std::filesystem::directory_iterator{ texture_dir, ec })
			ParseHeightmapPath(dir_entry.path(), false);
	}

	logger::debug("Creating constant buffers...");
	{
		shadowUpdateCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<ShadowUpdateCB>());
	}

	CompileComputeShaders();
}

void TerrainShadows::CompileComputeShaders()
{
	logger::debug("Compiling shaders...");
	{
		auto program_ptr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\TerrainShadows\\ShadowUpdate.cs.hlsl", {}, "cs_5_0"));
		if (program_ptr)
			shadowUpdateProgram.attach(program_ptr);
	}
}

bool TerrainShadows::IsHeightMapReady()
{
	if (auto tes = globals::game::tes)
		if (auto worldspace = tes->GetRuntimeData2().worldSpace)
			return cachedHeightmap && cachedHeightmap->worldspace == worldspace->GetFormEditorID();
	return false;
}

TerrainShadows::PerFrame TerrainShadows::GetCommonBufferData()
{
	bool isHeightmapReady = IsHeightMapReady();

	PerFrame data = {
		.EnableTerrainShadow = settings.EnableTerrainShadow && isHeightmapReady,
	};

	if (isHeightmapReady) {
		auto invScale = cachedHeightmap->pos1 - cachedHeightmap->pos0;
		data.Scale = float3(1.f, 1.f, 1.f) / invScale;
		data.Offset = -cachedHeightmap->pos0 * float2{ data.Scale.x, data.Scale.y };
		data.ZRange = cachedHeightmap->zRange;
	}

	return data;
}

void TerrainShadows::LoadHeightmap()
{
	auto tes = globals::game::tes;
	if (!tes)
		return;
	auto worldspace = tes->GetRuntimeData2().worldSpace;
	if (!worldspace)
		return;
	std::string worldspace_name = worldspace->GetFormEditorID();
	if (!heightmaps.contains(worldspace_name))  // no height map for that, but we don't remove cache
		return;
	if (cachedHeightmap && cachedHeightmap->worldspace == worldspace_name)  // already cached
		return;

	auto device = globals::d3d::device;

	logger::debug("Loading height map...");
	{
		auto& target_heightmap = heightmaps[worldspace_name];

		DirectX::ScratchImage image;
		try {
			std::filesystem::path path{ target_heightmap.dir };
			path /= target_heightmap.filename;

			DX::ThrowIfFailed(LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		ID3D11Resource* pResource = nullptr;
		try {
			DX::ThrowIfFailed(CreateTexture(device,
				image.GetImages(), image.GetImageCount(),
				image.GetMetadata(), &pResource));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		texHeightMap.release();
		texHeightMap = std::make_unique<Texture2D>(reinterpret_cast<ID3D11Texture2D*>(pResource));

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texHeightMap->desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};
		texHeightMap->CreateSRV(srvDesc);

		cachedHeightmap = &heightmaps[worldspace_name];
	}

	shadowUpdateIdx = 0;
	needPrecompute = true;
}

void TerrainShadows::Precompute()
{
	if (!cachedHeightmap)
		return;

	logger::info("Creating shadow texture...");
	{
		if (texShadowHeight) {
			auto context = globals::d3d::context;

			std::array<ID3D11ShaderResourceView*, 1> srvs = { nullptr };
			context->PSSetShaderResources(60, (uint)srvs.size(), srvs.data());
			context->CSSetShaderResources(60, (uint)srvs.size(), srvs.data());
		}

		texShadowHeight.release();

		D3D11_TEXTURE2D_DESC texDesc = {
			.Width = texHeightMap->desc.Width,
			.Height = texHeightMap->desc.Height,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R16G16_FLOAT,
			.SampleDesc = { .Count = 1 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS
		};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		texShadowHeight = std::make_unique<Texture2D>(texDesc);
		texShadowHeight->CreateSRV(srvDesc);
		texShadowHeight->CreateUAV(uavDesc);
	}

	needPrecompute = false;
}

void TerrainShadows::UpdateShadow()
{
	if (!IsHeightMapReady())
		return;

	// don't forget to change NTHREADS in shader!
	constexpr uint updateLength = 128u;
	constexpr uint logUpdateLength = std::bit_width(128u) - 1;  // integer log2, https://stackoverflow.com/questions/994593/how-to-do-an-integer-log2-in-c

	auto context = globals::d3d::context;

	if (texShadowHeight) {
		std::array<ID3D11ShaderResourceView*, 1> srvs = { nullptr };
		context->PSSetShaderResources(60, (uint)srvs.size(), srvs.data());
		context->CSSetShaderResources(60, (uint)srvs.size(), srvs.data());
	}

	auto accumulator = RE::BSGraphics::BSShaderAccumulator::GetCurrentAccumulator();
	auto sunLight = skyrim_cast<RE::NiDirectionalLight*>(accumulator->GetRuntimeData().activeShadowSceneNode->GetRuntimeData().sunLight->light.get());
	if (!sunLight)
		return;

	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Terrain Occlusion - Update Shadows");

	/* ---- UPDATE CB ---- */
	uint width = texHeightMap->desc.Width;
	uint height = texHeightMap->desc.Height;

	// only update direction at the start of each cycle
	static uint edgePxCoord;
	static int signDir;
	static uint maxUpdates;
	if (shadowUpdateIdx == 0) {
		auto direction = sunLight->GetWorldDirection();
		float3 dirLightDir = { direction.x, direction.y, direction.z };
		if (dirLightDir.z > 0)
			dirLightDir = -dirLightDir;

		// in UV
		float3 invScale = cachedHeightmap->pos1 - cachedHeightmap->pos0;
		invScale.z = cachedHeightmap->zRange.y - cachedHeightmap->zRange.x;
		float3 dirLightPxDir = dirLightDir / invScale;
		dirLightPxDir.x *= width;
		dirLightPxDir.y *= height;

		float stepMult;
		if (abs(dirLightPxDir.x) >= abs(dirLightPxDir.y)) {
			stepMult = 1.f / abs(dirLightPxDir.x);
			edgePxCoord = dirLightPxDir.x > 0 ? 0 : (width - 1);
			signDir = dirLightPxDir.x > 0 ? 1 : -1;
			maxUpdates = (width + updateLength - 1) >> logUpdateLength;
		} else {
			stepMult = 1.f / abs(dirLightPxDir.y);
			edgePxCoord = dirLightPxDir.y > 0 ? 0 : height - 1;
			signDir = dirLightPxDir.y > 0 ? 1 : -1;
			maxUpdates = (height + updateLength - 1) >> logUpdateLength;
		}
		dirLightPxDir *= stepMult;

		shadowUpdateCBData.LightPxDir = { dirLightPxDir.x, dirLightPxDir.y };

		// soft shadow angles
		float lenUV = float2{ dirLightDir.x, dirLightDir.y }.Length();
		float dirLightAngle = atan2(-dirLightDir.z, lenUV);
		float shadowSofteningRadiusAngle = 4.f * RE::NI_PI / 180.f;
		float upperAngle = std::max(0.f, dirLightAngle - shadowSofteningRadiusAngle);
		float lowerAngle = std::min(RE::NI_HALF_PI - 1e-2f, dirLightAngle + shadowSofteningRadiusAngle);

		shadowUpdateCBData.LightDeltaZ = -(lenUV / invScale.z * stepMult) * float2{ std::tan(upperAngle), std::tan(lowerAngle) };
	}

	shadowUpdateCBData.StartPxCoord = edgePxCoord + signDir * shadowUpdateIdx * updateLength;
	shadowUpdateCBData.PxSize = { 1.f / texHeightMap->desc.Width, 1.f / texHeightMap->desc.Height };

	shadowUpdateCBData.PosRange = { cachedHeightmap->pos0.z, cachedHeightmap->pos1.z };
	shadowUpdateCBData.ZRange = cachedHeightmap->zRange;

	shadowUpdateCB->Update(shadowUpdateCBData);

	shadowUpdateIdx = (shadowUpdateIdx + 1) % maxUpdates;

	/* ---- BACKUP ---- */
	struct ShaderState
	{
		ID3D11ShaderResourceView* srvs[1] = { nullptr };
		ID3D11ComputeShader* shader = nullptr;
		ID3D11UnorderedAccessView* uavs[1] = { nullptr };
		ID3D11Buffer* buffer = nullptr;
	} old, newer;

	/* ---- DISPATCH ---- */

	newer.srvs[0] = texHeightMap->srv.get();
	newer.uavs[0] = texShadowHeight->uav.get();
	newer.buffer = shadowUpdateCB->CB();

	context->CSSetShaderResources(0, ARRAYSIZE(newer.srvs), newer.srvs);
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(newer.uavs), newer.uavs, nullptr);
	context->CSSetConstantBuffers(0, 1, &newer.buffer);
	context->CSSetShader(shadowUpdateProgram.get(), nullptr, 0);
	context->Dispatch(abs(shadowUpdateCBData.LightPxDir.x) >= abs(shadowUpdateCBData.LightPxDir.y) ? height : width, 1, 1);

	/* ---- RESTORE ---- */
	context->CSSetShaderResources(0, ARRAYSIZE(old.srvs), old.srvs);
	context->CSSetShader(old.shader, nullptr, 0);
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(old.uavs), old.uavs, nullptr);
	context->CSSetConstantBuffers(0, 1, &old.buffer);
}

void TerrainShadows::ReflectionsPrepass()
{
	if (texShadowHeight) {
		auto context = globals::d3d::context;

		std::array<ID3D11ShaderResourceView*, 1> srvs = { texShadowHeight->srv.get() };
		context->PSSetShaderResources(60, (uint)srvs.size(), srvs.data());
		context->CSSetShaderResources(60, (uint)srvs.size(), srvs.data());
	}
}

void TerrainShadows::EarlyPrepass()
{
	LoadHeightmap();

	if (!settings.EnableTerrainShadow)
		return;

	if (needPrecompute)
		Precompute();

	UpdateShadow();

	if (texShadowHeight) {
		auto context = globals::d3d::context;

		std::array<ID3D11ShaderResourceView*, 1> srvs = { texShadowHeight->srv.get() };
		context->PSSetShaderResources(60, (uint)srvs.size(), srvs.data());
		context->CSSetShaderResources(60, (uint)srvs.size(), srvs.data());
	}
}
