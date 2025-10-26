// ShorelineMap: High-resolution shoreline distance field texture generation
// 
// This system generates per-pixel shoreline distance and normal data stored in textures,
// enabling smooth and artifact-free shoreline wave interactions without tile-boundary
// discontinuities that occur with sparse constant-buffer sampling.
//
// Based on Jump Flooding Algorithm (JFA) for computing distance fields:
// Rong, G., & Tan, T. S. (2006). "Jump flooding in GPU with applications to Voronoi 
// diagram and distance transform." In Proceedings of the 2006 symposium on Interactive 
// 3D graphics and games (pp. 109-116).
// https://www.comp.nus.edu.sg/%7Etants/jfa/i3d06.pdf
//
// The implementation uses BuildShorelineField from WaterCache which computes per-cell
// distance fields via Dijkstra's algorithm, then encodes the data into RGBA8 textures:
// - R channel: Shore normal X component (normalized, packed to [0,1])
// - G channel: Shore normal Y component (normalized, packed to [0,1])  
// - B channel: Distance to shore (normalized to max range, packed to [0,1])
// - A channel: Water presence mask
//
// This approach provides:
// 1. Per-pixel accuracy instead of per-tile interpolation
// 2. Smooth transitions across arbitrary boundaries via GPU texture filtering
// 3. Minimal runtime overhead (single texture sample vs complex interpolation)
// 4. Consistent with flowmap architecture for unified water texture management

#include "ShorelineMap.h"

#include <DDSTextureLoader.h>
#include <DirectXTex.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <sstream>
#include <vector>

#include "Globals.h"
#include "Util.h"
#include "WaterCache.h"

bool ShorelineMap::TryGetShorelineMap(RE::NiPointer<RE::NiSourceTexture>& outShorelineMapTex) const
{
	if (!shorelineMapTex) {
		logger::warn("[Unified Water] [ShorelineMap] TryGetShorelineMap failed: shorelineMapTex is nullptr");
		return false;
	}
	if (!shorelineMapTex->rendererTexture) {
		logger::warn("[Unified Water] [ShorelineMap] TryGetShorelineMap failed: rendererTexture is nullptr");
		return false;
	}
	if (!shorelineMapTex->rendererTexture->texture) {
		logger::warn("[Unified Water] [ShorelineMap] TryGetShorelineMap failed: texture is nullptr");
		return false;
	}
	if (!shorelineMapTex->rendererTexture->resourceView) {
		logger::warn("[Unified Water] [ShorelineMap] TryGetShorelineMap failed: resourceView is nullptr");
		return false;
	}

	outShorelineMapTex = this->shorelineMapTex;
	logger::info("[Unified Water] [ShorelineMap] TryGetShorelineMap succeeded!");
	return true;
}

void ShorelineMap::Reset()
{
	shorelineMapTex = nullptr;
	width = 0;
	height = 0;
	invWidth = 0.0f;
	invHeight = 0.0f;
	offsetX = 0;
	offsetY = 0;
}

bool ShorelineMap::LoadOrGenerateShorelineMap(WaterCache* waterCache, bool useMips)
{
	Reset();

	if (!LoadShorelineMap()) {
		logger::info("[Unified Water] [ShorelineMap] Could not load shoreline map - regenerating...");
		return RegenerateAndLoadShorelineMap(waterCache, useMips);
	}

	return true;
}

bool ShorelineMap::RegenerateAndLoadShorelineMap(WaterCache* waterCache, bool useMips)
{
	Reset();

	namespace fs = std::filesystem;
	const fs::path dir = Util::PathHelpers::GetDataPath() / "textures" / "water" / "shorelinemaps";

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
			logger::warn("[Unified Water] [ShorelineMap] Failed to remove '{}': {}", path.string(), rec.message());
	}

	if (!GenerateShorelineMap(waterCache, useMips)) {
		logger::error("[Unified Water] [ShorelineMap] Failed to generate shoreline map");
		return false;
	}

	if (!LoadShorelineMap()) {
		logger::error("[Unified Water] [ShorelineMap] Failed to load shoreline map after generation");
		Reset();
		return false;
	}

	logger::debug("[Unified Water] [ShorelineMap] Shoreline map regenerated and loaded");
	return true;
}

