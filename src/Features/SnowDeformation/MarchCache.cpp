#include "Features/SnowDeformation.h"

#include "Globals.h"
#include "State.h"
#include "Utils/D3D.h"

namespace
{
	constexpr uint32_t kMarchCacheRows = 32;
	constexpr uint32_t kMarchCacheFastRows = 256;
	constexpr uint32_t kMarchFarRows = 36;
	constexpr uint32_t kMarchFarFastRows = 288;
	constexpr uint32_t kMarchSunRows = 32;
	constexpr uint32_t kMarchSunFastRows = 256;
	constexpr UINT kMarchTilesSlot = 63;
	constexpr UINT kMarchSlot = 106;
	constexpr UINT kMarchSunSlot = 100;
	constexpr float kMarchSunFastTurn = 0.99985f;
	constexpr float kMarchSunFullTurn = 0.99619f;

	bool MarchSunDirection(float (&a_dir)[3])
	{
		auto* smState = globals::game::smState;
		auto* node = smState ? smState->shadowSceneNode[0] : nullptr;
		auto* sunLight = node ? node->GetRuntimeData().sunLight : nullptr;
		auto* dirLight = sunLight ? skyrim_cast<RE::NiDirectionalLight*>(sunLight->light.get()) : nullptr;
		if (!dirLight)
			return false;
		const auto& d = dirLight->GetWorldDirection();
		const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
		if (len < 1e-6f)
			return false;
		a_dir[0] = -d.x / len;
		a_dir[1] = -d.y / len;
		a_dir[2] = -d.z / len;
		return true;
	}
}

ID3D11ComputeShader* SnowDeformation::GetMarchHeightCS(MarchCS a_which)
{
	static constexpr const char* entries[kMarchCSCount] = { "MarchHeightTilesCS", "MarchHeightRowsCS", "MarchHeightFarRowsCS", "MarchSunTilesCS", "MarchSunRowsCS" };
	auto*& slot = marchHeightCS[a_which];
	if (!slot) {
		const bool sun = a_which == kSunTilesCS || a_which == kSunRowsCS;
		slot = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\SnowDeformation\\SnowShell.hlsl", { { sun ? "SNOW_MARCH_SUN_CS" : "SNOW_MARCH_HEIGHT_CS", "" } }, "cs_5_0", entries[a_which]));
	}
	return slot;
}

void SnowDeformation::ReleaseMarchCache()
{
	for (auto*& cs : marchHeightCS) {
		if (cs)
			cs->Release();
		cs = nullptr;
	}
	marchCacheValid = false;
	marchFarValid = false;
	marchSunValid = false;
}

void SnowDeformation::UnbindMarchCaches()
{
	ID3D11ShaderResourceView* nullSRVs[3] = {};
	globals::d3d::context->PSSetShaderResources(kMarchSlot, 3, nullSRVs);
	globals::d3d::context->PSSetShaderResources(kMarchSunSlot, 1, nullSRVs);
}

namespace
{
	Texture2D* EnsureMarchTexture(Texture2D*& a_tex, uint32_t a_dim, const char* a_name, bool& a_created)
	{
		if (a_tex && a_tex->desc.Width == a_dim)
			return a_tex;
		delete std::exchange(a_tex, nullptr);
		a_tex = SD_MakeTexture2D(a_dim, a_dim, DXGI_FORMAT_R32_FLOAT, a_name);
		a_created = true;
		return a_tex->srv && a_tex->uav ? a_tex : nullptr;
	}

	uint32_t MarchRows(bool a_full, uint32_t a_dim, uint32_t a_slow, uint32_t a_fast, uint32_t& a_fastLeft, uint32_t& a_cursor, uint32_t& a_start)
	{
		uint32_t rows = a_full ? a_dim : (a_fastLeft > 0 ? std::min(a_fast, a_fastLeft) : a_slow);
		rows = std::min(rows, a_dim);
		a_fastLeft -= std::min(a_fastLeft, rows);
		a_start = a_full ? 0u : a_cursor;
		if (!a_full)
			a_cursor = (a_cursor + rows) % a_dim;
		return rows;
	}
}

