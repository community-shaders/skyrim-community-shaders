#include "Flowmap.h"

#include <DDSTextureLoader.h>
#include <DirectXTex.h>

#include "WaterCache.h"
#include "Globals.h"

bool Flowmap::TryGetFlowmap(RE::NiPointer<RE::NiSourceTexture>& outFlowmapTex) const
{
	if (!flowmapTex || !flowmapTex->rendererTexture || !flowmapTex->rendererTexture->texture || !flowmapTex->rendererTexture->resourceView)
		return false;

	outFlowmapTex = this->flowmapTex;
	return true;
}

void Flowmap::Reset()
{
	flowmapTex = nullptr;
	width = 0;
	height = 0;
	invWidth = 0.0f;
	invHeight = 0.0f;
	offsetX = 0;
	offsetY = 0;
}

bool Flowmap::LoadOrGenerateFlowmap(WaterCache* waterCache, bool useMips)
{
	Reset();

	if (!LoadFlowmap()) {
		logger::info("[Unified Water] [Flowmap] Could not load flowmap - regenerating...");
		return RegenerateAndLoadFlowmap(waterCache, useMips);
	}

	return true;
}

bool Flowmap::RegenerateAndLoadFlowmap(WaterCache* waterCache, bool useMips)
{
	Reset();

	namespace fs = std::filesystem;
	const fs::path dir = Util::PathHelpers::GetDataPath() / "textures" / "water" / "flowmaps";

	std::error_code ec;
	fs::create_directories(dir, ec);

	if (!fs::exists(dir))
		return false;

	for (const auto& entry : fs::directory_iterator(dir, ec)) {
		if (ec)
			break;
		if (!entry.is_regular_file())
			continue;

		const auto& path = entry.path();
		if (path.extension() != ".dds")
			continue;

		std::error_code rec;
		fs::remove(path, rec);
		if (rec)
			logger::warn("[Unified Water] [Flowmap] Failed to remove '{}': {}", path.string(), rec.message());
	}

	if (!GenerateFlowmap(waterCache, useMips)) {
		logger::error("[Unified Water] [Flowmap] Failed to generate flowmap");
		return false;
	}

	if (!LoadFlowmap()) {
		logger::error("[Unified Water] [Flowmap] Failed to load flowmap after generation");
		Reset();
		return false;
	}

	logger::debug("[Unified Water] [Flowmap] Flowmap regenerated and loaded");
	return true;
}

bool Flowmap::LoadFlowmap()
{
	namespace fs = std::filesystem;

	const fs::path dir = Util::PathHelpers::GetDataPath() / "textures" / "water" / "flowmaps";

	fs::directory_entry file;

	if (fs::exists(dir) && fs::is_directory(dir)) {
		for (const auto& entry : fs::directory_iterator(dir)) {
			if (!entry.is_regular_file()) {
				continue;
			}

			const std::wstring name = entry.path().filename().wstring();
			if (name.rfind(L"Tamriel-Flowmap", 0) == 0) {
				file = entry;
				break;
			}
		}
	}

	if (file.path().empty()) {
		logger::debug("[Unified Water] [Flowmap] No flowmap found");
		return false;
	}

	std::vector<std::string> tokens;
	std::istringstream iss(file.path().filename().stem().string());
	std::string token;

	while (std::getline(iss, token, '.')) {
		tokens.push_back(token);
	}

	if (tokens.size() != 5) {
		logger::error("[Unified Water] [Flowmap] Invalid file name");
		return false;
	}

	auto path = std::format(R"(textures\water\flowmaps\{})", file.path().filename().string().c_str());
	RE::NiPointer<RE::NiTexture> tex;
	RE::BSShaderManager::GetTexture(path.c_str(), true, tex, false);

	if (!tex || tex->GetRTTI() != globals::rtti::NiSourceTextureRTTI.get()) {
		logger::error("[Unified Water] [Flowmap] Failed to load flowmap from {}", path);
		return false;
	}

	const auto sourceTex = static_cast<RE::NiSourceTexture*>(tex.get());

	// if (!sourceTex || !sourceTex->rendererTexture || !sourceTex->rendererTexture->texture) {
	// 	logger::error("[Unified Water] [Flowmap] Flowmap invalid", path);
	// 	return false;
	// }

	flowmapTex = RE::NiPointer(sourceTex);

	width = std::stoi(tokens[1]);
	height = std::stoi(tokens[2]);
	offsetX = std::stoi(tokens[3]);
	offsetY = std::stoi(tokens[4]);
	invWidth = 1.0f / static_cast<float>(width);
	invHeight = 1.0f / static_cast<float>(height);

	logger::debug("[Unified Water] [Flowmap] Flowmap loaded");
	return true;
}