bool ShorelineMap::LoadShorelineMap()
{
	namespace fs = std::filesystem;

	const fs::path dir = Util::PathHelpers::GetDataPath() / "textures" / "water" / "shorelinemaps";

	fs::directory_entry file;

	if (fs::exists(dir) && fs::is_directory(dir)) {
		for (const auto& entry : fs::directory_iterator(dir)) {
			if (!entry.is_regular_file()) {
				continue;
			}

			const std::wstring name = entry.path().filename().wstring();
			if (name.rfind(L"Tamriel-ShorelineMap", 0) == 0) {
				file = entry;
				break;
			}
		}
	}

	if (file.path().empty()) {
		logger::debug("[Unified Water] [ShorelineMap] No shoreline map found");
		return false;
	}

	std::vector<std::string> tokens;
	std::istringstream iss(file.path().filename().stem().string());
	std::string token;

	while (std::getline(iss, token, '.')) {
		tokens.push_back(token);
	}

	if (tokens.size() != 5) {
		logger::error("[Unified Water] [ShorelineMap] Invalid file name");
		return false;
	}

	auto path = std::format(R"(textures\water\shorelinemaps\{})", file.path().filename().string().c_str());
	RE::NiPointer<RE::NiTexture> tex;
	RE::BSShaderManager::GetTexture(path.c_str(), true, tex, false);

	if (!tex || tex->GetRTTI() != globals::rtti::NiSourceTextureRTTI.get()) {
		logger::error("[Unified Water] [ShorelineMap] Failed to load shoreline map from {}", path);
		return false;
	}

	logger::info("[Unified Water] [ShorelineMap] Successfully loaded texture from: {}", path);

	const auto sourceTex = static_cast<RE::NiSourceTexture*>(tex.get());

	shorelineMapTex = RE::NiPointer(sourceTex);

	width = std::stoi(tokens[1]);
	height = std::stoi(tokens[2]);
	offsetX = std::stoi(tokens[3]);
	offsetY = std::stoi(tokens[4]);
	invWidth = 1.0f / static_cast<float>(width);
	invHeight = 1.0f / static_cast<float>(height);

	logger::info("[Unified Water] [ShorelineMap] Shoreline map loaded - dimensions {}x{}, offset ({},{})", width, height, offsetX, offsetY);
	return true;
}

