#include "Features/SnowDeformation.h"

#include <DDSTextureLoader.h>

#include "Deferred.h"
#include "Features/CloudShadows.h"
#include "Features/ExponentialHeightFog.h"
#include "Features/IBL.h"
#include "Features/LightLimitFix.h"
#include "Features/TerrainShadows.h"
#include "Features/ScreenSpaceShadows.h"
#include "Features/Skylighting.h"
#include "Features/TerrainBlending.h"
#include "Globals.h"
#include "State.h"
#include "TruePBR.h"
#include "Utils/D3D.h"
#include "Utils/Game.h"

void SnowDeformation::CopySRVResource(ID3D11ShaderResourceView* a_srcSRV, const char* a_name,
	winrt::com_ptr<ID3D11Texture2D>& a_tex, winrt::com_ptr<ID3D11ShaderResourceView>& a_srv, const D3D11_BOX* a_box, const D3D11_BOX* a_extent)
{
	winrt::com_ptr<ID3D11Resource> srcRes;
	a_srcSRV->GetResource(srcRes.put());
	auto srcTex = srcRes.try_as<ID3D11Texture2D>();
	if (!srcTex)
		return;

	D3D11_TEXTURE2D_DESC srcDesc;
	srcTex->GetDesc(&srcDesc);
	const D3D11_BOX* hold = a_extent ? a_extent : a_box;
	const UINT width = hold ? std::clamp(hold->right, 1u, srcDesc.Width) : srcDesc.Width;
	const UINT height = hold ? std::clamp(hold->bottom, 1u, srcDesc.Height) : srcDesc.Height;

	bool recreate = !a_tex || !a_srv;
	if (a_tex) {
		D3D11_TEXTURE2D_DESC haveDesc;
		a_tex->GetDesc(&haveDesc);
		recreate |= haveDesc.Width != width || haveDesc.Height != height ||
		            haveDesc.ArraySize != srcDesc.ArraySize || haveDesc.Format != srcDesc.Format;
	}
	if (recreate) {
		a_srv = nullptr;
		a_tex = nullptr;

		D3D11_TEXTURE2D_DESC copyDesc = srcDesc;
		copyDesc.Width = width;
		copyDesc.Height = height;
		copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		copyDesc.MiscFlags = 0;
		copyDesc.Usage = D3D11_USAGE_DEFAULT;
		copyDesc.CPUAccessFlags = 0;
		if (FAILED(globals::d3d::device->CreateTexture2D(&copyDesc, nullptr, a_tex.put())))
			return;
		Util::SetResourceName(a_tex.get(), a_name);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		a_srcSRV->GetDesc(&srvDesc);
		if (FAILED(globals::d3d::device->CreateShaderResourceView(a_tex.get(), &srvDesc, a_srv.put()))) {
			a_tex = nullptr;
			return;
		}
	}

	D3D11_BOX box = a_box ? *a_box : D3D11_BOX{ 0, 0, 0, width, height, 1 };
	box.right = std::min(box.right, width);
	box.bottom = std::min(box.bottom, height);
	if (box.right > box.left && box.bottom > box.top)
		globals::d3d::context->CopySubresourceRegion(a_tex.get(), 0, box.left, box.top, 0, srcTex.get(), 0, &box);
}

bool SnowDeformation::LoadGameDDS(const std::string& a_dataRelativePath, winrt::com_ptr<ID3D11ShaderResourceView>& a_srv)
{
	a_srv = nullptr;
	if (a_dataRelativePath.empty())
		return false;

	std::string path = a_dataRelativePath;
	{
		const size_t dot = path.find_last_of('.');
		const size_t sep = path.find_last_of("\\/");
		if (dot == std::string::npos || (sep != std::string::npos && dot < sep))
			path += ".dds";
	}
	auto nameLoaded = [&a_srv, &path] {
		winrt::com_ptr<ID3D11Resource> texture;
		a_srv->GetResource(texture.put());
		Util::SetResourceName(texture.get(), "SnowDeformation::%s", path.c_str());
	};

	RE::BSResourceNiBinaryStream stream(path);
	if (stream.good() && stream.stream) {
		const uint32_t total = stream.stream->totalSize;
		if (total > 0) {
			std::vector<uint8_t> bytes(total);
			if (stream.read(reinterpret_cast<char*>(bytes.data()), total) &&
				SUCCEEDED(DirectX::CreateDDSTextureFromMemory(globals::d3d::device, bytes.data(), bytes.size(),
					nullptr, a_srv.put()))) {
				logger::debug("SnowDeformation: loaded {} through the game ({} bytes)", a_dataRelativePath, total);
				nameLoaded();
				return true;
			}
		}
		a_srv = nullptr;
	}

	const std::string diskPath = "Data\\" + path;
	const std::wstring wide(diskPath.begin(), diskPath.end());
	if (SUCCEEDED(DirectX::CreateDDSTextureFromFile(globals::d3d::device, wide.c_str(), nullptr, a_srv.put()))) {
		logger::debug("SnowDeformation: loaded {} off disk", path);
		nameLoaded();
		return true;
	}
	a_srv = nullptr;
	logger::debug("SnowDeformation: could not load {} from archives or disk", a_dataRelativePath);
	return false;
}

void SnowDeformation::EnsureTrenchDetailTextures()
{
	if (trenchDetailAttempted)
		return;
	trenchDetailAttempted = true;
	if (!LoadGameDDS("Shaders\\SnowDeformation\\TrenchDetailN.dds", trenchDetailNormalSRV) ||
		!LoadGameDDS("Shaders\\SnowDeformation\\TrenchDetailH.dds", trenchDetailHeightSRV)) {
		trenchDetailNormalSRV = nullptr;
		trenchDetailHeightSRV = nullptr;
		logger::info("[SNOW DEFORMATION] trench detail textures not found; the trench detail stays off");
		return;
	}
	logger::info("[SNOW DEFORMATION] trench detail textures loaded");
}

void SnowDeformation::EnsureShellSnowTextures()
{
	if (shellSnowTextureAttempted)
		return;
	shellSnowTextureAttempted = true;
	shellSnowDiffuseSRV = nullptr;
	shellSnowNormalSRV = nullptr;
	shellSnowRmaosSRV = nullptr;
	shellSnowHeightSRV = nullptr;
	shellSnowTextureIsPBR = false;

	auto tryLoadDDS = [](const std::string& a_path, winrt::com_ptr<ID3D11ShaderResourceView>& a_srv) {
		return LoadGameDDS(a_path, a_srv);
	};

	std::string chosenPath = settings.SnowTexturePath.empty() ? "Textures\\Landscape\\snow01.dds" : settings.SnowTexturePath;
	for (auto& pathChar : chosenPath)
		if (pathChar == '/')
			pathChar = '\\';
	std::string loweredPath = chosenPath;
	std::transform(loweredPath.begin(), loweredPath.end(), loweredPath.begin(),
		[](unsigned char c) { return (char)std::tolower(c); });

	std::string pbrPath;
	if (loweredPath.find("\\pbr\\") != std::string::npos || loweredPath.rfind("pbr\\", 0) == 0)
		pbrPath = chosenPath;
	else if (size_t texPos = loweredPath.find("textures\\"); texPos != std::string::npos)
		pbrPath = chosenPath.substr(0, texPos + 9) + "PBR\\" + chosenPath.substr(texPos + 9);

	if (!pbrPath.empty() && pbrPath.size() > 4 && tryLoadDDS(pbrPath, shellSnowDiffuseSRV)) {
		shellSnowTextureIsPBR = true;
		std::string base = pbrPath.substr(0, pbrPath.size() - 4);
		bool hasNormal = tryLoadDDS(base + "_n.dds", shellSnowNormalSRV);
		bool hasRmaos = tryLoadDDS(base + "_rmaos.dds", shellSnowRmaosSRV);
		bool hasHeight = tryLoadDDS(base + "_p.dds", shellSnowHeightSRV);
		logger::info("[SNOW DEFORMATION] PBR snow set: {} (normal={} rmaos={} height={})", pbrPath, hasNormal, hasRmaos, hasHeight);

		snowGlintLogDensity = 6.0f;
		snowGlintMicroRoughness = 0.3f;
		snowGlintDensityRandomization = 5.0f;
		snowGlintScreenSpaceScale = 1.0f;
		snowRoughnessScale = 0.7f;
		snowSpecularLevel = 0.02f;
		snowDisplacementScale = 1.0f;
		snowPBRSetName.clear();

		size_t slashPos = base.find_last_of('\\');
		std::string baseName = (slashPos == std::string::npos) ? base : base.substr(slashPos + 1);
		std::transform(baseName.begin(), baseName.end(), baseName.begin(),
			[](unsigned char c) { return (char)std::tolower(c); });
		const auto& pbrSets = globals::features::truePBR.pbrTextureSets;
		size_t matchCount = 0;
		if (!settings.SnowPBRConfig.empty()) {
			if (pbrSets.find(settings.SnowPBRConfig) != pbrSets.end()) {
				snowPBRSetName = settings.SnowPBRConfig;
			} else {
				std::string pinLower = settings.SnowPBRConfig;
				std::transform(pinLower.begin(), pinLower.end(), pinLower.begin(),
					[](unsigned char c) { return (char)std::tolower(c); });
				for (const auto& [setName, setData] : pbrSets) {
					std::string keyLower = setName;
					std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(),
						[](unsigned char c) { return (char)std::tolower(c); });
					if (keyLower == pinLower) {
						snowPBRSetName = setName;
						break;
					}
				}
				if (snowPBRSetName.empty())
					logger::info("[SNOW DEFORMATION] pinned TruePBR config '{}' is not loaded; falling back to the automatic match", settings.SnowPBRConfig);
			}
		}
		std::string bestKey;
		bool bestHasGlints = false;
		size_t bestExtra = 0;
		for (const auto& [setName, setData] : pbrSets) {
			std::string keyLower = setName;
			std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(),
				[](unsigned char c) { return (char)std::tolower(c); });
			if (keyLower.find(baseName) == std::string::npos)
				continue;
			++matchCount;
			if (keyLower == baseName && snowPBRSetName.empty()) {
				snowPBRSetName = setName;
				bestKey.clear();
				break;
			}
			const bool hasGlints = setData.glintParameters.enabled;
			const size_t extra = keyLower.size() - baseName.size();
			bool better = bestKey.empty();
			if (!better && hasGlints != bestHasGlints)
				better = hasGlints;
			else if (!better && extra != bestExtra)
				better = extra < bestExtra;
			else if (!better)
				better = setName < bestKey;
			if (better) {
				bestKey = setName;
				bestHasGlints = hasGlints;
				bestExtra = extra;
			}
		}
		if (snowPBRSetName.empty())
			snowPBRSetName = bestKey;
		if (!snowPBRSetName.empty()) {
			RefreshSnowPBRParams();
			logger::info("[SNOW DEFORMATION] TruePBR config matched: {} ({} of {} sets matched '{}'; glintDensity={:.1f} roughScale={:.2f} spec={:.3f})",
				snowPBRSetName, matchCount, pbrSets.size(), baseName, snowGlintLogDensity, snowRoughnessScale, snowSpecularLevel);
		} else {
			logger::info("[SNOW DEFORMATION] no TruePBR config matches '{}' ({} sets loaded); using built-in snow defaults",
				baseName, pbrSets.size());
		}
	} else {
		snowPBRSetName.clear();
		bool ok = tryLoadDDS(chosenPath, shellSnowDiffuseSRV);
		if (!ok && chosenPath != "Textures\\Landscape\\snow01.dds") {
			logger::info("[SNOW DEFORMATION] Snow diffuse not loose-file loadable: {}", chosenPath);
			chosenPath = "Textures\\Landscape\\snow01.dds";
			ok = tryLoadDDS(chosenPath, shellSnowDiffuseSRV);
		}
		logger::info("[SNOW DEFORMATION] Snow diffuse load ({}): {}", ok ? "ok (legacy)" : "missing, using fallback color", chosenPath);
	}
}