bool Flowmap::GenerateFlowmap(WaterCache* waterCache, bool useMips)
{
	const auto t0 = std::chrono::steady_clock::now();

	auto dvc = globals::d3d::device;
	auto ctx = globals::d3d::context;

	winrt::com_ptr<ID3D11DeviceContext> deferredCtx;
	if (FAILED(dvc->CreateDeferredContext(0, deferredCtx.put()))) {
		logger::error("[Unified Water] [Flowmap] Failed to create deferred context");
		return false;
	}

	static winrt::com_ptr<REX::W32::ID3D11Multithread> multithread;
	if (SUCCEEDED(ctx->QueryInterface(multithread.put()))) {
		multithread->SetMultithreadProtected(TRUE);
	} else {
		logger::error("[Unified Water] [Flowmap] ID3D11Multithread not available");
		return false;
	}

	multithread->Enter();

	const auto tamriel = RE::TESForm::LookupByEditorID<RE::TESWorldSpace>("Tamriel");
	if (!tamriel) {
		logger::error("[Unified Water] [Flowmap] Failed to load Tamriel WorldSpace");
		return false;
	}

	int32_t worldMinX, worldMinY, worldMaxX, worldMaxY;
	Util::WorldToCell(tamriel->minimumCoords, worldMinX, worldMinY);
	Util::WorldToCell(tamriel->maximumCoords, worldMaxX, worldMaxY);
	worldMaxX -= 1;
	worldMaxY -= 1;

	struct FlowCell
	{
		int32_t x;
		int32_t y;
		winrt::com_ptr<ID3D11Texture2D> tex;
	};

	int32_t mapMinX = INT_MAX;
	int32_t mapMinY = INT_MAX;
	int32_t mapMaxX = INT_MIN;
	int32_t mapMaxY = INT_MIN;

	auto cells = std::vector<FlowCell>();
	cells.reserve(1024);

	if (!waterCache) {
		logger::error("[Unified Water] [Flowmap] Water cache not available");
		return false;
	}

	logger::info("[Unified Water] [Flowmap] Generating shoreline-based flowmap...");

	{
		for (auto y = worldMinY; y < worldMaxY; ++y) {
			for (auto x = worldMinX; x < worldMaxX; ++x) {
				// Check if this cell has water via cache instructions
				std::vector<WaterCache::Instruction>* instructions = waterCache->GetInstructions(tamriel, 1, x, y);
				if (!instructions || instructions->empty())
					continue;

				// Get shoreline data
				float shoreNormalX = (*instructions)[0].shoreNormalX;
				float shoreNormalY = (*instructions)[0].shoreNormalY;
				float distanceToShore = (*instructions)[0].distanceToShore;

				float normalLen = std::sqrt(shoreNormalX * shoreNormalX + shoreNormalY * shoreNormalY);
				bool hasShorelineData = (normalLen > 0.001f && distanceToShore < 0.95f);

				// Generate 64x64 flowmap texture for this cell
				DirectX::ScratchImage image;
				auto hr = image.Initialize2D(DXGI_FORMAT_B8G8R8A8_UNORM, 64, 64, 1, 6);
				if (FAILED(hr)) {
					logger::warn("[Unified Water] [Flowmap] Failed to initialize image for {},{}", x, y);
					continue;
				}

				// Fill mip level 0
				auto* pixels = reinterpret_cast<uint8_t*>(image.GetImage(0, 0, 0)->pixels);
				const size_t rowPitch = image.GetImage(0, 0, 0)->rowPitch;

				for (int pixelY = 0; pixelY < 64; ++pixelY) {
					for (int pixelX = 0; pixelX < 64; ++pixelX) {
						uint8_t* pixel = pixels + pixelY * rowPitch + pixelX * 4;

						if (hasShorelineData) {
							// Flow perpendicular to shore normal (toward shore)
							float flowX = -shoreNormalY;
							float flowY = shoreNormalX;

							// Add spatial variation
							float cellFracX = pixelX / 64.0f;
							float cellFracY = pixelY / 64.0f;
							float variation = std::sin(cellFracX * 3.14159f * 4.0f) * std::cos(cellFracY * 3.14159f * 4.0f) * 0.15f;

							float angle = std::atan2(flowY, flowX) + variation;
							flowX = std::cos(angle);
							flowY = std::sin(angle);

							// BGRA format
							pixel[2] = static_cast<uint8_t>((flowX * 0.5f + 0.5f) * 255.0f);  // R: flow X
							pixel[1] = static_cast<uint8_t>((flowY * 0.5f + 0.5f) * 255.0f);  // G: flow Y
							pixel[0] = static_cast<uint8_t>((1.0f - distanceToShore) * 255.0f);  // B: strength
							pixel[3] = 255;  // A: mask
						} else {
							// Default northeast flow for open water
							pixel[2] = static_cast<uint8_t>(0.8535f * 255.0f);  // R: 0.7071 * 0.5 + 0.5
							pixel[1] = static_cast<uint8_t>(0.8535f * 255.0f);  // G: 0.7071 * 0.5 + 0.5
							pixel[0] = 128;  // B: medium strength
							pixel[3] = 255;  // A
						}
					}
				}

				// Generate mip chain
				if (FAILED(DirectX::GenerateMipMaps(*image.GetImage(0, 0, 0), DirectX::TEX_FILTER_DEFAULT, 6, image, false))) {
					logger::warn("[Unified Water] [Flowmap] Failed to generate mipmaps for {},{}", x, y);
					continue;
				}

				// Create texture from image
				winrt::com_ptr<ID3D11Resource> res;
				hr = DirectX::CreateTexture(dvc, image.GetImages(), image.GetImageCount(), image.GetMetadata(), res.put());
				if (FAILED(hr) || !res) {
					logger::warn("[Unified Water] [Flowmap] Failed to create texture for {},{}", x, y);
					continue;
				}

				winrt::com_ptr<ID3D11Texture2D> tex;
				hr = res->QueryInterface(IID_PPV_ARGS(tex.put()));
				if (FAILED(hr)) {
					logger::warn("[Unified Water] [Flowmap] Texture for {},{} is not a Texture2D", x, y);
					continue;
				}

				mapMinX = std::min(mapMinX, x);
				mapMinY = std::min(mapMinY, y);
				mapMaxX = std::max(mapMaxX, x);
				mapMaxY = std::max(mapMaxY, y);

				cells.emplace_back(FlowCell{ x, y, tex });
			}
		}
	}

	const auto width = mapMaxX - mapMinX + 1;
	const auto height = mapMaxY - mapMinY + 1;
	const auto offsetX = -mapMinX;
	const auto offsetY = -mapMinY;

	logger::debug("[Unified Water] [Flowmap] Loaded {} flow textures, creating a {}x{} flow map...", cells.size(), width, height);

	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = width * 64;
	desc.Height = height * 64;
	desc.MipLevels = 6;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc = { 1, 0 };
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.MiscFlags = 0;

	winrt::com_ptr<ID3D11Texture2D> flowmap;
	if (FAILED(dvc->CreateTexture2D(&desc, nullptr, flowmap.put()))) {
		logger::error("[Unified Water] [Flowmap] Failed to create texture");
		return false;
	}

	for (const auto& [x, y, flowTex] : cells) {
		D3D11_TEXTURE2D_DESC srcDesc{};
		flowTex->GetDesc(&srcDesc);

		const UINT sx = static_cast<UINT>(x + offsetX);
		const UINT sy = static_cast<UINT>(y + offsetY);
		const UINT dstX0 = sx * 64;

		const UINT maxMipLevel = useMips ? 6u : 1u;
		for (UINT mipLevel = 0; mipLevel < maxMipLevel; ++mipLevel) {
			const UINT srcSub = D3D11CalcSubresource(mipLevel, 0, srcDesc.MipLevels);
			const UINT dstSub = D3D11CalcSubresource(mipLevel, 0, desc.MipLevels);
			const UINT tileSize = std::max(1u, 64u >> mipLevel);
			const UINT flowmapHeight = std::max(1u, desc.Height >> mipLevel);
			const UINT dstX = dstX0 >> mipLevel;
			const UINT dstY = flowmapHeight - (sy + 1) * tileSize;

			deferredCtx->CopySubresourceRegion(flowmap.get(), dstSub, dstX, dstY, 0, flowTex.get(), srcSub, nullptr);
		}
	}

	winrt::com_ptr<ID3D11CommandList> commandList;
	if (deferredCtx && FAILED(deferredCtx->FinishCommandList(FALSE, commandList.put()))) {
		logger::error("[Unified Water] [Flowmap] FinishCommandList failed");
		return false;
	}

	{
		ctx->ExecuteCommandList(commandList.get(), TRUE);

		const auto filename = std::format(L"Tamriel-Flowmap.{}.{}.{}.{}.dds", width, height, offsetX, offsetY);
		const auto path = Util::PathHelpers::GetDataPath() / "textures" / "water" / "flowmaps" / filename;
		const auto hr = Util::SaveTextureToFile(dvc, ctx, path, flowmap.get());
		
		if (FAILED(hr)) {
			logger::error("[Unified Water] [Flowmap] Failed to save flowmap to {}: hr={:08X}", path.string().c_str(), static_cast<uint32_t>(hr));
			return false;
		}
	}
	
	multithread->Leave();
	multithread->SetMultithreadProtected(FALSE);

	const auto t1 = std::chrono::steady_clock::now();
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
	logger::info("[Unified Water] [Flowmap] Generated in {} ms", ms);

	return true;
}