bool ShorelineMap::GenerateShorelineMap(WaterCache* waterCache, bool useMips)
{
	// TEMPORARY: Force mipmaps OFF to test if they're causing alpha channel corruption
	useMips = false;
	
	const auto t0 = std::chrono::steady_clock::now();

	auto dvc = globals::d3d::device;
	auto ctx = globals::d3d::context;

	winrt::com_ptr<ID3D11DeviceContext> deferredCtx;
	if (FAILED(dvc->CreateDeferredContext(0, deferredCtx.put()))) {
		logger::error("[Unified Water] [ShorelineMap] Failed to create deferred context");
		return false;
	}

	static winrt::com_ptr<REX::W32::ID3D11Multithread> multithread;
	if (FAILED(ctx->QueryInterface(multithread.put()))) {
		logger::error("[Unified Water] [ShorelineMap] ID3D11Multithread not available");
		return false;
	}

	struct MultithreadScopeGuard
	{
		REX::W32::ID3D11Multithread* multithread{ nullptr };

		explicit MultithreadScopeGuard(REX::W32::ID3D11Multithread* inMultithread) : multithread(inMultithread)
		{
			if (multithread) {
				multithread->SetMultithreadProtected(TRUE);
				multithread->Enter();
			}
		}

		~MultithreadScopeGuard()
		{
			if (multithread) {
				multithread->Leave();
				multithread->SetMultithreadProtected(FALSE);
			}
		}
	};

	MultithreadScopeGuard multithreadGuard(multithread.get());

	const auto tamriel = RE::TESForm::LookupByEditorID<RE::TESWorldSpace>("Tamriel");
	if (!tamriel) {
		logger::error("[Unified Water] [ShorelineMap] Failed to load Tamriel WorldSpace");
		return false;
	}

	int32_t worldMinX, worldMinY, worldMaxX, worldMaxY;
	Util::WorldToCell(tamriel->minimumCoords, worldMinX, worldMinY);
	Util::WorldToCell(tamriel->maximumCoords, worldMaxX, worldMaxY);
	worldMaxX -= 1;
	worldMaxY -= 1;

	struct ShorelineCell
	{
		int32_t x;
		int32_t y;
		winrt::com_ptr<ID3D11Texture2D> tex;
	};

	int32_t mapMinX = INT_MAX;
	int32_t mapMinY = INT_MAX;
	int32_t mapMaxX = INT_MIN;
	int32_t mapMaxY = INT_MIN;

	auto cells = std::vector<ShorelineCell>();
	cells.reserve(1024);

	if (!waterCache) {
		logger::error("[Unified Water] [ShorelineMap] Water cache not available");
		return false;
	}

	logger::info("[Unified Water] [ShorelineMap] Generating high-resolution shoreline distance field...");

	// Get the disk cache which already contains shoreline field data
	auto diskCache = waterCache->GetDiskCache(tamriel);
	if (!diskCache) {
		logger::error("[Unified Water] [ShorelineMap] Failed to get disk cache for Tamriel");
		return false;
	}

	// Verify we have shoreline data
	if (diskCache->shorelineDistance.empty() || diskCache->shorelineNormalX.empty() || 
		diskCache->shorelineNormalY.empty() || diskCache->shorelineMask.empty()) {
		logger::error("[Unified Water] [ShorelineMap] Disk cache missing shoreline field data");
		return false;
	}

	const int32_t cacheWidth = diskCache->header.width;
	const int32_t cacheHeight = diskCache->header.height;
	const int32_t cacheOffsetX = -diskCache->header.bounds.minX;
	const int32_t cacheOffsetY = -diskCache->header.bounds.minY;

	logger::info("[Unified Water] [ShorelineMap] Cache dimensions: {}x{}, offset: {},{}", 
		cacheWidth, cacheHeight, cacheOffsetX, cacheOffsetY);

	// Use shoreline field data from disk cache
	const auto& shorelineDistance = diskCache->shorelineDistance;
	const auto& shorelineNormalX = diskCache->shorelineNormalX;
	const auto& shorelineNormalY = diskCache->shorelineNormalY;
	const auto& shorelineMask = diskCache->shorelineMask;

	if (shorelineDistance.empty() || shorelineNormalX.empty() || shorelineNormalY.empty()) {
		logger::error("[Unified Water] [ShorelineMap] Failed to build shoreline field data");
		return false;
	}

	// Debug: Log mask statistics at texture generation time
	{
		size_t nonZero = 0;
		double sum = 0.0;
		for (size_t i = 0; i < shorelineMask.size(); ++i) {
			sum += static_cast<double>(shorelineMask[i]);
			if (shorelineMask[i] > 0.001f)
				++nonZero;
		}
		logger::info("[Unified Water] [ShorelineMap] Tamriel mask at texture gen - cells={}, nonZero={}, sum={}", shorelineMask.size(), nonZero, sum);
		// Sample first 8 values
		if (shorelineMask.size() >= 8) {
			logger::info("[Unified Water] [ShorelineMap] First 8 mask values: {:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}",
				shorelineMask[0], shorelineMask[1], shorelineMask[2], shorelineMask[3],
				shorelineMask[4], shorelineMask[5], shorelineMask[6], shorelineMask[7]);
		}
	}

	// Generate 64x64 texture per cell from the high-resolution cache data
	int32_t texturesGenerated = 0;
	int32_t cellsSkipped = 0;
	for (int32_t cellY = 0; cellY < cacheHeight; ++cellY) {
		for (int32_t cellX = 0; cellX < cacheWidth; ++cellX) {
			const int32_t cacheIdx = cellY * cacheWidth + cellX;
			
			// Check if this cell has valid shoreline data
			const float distance = shorelineDistance[cacheIdx];
			const float normalX = shorelineNormalX[cacheIdx];
			const float normalY = shorelineNormalY[cacheIdx];
			const float mask = shorelineMask[cacheIdx];

			// Skip cells without water
			if (mask < 0.5f) {
				cellsSkipped++;
				continue;
			}

			const int32_t worldCellX = cellX - cacheOffsetX;
			const int32_t worldCellY = cellY - cacheOffsetY;

			// Generate 64x64 shoreline texture for this cell
			DirectX::ScratchImage image;
			auto hr = image.Initialize2D(DXGI_FORMAT_B8G8R8A8_UNORM, 64, 64, 1, useMips ? 6 : 1);
			if (FAILED(hr)) {
				logger::warn("[Unified Water] [ShorelineMap] Failed to initialize image for {},{}", worldCellX, worldCellY);
				continue;
			}

			// Fill mip level 0
			auto* pixels = reinterpret_cast<uint8_t*>(image.GetImage(0, 0, 0)->pixels);
			const size_t rowPitch = image.GetImage(0, 0, 0)->rowPitch;

			// Debug: Log first pixel values for first few cells
			static int debugCellCount = 0;
			bool logThisCell = (debugCellCount < 3);
			if (logThisCell) {
				logger::info("[Unified Water] [ShorelineMap] Cell ({},{}) - mask={:.3f}, distance={:.3f}, normal=({:.3f},{:.3f})",
					worldCellX, worldCellY, mask, distance, normalX, normalY);
			}

			for (int pixelY = 0; pixelY < 64; ++pixelY) {
				for (int pixelX = 0; pixelX < 64; ++pixelX) {
					uint8_t* pixel = pixels + pixelY * rowPitch + pixelX * 4;

					// For now, use the cell's uniform data
					// In the future, could interpolate between neighboring cells for smoother transitions
					
					// Normalize the shore normal
					float len = std::sqrt(normalX * normalX + normalY * normalY);
					float normX = len > 0.001f ? normalX / len : 0.0f;
					float normY = len > 0.001f ? normalY / len : 0.0f;

					// Pack into BGRA8 (to match DXGI_FORMAT_B8G8R8A8_UNORM):
					// B (pixel[0]): distance to shore
					// G (pixel[1]): shore normal Y ([-1, 1] -> [0, 255])
					// R (pixel[2]): shore normal X ([-1, 1] -> [0, 255])
					// A (pixel[3]): mask (water presence)
					
					pixel[2] = static_cast<uint8_t>((normX * 0.5f + 0.5f) * 255.0f);  // R
					pixel[1] = static_cast<uint8_t>((normY * 0.5f + 0.5f) * 255.0f);  // G
					
					// Normalize distance: assume max useful distance is 50 cells (larger range for better gradients)
					// Use sqrt to compress distant values while preserving detail near shore
					const float maxDistance = 50.0f;
					float normalizedDistance = std::min(distance / maxDistance, 1.0f);
					normalizedDistance = std::sqrt(normalizedDistance);  // Square root gives more detail near shore
					pixel[0] = static_cast<uint8_t>(normalizedDistance * 255.0f);  // B
					
					pixel[3] = static_cast<uint8_t>(mask * 255.0f);  // A
					
					// Debug: Log first pixel of first few cells
					if (logThisCell && pixelX == 0 && pixelY == 0) {
						logger::info("[Unified Water] [ShorelineMap]   First pixel RGBA: ({},{},{},{})",
							pixel[0], pixel[1], pixel[2], pixel[3]);
					}
				}
			}
			
			if (logThisCell) debugCellCount++;

			// Generate mip chain if requested
			if (useMips) {
				if (FAILED(DirectX::GenerateMipMaps(*image.GetImage(0, 0, 0), DirectX::TEX_FILTER_DEFAULT, 6, image, false))) {
					logger::warn("[Unified Water] [ShorelineMap] Failed to generate mipmaps for {},{}", worldCellX, worldCellY);
				}
			}

			// Create texture from image
			winrt::com_ptr<ID3D11Resource> res;
			hr = DirectX::CreateTexture(dvc, image.GetImages(), image.GetImageCount(), image.GetMetadata(), res.put());
			if (FAILED(hr) || !res) {
				logger::warn("[Unified Water] [ShorelineMap] Failed to create texture for {},{}", worldCellX, worldCellY);
				continue;
			}

			winrt::com_ptr<ID3D11Texture2D> tex;
			hr = res->QueryInterface(IID_PPV_ARGS(tex.put()));
			if (FAILED(hr)) {
				logger::warn("[Unified Water] [ShorelineMap] Texture for {},{} is not a Texture2D", worldCellX, worldCellY);
				continue;
			}

			mapMinX = std::min(mapMinX, worldCellX);
			mapMinY = std::min(mapMinY, worldCellY);
			mapMaxX = std::max(mapMaxX, worldCellX);
			mapMaxY = std::max(mapMaxY, worldCellY);

			cells.emplace_back(ShorelineCell{ worldCellX, worldCellY, tex });
			texturesGenerated++;
		}
	}

	logger::info("[Unified Water] [ShorelineMap] Texture generation complete: {} textures generated, {} cells skipped (mask<0.5)", texturesGenerated, cellsSkipped);

	if (cells.empty()) {
		logger::warn("[Unified Water] [ShorelineMap] No shoreline tiles found - skipping texture generation");
		return false;
	}

	const auto width = mapMaxX - mapMinX + 1;
	const auto height = mapMaxY - mapMinY + 1;
	const auto offsetX = -mapMinX;
	const auto offsetY = -mapMinY;

	logger::debug("[Unified Water] [ShorelineMap] Loaded {} shoreline textures, creating a {}x{} shoreline map...", cells.size(), width, height);

	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = width * 64;
	desc.Height = height * 64;
	desc.MipLevels = useMips ? 6 : 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // Match flowmap format
	desc.SampleDesc = { 1, 0 };
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.MiscFlags = 0;

	winrt::com_ptr<ID3D11Texture2D> shorelineMap;
	if (FAILED(dvc->CreateTexture2D(&desc, nullptr, shorelineMap.put()))) {
		logger::error("[Unified Water] [ShorelineMap] Failed to create texture");
		return false;
	}

	for (const auto& [x, y, shorelineTex] : cells) {
		D3D11_TEXTURE2D_DESC srcDesc{};
		shorelineTex->GetDesc(&srcDesc);

		const UINT sx = static_cast<UINT>(x + offsetX);
		const UINT sy = static_cast<UINT>(y + offsetY);
		const UINT dstX0 = sx * 64;

		const UINT maxMipLevel = useMips ? 6u : 1u;
		for (UINT mipLevel = 0; mipLevel < maxMipLevel; ++mipLevel) {
			const UINT srcSub = D3D11CalcSubresource(mipLevel, 0, srcDesc.MipLevels);
			const UINT dstSub = D3D11CalcSubresource(mipLevel, 0, desc.MipLevels);
			const UINT tileSize = std::max(1u, 64u >> mipLevel);
			const UINT shorelineMapHeight = std::max(1u, desc.Height >> mipLevel);
			const UINT dstX = dstX0 >> mipLevel;
			const UINT dstY = shorelineMapHeight - (sy + 1) * tileSize;

			deferredCtx->CopySubresourceRegion(shorelineMap.get(), dstSub, dstX, dstY, 0, shorelineTex.get(), srcSub, nullptr);
		}
	}

	winrt::com_ptr<ID3D11CommandList> commandList;
	if (deferredCtx && FAILED(deferredCtx->FinishCommandList(FALSE, commandList.put()))) {
		logger::error("[Unified Water] [ShorelineMap] FinishCommandList failed");
		return false;
	}

	{
		ctx->ExecuteCommandList(commandList.get(), TRUE);

		const auto filename = std::format(L"Tamriel-ShorelineMap.{}.{}.{}.{}.dds", width, height, offsetX, offsetY);
		const auto path = Util::PathHelpers::GetDataPath() / "textures" / "water" / "shorelinemaps" / filename;
		const auto hr = Util::SaveTextureToFile(dvc, ctx, path, shorelineMap.get());
		
		if (FAILED(hr)) {
			logger::error("[Unified Water] [ShorelineMap] Failed to save shoreline map to {}: hr={:08X}", path.string().c_str(), static_cast<uint32_t>(hr));
			return false;
		}
	}
	
	const auto t1 = std::chrono::steady_clock::now();
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
	logger::info("[Unified Water] [ShorelineMap] Generated in {} ms", ms);

	return true;
}