bool SnowDeformation::UpdateMarchCaches(const ShellCB& a_cb)
{
	auto* deform = deformationTextures[0];
	if (!deform || !deform->srv || !bermFieldTexture || !bermFieldTexture->srv || !bermTileSRV || !bermArgsBuffer || !shellTerrainTexture || !shellTerrainTexture->srv ||
		!heightBottomFiltered) {
		marchCacheValid = false;
		marchFarValid = false;
		return false;
	}
	for (uint32_t i = 0; i < kMarchCSCount; i++)
		if (!GetMarchHeightCS(MarchCS(i)))
			return false;
	const bool farCache = shellFineValid && shellTerrainFine && a_cb.FineWindow.z > 0.5f;

	const uint32_t dim = deform->desc.Width;
	bool created = false;
	if (!EnsureMarchTexture(marchHeight[0], dim, "SnowDeformation::MarchHeightGround", created))
		return false;
	if (created)
		marchCacheValid = false;
	bool farCreated = false;
	if (farCache && !EnsureMarchTexture(marchHeight[2], kShellFineDim, "SnowDeformation::MarchHeightFar", farCreated))
		return false;
	if (farCreated)
		marchFarValid = false;
	bool sunCreated = false;
	if (!EnsureMarchTexture(marchSunEnvelope, dim, "SnowDeformation::MarchSunEnvelope", sunCreated))
		return false;
	if (sunCreated)
		marchSunValid = false;
	float sunNow[3];
	if (!shellSnowSampler || !MarchSunDirection(sunNow))
		return false;
	if (!marchCacheCB)
		marchCacheCB = new ConstantBuffer(ConstantBufferDesc<MarchCacheCB>(), "SnowDeformation::MarchCacheCB");

	uint64_t key = 1469598103934665603ull;
	auto mix = [&](const void* a_p, size_t a_n) {
		const auto* b = static_cast<const uint8_t*>(a_p);
		for (size_t n = 0; n < a_n; n++) {
			key ^= b[n];
			key *= 1099511628211ull;
		}
	};
	const float keyFloats[] = { a_cb.DeformInvWorldSize, a_cb.UndulationAmp, a_cb.UndulationScale, settings.BermHeight, settings.RimLip,
		a_cb.BermBakeActive, a_cb.ObjectLiftCap > 0.0f ? 1.0f : 0.0f, a_cb.ExclusionFieldWindow.w, a_cb.FineWindow.z,
		a_cb.ObjectHeightHalfExtent, a_cb.FieldDims.x, a_cb.FieldDims.y, a_cb.FieldDims.z, a_cb.FieldDims.w,
		settings.FreshSnowSoftness, settings.SnowClimateStrength, settings.TrackAgeing };
	mix(keyFloats, sizeof(keyFloats));
	mix(&dim, sizeof(dim));
	const bool keyChanged = key != marchCacheKey;
	marchCacheKey = key;
	const bool terrainMoved = shellTerrainVersion != marchCacheTerrainVersion;
	marchCacheTerrainVersion = shellTerrainVersion;

	const bool missedBake = bermBakeSeq > marchCacheBermSeq + 1;
	const bool full = !marchCacheValid || keyChanged || missedBake;
	const bool tiles = !full && bermBakeSeq == marchCacheBermSeq + 1;
	marchCacheBermSeq = bermBakeSeq;
	const bool fineMoved = shellFineOriginX != marchCacheFineOrigin[0] || shellFineOriginY != marchCacheFineOrigin[1];
	const bool tracksAged = std::abs(a_cb.TrackAge.x - marchCacheAgeingClock) > 2.0f;
	marchCacheAgeingClock = a_cb.TrackAge.x;
	if (full)
		marchCacheFastRows = 0;
	else if (terrainMoved || fineMoved || tracksAged)
		marchCacheFastRows = dim;
	uint32_t nearStart = 0;
	const uint32_t nearRows = MarchRows(full, dim, kMarchCacheRows, kMarchCacheFastRows, marchCacheFastRows, marchCacheRowCursor, nearStart);

	const bool farFull = farCache && (!marchFarValid || keyChanged || fineMoved);
	if (farCache && !farFull && terrainMoved)
		marchFarFastRows = kShellFineDim;
	uint32_t farStart = 0;
	const uint32_t farRows = farCache ? MarchRows(farFull, kShellFineDim, kMarchFarRows, kMarchFarFastRows, marchFarFastRows, marchFarRowCursor, farStart) : 0;
	marchCacheFineOrigin[0] = shellFineOriginX;
	marchCacheFineOrigin[1] = shellFineOriginY;

	const float sunDot = sunNow[0] * marchSunDir[0] + sunNow[1] * marchSunDir[1] + sunNow[2] * marchSunDir[2];
	const bool sunFull = full || !marchSunValid || sunDot < kMarchSunFullTurn;
	const bool sunTurned = !sunFull && sunDot < kMarchSunFastTurn;
	if (sunFull || sunTurned || marchSunRowCursor == 0)
		std::copy(std::begin(sunNow), std::end(sunNow), marchSunDir);
	if (sunFull)
		marchSunFastRows = 0;
	else if (sunTurned || terrainMoved || fineMoved || tracksAged)
		marchSunFastRows = dim;
	uint32_t sunStart = 0;
	const uint32_t sunRows = MarchRows(sunFull, dim, kMarchSunRows, kMarchSunFastRows, marchSunFastRows, marchSunRowCursor, sunStart);
	const float sunLen2D = std::max(std::hypot(marchSunDir[0], marchSunDir[1]), 1e-4f);
	const float4 sunCB = { marchSunDir[0] / sunLen2D, marchSunDir[1] / sunLen2D, std::min(marchSunDir[2] / sunLen2D, 100.0f), 0.0f };

	auto context = globals::d3d::context;
	globals::profiler->BeginPass("SnowDeformation::Ground - March Cache");
	ID3D11Buffer* cbs[3] = { shellCB->CB(), nullptr, marchCacheCB->CB() };
	ID3D11ShaderResourceView* tileSRV = bermTileSRV.get();
	context->CSSetShaderResources(kMarchTilesSlot, 1, &tileSRV);
	ID3D11ShaderResourceView* deformSRV = deform->srv.get();
	ID3D11ShaderResourceView* bermSRV = bermFieldTexture->srv.get();
	ID3D11ShaderResourceView* terrainSRV = shellTerrainTexture->srv.get();
	auto rowsDispatch = [&](MarchCS a_cs, uint32_t a_dim, uint32_t a_start, uint32_t a_rows) {
		MarchCacheCB band{};
		band.RowStart = a_start;
		band.RowCount = a_rows;
		band.Sun = sunCB;
		marchCacheCB->Update(band);
		context->CSSetShader(marchHeightCS[a_cs], nullptr, 0);
		context->Dispatch((a_dim + 7) / 8, (a_rows + 7) / 8, 1);
	};
	auto setUAV = [&](Texture2D* a_tex) {
		ID3D11UnorderedAccessView* uav = a_tex->uav.get();
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	};

	context->CSSetConstantBuffers(0, 3, cbs);
	ID3D11ShaderResourceView* wetSRV = TerrainWetSRV();
	ID3D11ShaderResourceView* fineSRV = (shellFineValid && shellTerrainFine) ? shellTerrainFine->srv.get() : nullptr;
	ID3D11ShaderResourceView* bottomsSRV = heightBottomFiltered->srv.get();
	ID3D11ShaderResourceView* exclusionSRV = exclusionFieldTexture ? exclusionFieldTexture->srv.get() : nullptr;
	context->CSSetShaderResources(0, 1, &terrainSRV);
	context->CSSetShaderResources(1, 1, &deformSRV);
	context->CSSetShaderResources(5, 1, &bottomsSRV);
	context->CSSetShaderResources(13, 1, &fineSRV);
	context->CSSetShaderResources(14, 1, &bermSRV);
	context->CSSetShaderResources(15, 1, &exclusionSRV);
	context->CSSetShaderResources(kTerrainWetSlot, 1, &wetSRV);
	setUAV(marchHeight[0]);
	if (tiles) {
		context->CSSetShader(marchHeightCS[kGroundTilesCS], nullptr, 0);
		context->DispatchIndirect(bermArgsBuffer.get(), 0);
	}
	rowsDispatch(kGroundRowsCS, dim, nearStart, nearRows);
	if (farCache) {
		setUAV(marchHeight[2]);
		rowsDispatch(kFarRowsCS, kShellFineDim, farStart, farRows);
	}

	ID3D11ShaderResourceView* groundSRV = marchHeight[0]->srv.get();
	ID3D11SamplerState* sunSampler = shellSnowSampler.get();
	setUAV(marchSunEnvelope);
	context->CSSetShaderResources(kMarchSlot, 1, &groundSRV);
	context->CSSetSamplers(0, 1, &sunSampler);
	if (tiles) {
		MarchCacheCB sunBand{};
		sunBand.Sun = sunCB;
		marchCacheCB->Update(sunBand);
		context->CSSetShader(marchHeightCS[kSunTilesCS], nullptr, 0);
		context->DispatchIndirect(bermArgsBuffer.get(), 0);
	}
	rowsDispatch(kSunRowsCS, dim, sunStart, sunRows);

	ID3D11UnorderedAccessView* nullUAV = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	ID3D11ShaderResourceView* nullSRVs[16] = {};
	context->CSSetShaderResources(0, 16, nullSRVs);
	context->CSSetShaderResources(kTerrainWetSlot, 1, nullSRVs);
	context->CSSetShaderResources(kMarchTilesSlot, 1, nullSRVs);
	context->CSSetShaderResources(kMarchSlot, 1, nullSRVs);
	ID3D11SamplerState* nullSampler = nullptr;
	context->CSSetSamplers(0, 1, &nullSampler);
	ID3D11Buffer* nullCBs[3] = {};
	context->CSSetConstantBuffers(0, 3, nullCBs);
	context->CSSetShader(nullptr, nullptr, 0);
	globals::profiler->EndPass();

	marchCacheValid = true;
	marchFarValid = farCache;
	marchSunValid = true;
	ID3D11ShaderResourceView* cacheSRVs[3] = { marchHeight[0]->srv.get(), nullptr, farCache ? marchHeight[2]->srv.get() : nullptr };
	context->PSSetShaderResources(kMarchSlot, 3, cacheSRVs);
	ID3D11ShaderResourceView* sunSRV = marchSunEnvelope->srv.get();
	context->PSSetShaderResources(kMarchSunSlot, 1, &sunSRV);
	return true;
}