void SnowDeformation::RefreshSnowPBRParams()
{
	if (snowPBRSetName.empty())
		return;
	const auto& pbrSets = globals::features::truePBR.pbrTextureSets;
	auto it = pbrSets.find(snowPBRSetName);
	if (it == pbrSets.end())
		return;
	const auto& cfg = it->second;
	snowRoughnessScale = cfg.roughnessScale;
	snowSpecularLevel = cfg.specularLevel;
	snowDisplacementScale = cfg.displacementScale;
	if (cfg.glintParameters.enabled) {
		snowGlintLogDensity = cfg.glintParameters.logMicrofacetDensity;
		snowGlintMicroRoughness = std::clamp(cfg.glintParameters.microfacetRoughness, 0.005f, 0.3f);
		snowGlintDensityRandomization = std::clamp(cfg.glintParameters.densityRandomization, 0.0f, 5.0f);
		snowGlintScreenSpaceScale = std::max(1.0f, cfg.glintParameters.screenSpaceScale);
	} else {
		snowGlintLogDensity = 0.0f;
	}
}

std::vector<std::pair<const char*, const char*>> SnowDeformation::ShellPSDefines(const char* a_extra)
{
	std::vector<std::pair<const char*, const char*>> defines = { { "PSHADER", "" } };
	if (a_extra)
		defines.emplace_back(a_extra, "");
	if (globals::features::exponentialHeightFog.loaded)
		defines.emplace_back("SNOW_EXP_HEIGHT_FOG", "");
	if (globals::features::ibl.loaded)
		defines.emplace_back("SNOW_IBL", "");
	return defines;
}

ID3D11VertexShader* SnowDeformation::GetShellFillVS()
{
	if (!shellFillVS)
		shellFillVS.attach(static_cast<ID3D11VertexShader*>(Util::CompileShader(L"Data\\Shaders\\SnowDeformation\\DepthSyncCS.hlsl", {}, "vs_5_0", "FillVS")));
	return shellFillVS.get();
}

ID3D11PixelShader* SnowDeformation::GetShellFillPS()
{
	if (!shellFillPS)
		shellFillPS.attach(static_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\SnowDeformation\\DepthSyncCS.hlsl", {}, "ps_5_0", "FillPS")));
	return shellFillPS.get();
}

ID3D11PixelShader* SnowDeformation::GetShellFillPSClaim()
{
	if (!shellFillPSClaim)
		shellFillPSClaim.attach(static_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\SnowDeformation\\DepthSyncCS.hlsl", { { "FILL_CLAIM", "" } }, "ps_5_0", "FillPS")));
	return shellFillPSClaim.get();
}

ID3D11ShaderResourceView* SnowDeformation::ShellWaterSRV() const
{
	return waterVisibleTexture && waterVisibleTexture->srv ? waterVisibleTexture->srv.get() : (waterHeightTexture ? waterHeightTexture->srv.get() : nullptr);
}

ID3D11ShaderResourceView* SnowDeformation::TerrainWetSRV() const
{
	if (shellTerrainWetValid)
		return shellTerrainWet->srv.get();
	return shellTerrainTexture ? shellTerrainTexture->srv.get() : nullptr;
}

void SnowDeformation::UpdateTerrainWet()
{
	auto* water = ShellWaterSRV();
	const uint64_t key[3] = { shellTerrainVersion, waterWindowVersion, reinterpret_cast<uint64_t>(water) };
	if (shellTerrainWetValid && std::equal(std::begin(key), std::end(key), std::begin(shellTerrainWetKey)))
		return;
	shellTerrainWetValid = false;
	auto* cs = GetSnowCS(kShellTerrainCutCS);
	if (!cs || !shellTerrainTexture || !shellTerrainTexture->srv || !shellTerrainWet || !shellTerrainWet->uav)
		return;
	auto context = globals::d3d::context;
	globals::profiler->BeginPass("SnowDeformation::Ground - Field Bakes");
	ID3D11Buffer* cb = shellCB->CB();
	context->CSSetConstantBuffers(0, 1, &cb);
	ID3D11ShaderResourceView* terrainSRV = shellTerrainTexture->srv.get();
	context->CSSetShaderResources(0, 1, &terrainSRV);
	context->CSSetShaderResources(27, 1, &water);
	ID3D11UnorderedAccessView* wetUAV = shellTerrainWet->uav.get();
	context->CSSetUnorderedAccessViews(4, 1, &wetUAV, nullptr);
	context->CSSetShader(cs, nullptr, 0);
	context->Dispatch((kShellWindowDim + 7) / 8, (kShellWindowDim + 7) / 8, 1);
	ID3D11UnorderedAccessView* nullUAV = nullptr;
	context->CSSetUnorderedAccessViews(4, 1, &nullUAV, nullptr);
	ID3D11ShaderResourceView* nullSRV = nullptr;
	context->CSSetShaderResources(0, 1, &nullSRV);
	context->CSSetShaderResources(27, 1, &nullSRV);
	context->CSSetShader(nullptr, nullptr, 0);
	globals::profiler->EndPass();
	std::copy(std::begin(key), std::end(key), std::begin(shellTerrainWetKey));
	shellTerrainWetValid = true;
}

bool SnowDeformation::EnsurePrepassResources(ID3D11ShaderResourceView* a_mainDepthSRV)
{
	if (!a_mainDepthSRV)
		return false;
	winrt::com_ptr<ID3D11Resource> depthRes;
	a_mainDepthSRV->GetResource(depthRes.put());
	auto depthTex = depthRes.try_as<ID3D11Texture2D>();
	if (!depthTex)
		return false;
	D3D11_TEXTURE2D_DESC depthDesc{};
	depthTex->GetDesc(&depthDesc);

	auto device = globals::d3d::device;
	if (shellRasterDepth && (shellRasterDepth->desc.Width != depthDesc.Width || shellRasterDepth->desc.Height != depthDesc.Height)) {
		delete shellRasterDepth;
		shellRasterDepth = nullptr;
		shellTestDepthSRV = nullptr;
		shellTestDepthDSV = nullptr;
		shellTestDepth = nullptr;
	}
	if (!shellRasterDepth) {
		shellRasterDepth = SD_MakeTexture2D(depthDesc.Width, depthDesc.Height, DXGI_FORMAT_R32_FLOAT, "SnowDeformation::ShellRasterDepth", D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);

		D3D11_TEXTURE2D_DESC testDesc = shellRasterDepth->desc;
		testDesc.Format = DXGI_FORMAT_R32_TYPELESS;
		testDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
		if (FAILED(device->CreateTexture2D(&testDesc, nullptr, shellTestDepth.put())))
			return false;
		Util::SetResourceName(shellTestDepth.get(), "SnowDeformation::ShellTestDepth");
		D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
		dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
		dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		if (FAILED(device->CreateDepthStencilView(shellTestDepth.get(), &dsvDesc, shellTestDepthDSV.put())))
			return false;
		Util::SetResourceName(shellTestDepthDSV.get(), "SnowDeformation::ShellTestDepth DSV");
		D3D11_SHADER_RESOURCE_VIEW_DESC testSrvDesc = {
			.Format = DXGI_FORMAT_R32_FLOAT,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};
		if (FAILED(device->CreateShaderResourceView(shellTestDepth.get(), &testSrvDesc, shellTestDepthSRV.put())))
			return false;
		Util::SetResourceName(shellTestDepthSRV.get(), "SnowDeformation::ShellTestDepth SRV");
	}
	if (!shellFillDepthState) {
		D3D11_DEPTH_STENCIL_DESC dsDesc{};
		dsDesc.DepthEnable = TRUE;
		dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		dsDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		if (FAILED(device->CreateDepthStencilState(&dsDesc, shellFillDepthState.put())))
			return false;
		Util::SetResourceName(shellFillDepthState.get(), "SnowDeformation::ShellFillDepthState");
	}
	return shellRasterDepth->srv && shellRasterDepth->rtv && shellTestDepthDSV && shellTestDepthSRV;
}

bool SnowDeformation::EnsureDeferredResources()
{
	if (!shellRasterDepth)
		return false;
	const auto& base = shellRasterDepth->desc;
	static constexpr DXGI_FORMAT kFormats[3] = { DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT };
	static constexpr const char* kNames[3] = { "SnowDeformation::ShellDeferredGrid", "SnowDeformation::ShellDeferredGridDeriv", "SnowDeformation::ShellDeferredDepth" };
	for (int i = 0; i < 3; i++) {
		auto*& attr = shellDeferredAttr[i];
		if (attr && (attr->desc.Width != base.Width || attr->desc.Height != base.Height)) {
			delete attr;
			attr = nullptr;
		}
		if (!attr)
			attr = SD_MakeTexture2D(base.Width, base.Height, kFormats[i], kNames[i], D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
	}
	if (!shellDeferredDepthState) {
		D3D11_DEPTH_STENCIL_DESC dsDesc{};
		dsDesc.DepthEnable = TRUE;
		dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		dsDesc.DepthFunc = D3D11_COMPARISON_LESS;
		if (FAILED(globals::d3d::device->CreateDepthStencilState(&dsDesc, shellDeferredDepthState.put())))
			return false;
		Util::SetResourceName(shellDeferredDepthState.get(), "SnowDeformation::ShellDeferredDepthState");
	}
	return shellDeferredAttr[0]->rtv && shellDeferredAttr[1]->rtv && shellDeferredAttr[2]->rtv;
}

ID3D11PixelShader* SnowDeformation::GetShellPSDeferred()
{
	if (!shellPSDeferred) {
		auto defines = ShellPSDefines();
		defines.emplace_back("SNOW_SHELL_DEFERRED", "");
		shellPSDeferred.attach(static_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\SnowDeformation\\SnowShell.hlsl", defines, "ps_5_0")));
	}
	return shellPSDeferred.get();
}

ID3D11PixelShader* SnowDeformation::GetShellPSPrepassAttr(bool a_keepHidden)
{
	auto& slot = shellPSPrepassAttr[a_keepHidden ? 1 : 0];
	if (!slot) {
		auto defines = ShellPSDefines();
		defines.emplace_back("SNOW_SHELL_DEPTH_PREPASS", "");
		defines.emplace_back("SNOW_PREPASS_ATTR", "");
		if (a_keepHidden)
			defines.emplace_back("SNOW_PREPASS_KEEP_HIDDEN", "");
		slot.attach(static_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\SnowDeformation\\SnowShell.hlsl", defines, "ps_5_0")));
	}
	return slot.get();
}

ID3D11PixelShader* SnowDeformation::GetShellPSPrepassEarly()
{
	if (!shellPSPrepassEarly) {
		auto defines = ShellPSDefines();
		defines.emplace_back("SNOW_SHELL_DEPTH_PREPASS", "");
		defines.emplace_back("SNOW_PREPASS_ATTR", "");
		defines.emplace_back("SNOW_PREPASS_EARLY", "");
		shellPSPrepassEarly.attach(static_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\SnowDeformation\\SnowShell.hlsl", defines, "ps_5_0")));
	}
	return shellPSPrepassEarly.get();
}

ID3D11PixelShader* SnowDeformation::GetShellPSEarlyResolve()
{
	if (!shellPSEarlyResolve)
		shellPSEarlyResolve.attach(static_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\SnowDeformation\\SnowShell.hlsl", ShellPSDefines("SNOW_S1_RESOLVE"), "ps_5_0")));
	return shellPSEarlyResolve.get();
}

bool SnowDeformation::EnsureEarlyPrepassResources()
{
	if (!shellRasterDepth)
		return false;
	const auto& base = shellRasterDepth->desc;
	auto ensure = [&](Texture2D*& a_tex, DXGI_FORMAT a_format, const char* a_name, bool a_uav) {
		if (a_tex && (a_tex->desc.Width != base.Width || a_tex->desc.Height != base.Height)) {
			delete a_tex;
			a_tex = nullptr;
		}
		if (!a_tex)
			a_tex = SD_MakeTexture2D(base.Width, base.Height, a_format, a_name,
				D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | (a_uav ? D3D11_BIND_UNORDERED_ACCESS : 0u));
		return a_tex->srv && a_tex->rtv;
	};
	return ensure(shellEarlyPending, DXGI_FORMAT_R16G16_FLOAT, "SnowDeformation::ShellEarlyPending", false) &&
	       ensure(shellEarlyMarks, DXGI_FORMAT_R32_FLOAT, "SnowDeformation::ShellEarlyMarks", true);
}

void SnowDeformation::SetShellSeamBounds(ShellCB& a_cb, int a_minX, int a_minY, int a_maxX, int a_maxY)
{
	a_cb.SeamBounds = { a_minX * 4096.0f, a_minY * 4096.0f, (a_maxX + 1) * 4096.0f, (a_maxY + 1) * 4096.0f };
	a_cb.SeamRampInv = 1.0f / 2048.0f;
}

ConstantBuffer* SnowDeformation::DepthSyncRectBuffer()
{
	if (!depthSyncRectCB)
		depthSyncRectCB = new ConstantBuffer(ConstantBufferDesc<DepthSyncRectCB>(), "SnowDeformation::DepthSyncRectCB");
	return depthSyncRectCB;
}

D3D11_BOX SnowDeformation::ScreenRectBox(const D3D11_VIEWPORT* a_viewport, uint32_t a_width, uint32_t a_height, uint32_t a_guard) const
{
	D3D11_BOX box{ 0, 0, 0, a_width, a_height, 1 };
	if (!a_viewport || a_viewport->Width <= 0.0f || a_viewport->Height <= 0.0f)
		return box;
	const int64_t left = int64_t(std::floor(a_viewport->TopLeftX)) - a_guard;
	const int64_t top = int64_t(std::floor(a_viewport->TopLeftY)) - a_guard;
	const int64_t right = int64_t(std::ceil(a_viewport->TopLeftX + a_viewport->Width)) + a_guard;
	const int64_t bottom = int64_t(std::ceil(a_viewport->TopLeftY + a_viewport->Height)) + a_guard;
	box.left = UINT(std::clamp<int64_t>(left, 0, a_width));
	box.top = UINT(std::clamp<int64_t>(top, 0, a_height));
	box.right = UINT(std::clamp<int64_t>(right, int64_t(box.left), a_width));
	box.bottom = UINT(std::clamp<int64_t>(bottom, int64_t(box.top), a_height));
	return box;
}

void SnowDeformation::RefreshShellGridPlacement(ShellCB& a_cb, const float4* a_camAdjust)
{
	auto& fb = globals::game::frameBufferCached;
	const auto camAdjust = a_camAdjust ? *a_camAdjust : fb.GetCameraPosAdjust();
	const float shellSpacing = kShellGridSpacing;
	a_cb.GridSpacing = shellSpacing;
	const float warpedHalfSpan = ShellWarpedHalfSpan(shellSpacing);
	a_cb.WarpedHalfSpan = warpedHalfSpan;
	a_cb.GridOrigin = {
		std::floor(camAdjust.x / kShellOriginSnap) * kShellOriginSnap - warpedHalfSpan,
		std::floor(camAdjust.y / kShellOriginSnap) * kShellOriginSnap - warpedHalfSpan
	};
	a_cb.TerrainTexelSize = kShellVertexSpacing;
	a_cb.TerrainDim = kShellWindowDim;
	constexpr float cellSize = kShellVertexSpacing * kShellTexelsPerCell;
	a_cb.GridToTerrainOffset = {
		a_cb.GridOrigin.x - shellWindowCellX * cellSize,
		a_cb.GridOrigin.y - shellWindowCellY * cellSize
	};
	a_cb.GridToDeformOffset = {
		a_cb.GridOrigin.x - windowOrigin.x,
		a_cb.GridOrigin.y - windowOrigin.y
	};
	a_cb.DeformMapOrigin = mapOrigin;
	a_cb.ShellFlags.x = settings.ShellHorizonMarch ? 1 : 0;
	const bool fine = shellFineValid && shellTerrainFine && shellTerrainFine->srv;
	a_cb.FineWindow = { a_cb.GridOrigin.x - shellFineOriginX, a_cb.GridOrigin.y - shellFineOriginY,
		fine ? float(kShellFineDim) : 0.0f, kShellFineTexel };
	a_cb.SlopeDrape = { std::clamp(settings.SlopeDrape, 0.0f, 1.0f), 0.5f, 0.866f, 0.0f };
	constexpr float kSnowUVTile = 4096.0f / 24.0f;
	a_cb.SnowUVOffset = {
		std::fmod(a_cb.GridOrigin.x, kSnowUVTile),
		std::fmod(a_cb.GridOrigin.y, kSnowUVTile)
	};
}

float SnowDeformation::ShellTessTargetRenderPx() const
{
	const auto& dynRes = globals::game::frameBufferCached.GetDynamicResolutionParams1();
	const float scale = (dynRes.x > 0.0f && dynRes.y > 0.0f) ? std::clamp(std::sqrt(dynRes.x * dynRes.y), 0.25f, 1.0f) : 1.0f;
	return kShellTessTargetPx * scale;
}

void SnowDeformation::DrawShellEarlyPrepass()
{
	shellEarlyState = EarlyPrepass::NotDrawing;
	const uint32_t frame = globals::state->frameCount;
	if (!settings.EnableSnowDeformation || !globals::state->inWorld || SnowShadersPending(1) || !lastShellCBData ||
		shellDrawViewportFrame + 1 != frame || !shellDrawGroundGated)
		return;
	const auto& frameDynRes = globals::game::frameBufferCached.GetDynamicResolutionParams1();
	if (frameDynRes.x != shellDrawDynRes[0] || frameDynRes.y != shellDrawDynRes[1])
		return;
	auto& tb = globals::features::terrainBlending;
	if (!tb.loaded || !tb.settings.Enabled || !tb.blendedDepthTexture || !tb.blendedDepthTexture->srv) {
		shellEarlyState = EarlyPrepass::NeedsTerrainBlending;
		return;
	}
	shellEarlyState = EarlyPrepass::Unavailable;
	auto renderer = globals::game::renderer;
	const auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	auto* psEarly = GetShellPSPrepassEarly();
	auto* fillVS = GetShellFillVS();
	auto* fillPS = fillVS ? GetShellFillPS() : nullptr;
	auto* fillClaim = fillVS ? GetShellFillPSClaim() : nullptr;
	if (!psEarly || !fillPS || !fillClaim || !GetShellPSEarlyResolve() || !GetShellPSDeferred() || !mainDepth.views[0] || !mainDepth.texture ||
		!shellFillDepthState || !LandscapeShadeReady())
		return;
	D3D11_TEXTURE2D_DESC mainDesc{};
	mainDepth.texture->GetDesc(&mainDesc);
	if (!shellRasterDepth || shellRasterDepth->desc.Width != mainDesc.Width || shellRasterDepth->desc.Height != mainDesc.Height || !shellTestDepthDSV ||
		!EnsureDeferredResources() || !EnsureEarlyPrepassResources() || !shellTerrainTexture || !shellTerrainTexture->srv ||
		!heightBottomFiltered)
		return;

	auto context = globals::d3d::context;
	auto& fb = globals::game::frameBufferCached;
	ShellCB cb = *lastShellCBData;
	cb.CameraViewProj = fb.GetCameraViewProj();
	cb.CameraViewProjUnjittered = fb.GetCameraViewProjUnjittered();
	cb.CameraPreviousViewProjUnjittered = fb.GetCameraPreviousViewProjUnjittered();
	cb.CameraView = fb.GetCameraView();
	cb.CameraPosAdjust = fb.GetCameraPosAdjust();
	cb.CameraPreviousPosAdjust = fb.GetCameraPreviousPosAdjust();
	RefreshShellGridPlacement(cb);
	cb.SeamRampInv = 0.0f;
	int seamMinX, seamMinY, seamMaxX, seamMaxY;
	if (LoadedCellSquare(seamMinX, seamMinY, seamMaxX, seamMaxY))
		SetShellSeamBounds(cb, seamMinX, seamMinY, seamMaxX, seamMaxY);
	cb.DeformInvWorldSize = 1.0f / deformWorldSize;
	cb.ObjectHeightCenter = heightWindowCenter;
	cb.ObjectHeightHalfExtent = ObjectRasterHalfExtent();
	cb.BermBakeActive = bermFieldTexture ? 1.0f : 0.0f;
	cb.TrackAge = TrackAgeConstants();
	cb.UndulationFieldWindow = { undulationFieldCenter.x, undulationFieldCenter.y,
		1.0f / kUndulationFieldHalfExtent,
		undulationFieldValid ? 1.0f : 0.0f };
	cb.ExclusionFieldWindow = { exclusionFieldCenter.x, exclusionFieldCenter.y,
		1.0f / kExclusionFieldHalfExtent,
		exclusionFieldValid ? 1.0f : 0.0f };
	const auto mapDim = [](const Texture2D* a_tex) { return (a_tex && a_tex->srv) ? float(a_tex->desc.Width) : 0.0f; };
	cb.FieldDims = { mapDim(deformationTextures[0]), mapDim(bermFieldTexture), mapDim(heightBottomFiltered), mapDim(exclusionFieldTexture) };
	cb.TessScreen = { ShellTessTargetRenderPx(), shellDrawViewport.Width * 0.5f, shellDrawViewport.Height * 0.5f, shellDrawViewport.MaxDepth - shellDrawViewport.MinDepth };
	shellCB->Update(cb);

	struct StageBinds
	{
		ID3D11ShaderResourceView* srv[34]{};
		ID3D11Buffer* cb[4]{};
		ID3D11SamplerState* sampler = nullptr;
	};
	StageBinds saved[5];
#define SNOW_SAVE_STAGE(P, b)                                    \
	context->P##GetShaderResources(0, 30, b.srv);                \
	context->P##GetShaderResources(56, 4, b.srv + 30);           \
	context->P##GetConstantBuffers(0, 1, b.cb);                  \
	context->P##GetConstantBuffers(4, 3, b.cb + 1);              \
	context->P##GetSamplers(0, 1, &b.sampler)
#define SNOW_RESTORE_STAGE(P, b)                                 \
	context->P##SetShaderResources(0, 30, b.srv);                \
	context->P##SetShaderResources(56, 4, b.srv + 30);           \
	context->P##SetConstantBuffers(0, 1, b.cb);                  \
	context->P##SetConstantBuffers(4, 3, b.cb + 1);              \
	context->P##SetSamplers(0, 1, &b.sampler)
	SNOW_SAVE_STAGE(VS, saved[0]);
	SNOW_SAVE_STAGE(HS, saved[1]);
	SNOW_SAVE_STAGE(DS, saved[2]);
	SNOW_SAVE_STAGE(PS, saved[3]);
	SNOW_SAVE_STAGE(CS, saved[4]);
	winrt::com_ptr<ID3D11VertexShader> prevVS;
	winrt::com_ptr<ID3D11HullShader> prevHS;
	winrt::com_ptr<ID3D11DomainShader> prevDS;
	winrt::com_ptr<ID3D11PixelShader> prevPS;
	winrt::com_ptr<ID3D11ComputeShader> prevCS;
	context->VSGetShader(prevVS.put(), nullptr, nullptr);
	context->HSGetShader(prevHS.put(), nullptr, nullptr);
	context->DSGetShader(prevDS.put(), nullptr, nullptr);
	context->PSGetShader(prevPS.put(), nullptr, nullptr);
	context->CSGetShader(prevCS.put(), nullptr, nullptr);
	ID3D11UnorderedAccessView* prevCSUAVs[5]{};
	context->CSGetUnorderedAccessViews(1, 5, prevCSUAVs);
	winrt::com_ptr<ID3D11InputLayout> prevLayout;
	context->IAGetInputLayout(prevLayout.put());
	D3D11_PRIMITIVE_TOPOLOGY prevTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
	context->IAGetPrimitiveTopology(&prevTopology);
	winrt::com_ptr<ID3D11RasterizerState> prevRaster;
	context->RSGetState(prevRaster.put());
	D3D11_VIEWPORT prevViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
	UINT prevViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
	context->RSGetViewports(&prevViewportCount, prevViewports);
	winrt::com_ptr<ID3D11DepthStencilState> prevDepth;
	UINT prevStencilRef = 0;
	context->OMGetDepthStencilState(prevDepth.put(), &prevStencilRef);
	winrt::com_ptr<ID3D11BlendState> prevBlend;
	FLOAT prevBlendFactor[4]{};
	UINT prevSampleMask = 0xFFFFFFFF;
	context->OMGetBlendState(prevBlend.put(), prevBlendFactor, &prevSampleMask);
	ID3D11RenderTargetView* prevRTVs[8]{};
	ID3D11DepthStencilView* prevDSV = nullptr;
	context->OMGetRenderTargets(8, prevRTVs, &prevDSV);

	if (shellTerrainVersion != shellDrawTerrainVersion) {
		ID3D11Buffer* prevVB = nullptr;
		UINT prevVBStride = 0, prevVBOffset = 0;
		ID3D11Buffer* prevIB = nullptr;
		DXGI_FORMAT prevIBFormat = DXGI_FORMAT_UNKNOWN;
		UINT prevIBOffset = 0;
		ID3D11Buffer* prevVSCB1 = nullptr;
		ID3D11ShaderResourceView* prevWaterFineSRV = nullptr;
		ID3D11UnorderedAccessView* prevCSUAV0 = nullptr;
		context->IAGetVertexBuffers(0, 1, &prevVB, &prevVBStride, &prevVBOffset);
		context->IAGetIndexBuffer(&prevIB, &prevIBFormat, &prevIBOffset);
		context->VSGetConstantBuffers(1, 1, &prevVSCB1);
		context->PSGetShaderResources(kWaterWindowSlot, 1, &prevWaterFineSRV);
		context->CSGetUnorderedAccessViews(0, 1, &prevCSUAV0);
		RenderWaterCapture();
		CoverWaterWindow();
		context->IASetVertexBuffers(0, 1, &prevVB, &prevVBStride, &prevVBOffset);
		context->IASetIndexBuffer(prevIB, prevIBFormat, prevIBOffset);
		context->VSSetConstantBuffers(1, 1, &prevVSCB1);
		context->PSSetShaderResources(kWaterWindowSlot, 1, &prevWaterFineSRV);
		const UINT keepCount = UINT(-1);
		context->CSSetUnorderedAccessViews(0, 1, &prevCSUAV0, &keepCount);
		const auto release = [](IUnknown* a_held) {
			if (a_held)
				a_held->Release();
		};
		release(prevVB);
		release(prevIB);
		release(prevVSCB1);
		release(prevWaterFineSRV);
		release(prevCSUAV0);
	}

	UpdateTerrainWet();

	ID3D11Buffer* cbs[1] = { shellCB->CB() };
	auto state = globals::state;
	ID3D11Buffer* sharedBuffers[3] = { state->permutationCB->CB(), state->sharedDataCB->CB(), state->featureDataCB->CB() };
	ID3D11ShaderResourceView* shellSRVs[9] = { shellTerrainTexture->srv.get(), GetDeformationSRV(), shellSnowDiffuseSRV.get(), Util::GetCurrentSceneDepthSRV(false), nullptr, heightBottomFiltered->srv.get(), shellSnowNormalSRV.get(), shellSnowRmaosSRV.get(), shellSnowHeightSRV.get() };
	ID3D11ShaderResourceView* wetSRV = TerrainWetSRV();
	ID3D11ShaderResourceView* fineSRV = (shellFineValid && shellTerrainFine) ? shellTerrainFine->srv.get() : nullptr;
	ID3D11ShaderResourceView* waterSRVs[2] = { ShellWaterSRV(), waterHeightTexture ? waterHeightTexture->srv.get() : nullptr };
	ID3D11ShaderResourceView* waterNearSRV = waterNearTexture && waterNearTexture->srv ? waterNearTexture->srv.get() : nullptr;
	ID3D11ShaderResourceView* untouchedSRV = untouchedTilesSRV.get();
	ID3D11ShaderResourceView* exclusionSRV = GetExclusionFieldSRV();
	ID3D11SamplerState* snowSampler = shellSnowSampler.get();

	globals::profiler->BeginPass("SnowDeformation::Ground - Shell Depth Prepass");
	D3D11_VIEWPORT fillViewport = shellDrawViewport;
	fillViewport.MinDepth = 0.0f;
	fillViewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &fillViewport);
	context->RSSetState(shellRasterState.get());
	context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
	context->IASetInputLayout(nullptr);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->OMSetRenderTargets(0, nullptr, shellTestDepthDSV.get());
	context->OMSetDepthStencilState(shellFillDepthState.get(), 0);
	context->VSSetShader(fillVS, nullptr, 0);
	context->HSSetShader(nullptr, nullptr, 0);
	context->DSSetShader(nullptr, nullptr, 0);
	context->PSSetShader(fillPS, nullptr, 0);
	ID3D11ShaderResourceView* sceneDepthSRV = tb.blendedDepthTexture->srv.get();
	context->PSSetShaderResources(9, 1, &sceneDepthSRV);
	context->Draw(3, 0);
	ID3D11ShaderResourceView* nullSRV = nullptr;
	context->PSSetShaderResources(9, 1, &nullSRV);

	const float rasterClear[4] = {};
	context->ClearRenderTargetView(shellRasterDepth->rtv.get(), rasterClear);
	ID3D11RenderTargetView* prepassRTVs[5] = { shellRasterDepth->rtv.get(), shellDeferredAttr[0]->rtv.get(), shellDeferredAttr[1]->rtv.get(), shellDeferredAttr[2]->rtv.get(), shellEarlyPending->rtv.get() };
	context->OMSetRenderTargets(5, prepassRTVs, shellTestDepthDSV.get());
	context->OMSetDepthStencilState(shellDepthState.get(), 0);
	context->RSSetViewports(1, &shellDrawViewport);
	context->PSSetShader(psEarly, nullptr, 0);
	context->PSSetConstantBuffers(0, 1, cbs);
	context->PSSetConstantBuffers(4, 3, sharedBuffers);
	context->PSSetShaderResources(0, 9, shellSRVs);
	context->PSSetShaderResources(13, 1, &fineSRV);
	context->PSSetShaderResources(15, 1, &exclusionSRV);
	context->PSSetShaderResources(19, 1, &waterNearSRV);
	context->PSSetShaderResources(27, 2, waterSRVs);
	context->PSSetShaderResources(kUntouchedTilesSlot, 1, &untouchedSRV);
	context->PSSetShaderResources(kTerrainWetSlot, 1, &wetSRV);
	ID3D11ShaderResourceView* bareSRV = shellTerrainBare ? shellTerrainBare->srv.get() : nullptr;
	context->PSSetShaderResources(kTerrainBareSlot, 1, &bareSRV);
	context->PSSetSamplers(0, 1, &snowSampler);
	ID3D11ShaderResourceView* tbMaskSRV = tb.terrainDepth.depthSRV;
	context->PSSetShaderResources(kTBMaskSlot, 1, &tbMaskSRV);
	DrawLandscapeSnowPrepass(context, cb, cbs[0]);
	tbMaskSRV = nullptr;
	context->PSSetShaderResources(kTBMaskSlot, 1, &tbMaskSRV);

	context->OMSetRenderTargets(0, nullptr, mainDepth.views[0]);
	context->RSSetViewports(1, &fillViewport);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(fillVS, nullptr, 0);
	context->HSSetShader(nullptr, nullptr, 0);
	context->DSSetShader(nullptr, nullptr, 0);
	context->PSSetShader(fillClaim, nullptr, 0);
	ID3D11ShaderResourceView* rasterSRV = shellRasterDepth->srv.get();
	context->PSSetShaderResources(9, 1, &rasterSRV);
	context->Draw(3, 0);
	context->PSSetShaderResources(9, 1, &nullSRV);
	globals::profiler->EndPass();

	context->OMSetRenderTargets(8, prevRTVs, prevDSV);
	SNOW_RESTORE_STAGE(VS, saved[0]);
	SNOW_RESTORE_STAGE(HS, saved[1]);
	SNOW_RESTORE_STAGE(DS, saved[2]);
	SNOW_RESTORE_STAGE(PS, saved[3]);
	SNOW_RESTORE_STAGE(CS, saved[4]);
#undef SNOW_SAVE_STAGE
#undef SNOW_RESTORE_STAGE
	const UINT keepCounts[5] = { UINT(-1), UINT(-1), UINT(-1), UINT(-1), UINT(-1) };
	context->CSSetUnorderedAccessViews(1, 5, prevCSUAVs, keepCounts);
	context->VSSetShader(prevVS.get(), nullptr, 0);
	context->HSSetShader(prevHS.get(), nullptr, 0);
	context->DSSetShader(prevDS.get(), nullptr, 0);
	context->PSSetShader(prevPS.get(), nullptr, 0);
	context->CSSetShader(prevCS.get(), nullptr, 0);
	context->IASetInputLayout(prevLayout.get());
	context->IASetPrimitiveTopology(prevTopology);
	context->RSSetState(prevRaster.get());
	if (prevViewportCount)
		context->RSSetViewports(prevViewportCount, prevViewports);
	context->OMSetDepthStencilState(prevDepth.get(), prevStencilRef);
	context->OMSetBlendState(prevBlend.get(), prevBlendFactor, prevSampleMask);
	for (auto& b : saved) {
		for (auto* v : b.srv)
			if (v)
				v->Release();
		for (auto* c : b.cb)
			if (c)
				c->Release();
		if (b.sampler)
			b.sampler->Release();
	}
	for (auto* u : prevCSUAVs)
		if (u)
			u->Release();
	for (auto* r : prevRTVs)
		if (r)
			r->Release();
	if (prevDSV)
		prevDSV->Release();
	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET, RE::BSGraphics::ShaderFlags::DIRTY_VIEWPORT,
		RE::BSGraphics::ShaderFlags::DIRTY_DEPTH_MODE, RE::BSGraphics::ShaderFlags::DIRTY_ALPHA_BLEND, RE::BSGraphics::ShaderFlags::DIRTY_RASTER_CULL_MODE,
		RE::BSGraphics::ShaderFlags::DIRTY_PRIMITIVE_TOPO);

	if (!shellEarlyCB)
		shellEarlyCB = std::make_unique<ShellCB>();
	*shellEarlyCB = cb;
	shellEarlyViewport = shellDrawViewport;
	shellEarlyFrame = frame;
	shellEarlyState = EarlyPrepass::Drawing;
}

void SnowDeformation::SyncSssDepth()
{
	if (!settings.EnableSnowDeformation || !globals::state->inWorld)
		return;
	const uint32_t frame = globals::state->frameCount;
	auto& ss = globals::features::screenSpaceShadows;
	auto& tb = globals::features::terrainBlending;
	auto* blended = tb.blendedDepthTexture;
	auto sky = globals::game::sky;
	const bool marched = settings.SnowScreenSpaceShadows && ss.loaded && ss.screenSpaceShadowsTexture && ss.bendSettings.Enable &&
	                     sky && sky->mode.get() == RE::Sky::Mode::kFull && tb.loaded && tb.settings.Enabled;
	static const bool marchFirst = [] {
		const auto& list = Feature::GetFeatureList();
		const auto at = [&](const Feature* a_feature) { return std::find(list.begin(), list.end(), a_feature) - list.begin(); };
		const bool ok = at(&globals::features::screenSpaceShadows) < at(&globals::features::snowDeformation);
		if (!ok)
			logger::warn("[SNOW DEFORMATION] Screen-Space Shadows is not ahead of Snow Deformation in the feature list; the shadow depth sync stays off");
		return ok;
	}();
	const auto rasterSized = [&](const Texture2D* a_tex) {
		return a_tex && a_tex->uav && a_tex->desc.Width == shellRasterDepth->desc.Width && a_tex->desc.Height == shellRasterDepth->desc.Height;
	};
	const bool early = shellEarlyFrame == frame && shellEarlyState == EarlyPrepass::Drawing;
	auto* syncCS = (early && marchFirst && marched) ? GetSnowCS(kSssSyncCS) : nullptr;
	if (!syncCS || !shellRasterDepth || !shellRasterDepth->srv || !rasterSized(blended) || !rasterSized(shellEarlyMarks) || !GetSnowCS(kSssRestoreCS) ||
		!DepthSyncRectBuffer()) {
		globals::profiler->MarkPassSkipped("SnowDeformation::Shadows - Snow Depth Sync");
		return;
	}
	shellSssRect = ScreenRectBox(&shellEarlyViewport, blended->desc.Width, blended->desc.Height, 0);
	DispatchSssDepth(syncCS, "SnowDeformation::Shadows - Snow Depth Sync");
	shellSssSyncFrame = frame;
}

void SnowDeformation::RestoreSssDepth()
{
	if (shellSssSyncFrame == globals::state->frameCount)
		DispatchSssDepth(GetSnowCS(kSssRestoreCS), "SnowDeformation::Shadows - Snow Depth Sync");
}

void SnowDeformation::DispatchSssDepth(ID3D11ComputeShader* a_cs, const char* a_pass)
{
	auto& tb = globals::features::terrainBlending;
	if (!a_cs || !tb.blendedDepthTexture || !tb.blendedDepthTexture->uav || !shellRasterDepth || !shellRasterDepth->srv || !shellEarlyMarks ||
		!shellEarlyMarks->uav || !depthSyncRectCB)
		return;
	auto context = globals::d3d::context;
	ID3D11ShaderResourceView* prevSRV = nullptr;
	ID3D11UnorderedAccessView* prevUAVs[2]{};
	ID3D11Buffer* prevCB = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> prevCS;
	context->CSGetShaderResources(1, 1, &prevSRV);
	context->CSGetUnorderedAccessViews(0, 2, prevUAVs);
	context->CSGetConstantBuffers(2, 1, &prevCB);
	context->CSGetShader(prevCS.put(), nullptr, nullptr);

	globals::profiler->BeginPass(a_pass);
	DepthSyncRectCB rectData{ { shellSssRect.left, shellSssRect.top }, { shellSssRect.right - shellSssRect.left, shellSssRect.bottom - shellSssRect.top } };
	depthSyncRectCB->Update(rectData);
	ID3D11Buffer* rectCB = depthSyncRectCB->CB();
	ID3D11ShaderResourceView* rasterSRV = shellRasterDepth->srv.get();
	ID3D11UnorderedAccessView* uavs[2] = { tb.blendedDepthTexture->uav.get(), shellEarlyMarks->uav.get() };
	context->CSSetConstantBuffers(2, 1, &rectCB);
	context->CSSetShaderResources(1, 1, &rasterSRV);
	context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
	context->CSSetShader(a_cs, nullptr, 0);
	context->Dispatch((rectData.Extent[0] + 7) / 8, (rectData.Extent[1] + 7) / 8, 1);
	globals::profiler->EndPass();

	const UINT keepCounts[2] = { UINT(-1), UINT(-1) };
	context->CSSetUnorderedAccessViews(0, 2, prevUAVs, keepCounts);
	context->CSSetShaderResources(1, 1, &prevSRV);
	context->CSSetConstantBuffers(2, 1, &prevCB);
	context->CSSetShader(prevCS.get(), nullptr, 0);
	if (prevSRV)
		prevSRV->Release();
	for (auto* u : prevUAVs)
		if (u)
			u->Release();
	if (prevCB)
		prevCB->Release();
}

void SnowDeformation::DrawShell()
{
	if (!settings.EnableSnowDeformation)
		return;
	if (SnowShadersPending(1))
		return;

	if (!globals::state->inWorld)
		return;

	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;
	auto& fb = globals::game::frameBufferCached;

	ShellCB cbData{};
	cbData.CameraViewProj = fb.GetCameraViewProj();
	cbData.CameraViewProjUnjittered = fb.GetCameraViewProjUnjittered();
	cbData.CameraPreviousViewProjUnjittered = fb.GetCameraPreviousViewProjUnjittered();
	cbData.CameraView = fb.GetCameraView();
	cbData.CameraPosAdjust = fb.GetCameraPosAdjust();
	cbData.CameraPreviousPosAdjust = fb.GetCameraPreviousPosAdjust();

	RefreshShellGridPlacement(cbData);

	cbData.SeamRampInv = 0.0f;
	int seamMinX, seamMinY, seamMaxX, seamMaxY;
	if (LoadedCellSquare(seamMinX, seamMinY, seamMaxX, seamMaxY))
		SetShellSeamBounds(cbData, seamMinX, seamMinY, seamMaxX, seamMaxY);
	cbData.DeformInvWorldSize = 1.0f / deformWorldSize;

	EnsureShellSnowTextures();
	RefreshSnowPBRParams();
	cbData.HasSnowTexture = shellSnowDiffuseSRV != nullptr;
	cbData.SnowTextureIsLinear = shellSnowTextureIsPBR ? 1.0f : 0.0f;
	cbData.HasSnowHeight = shellSnowHeightSRV ? 1.0f : 0.0f;
	cbData.SnowReliefDepth = 0.0f;
	cbData.SnowParallax = {
		snowDisplacementScale,
		std::clamp(settings.ParallaxShadowStrength, 0.0f, 2.0f),
		std::clamp(settings.ParallaxDepth, 0.0f, 2.0f),
		0.0f
	};
	cbData.TrackAge = TrackAgeConstants();
	const float snowSoftness = GetSnowSoftness();
	const SnowClimateLook look = ComputeSnowClimateLook();
	const float bermSlider = std::clamp(settings.BermHeight, 0.0f, 1.0f);
	const float lipSlider = std::clamp(settings.RimLip, 0.0f, 0.3f);
	const float teethSlider = std::clamp(settings.RimTeeth, 0.0f, 1.0f);
	cbData.BermHeightAmp = std::lerp(bermSlider * std::lerp(1.0f, 0.35f, snowSoftness), bermSlider * look.berm, look.strength);
	cbData.BermReach = BermReachScale();
	cbData.RimStyle = { std::lerp(lipSlider * (1.0f - snowSoftness), lipSlider * look.lip, look.strength),
		std::lerp(teethSlider * (1.0f - snowSoftness), teethSlider * look.teeth, look.strength),
		0.0f,
		GetAccumulationDepthScale() };
	cbData.TrampleStyle = { 0.0f, std::clamp(settings.TrampledFlattening, 0.0f, 1.0f), BodyPrintScale(), 0.0f };
	EnsureTrenchDetailTextures();
	const bool trenchDetail = trenchDetailNormalSRV && trenchDetailHeightSRV;
	cbData.TrenchDetailRelief = trenchDetail ? std::clamp(settings.TrenchDetailRelief, 0.0f, 2.0f) : 0.0f;
	cbData.TrenchDetailRoughness = trenchDetail ? std::clamp(settings.TrenchDetailRoughness, 0.3f, 1.5f) : 1.0f;
	cbData.TrenchDetailCoverage = std::clamp(settings.TrenchDetailCoverage, 0.0f, 2.5f);
	cbData.TrenchDetailShadow = trenchDetail ? std::clamp(settings.ParallaxDepth, 0.0f, 1.0f) : 0.0f;
	cbData.SpeedSpoil = trenchDetail && settings.SpeedSpoil ? 1.0f : 0.0f;
	cbData.HasSnowNormal = shellSnowNormalSRV ? 1.0f : 0.0f;
	cbData.HasSnowRmaos = shellSnowRmaosSRV ? 1.0f : 0.0f;
	cbData.SnowRoughnessScale = snowRoughnessScale;
	cbData.SnowGlintParams = { snowGlintLogDensity, snowGlintMicroRoughness, snowGlintDensityRandomization, snowGlintScreenSpaceScale };
	cbData.SnowSpecularLevel = snowSpecularLevel;
	cbData.EnableGlints = (settings.SnowGlints && globals::features::truePBR.glintsNoiseTexture) ? 1.0f : 0.0f;
	cbData.BorderStyle = { 0.0f, 0.0f, (float)sunCascadeSlice[0], (float)sunCascadeSlice[1] };
	const auto& dynRes = globals::game::frameBufferCached.GetDynamicResolutionParams1();
	const float2 landContest = LandBlendContest();
	cbData.CompactLook = { 0.0f, landContest.x, landContest.y, look.strength * look.slough };
	{
		const SnowCondition condition = GetSnowCondition();
		auto* player = RE::PlayerCharacter::GetSingleton();
		cbData.SnowClimate = { condition.temperature, kClimateLapse, player ? player->GetPosition().z : 0.0f, condition.freshness };
		cbData.SnowClimate2 = { condition.windPacking, condition.wetness, 0.0f, 1.0f - look.strength };
		cbData.SnowClimateOverride = { -1.0f, -1.0f, -1.0f, 0.0f };
		std::copy(look.lut.begin(), look.lut.end(), cbData.SnowClimateLUT);
	}
	cbData.DebugNoFarPad = 0.0f;
	cbData.ShellCullBare = shellTerrainTexture ? 1.0f : 0.0f;
	cbData.ObjectLiftCap = 0.5f;
	cbData.ObjectHeightCenter = heightWindowCenter;
	cbData.ObjectHeightHalfExtent = ObjectRasterHalfExtent();

	cbData.CrispShadows = ShadowAtlasSRV() ? 1.0f : 0.0f;
	auto& screenSpaceShadowsFeature = globals::features::screenSpaceShadows;
	cbData.ScreenSpaceShadowsActive = (settings.SnowScreenSpaceShadows && screenSpaceShadowsFeature.loaded && screenSpaceShadowsFeature.screenSpaceShadowsTexture) ?
	                                      (shellSssSyncFrame == globals::state->frameCount ? 2.0f : 1.0f) :
	                                      0.0f;

	cbData.UndulationAmp = std::max(settings.UndulationStrength, 0.0f);
	cbData.UndulationScale = std::max(settings.UndulationSpacing, 0.05f);
	cbData.BermBakeActive = bermFieldTexture ? 1.0f : 0.0f;
	cbData.UndulationFieldWindow = { undulationFieldCenter.x, undulationFieldCenter.y,
		1.0f / kUndulationFieldHalfExtent,
		undulationFieldValid ? 1.0f : 0.0f };
	cbData.ExclusionFieldWindow = { exclusionFieldCenter.x, exclusionFieldCenter.y,
		1.0f / kExclusionFieldHalfExtent,
		exclusionFieldValid ? 1.0f : 0.0f };

	auto& lightLimitFix = globals::features::lightLimitFix;
	cbData.PointLightsActive = (lightLimitFix.loaded && lightLimitFix.lights && lightLimitFix.lightIndexList && lightLimitFix.lightGrid) ? 1.0f : 0.0f;
	auto& skylighting = globals::features::skylighting;
	cbData.SkylightingActive = (skylighting.loaded && skylighting.texProbeArray) ? 1.0f : 0.0f;

	shellCB->Update(cbData);

	winrt::com_ptr<ID3D11RasterizerState> prevRaster;
	winrt::com_ptr<ID3D11DepthStencilState> prevDepth;
	winrt::com_ptr<ID3D11BlendState> prevBlend;
	UINT prevStencilRef = 0;
	FLOAT prevBlendFactor[4]{};
	UINT prevSampleMask = 0xFFFFFFFF;
	D3D11_PRIMITIVE_TOPOLOGY prevTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
	context->RSGetState(prevRaster.put());
	context->OMGetDepthStencilState(prevDepth.put(), &prevStencilRef);
	context->OMGetBlendState(prevBlend.put(), prevBlendFactor, &prevSampleMask);
	context->IAGetPrimitiveTopology(&prevTopology);
	winrt::com_ptr<ID3D11Buffer> prevIB;
	DXGI_FORMAT prevIBFormat = DXGI_FORMAT_UNKNOWN;
	UINT prevIBOffset = 0;
	{
		ID3D11Buffer* ib = nullptr;
		context->IAGetIndexBuffer(&ib, &prevIBFormat, &prevIBOffset);
		prevIB.attach(ib);
	}
	D3D11_VIEWPORT prevViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
	UINT prevViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
	context->RSGetViewports(&prevViewportCount, prevViewports);

	if (InInteriorCell()) {
		shellSnowVerdict = kSnowGateBare;
	} else {
		int sqMinX, sqMinY, sqMaxX, sqMaxY;
		shellSnowVerdict = LoadedCellSquare(sqMinX, sqMinY, sqMaxX, sqMaxY) ?
		                       ViewSnowVerdict(sqMinX, sqMinY, sqMaxX, sqMaxY, cbData) :
		                       kSnowGateUnknown;
	}
	shellGateBareFrames = shellSnowVerdict == kSnowGateBare ? std::min(shellGateBareFrames + 1, kShellGateBareFrames) : 0;
	const bool early = shellEarlyFrame == globals::state->frameCount;
	shellDrawGroundGated = shellGateBareFrames < kShellGateBareFrames && !GroundShellsSuspended();
	const bool shellGround = shellDrawGroundGated || early;
	const bool cascadeReads = settings.SnowCastsShadows && !GroundShellsSuspended();
	const bool interior = InInteriorCell();
	cascadeSnowSoonLast = !interior && CascadeSnowSoon();
	const bool landscapeReads = cascadeReads && !interior && cascadeSnowSoonLast;
	if (!shellGround && !landscapeReads && !settings.VolumeSnow)
		CullUnseenCaptures(cbData);
	else {
		captureViewCulled = 0;
	}

	const bool nothingDraws = !shellGround && !landscapeReads && !settings.VolumeSnow;
	const bool idleInterior = nothingDraws && cascadeReads && interior;
	if (idleInterior && HeightMapIdleReady())
		IdleObjectHeightMap();
	else if (nothingDraws && !idleInterior)
		SkipObjectHeightMap();
	else if (!SnowShadersPending(2) && EnsureHeightRasterShaders())
		RenderObjectHeightMap();
	RenderWaterCapture();
	CoverWaterWindow();
	UpdateTerrainWet();
	shellDrawTerrainVersion = shellTerrainVersion;
	if (prevViewportCount)
		context->RSSetViewports(prevViewportCount, prevViewports);
	const bool mainRange = prevViewportCount && mainViewportFrame == globals::state->frameCount;
	if (mainRange) {
		D3D11_VIEWPORT shellViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
		for (UINT i = 0; i < prevViewportCount; i++) {
			shellViewports[i] = prevViewports[i];
			shellViewports[i].MinDepth = mainViewportMinDepth;
			shellViewports[i].MaxDepth = mainViewportMaxDepth;
		}
		context->RSSetViewports(prevViewportCount, shellViewports);
	}
	shellDrawViewportFrame = UINT32_MAX;
	if (prevViewportCount) {
		D3D11_VIEWPORT used = prevViewports[0];
		if (mainRange) {
			used.MinDepth = mainViewportMinDepth;
			used.MaxDepth = mainViewportMaxDepth;
		}
		shellDrawViewport = used;
		shellDrawViewportFrame = globals::state->frameCount;
		shellDrawDynRes[0] = dynRes.x;
		shellDrawDynRes[1] = dynRes.y;
	}

	cbData.ObjectHeightCenter = heightWindowCenter;
	cbData.ExclusionFieldWindow = { exclusionFieldCenter.x, exclusionFieldCenter.y,
		1.0f / kExclusionFieldHalfExtent,
		exclusionFieldValid ? 1.0f : 0.0f };
	if (prevViewportCount)
		cbData.TessScreen = { ShellTessTargetRenderPx(), prevViewports[0].Width * 0.5f, prevViewports[0].Height * 0.5f, shellDrawViewport.MaxDepth - shellDrawViewport.MinDepth };
	const auto mapDim = [](const Texture2D* a_tex) { return (a_tex && a_tex->srv) ? float(a_tex->desc.Width) : 0.0f; };
	cbData.FieldDims = { mapDim(deformationTextures[0]), mapDim(bermFieldTexture), mapDim(heightBottomFiltered), mapDim(exclusionFieldTexture) };
	shellCB->Update(cbData);

	if (!lastShellCBData)
		lastShellCBData = std::make_unique<ShellCB>();
	*lastShellCBData = cbData;

	UnbindMarchCaches();
	marchIdleFrames = shellGround ? 0 : std::min(marchIdleFrames + 1, kMarchIdleFrames);
	const bool marchIdle = marchIdleFrames >= kMarchIdleFrames;
	if (marchIdle) {
		marchCacheValid = false;
		marchFarValid = false;
		marchSunValid = false;
		globals::profiler->MarkPassSkipped("SnowDeformation::Ground - March Cache");
	}
	if ((cbData.ShellFlags.x & 1) != 0 && (marchIdle || !UpdateMarchCaches(cbData))) {
		cbData.ShellFlags.x &= ~1;
		shellCB->Update(cbData);
		*lastShellCBData = cbData;
	}

	{
		auto& masksRT = renderer->GetRuntimeData().renderTargets[MASKS];
		if (masksRT.SRV && masksRT.texture && shellGround) {
			context->OMSetRenderTargets(0, nullptr, nullptr);
			globals::profiler->BeginPass("SnowDeformation::Ground - Depth & Mask Copies");
			D3D11_TEXTURE2D_DESC masksDesc{};
			masksRT.texture->GetDesc(&masksDesc);
			const D3D11_BOX masksBox = ScreenRectBox(prevViewportCount ? &prevViewports[0] : nullptr, masksDesc.Width, masksDesc.Height, 1);
			CopySRVResource(masksRT.SRV, "SnowDeformation::LandMasksCopy", landMasksCopyTex, landMasksCopySRV, &masksBox);
			globals::profiler->EndPass();
		} else {
			globals::profiler->MarkPassSkipped("SnowDeformation::Ground - Depth & Mask Copies");
		}
	}

	const auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	const bool keepHidden = !globals::features::terrainBlending.loaded || !globals::features::terrainBlending.settings.Enabled;
	auto* fillVS = shellGround ? GetShellFillVS() : nullptr;
	auto* fillPS = fillVS ? GetShellFillPS() : nullptr;
	auto* psPrepass = (fillPS && !early) ? GetShellPSPrepassAttr(keepHidden) : nullptr;
	const bool ground = fillPS && (early || (psPrepass && LandscapeShadeReady())) && GetShellPSDeferred() &&
	                    EnsurePrepassResources(mainDepth.depthSRV) && EnsureDeferredResources();

	auto& rtData = renderer->GetRuntimeData();
	ID3D11RenderTargetView* rtvs[8] = {
		rtData.renderTargets[RE::RENDER_TARGETS::kMAIN].RTV,
		rtData.renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR].RTV,
		rtData.renderTargets[NORMALROUGHNESS].RTV,
		rtData.renderTargets[ALBEDO].RTV,
		rtData.renderTargets[SPECULAR].RTV,
		rtData.renderTargets[REFLECTANCE].RTV,
		rtData.renderTargets[MASKS].RTV,
		rtData.renderTargets[MASKS2].RTV
	};
	auto dsv = mainDepth.views[0];
	context->OMSetRenderTargets(8, rtvs, dsv);

	context->IASetInputLayout(nullptr);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->RSSetState(shellRasterState.get());
	context->OMSetDepthStencilState(shellDepthState.get(), 0);
	context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);

	ID3D11Buffer* cbs[1] = { shellCB->CB() };
	context->VSSetConstantBuffers(0, 1, cbs);
	context->PSSetConstantBuffers(0, 1, cbs);
	auto state = globals::state;
	ID3D11Buffer* sharedBuffers[3] = { state->permutationCB->CB(), state->sharedDataCB->CB(), state->featureDataCB->CB() };
	context->PSSetConstantBuffers(4, 3, sharedBuffers);
	ID3D11ShaderResourceView* shellSRVs[9] = { shellTerrainTexture->srv.get(), GetDeformationSRV(), shellSnowDiffuseSRV.get(), Util::GetCurrentSceneDepthSRV(false), nullptr, heightBottomFiltered->srv.get(), shellSnowNormalSRV.get(), shellSnowRmaosSRV.get(), shellSnowHeightSRV.get() };
	context->VSSetShaderResources(1, 1, &shellSRVs[1]);
	ID3D11ShaderResourceView* wetSRV = TerrainWetSRV();
	ID3D11ShaderResourceView* fineSRV = (shellFineValid && shellTerrainFine) ? shellTerrainFine->srv.get() : nullptr;
	ID3D11ShaderResourceView* waterSRV = waterVisibleTexture && waterVisibleTexture->srv ? waterVisibleTexture->srv.get() : (waterHeightTexture ? waterHeightTexture->srv.get() : nullptr);
	ID3D11ShaderResourceView* waterSRVs[2] = { waterSRV, waterHeightTexture ? waterHeightTexture->srv.get() : nullptr };
	ID3D11ShaderResourceView* waterNearSRV = waterNearTexture && waterNearTexture->srv ? waterNearTexture->srv.get() : nullptr;
	context->VSSetShaderResources(27, 2, waterSRVs);
	context->VSSetShaderResources(19, 1, &waterNearSRV);
	winrt::com_ptr<ID3D11ShaderResourceView> prevShellSRVs[9];
	for (uint i = 0; i < 9; i++)
		context->PSGetShaderResources(i, 1, prevShellSRVs[i].put());
	winrt::com_ptr<ID3D11ShaderResourceView> prevFrostSlotSRVs[2];
	for (uint i = 0; i < 2; i++)
		context->PSGetShaderResources(16 + i, 1, prevFrostSlotSRVs[i].put());
	context->PSSetShaderResources(0, 9, shellSRVs);
	context->PSSetShaderResources(kTerrainWetSlot, 1, &wetSRV);
	ID3D11ShaderResourceView* bareSRV = shellTerrainBare ? shellTerrainBare->srv.get() : nullptr;
	context->PSSetShaderResources(kTerrainBareSlot, 1, &bareSRV);
	context->PSSetShaderResources(13, 1, &fineSRV);
	context->PSSetShaderResources(27, 2, waterSRVs);
	context->PSSetShaderResources(19, 1, &waterNearSRV);
	ID3D11ShaderResourceView* objectTopSRV = heightTopRaw[heightCurrent]->srv.get();
	context->PSSetShaderResources(11, 1, &objectTopSRV);
	if (landMasksCopySRV) {
		ID3D11ShaderResourceView* landMasksSRV = landMasksCopySRV.get();
		context->PSSetShaderResources(10, 1, &landMasksSRV);
	}
	ID3D11ShaderResourceView* bermSRV = GetBermFieldSRV();
	context->VSSetShaderResources(14, 1, &bermSRV);
	context->PSSetShaderResources(14, 1, &bermSRV);
	ID3D11ShaderResourceView* untouchedSRV = untouchedTilesSRV.get();
	context->PSSetShaderResources(kUntouchedTilesSlot, 1, &untouchedSRV);
	ID3D11ShaderResourceView* undulationSRV = GetUndulationFieldSRV();
	context->VSSetShaderResources(29, 1, &undulationSRV);
	context->PSSetShaderResources(29, 1, &undulationSRV);
	ID3D11ShaderResourceView* noiseSRV = NoiseHashSRV();
	context->PSSetShaderResources(kNoiseHashSlot, 1, &noiseSRV);
	ID3D11ShaderResourceView* trenchDetailSRVs[] = { trenchDetailNormalSRV.get(), trenchDetailHeightSRV.get() };
	context->PSSetShaderResources(kTrenchDetailSlot, ARRAYSIZE(trenchDetailSRVs), trenchDetailSRVs);
	ID3D11ShaderResourceView* bodyReachSRV = bodyReachTexture ? bodyReachTexture->srv.get() : nullptr;
	context->PSSetShaderResources(kBodyReachSlot, 1, &bodyReachSRV);
	ID3D11ShaderResourceView* exclusionSRV = GetExclusionFieldSRV();
	context->VSSetShaderResources(15, 1, &exclusionSRV);
	context->PSSetShaderResources(15, 1, &exclusionSRV);
	if (globals::features::truePBR.glintsNoiseTexture) {
		ID3D11ShaderResourceView* glintSRV = globals::features::truePBR.glintsNoiseTexture->srv.get();
		context->PSSetShaderResources(20, 1, &glintSRV);
	}
	if (globals::features::ibl.loaded && globals::features::ibl.envIBLTexture && globals::features::ibl.skyIBLTexture) {
		ID3D11ShaderResourceView* iblSRVs[2] = { globals::features::ibl.envIBLTexture->srv.get(), globals::features::ibl.skyIBLTexture->srv.get() };
		context->PSSetShaderResources(76, 2, iblSRVs);
	}
	if (globals::features::terrainShadows.loaded && globals::features::terrainShadows.texShadowHeight) {
		ID3D11ShaderResourceView* terrainShadowSRV = globals::features::terrainShadows.texShadowHeight->srv.get();
		context->PSSetShaderResources(60, 1, &terrainShadowSRV);
	}
	winrt::com_ptr<ID3D11ShaderResourceView> prevCloudShadowSRV;
	context->PSGetShaderResources(25, 1, prevCloudShadowSRV.put());
	ID3D11ShaderResourceView* cloudShadowSRV = lightingCloudShadowSRV.get();
	context->PSSetShaderResources(25, 1, &cloudShadowSRV);
	if (cbData.CrispShadows > 0.5f || PointShadowAtlasSRV()) {
		if (!shadowCmpSampler) {
			D3D11_SAMPLER_DESC cmpDesc{};
			cmpDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
			cmpDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			cmpDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			cmpDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			cmpDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
			cmpDesc.MaxLOD = D3D11_FLOAT32_MAX;
			globals::d3d::device->CreateSamplerState(&cmpDesc, shadowCmpSampler.put());
			Util::SetResourceName(shadowCmpSampler.get(), "SnowDeformation::ShadowCmpSampler");
		}
		ID3D11SamplerState* cmpSampler = shadowCmpSampler.get();
		context->PSSetSamplers(2, 1, &cmpSampler);
	}
	if (cbData.CrispShadows > 0.5f) {
		ID3D11ShaderResourceView* shadowAtlasSRV = ShadowAtlasSRV();
		context->PSSetShaderResources(22, 1, &shadowAtlasSRV);
	}
	if (cbData.ScreenSpaceShadowsActive > 0.5f) {
		ID3D11ShaderResourceView* sssSRV = screenSpaceShadowsFeature.screenSpaceShadowsTexture->srv.get();
		context->PSSetShaderResources(45, 1, &sssSRV);
	}
	if (cbData.PointLightsActive > 0.5f) {
		ID3D11ShaderResourceView* lightSRVs[3] = { lightLimitFix.lights->srv.get(), lightLimitFix.lightIndexList->srv.get(), lightLimitFix.lightGrid->srv.get() };
		context->PSSetShaderResources(35, 3, lightSRVs);
		UpdatePointShadowLights();
		if (pointShadowLights) {
			ID3D11ShaderResourceView* pointShadowSRVs[2] = { pointShadowLights->srv.get(), PointShadowAtlasSRV() };
			context->PSSetShaderResources(38, 2, pointShadowSRVs);
		}
	}
	if (cbData.SkylightingActive > 0.5f) {
		ID3D11ShaderResourceView* probeSRV = skylighting.texProbeArray->srv.get();
		context->PSSetShaderResources(50, 1, &probeSRV);
	}

	winrt::com_ptr<ID3D11SamplerState> prevSamplers[2];
	context->PSGetSamplers(0, 1, prevSamplers[0].put());
	context->PSGetSamplers(1, 1, prevSamplers[1].put());
	ID3D11SamplerState* shellSamplers[2] = { shellSnowSampler.get(), shellLinearSampler.get() };
	context->PSSetSamplers(0, 2, shellSamplers);

	const bool earlyMarks = ground && early && shellPSEarlyResolve && shellEarlyPending && shellEarlyMarks;
	if (!ground) {
		globals::profiler->MarkPassSkipped("SnowDeformation::Ground - Shell Shading");
	} else {
		globals::profiler->BeginPass("SnowDeformation::Ground - Shell Shading");
		auto splitRow = [&](const char* a_name) {
			globals::profiler->EndPass();
			globals::profiler->BeginPass(a_name);
		};
		context->DSSetShaderResources(14, 1, &bermSRV);
		context->DSSetShaderResources(15, 1, &exclusionSRV);
		context->DSSetShaderResources(29, 1, &undulationSRV);

		if (!earlyMarks) {
			splitRow("SnowDeformation::Ground - Shell Depth Prepass");
			const float rasterClear[4] = {};
			context->ClearRenderTargetView(shellRasterDepth->rtv.get(), rasterClear);
			ID3D11RenderTargetView* prepassRTVs[4] = { shellRasterDepth->rtv.get(), shellDeferredAttr[0]->rtv.get(), shellDeferredAttr[1]->rtv.get(), shellDeferredAttr[2]->rtv.get() };
			context->OMSetRenderTargets(4, prepassRTVs, dsv);
			context->PSSetShader(psPrepass, nullptr, 0);
			const auto& terrainBlend = globals::features::terrainBlending;
			ID3D11ShaderResourceView* tbMaskSRV = (terrainBlend.loaded && terrainBlend.settings.Enabled) ? terrainBlend.terrainDepth.depthSRV : nullptr;
			context->PSSetShaderResources(kTBMaskSlot, 1, &tbMaskSRV);
			DrawLandscapeSnowPrepass(context, cbData, cbs[0]);
			tbMaskSRV = nullptr;
			context->PSSetShaderResources(kTBMaskSlot, 1, &tbMaskSRV);
			context->OMSetRenderTargets(0, nullptr, shellTestDepthDSV.get());
			context->OMSetDepthStencilState(shellFillDepthState.get(), 0);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(fillVS, nullptr, 0);
			context->HSSetShader(nullptr, nullptr, 0);
			context->DSSetShader(nullptr, nullptr, 0);
			ID3D11ShaderResourceView* rasterSRV = shellRasterDepth->srv.get();
			context->PSSetShaderResources(9, 1, &rasterSRV);
			context->PSSetShader(fillPS, nullptr, 0);
			context->Draw(3, 0);
			ID3D11ShaderResourceView* nullRasterSRV = nullptr;
			context->PSSetShaderResources(9, 1, &nullRasterSRV);
			splitRow("SnowDeformation::Ground - Shell Shading");
		}

		if (earlyMarks) {
			const float marksClear[4] = {};
			context->ClearRenderTargetView(shellEarlyMarks->rtv.get(), marksClear);
			ID3D11RenderTargetView* marksRTV = shellEarlyMarks->rtv.get();
			context->OMSetRenderTargets(1, &marksRTV, dsv);
			context->OMSetDepthStencilState(shellDepthState.get(), 0);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(fillVS, nullptr, 0);
			context->HSSetShader(nullptr, nullptr, 0);
			context->DSSetShader(nullptr, nullptr, 0);
			ID3D11ShaderResourceView* landSRV = landMasksCopySRV.get();
			context->PSSetShaderResources(10, 1, &landSRV);
			ID3D11ShaderResourceView* resolveSRVs[2] = { shellRasterDepth->srv.get(), shellEarlyPending->srv.get() };
			context->PSSetShaderResources(61, 2, resolveSRVs);
			ID3D11ShaderResourceView* attrDepthSRV = shellDeferredAttr[2]->srv.get();
			context->PSSetShaderResources(93, 1, &attrDepthSRV);
			context->PSSetShader(shellPSEarlyResolve.get(), nullptr, 0);
			context->Draw(3, 0);
			ID3D11ShaderResourceView* nullResolveSRVs[2] = {};
			context->PSSetShaderResources(61, 2, nullResolveSRVs);
			context->PSSetShaderResources(93, 1, nullResolveSRVs);
			context->OMSetRenderTargets(0, nullptr, shellTestDepthDSV.get());
			context->OMSetDepthStencilState(shellFillDepthState.get(), 0);
			ID3D11ShaderResourceView* marksSRV = shellEarlyMarks->srv.get();
			context->PSSetShaderResources(9, 1, &marksSRV);
			context->PSSetShader(fillPS, nullptr, 0);
			context->Draw(3, 0);
			context->PSSetShaderResources(9, 1, nullResolveSRVs);
		}
		context->OMSetRenderTargets(8, rtvs, shellTestDepthDSV.get());
		context->OMSetDepthStencilState(shellDeferredDepthState.get(), 0);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(fillVS, nullptr, 0);
		context->HSSetShader(nullptr, nullptr, 0);
		context->DSSetShader(nullptr, nullptr, 0);
		ID3D11ShaderResourceView* attrSRVs[3] = { shellDeferredAttr[0]->srv.get(), shellDeferredAttr[1]->srv.get(), shellDeferredAttr[2]->srv.get() };
		context->PSSetShaderResources(91, 3, attrSRVs);
		context->PSSetShader(shellPSDeferred.get(), nullptr, 0);
		context->Draw(3, 0);
		ID3D11ShaderResourceView* nullAttrSRVs[3] = {};
		context->PSSetShaderResources(91, 3, nullAttrSRVs);
		context->OMSetRenderTargets(8, rtvs, dsv);
		context->OMSetDepthStencilState(shellDepthState.get(), 0);
		globals::profiler->EndPass();
	}
	const bool rasterMarks = ground && shellRasterDepth && shellRasterDepth->srv;
	shellGroundMarksSRV = rasterMarks ? (earlyMarks ? shellEarlyMarks->srv.get() : shellRasterDepth->srv.get()) : nullptr;
	CreateCaptureLayouts();

	{
		ID3D11ShaderResourceView* nullDSSRVs[24] = {};
		context->DSSetShaderResources(0, 24, nullDSSRVs);
		context->HSSetShaderResources(0, 24, nullDSSRVs);
		context->DSSetShaderResources(kTerrainWetSlot, 1, nullDSSRVs);
		context->HSSetShaderResources(kTerrainWetSlot, 1, nullDSSRVs);
		ID3D11Buffer* nullStageCB = nullptr;
		context->DSSetConstantBuffers(0, 1, &nullStageCB);
		context->HSSetConstantBuffers(0, 1, &nullStageCB);
		ID3D11SamplerState* nullDSSampler = nullptr;
		context->DSSetSamplers(0, 1, &nullDSSampler);
	}
	context->VSSetShader(nullptr, nullptr, 0);
	context->PSSetShader(nullptr, nullptr, 0);
	ID3D11Buffer* nullCB = nullptr;
	context->VSSetConstantBuffers(0, 1, &nullCB);
	context->PSSetConstantBuffers(0, 1, &nullCB);
	ID3D11ShaderResourceView* nullSRVs[16] = {};
	context->VSSetShaderResources(0, 16, nullSRVs);
	context->PSSetShaderResources(0, 16, nullSRVs);
	context->PSSetShaderResources(kTerrainWetSlot, 1, nullSRVs);
	ID3D11ShaderResourceView* nullShadowSRVs[4] = { nullptr, nullptr, nullptr, nullptr };
	context->PSSetShaderResources(20, 4, nullShadowSRVs);
	ID3D11ShaderResourceView* restoreShellSRVs[9];
	for (uint i = 0; i < 9; i++)
		restoreShellSRVs[i] = prevShellSRVs[i].get();
	context->PSSetShaderResources(0, 9, restoreShellSRVs);
	ID3D11ShaderResourceView* nullFinePS[2] = {};
	context->PSSetShaderResources(13, 1, nullFinePS);
	context->PSSetShaderResources(27, 2, nullFinePS);
	context->PSSetShaderResources(19, 1, nullFinePS);
	context->PSSetShaderResources(24, 1, &nullFinePS[0]);
	context->PSSetShaderResources(kUntouchedTilesSlot, 1, &nullFinePS[0]);
	context->PSSetShaderResources(kBodyReachSlot, 1, &nullFinePS[0]);
	context->PSSetShaderResources(26, 1, &nullFinePS[1]);
	UnbindMarchCaches();
	ID3D11SamplerState* restoreSamplers[2] = { prevSamplers[0].get(), prevSamplers[1].get() };
	context->PSSetSamplers(0, 2, restoreSamplers);
	ID3D11SamplerState* nullCmpSampler = nullptr;
	context->PSSetSamplers(2, 1, &nullCmpSampler);
	context->OMSetRenderTargets(0, nullptr, nullptr);
	context->RSSetState(prevRaster.get());
	context->OMSetDepthStencilState(prevDepth.get(), prevStencilRef);
	context->OMSetBlendState(prevBlend.get(), prevBlendFactor, prevSampleMask);
	context->IASetPrimitiveTopology(prevTopology);
	context->IASetIndexBuffer(prevIB.get(), prevIBFormat, prevIBOffset);
	if (prevViewportCount)
		context->RSSetViewports(prevViewportCount, prevViewports);
	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);

	auto& tb = globals::features::terrainBlending;
	const bool syncable = rasterMarks;
	if (!syncable)
		globals::profiler->MarkPassSkipped("SnowDeformation::Ground - Depth & Mask Copies");
	if (syncable && tb.loaded && tb.settings.Enabled && tb.blendedDepthTexture && tb.blendedDepthTexture16) {
		if (auto cs = GetSnowCS(kDepthSyncCS); cs && DepthSyncRectBuffer()) {
			auto mainDepthSRV = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].depthSRV;
			ID3D11UnorderedAccessView* syncUAVs[2] = { tb.blendedDepthTexture->uav.get(), tb.blendedDepthTexture16->uav.get() };
			ID3D11ShaderResourceView* syncSRVs[3] = { mainDepthSRV,
				shellGroundMarksSRV,
				nullptr };
			const auto& depthDesc = tb.blendedDepthTexture->desc;
			const D3D11_BOX rect = ScreenRectBox(prevViewportCount ? &prevViewports[0] : nullptr, depthDesc.Width, depthDesc.Height, 0);
			DepthSyncRectCB rectData{ { rect.left, rect.top }, { rect.right - rect.left, rect.bottom - rect.top } };
			depthSyncRectCB->Update(rectData);
			ID3D11Buffer* rectCB = depthSyncRectCB->CB();
			context->CSSetConstantBuffers(2, 1, &rectCB);
			context->CSSetShaderResources(0, 3, syncSRVs);
			context->CSSetUnorderedAccessViews(0, 2, syncUAVs, nullptr);
			context->CSSetShader(cs, nullptr, 0);
			globals::profiler->BeginPass("SnowDeformation::Ground - Depth & Mask Copies");
			context->Dispatch((rectData.Extent[0] + 7) / 8, (rectData.Extent[1] + 7) / 8, 1);
			globals::profiler->EndPass();

			ID3D11ShaderResourceView* nullSyncSRVs[3] = { nullptr, nullptr, nullptr };
			ID3D11UnorderedAccessView* nullSyncUAVs[2] = { nullptr, nullptr };
			ID3D11Buffer* nullSyncCB = nullptr;
			context->CSSetShaderResources(0, 3, nullSyncSRVs);
			context->CSSetUnorderedAccessViews(0, 2, nullSyncUAVs, nullptr);
			context->CSSetConstantBuffers(2, 1, &nullSyncCB);
			context->CSSetShader(nullptr, nullptr, 0);
		}
	}
	ID3D11ShaderResourceView* restoreFrostSlotSRVs[2] = { prevFrostSlotSRVs[0].get(), prevFrostSlotSRVs[1].get() };
	context->PSSetShaderResources(16, 2, restoreFrostSlotSRVs);
	ID3D11ShaderResourceView* restoreCloudShadowSRV = prevCloudShadowSRV.get();
	context->PSSetShaderResources(25, 1, &restoreCloudShadowSRV);
}
