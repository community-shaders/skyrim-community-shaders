#include "Features/SnowDeformation.h"

#include "Deferred.h"
#include "Features/Skylighting.h"
#include "Features/TerrainBlending.h"
#include "Globals.h"
#include "ShaderCache.h"
#include "State.h"
#include "Utils/D3D.h"
#include "Utils/Game.h"

#include <future>

namespace
{
	enum : uint32_t
	{
		kLsMain,
		kLsLightingOther,
		kLsShadowSun,
		kLsShadowLocal,
		kLsDepth,
		kLsUtilityOther,
		kLsSkyOcclusion,
		kLsCategories
	};
	constexpr const char* kLsNames[kLsCategories] = { "mainDeferred", "lightingOther", "shadowSun", "shadowLocal", "depthPrepass", "utilityOther", "skyOcclusion" };
	constexpr uint32_t kLsMaxTargets = 8;
	constexpr uint32_t kLsSlots = 6;
	constexpr uint32_t kLsMaxDraws = 1024;
	constexpr uint32_t kLsSRVCount = SnowDeformation::kShellSurfaceSRVs;

	bool LsShadowPass(uint32_t a_cat) { return a_cat == kLsShadowSun || a_cat == kLsShadowLocal || a_cat == kLsSkyOcclusion; }

	struct LsConfig
	{
		bool tessellate = true;
		bool gate = true;
		int gateMask = 23;
		bool cull = true;
		bool measure = false;
		bool stats = false;
		float maxFactor = 64.0f;
		float guard = 0.05f;
		float lift = 512.0f;
		float tessReach = 2600.0f;
		float shadowTessReach = 2000.0f;
		float blendBonus = 6.0f;
		float blendPenalty = 16.0f;
		bool culledLand = true;
	};

	constexpr uint32_t kLsMaxSmallExclusions = 16;

	struct alignas(16) LsCBData
	{
		float geometry[4];
		float tess[4];
		float cull[4];
		float exclusion[kLsMaxSmallExclusions][4];
		float exclusionAxis[kLsMaxSmallExclusions][4];
	};
	STATIC_ASSERT_ALIGNAS_16(LsCBData);

	constexpr float kLsSmallExclusionReach = 100.0f;

	int LsFillSmallExclusions(LsCBData& a_data, const std::vector<std::pair<float4, float4>>& a_exclusions, float2 a_boundXY, float a_boundRadius, float2 a_gridOrigin)
	{
		uint32_t n = 0;
		bool overflow = false;
		for (const auto& [posRadius, dirExtType] : a_exclusions) {
			const bool sealed = dirExtType.w > 2.5f;
			const float reach = 1.4f * std::max(posRadius.w, 1.0f) * (1.0f + std::sqrt(dirExtType.x * dirExtType.x + dirExtType.y * dirExtType.y)) +
			                    1.2f * std::max(std::abs(dirExtType.z), 1.0f);
			if (!sealed && reach >= kLsSmallExclusionReach)
				continue;
			const float dx = posRadius.x - a_boundXY.x;
			const float dy = posRadius.y - a_boundXY.y;
			const float r = reach + a_boundRadius + 8.0f;
			if (dx * dx + dy * dy > r * r)
				continue;
			if (n == kLsMaxSmallExclusions) {
				a_data.tess[2] = 0.0f;
				n = 0;
				overflow = true;
				break;
			}
			auto& c = a_data.exclusion[n];
			auto& axis = a_data.exclusionAxis[n];
			c[0] = float(double(posRadius.x) - double(a_gridOrigin.x));
			c[1] = float(double(posRadius.y) - double(a_gridOrigin.y));
			c[2] = sealed ? posRadius.w : reach;
			c[3] = sealed ? dirExtType.z : reach;
			axis[0] = sealed ? dirExtType.y : 1.0f;
			axis[1] = sealed ? -dirExtType.x : 0.0f;
			axis[2] = 0.0f;
			axis[3] = 0.0f;
			n++;
		}
		a_data.cull[2] = float(n);
		return overflow ? -1 : int(n);
	}

	struct LsShaders
	{
		winrt::com_ptr<ID3D11VertexShader> vsRaise;
		winrt::com_ptr<ID3D11VertexShader> vs;
		winrt::com_ptr<ID3D11HullShader> hs;
		winrt::com_ptr<ID3D11DomainShader> ds;
		std::string defines;
		double compileMs = 0.0;
	};

	struct LsEntry
	{
		int state = 0;
		std::future<LsShaders> pending;
		LsShaders shaders;
		uint64_t tessAttaches = 0;
		uint64_t raiseAttaches = 0;
	};

	struct LsSlot
	{
		bool open = false;
		bool pendingRead = false;
		winrt::com_ptr<ID3D11Query> disjoint;
		std::vector<winrt::com_ptr<ID3D11Query>> ts;
		std::vector<winrt::com_ptr<ID3D11Query>> stats;
		std::array<uint8_t, kLsMaxDraws> cat{};
		std::array<uint8_t, kLsMaxDraws> hasStats{};
		std::array<uint8_t, kLsMaxDraws> target{};
		uint32_t drawCount = 0;
		uint32_t overflow = 0;
		uint32_t draws[kLsCategories]{};
		uint32_t tess[kLsCategories]{};
		uint32_t raise[kLsCategories]{};
		uint64_t tris[kLsCategories]{};
		uint32_t targetDraws[kLsMaxTargets]{};
		uint64_t targetTris[kLsMaxTargets]{};
	};

	struct LsTarget
	{
		void* texture = nullptr;
		uint32_t slice = 0;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t arraySize = 0;
		uint32_t category = 0;
	};

	struct LsTotals
	{
		uint64_t frames = 0;
		uint64_t lost = 0;
		uint64_t disjoint = 0;
		uint64_t overflow = 0;
		double gpuMs[kLsCategories]{};
		uint64_t draws[kLsCategories]{};
		uint64_t tess[kLsCategories]{};
		uint64_t raise[kLsCategories]{};
		uint64_t tris[kLsCategories]{};
		uint64_t statFrames = 0;
		uint64_t ia[kLsCategories]{};
		uint64_t vs[kLsCategories]{};
		uint64_t hs[kLsCategories]{};
		uint64_t ds[kLsCategories]{};
		uint64_t cinv[kLsCategories]{};
		uint64_t cprim[kLsCategories]{};
		double targetMs[kLsMaxTargets]{};
		uint64_t targetDraws[kLsMaxTargets]{};
		uint64_t targetTris[kLsMaxTargets]{};
	};

	enum class LsMode : uint8_t
	{
		kNone,
		kTess,
		kRaise
	};

	struct LsCapture
	{
		const void* geometry = nullptr;
		winrt::com_ptr<ID3D11InputLayout> layout;
		winrt::com_ptr<ID3D11Buffer> vb;
		UINT stride = 0;
		UINT vbOffset = 0;
		winrt::com_ptr<ID3D11Buffer> ib;
		DXGI_FORMAT ibFormat = DXGI_FORMAT_UNKNOWN;
		UINT ibOffset = 0;
		UINT indexCount = 0;
		double translate[3]{};
		float2 boundXY{};
		float boundRadius = 0.0f;
		uint64_t vertexDesc = 0;
	};

	struct LssShaders
	{
		winrt::com_ptr<ID3D11VertexShader> vs;
		winrt::com_ptr<ID3D11HullShader> hs;
		winrt::com_ptr<ID3D11DomainShader> ds;
		double compileMs = 0.0;
	};

	struct LsExclCounts
	{
		uint32_t draws = 0;
		uint32_t rects = 0;
		uint32_t overflow = 0;
		void Add(int a_filled)
		{
			if (a_filled < 0)
				overflow++;
			else if (a_filled > 0) {
				draws++;
				rects += uint32_t(a_filled);
			}
		}
	};

	struct LsShadeCounts
	{
		uint32_t captures = 0;
		uint32_t mainDraws = 0;
		uint32_t mainMissed = 0;
		uint32_t replayCalls = 0;
		uint32_t replayDraws = 0;
		uint32_t replayTess = 0;
		uint32_t replayCulled = 0;
		LsExclCounts replayExcl;
		LsExclCounts raiseExcl;
	};

	struct LsState
	{
		LsConfig cfg;
		std::unordered_map<uint64_t, LsEntry> entries;
		std::unique_ptr<ConstantBuffer> drawCB;
		std::unique_ptr<ConstantBuffer> shellCB;
		LsSlot slots[kLsSlots];
		uint32_t slotIndex = 0;
		uint32_t lastFrame = UINT32_MAX;
		LsTotals totals;
		int curDraw = -1;
		LsTarget targets[kLsMaxTargets];
		uint32_t targetCount = 0;

		uint32_t cbFrame = UINT32_MAX;
		uint32_t cbCategory = UINT32_MAX;
		bool cbValid = false;
		ID3D11ShaderResourceView* srvs[kLsSRVCount]{};
		ID3D11SamplerState* sampler = nullptr;
		float4 liveRect{};
		float4 camUsed[kLsCategories]{};
		float4 camFrame[kLsCategories]{};

		LsMode mode = LsMode::kNone;
		winrt::com_ptr<ID3D11VertexShader> savedVS;
		ID3D11ShaderResourceView* savedVSSRVs[kLsSRVCount]{};
		ID3D11Buffer* savedVSCB[2]{};

		std::vector<LsCapture> captures;
		std::unordered_set<const void*> captured;
		uint32_t captureFrame = UINT32_MAX;
		bool capturePending = false;
		LsCapture pendingCapture;
		int shadeState = 0;
		std::future<LssShaders> shadePending;
		LssShaders shade;
		LsShadeCounts shadeFrame;
		LsShadeCounts shadeLast;
		uint64_t shadeFallbacks = 0;
	};

	LsState ls;
	SnowDeformation::ShellCB lsCB{};
	std::mutex lsMutex;

	LsShaders LsCompile(std::string a_defines)
	{
		LsShaders out;
		out.defines = a_defines;
		const auto t0 = std::chrono::steady_clock::now();
		std::vector<std::pair<std::string, std::string>> parsed;
		std::istringstream ss(a_defines);
		std::string tok;
		while (ss >> tok) {
			const auto eq = tok.find('=');
			parsed.emplace_back(tok.substr(0, eq), eq == std::string::npos ? std::string() : tok.substr(eq + 1));
		}
		std::vector<std::pair<const char*, const char*>> defs;
		for (const auto& [k, v] : parsed)
			defs.emplace_back(k.c_str(), v.c_str());
		const wchar_t* path = L"Data\\Shaders\\SnowDeformation\\LandscapeSnow.hlsl";
		out.vsRaise.attach(static_cast<ID3D11VertexShader*>(Util::CompileShader(path, defs, "vs_5_0", "LS_VSRaise")));
		out.vs.attach(static_cast<ID3D11VertexShader*>(Util::CompileShader(path, defs, "vs_5_0", "LS_VS")));
		out.hs.attach(static_cast<ID3D11HullShader*>(Util::CompileShader(path, defs, "hs_5_0", "LS_HS")));
		out.ds.attach(static_cast<ID3D11DomainShader*>(Util::CompileShader(path, defs, "ds_5_0", "LS_DS")));
		out.compileMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		return out;
	}

	LssShaders LssCompile()
	{
		LssShaders out;
		const auto t0 = std::chrono::steady_clock::now();
		const wchar_t* path = L"Data\\Shaders\\SnowDeformation\\LandscapeSnowShell.hlsl";
		std::vector<std::pair<const char*, const char*>> defs;
		out.vs.attach(static_cast<ID3D11VertexShader*>(Util::CompileShader(path, defs, "vs_5_0")));
		out.hs.attach(static_cast<ID3D11HullShader*>(Util::CompileShader(path, defs, "hs_5_0")));
		out.ds.attach(static_cast<ID3D11DomainShader*>(Util::CompileShader(path, defs, "ds_5_0")));
		out.compileMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		return out;
	}

	bool LssPoll()
	{
		if (ls.shadeState == 0) {
			ls.shadePending = std::async(std::launch::async, LssCompile);
			ls.shadeState = 1;
		}
		if (ls.shadeState == 1 && ls.shadePending.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
			ls.shade = ls.shadePending.get();
			ls.shadeState = (ls.shade.vs && ls.shade.hs && ls.shade.ds) ? 2 : 3;
			if (ls.shadeState == 2)
				logger::info("[SNOW DEFORMATION] Snow on Landscape: replay shaders ready in {:.0f} ms", ls.shade.compileMs);
			else
				logger::warn("[SNOW DEFORMATION] Snow on Landscape: LandscapeSnowShell.hlsl failed to compile, no ground snow draws");
		}
		return ls.shadeState == 2;
	}

	void LsShadeTrack(RE::BSRenderPass* a_pass, uint32_t a_cat, uint32_t a_frame)
	{
		if (a_frame != ls.captureFrame) {
			ls.captures.clear();
			ls.captured.clear();
			ls.captureFrame = a_frame;
		}
		const void* geometry = a_pass->geometry;
		if (a_cat == kLsMain) {
			ls.shadeFrame.mainDraws++;
			if (!ls.captured.contains(geometry))
				ls.shadeFrame.mainMissed++;
			return;
		}
		auto* triShape = a_pass->geometry->AsTriShape();
		if (!triShape || ls.captured.contains(geometry))
			return;
		const auto& world = a_pass->geometry->world;
		if (std::abs(world.scale - 1.0f) > 1e-4f)
			return;
		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 3; c++)
				if (std::abs(world.rotate.entry[r][c] - (r == c ? 1.0f : 0.0f)) > 1e-4f)
					return;
		auto& cap = ls.pendingCapture;
		cap = {};
		cap.geometry = geometry;
		cap.indexCount = uint32_t(triShape->GetTrishapeRuntimeData().triangleCount) * 3;
		cap.translate[0] = world.translate.x;
		cap.translate[1] = world.translate.y;
		cap.translate[2] = world.translate.z;
		const auto& b = a_pass->geometry->worldBound;
		cap.boundXY = { b.center.x, b.center.y };
		cap.boundRadius = b.radius;
		if (auto* rendererData = triShape->GetGeometryRuntimeData().rendererData)
			std::memcpy(&cap.vertexDesc, &rendererData->vertexDesc, sizeof(cap.vertexDesc));
		ls.capturePending = cap.indexCount > 0;
	}

	void LsShadeCapture()
	{
		ls.capturePending = false;
		auto* context = globals::d3d::context;
		auto& cap = ls.pendingCapture;
		context->IAGetInputLayout(cap.layout.put());
		context->IAGetVertexBuffers(0, 1, cap.vb.put(), &cap.stride, &cap.vbOffset);
		context->IAGetIndexBuffer(cap.ib.put(), &cap.ibFormat, &cap.ibOffset);
		if (cap.layout && cap.vb && cap.ib && cap.stride) {
			ls.captured.insert(cap.geometry);
			ls.captures.push_back(std::move(cap));
		}
		cap = {};
	}

	float4 LsLiveRect(const SnowDeformation::ShellCB& a_cb)
	{
		const float2 spanLo = a_cb.GridOrigin;
		const float2 spanHi = { a_cb.GridOrigin.x + 2.0f * a_cb.WarpedHalfSpan, a_cb.GridOrigin.y + 2.0f * a_cb.WarpedHalfSpan };
		if (a_cb.SeamRampInv > 0.0f)
			return { std::max(spanLo.x, a_cb.SeamBounds.x), std::max(spanLo.y, a_cb.SeamBounds.y), std::min(spanHi.x, a_cb.SeamBounds.z), std::min(spanHi.y, a_cb.SeamBounds.w) };
		return { spanLo.x, spanLo.y, spanHi.x, spanHi.y };
	}

	using LsViewPlanes = std::array<std::array<double, 4>, 5>;

	LsViewPlanes LsOffViewPlanes(const SnowDeformation::ShellCB& a_cb, float a_guard)
	{
		const auto& m = a_cb.CameraViewProj;
		const double k = 1.0 + double(a_guard);
		const int rows[5] = { 0, 0, 1, 1, 2 };
		const double signs[5] = { 1.0, -1.0, 1.0, -1.0, 1.0 };
		const double wScale[5] = { k, k, k, k, 0.0 };
		LsViewPlanes planes;
		for (int i = 0; i < 5; i++) {
			for (int c = 0; c < 4; c++)
				planes[i][c] = wScale[i] * double(m.m[3][c]) + signs[i] * double(m.m[rows[i]][c]);
			planes[i][3] -= planes[i][0] * a_cb.CameraPosAdjust.x + planes[i][1] * a_cb.CameraPosAdjust.y + planes[i][2] * a_cb.CameraPosAdjust.z;
		}
		return planes;
	}

	bool LsBoxMeetsView(const LsViewPlanes& a_planes, const double (&a_box)[2][3])
	{
		for (const auto& p : a_planes)
			if (p[0] * a_box[p[0] >= 0.0][0] + p[1] * a_box[p[1] >= 0.0][1] + p[2] * a_box[p[2] >= 0.0][2] + p[3] < 0.0)
				return false;
		return true;
	}

	void LsDetach()
	{
		auto* context = globals::d3d::context;
		if (ls.mode == LsMode::kTess) {
			context->HSSetShader(nullptr, nullptr, 0);
			context->DSSetShader(nullptr, nullptr, 0);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			ID3D11ShaderResourceView* none[kLsSRVCount]{};
			context->HSSetShaderResources(0, kLsSRVCount, none);
			context->DSSetShaderResources(0, kLsSRVCount, none);
		} else if (ls.mode == LsMode::kRaise) {
			context->VSSetShaderResources(0, kLsSRVCount, ls.savedVSSRVs);
			context->VSSetConstantBuffers(11, 1, &ls.savedVSCB[0]);
			context->VSSetConstantBuffers(13, 1, &ls.savedVSCB[1]);
			for (auto*& srv : ls.savedVSSRVs) {
				if (srv)
					srv->Release();
				srv = nullptr;
			}
			for (auto*& cb : ls.savedVSCB) {
				if (cb)
					cb->Release();
				cb = nullptr;
			}
		}
		context->VSSetShader(ls.savedVS.get(), nullptr, 0);
		ls.savedVS = nullptr;
		ls.mode = LsMode::kNone;
	}

	bool LsRead(LsSlot& a_slot)
	{
		auto* context = globals::d3d::context;
		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
		if (!a_slot.disjoint || context->GetData(a_slot.disjoint.get(), &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
			return false;
		double ms[kLsCategories]{};
		double tms[kLsMaxTargets]{};
		uint64_t ia[kLsCategories]{}, vs[kLsCategories]{}, hs[kLsCategories]{}, ds[kLsCategories]{}, cinv[kLsCategories]{}, cprim[kLsCategories]{};
		bool anyStats = false;
		for (uint32_t i = 0; i < a_slot.drawCount; i++) {
			UINT64 a = 0, b = 0;
			if (context->GetData(a_slot.ts[2 * i].get(), &a, sizeof(a), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
				context->GetData(a_slot.ts[2 * i + 1].get(), &b, sizeof(b), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
				return false;
			const uint32_t c = a_slot.cat[i];
			if (b > a && dj.Frequency) {
				ms[c] += double(b - a) * 1000.0 / double(dj.Frequency);
				if (a_slot.target[i] < kLsMaxTargets)
					tms[a_slot.target[i]] += double(b - a) * 1000.0 / double(dj.Frequency);
			}
			if (a_slot.hasStats[i]) {
				D3D11_QUERY_DATA_PIPELINE_STATISTICS ps{};
				if (context->GetData(a_slot.stats[i].get(), &ps, sizeof(ps), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK) {
					anyStats = true;
					ia[c] += ps.IAPrimitives;
					vs[c] += ps.VSInvocations;
					hs[c] += ps.HSInvocations;
					ds[c] += ps.DSInvocations;
					cinv[c] += ps.CInvocations;
					cprim[c] += ps.CPrimitives;
				}
			}
		}
		std::lock_guard lock(lsMutex);
		auto& t = ls.totals;
		if (dj.Disjoint) {
			t.disjoint++;
			return true;
		}
		t.frames++;
		t.overflow += a_slot.overflow;
		if (anyStats)
			t.statFrames++;
		for (uint32_t k = 0; k < kLsMaxTargets; k++) {
			t.targetMs[k] += tms[k];
			t.targetDraws[k] += a_slot.targetDraws[k];
			t.targetTris[k] += a_slot.targetTris[k];
		}
		for (uint32_t c = 0; c < kLsCategories; c++) {
			t.gpuMs[c] += ms[c];
			t.draws[c] += a_slot.draws[c];
			t.tess[c] += a_slot.tess[c];
			t.raise[c] += a_slot.raise[c];
			t.tris[c] += a_slot.tris[c];
			t.ia[c] += ia[c];
			t.vs[c] += vs[c];
			t.hs[c] += hs[c];
			t.ds[c] += ds[c];
			t.cinv[c] += cinv[c];
			t.cprim[c] += cprim[c];
		}
		return true;
	}

	void LsNewFrame(uint32_t a_frame)
	{
		ls.lastFrame = a_frame;
		ls.shadeLast = ls.shadeFrame;
		ls.shadeLast.captures = ls.captureFrame + 1 == a_frame ? uint32_t(ls.captures.size()) : 0;
		ls.shadeFrame = {};
		auto* context = globals::d3d::context;
		auto& cur = ls.slots[ls.slotIndex];
		if (cur.open) {
			context->End(cur.disjoint.get());
			cur.open = false;
			cur.pendingRead = true;
		}
		ls.slotIndex = (ls.slotIndex + 1) % kLsSlots;
		auto& next = ls.slots[ls.slotIndex];
		if (next.pendingRead) {
			if (!LsRead(next)) {
				std::lock_guard lock(lsMutex);
				ls.totals.lost++;
			}
			next.pendingRead = false;
		}
		next.drawCount = 0;
		next.overflow = 0;
		std::fill(std::begin(next.draws), std::end(next.draws), 0u);
		std::fill(std::begin(next.tess), std::end(next.tess), 0u);
		std::fill(std::begin(next.raise), std::end(next.raise), 0u);
		std::fill(std::begin(next.tris), std::end(next.tris), 0ull);
		std::fill(std::begin(next.targetDraws), std::end(next.targetDraws), 0u);
		std::fill(std::begin(next.targetTris), std::end(next.targetTris), 0ull);
		if (!ls.cfg.measure)
			return;
		if (!next.disjoint) {
			D3D11_QUERY_DESC qd{ D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
			globals::d3d::device->CreateQuery(&qd, next.disjoint.put());
		}
		if (next.disjoint) {
			context->Begin(next.disjoint.get());
			next.open = true;
		}
	}

	uint32_t LsTargetIndex(uint32_t a_cat)
	{
		auto* context = globals::d3d::context;
		ID3D11DepthStencilView* dsv = nullptr;
		context->OMGetRenderTargets(0, nullptr, &dsv);
		if (!dsv)
			return kLsMaxTargets;
		D3D11_DEPTH_STENCIL_VIEW_DESC dd{};
		dsv->GetDesc(&dd);
		ID3D11Resource* res = nullptr;
		dsv->GetResource(&res);
		const uint32_t sliceIdx = dd.ViewDimension == D3D11_DSV_DIMENSION_TEXTURE2DARRAY ? dd.Texture2DArray.FirstArraySlice : 0;
		uint32_t index = kLsMaxTargets;
		for (uint32_t k = 0; k < ls.targetCount; k++)
			if (ls.targets[k].texture == res && ls.targets[k].slice == sliceIdx && ls.targets[k].category == a_cat)
				index = k;
		if (index == kLsMaxTargets && ls.targetCount < kLsMaxTargets) {
			auto& tg = ls.targets[ls.targetCount];
			tg.texture = res;
			tg.slice = sliceIdx;
			tg.category = a_cat;
			D3D11_TEXTURE2D_DESC td{};
			if (auto* tex = static_cast<ID3D11Texture2D*>(res))
				tex->GetDesc(&td);
			tg.width = td.Width;
			tg.height = td.Height;
			tg.arraySize = td.ArraySize;
			index = ls.targetCount++;
		}
		if (res)
			res->Release();
		dsv->Release();
		return index;
	}

	struct LS_Lighting_RestoreGeometry
	{
		static void thunk(RE::BSShader* a_shader, RE::BSRenderPass* a_pass, uint32_t a_flags)
		{
			auto& snow = globals::features::snowDeformation;
			snow.NoteMainViewport();
			snow.LandscapeSnowRestore();
			func(a_shader, a_pass, a_flags);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct LS_Utility_SetupGeometry
	{
		static void thunk(RE::BSShader* a_shader, RE::BSRenderPass* a_pass, uint32_t a_flags)
		{
			func(a_shader, a_pass, a_flags);
			auto& snow = globals::features::snowDeformation;
			if (snow.loaded)
				snow.LandscapeSnowSetup(a_pass, false);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct LS_Utility_RestoreGeometry
	{
		static void thunk(RE::BSShader* a_shader, RE::BSRenderPass* a_pass, uint32_t a_flags)
		{
			globals::features::snowDeformation.LandscapeSnowRestore();
			func(a_shader, a_pass, a_flags);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

void SnowDeformation::InstallLandscapeSnow()
{
	logger::info("[SNOW DEFORMATION] Landscape snow: hooking Lighting RestoreGeometry, Utility Setup/RestoreGeometry");
	stl::write_vfunc<0x7, LS_Lighting_RestoreGeometry>(RE::VTABLE_BSLightingShader[0]);
	stl::write_vfunc<0x6, LS_Utility_SetupGeometry>(RE::VTABLE_BSUtilityShader[0]);
	stl::write_vfunc<0x7, LS_Utility_RestoreGeometry>(RE::VTABLE_BSUtilityShader[0]);
}

float SnowDeformation::LsRiseReach() const
{
	return std::max(ls.cfg.lift, 0.0f);
}

bool SnowDeformation::LsCascadeMeetsSnow()
{
	const auto& fb = globals::game::frameBufferCached;
	const auto& m = fb.GetCameraViewProj();
	const auto& cam = fb.GetCameraPosAdjust();
	const uint32_t frame = globals::state->frameCountAtomic.load(std::memory_order_relaxed);
	if (cascadeCountFrame != frame) {
		cascadeCountFrame = frame;
		cascadeGateFrame = UINT32_MAX;
	}
	if (cascadeGateFrame == frame && std::memcmp(&cascadeGateVP, &m, sizeof(m)) == 0 && std::memcmp(&cascadeGateCam, &cam, sizeof(cam)) == 0)
		return cascadeGateMeets;
	cascadeGateFrame = frame;
	cascadeGateVP = m;
	cascadeGateCam = cam;
	FootprintPlanes planes;
	for (int i = 0; i < 4; i++) {
		const int r = i >> 1;
		const double s = (i & 1) ? -1.0 : 1.0;
		for (int c = 0; c < 4; c++)
			planes[i][c] = double(m.m[3][c]) + s * double(m.m[r][c]);
		planes[i][3] -= planes[i][0] * cam.x + planes[i][1] * cam.y + planes[i][2] * cam.z;
	}
	cascadeGateMeets = SnowCellsMeetFootprints(&planes, 1, LsRiseReach(), false);
	return cascadeGateMeets;
}

bool SnowDeformation::LandscapeSnowRefresh()
{
	ls.cbValid = false;
	if (!lastShellCBData || !shellTerrainTexture || !shellTerrainTexture->srv || !GetDeformationSRV() || !heightBottomFiltered ||
		!heightTopRaw[heightCurrent] || !shellSnowSampler || !ShellFootprintHasSnow())
		return false;

	auto& fb = globals::game::frameBufferCached;
	ShellCB cb = *lastShellCBData;
	const uint32_t cat = std::min(ls.cbCategory, kLsCategories - 1);
	ls.camFrame[cat] = fb.GetCameraPosAdjust();
	cb.CameraViewProj = fb.GetCameraViewProj();
	cb.CameraViewProjUnjittered = fb.GetCameraViewProjUnjittered();
	cb.CameraPreviousViewProjUnjittered = fb.GetCameraPreviousViewProjUnjittered();
	cb.CameraView = fb.GetCameraView();
	cb.CameraPosAdjust = fb.GetCameraPosAdjust();
	cb.CameraPreviousPosAdjust = fb.GetCameraPreviousPosAdjust();
	if (LsShadowPass(ls.cbCategory)) {
		if (auto* camera = RE::Main::WorldRootCamera()) {
			const auto& eye = camera->world.translate;
			cb.CameraPosAdjust = { eye.x, eye.y, eye.z, 0.0f };
			cb.CameraPreviousPosAdjust = cb.CameraPosAdjust;
		}
	}
	PlaceShellFields(cb);
	ls.camUsed[cat] = cb.CameraPosAdjust;

	if (!ls.shellCB)
		ls.shellCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<ShellCB>(), "SnowDeformation::LandscapeShellCB");
	ls.shellCB->Update(cb);
	lsCB = cb;

	LandscapeSurfaceSRVs(ls.srvs);
	ls.sampler = shellSnowSampler.get();
	ls.liveRect = LsLiveRect(cb);
	ls.cbValid = true;
	return true;
}

void SnowDeformation::PlaceShellFields(ShellCB& a_cb)
{
	RefreshShellGridPlacement(a_cb, &a_cb.CameraPosAdjust);
	a_cb.SeamRampInv = 0.0f;
	int seamMinX, seamMinY, seamMaxX, seamMaxY;
	if (LoadedCellSquare(seamMinX, seamMinY, seamMaxX, seamMaxY))
		SetShellSeamBounds(a_cb, seamMinX, seamMinY, seamMaxX, seamMaxY);
	a_cb.DeformInvWorldSize = 1.0f / deformWorldSize;
	a_cb.ObjectHeightCenter = heightWindowCenter;
	a_cb.ObjectHeightHalfExtent = ObjectRasterHalfExtent();
	a_cb.BermBakeActive = bermFieldTexture ? 1.0f : 0.0f;
	a_cb.TrackAge = TrackAgeConstants();
	a_cb.UndulationFieldWindow = { undulationFieldCenter.x, undulationFieldCenter.y, 1.0f / kUndulationFieldHalfExtent,
		undulationFieldValid ? 1.0f : 0.0f };
	a_cb.ExclusionFieldWindow = { exclusionFieldCenter.x, exclusionFieldCenter.y, 1.0f / kExclusionFieldHalfExtent,
		exclusionFieldValid ? 1.0f : 0.0f };
	const auto mapDim = [](const Texture2D* a_tex) { return (a_tex && a_tex->srv) ? float(a_tex->desc.Width) : 0.0f; };
	a_cb.FieldDims = { mapDim(deformationTextures[0]), mapDim(bermFieldTexture), mapDim(heightBottomFiltered), mapDim(exclusionFieldTexture) };
	a_cb.TessScreen = { ShellTessTargetRenderPx(), shellDrawViewport.Width * 0.5f, shellDrawViewport.Height * 0.5f, 0.0f };
}

void SnowDeformation::LandscapeSurfaceSRVs(ID3D11ShaderResourceView** a_srvs)
{
	auto* s = a_srvs;
	std::fill(s, s + kLsSRVCount, nullptr);
	s[0] = shellTerrainTexture->srv.get();
	s[1] = GetDeformationSRV();
	s[5] = heightBottomFiltered->srv.get();
	s[8] = shellSnowHeightSRV.get();
	s[11] = heightTopRaw[heightCurrent]->srv.get();
	s[13] = (shellFineValid && shellTerrainFine) ? shellTerrainFine->srv.get() : nullptr;
	s[14] = GetBermFieldSRV();
	s[15] = GetExclusionFieldSRV();
	s[19] = waterNearTexture && waterNearTexture->srv ? waterNearTexture->srv.get() : nullptr;
	s[27] = ShellWaterSRV();
	s[28] = waterHeightTexture ? waterHeightTexture->srv.get() : nullptr;
	s[29] = GetUndulationFieldSRV();
	s[kUntouchedTilesSlot] = untouchedTilesSRV.get();
	s[kTerrainWetSlot] = TerrainWetSRV();
	s[kTerrainBareSlot] = shellTerrainBare ? shellTerrainBare->srv.get() : nullptr;
}

bool SnowDeformation::LandscapeShadeReady()
{
	if (!settings.EnableSnowDeformation)
		return false;
	const bool ready = ls.shadeState == 2 && ls.captureFrame == globals::state->frameCount;
	if (!ready)
		ls.shadeFallbacks++;
	return ready;
}

float2 SnowDeformation::LandBlendContest() const
{
	return { std::max(ls.cfg.blendBonus, 0.0f), std::max(ls.cfg.blendPenalty, 0.0f) };
}

uint32_t SnowDeformation::DrawLandscapeSnowPrepass(ID3D11DeviceContext* a_context, const ShellCB& a_cb, ID3D11Buffer* a_shellCB)
{
	auto& counts = ls.shadeFrame;
	counts.replayCalls++;
	if (ls.captures.empty())
		return 0;
	const auto& cfg = ls.cfg;
	if (!ls.drawCB)
		ls.drawCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<LsCBData>(), "SnowDeformation::LandscapeSnowCB");

	winrt::com_ptr<ID3D11InputLayout> prevLayout;
	a_context->IAGetInputLayout(prevLayout.put());
	winrt::com_ptr<ID3D11Buffer> prevVB, prevIB;
	UINT prevStride = 0, prevVBOffset = 0, prevIBOffset = 0;
	DXGI_FORMAT prevIBFormat = DXGI_FORMAT_UNKNOWN;
	a_context->IAGetVertexBuffers(0, 1, prevVB.put(), &prevStride, &prevVBOffset);
	a_context->IAGetIndexBuffer(prevIB.put(), &prevIBFormat, &prevIBOffset);
	ID3D11Buffer* prevCB11[3]{};
	a_context->VSGetConstantBuffers(11, 1, &prevCB11[0]);
	a_context->HSGetConstantBuffers(11, 1, &prevCB11[1]);
	a_context->DSGetConstantBuffers(11, 1, &prevCB11[2]);

	ID3D11ShaderResourceView* srvs[kLsSRVCount];
	LandscapeSurfaceSRVs(srvs);
	ID3D11SamplerState* sampler = shellSnowSampler.get();
	const auto& shaders = ls.shade;
	a_context->VSSetShader(shaders.vs.get(), nullptr, 0);
	a_context->HSSetShader(shaders.hs.get(), nullptr, 0);
	a_context->DSSetShader(shaders.ds.get(), nullptr, 0);
	a_context->HSSetConstantBuffers(0, 1, &a_shellCB);
	a_context->DSSetConstantBuffers(0, 1, &a_shellCB);
	a_context->HSSetShaderResources(0, kLsSRVCount, srvs);
	a_context->DSSetShaderResources(0, kLsSRVCount, srvs);
	a_context->DSSetSamplers(0, 1, &sampler);
	a_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
	ID3D11Buffer* drawCB = ls.drawCB->CB();
	a_context->VSSetConstantBuffers(11, 1, &drawCB);
	a_context->HSSetConstantBuffers(11, 1, &drawCB);
	a_context->DSSetConstantBuffers(11, 1, &drawCB);

	std::vector<LsCapture> culled;
	auto* grid = cfg.culledLand && !InInteriorCell() && globals::game::tes ? globals::game::tes->gridCells : nullptr;
	if (grid) {
		const LsViewPlanes view = LsOffViewPlanes(a_cb, 0.0f);
		const LsViewPlanes guarded = LsOffViewPlanes(a_cb, std::max(cfg.guard, 0.0f));
		const double rise = double(LsRiseReach());
		constexpr double kCell = kShellVertexSpacing * 32.0;
		constexpr double kQuad = kCell * 0.5;
		const auto quadOf = [&](double a_x, double a_y) { return std::pair{ int(std::floor(a_x / kQuad)), int(std::floor(a_y / kQuad)) }; };
		std::vector<std::pair<int, int>> drawn;
		drawn.reserve(ls.captures.size());
		for (const auto& c : ls.captures)
			drawn.push_back(quadOf(c.boundXY.x, c.boundXY.y));
		const uint32_t worldspace = activeWorldspace.load(std::memory_order_acquire);
		const std::shared_lock lock(shellCellMutex);
		for (uint32_t gx = 0; gx < grid->length; gx++) {
			for (uint32_t gy = 0; gy < grid->length; gy++) {
				auto* cell = grid->GetCell(gx, gy);
				auto* land = cell && cell->IsAttached() ? cell->GetRuntimeData().cellLand : nullptr;
				const auto* coords = land ? cell->GetCoordinates() : nullptr;
				if (!land || !land->loadedData || !coords)
					continue;
				const auto baked = shellCells.find((uint64_t(uint32_t(coords->cellX)) << 32) | uint32_t(coords->cellY));
				if (baked == shellCells.end() || baked->second.worldspaceID != worldspace)
					continue;
				const auto& lattice = baked->second.height;
				for (uint32_t q = 0; q < 4; q++) {
					auto* geometry = LandQuadGeometry(land->loadedData, q);
					auto* rendererData = geometry ? geometry->GetGeometryRuntimeData().rendererData : nullptr;
					if (!rendererData || !rendererData->vertexBuffer || ls.captured.contains(geometry))
						continue;
					const auto& bound = geometry->worldBound;
					const auto quad = quadOf(bound.center.x, bound.center.y);
					const int qx = quad.first - 2 * coords->cellX;
					const int qy = quad.second - 2 * coords->cellY;
					if (qx < 0 || qx > 1 || qy < 0 || qy > 1 || std::find(drawn.begin(), drawn.end(), quad) != drawn.end())
						continue;
					const double x0 = quad.first * kQuad;
					const double y0 = quad.second * kQuad;
					bool placed = true;
					for (const RE::NiAVObject* o = geometry; o && placed; o = o->parent)
						placed = !o->GetAppCulled();
					const auto& world = geometry->world;
					placed = placed && std::abs(world.scale - 1.0f) <= 1e-4f;
					for (int r = 0; r < 3 && placed; r++)
						for (int c = 0; c < 3; c++)
							placed = placed && std::abs(world.rotate.entry[r][c] - (r == c ? 1.0f : 0.0f)) <= 1e-4f;
					if (!placed)
						continue;
					const auto latticeZ = [&](int a_ix, int a_iy) { return double(lattice[size_t(qy * 16 + a_iy) * 33 + size_t(qx * 16 + a_ix)]); };
					bool landOut = false;
					for (const auto& p : view) {
						landOut = true;
						for (int iy = 0; iy <= 16 && landOut; iy++)
							for (int ix = 0; ix <= 16 && landOut; ix++)
								landOut = p[0] * (x0 + ix * kShellVertexSpacing) + p[1] * (y0 + iy * kShellVertexSpacing) + p[2] * latticeZ(ix, iy) + p[3] < 0.0;
						if (landOut)
							break;
					}
					if (!landOut)
						continue;
					double zLo = std::numeric_limits<double>::max();
					double zHi = std::numeric_limits<double>::lowest();
					for (int iy = 0; iy <= 16; iy++) {
						for (int ix = 0; ix <= 16; ix++) {
							zLo = std::min(zLo, latticeZ(ix, iy));
							zHi = std::max(zHi, latticeZ(ix, iy));
						}
					}
					const double box[2][3] = { { x0, y0, zLo - 32.0 }, { x0 + kQuad, y0 + kQuad, zHi + rise } };
					if (!LsBoxMeetsView(guarded, box))
						continue;
					uint64_t desc = 0;
					std::memcpy(&desc, &rendererData->vertexDesc, sizeof(desc));
					const UINT indexCount = UINT(geometry->GetTrishapeRuntimeData().triangleCount) * 3;
					const auto donor = std::find_if(ls.captures.begin(), ls.captures.end(), [&](const LsCapture& c) { return c.indexCount == indexCount && c.vertexDesc == desc; });
					if (indexCount == 0 || donor == ls.captures.end())
						continue;
					auto& cap = culled.emplace_back();
					cap.geometry = geometry;
					cap.layout = donor->layout;
					cap.vb.copy_from(reinterpret_cast<ID3D11Buffer*>(rendererData->vertexBuffer));
					cap.stride = donor->stride;
					cap.ib = donor->ib;
					cap.ibFormat = donor->ibFormat;
					cap.ibOffset = donor->ibOffset;
					cap.indexCount = indexCount;
					cap.translate[0] = world.translate.x;
					cap.translate[1] = world.translate.y;
					cap.translate[2] = world.translate.z;
					cap.boundXY = { bound.center.x, bound.center.y };
					cap.boundRadius = bound.radius;
					cap.vertexDesc = desc;
				}
			}
		}
	}

	const float4 live = LsLiveRect(a_cb);
	const auto& cam = a_cb.CameraPosAdjust;
	const int gateMask = cfg.gate ? std::clamp(cfg.gateMask, 0, 31) : 0;
	ID3D11InputLayout* boundLayout = nullptr;
	uint32_t issued = 0;
	const auto replay = [&](const LsCapture& cap) {
		const auto& c = cap.boundXY;
		const float r = cap.boundRadius;
		if (c.x + r < live.x || c.y + r < live.y || c.x - r > live.z || c.y - r > live.w)
			return false;
		const float dx = c.x - cam.x;
		const float dy = c.y - cam.y;
		const bool tessNear = cfg.tessellate && settings.Tessellation && std::sqrt(dx * dx + dy * dy) - r < cfg.tessReach;
		LsCBData data{
			{ float(cap.translate[0] - double(a_cb.GridOrigin.x)), float(cap.translate[1] - double(a_cb.GridOrigin.y)), float(cap.translate[2]), std::max(cfg.lift, 0.0f) },
			{ 1.0f, tessNear ? std::clamp(cfg.maxFactor, 1.0f, 64.0f) : 1.0f, float(gateMask), cfg.cull ? 1.0f : 0.0f },
			{ std::max(cfg.guard, 0.0f), 0.0f, 0.0f, 0.0f }
		};
		if (tessNear && (uint32_t(data.tess[2]) & 16) != 0)
			counts.replayExcl.Add(LsFillSmallExclusions(data, staticExclusions, cap.boundXY, cap.boundRadius, a_cb.GridOrigin));
		ls.drawCB->Update(data);
		if (cap.layout.get() != boundLayout) {
			boundLayout = cap.layout.get();
			a_context->IASetInputLayout(boundLayout);
		}
		ID3D11Buffer* vb = cap.vb.get();
		a_context->IASetVertexBuffers(0, 1, &vb, &cap.stride, &cap.vbOffset);
		a_context->IASetIndexBuffer(cap.ib.get(), cap.ibFormat, cap.ibOffset);
		a_context->DrawIndexed(cap.indexCount, 0, 0);
		issued++;
		counts.replayTess += tessNear ? 1 : 0;
		return true;
	};
	for (const auto& cap : ls.captures)
		replay(cap);
	for (const auto& cap : culled)
		counts.replayCulled += replay(cap) ? 1 : 0;
	counts.replayDraws += issued;

	a_context->IASetInputLayout(prevLayout.get());
	ID3D11Buffer* vb = prevVB.get();
	a_context->IASetVertexBuffers(0, 1, &vb, &prevStride, &prevVBOffset);
	a_context->IASetIndexBuffer(prevIB.get(), prevIBFormat, prevIBOffset);
	a_context->VSSetConstantBuffers(11, 1, &prevCB11[0]);
	a_context->HSSetConstantBuffers(11, 1, &prevCB11[1]);
	a_context->DSSetConstantBuffers(11, 1, &prevCB11[2]);
	for (auto* b : prevCB11)
		if (b)
			b->Release();
	return issued;
}

void SnowDeformation::LandscapeSnowSetup(RE::BSRenderPass* a_pass, bool a_lighting)
{
	if (ls.mode != LsMode::kNone)
		LsDetach();
	ls.curDraw = -1;
	ls.capturePending = false;
	const auto& cfg = ls.cfg;
	const bool snowOn = settings.EnableSnowDeformation && !SnowShadersPending(1) && !GroundShellsSuspended();
	if (!snowOn && !cfg.measure)
		return;
	if (!a_pass || !a_pass->shaderProperty || !a_pass->geometry)
		return;
	if (!a_pass->shaderProperty->flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kMultiTextureLandscape))
		return;
	auto* state = globals::state;
	auto* shader = state->currentShader;
	if (!shader)
		return;
	auto* context = globals::d3d::context;

	uint32_t cat;
	if (a_lighting) {
		cat = globals::deferred->deferredPass ? kLsMain : kLsLightingOther;
	} else {
		using U = SIE::ShaderCache::UtilityShaderFlags;
		const uint32_t raw = state->currentPixelDescriptor | state->currentVertexDescriptor;
		const auto& sky = globals::features::skylighting;
		if (sky.loaded && sky.inOcclusion)
			cat = kLsSkyOcclusion;
		else if (raw & uint32_t(U::RenderShadowmap))
			cat = (raw & uint32_t(U::RenderShadowmapPb)) ? kLsShadowLocal : kLsShadowSun;
		else if (raw & uint32_t(U::RenderDepth))
			cat = kLsDepth;
		else
			cat = kLsUtilityOther;
	}

	const uint32_t frame = state->frameCountAtomic.load(std::memory_order_relaxed);
	if (frame != ls.lastFrame)
		LsNewFrame(frame);
	auto& slot = ls.slots[ls.slotIndex];
	if (snowOn && (cat == kLsDepth || cat == kLsMain) && LssPoll())
		LsShadeTrack(a_pass, cat, frame);
	slot.draws[cat]++;
	auto* triShape = a_pass->geometry->AsTriShape();
	if (triShape)
		slot.tris[cat] += triShape->GetTrishapeRuntimeData().triangleCount;

	LsMode want = LsMode::kNone;
	if (snowOn && cat == kLsShadowSun && settings.SnowCastsShadows) {
		want = LsMode::kRaise;
		const auto& world = a_pass->geometry->world;
		if (std::abs(world.scale - 1.0f) > 1e-4f)
			want = LsMode::kNone;
		for (int r = 0; r < 3 && want != LsMode::kNone; r++)
			for (int c = 0; c < 3; c++)
				if (std::abs(world.rotate.entry[r][c] - (r == c ? 1.0f : 0.0f)) > 1e-4f)
					want = LsMode::kNone;
		if (want != LsMode::kNone && (ls.cbFrame != frame || ls.cbCategory != cat)) {
			ls.cbFrame = frame;
			ls.cbCategory = cat;
			LandscapeSnowRefresh();
		}
		if (!ls.cbValid)
			want = LsMode::kNone;
		else if (want != LsMode::kNone && !LsCascadeMeetsSnow())
			want = LsMode::kNone;
	}
	float2 boundXY{};
	float boundRadius = 0.0f;
	if (want != LsMode::kNone) {
		const auto& b = a_pass->geometry->worldBound;
		boundXY = { b.center.x, b.center.y };
		boundRadius = b.radius;
		const auto& r = ls.liveRect;
		if (boundXY.x + boundRadius < r.x || boundXY.y + boundRadius < r.y || boundXY.x - boundRadius > r.z || boundXY.y - boundRadius > r.w)
			want = LsMode::kNone;
	}
	if (want == LsMode::kRaise && cfg.tessellate && settings.Tessellation) {
		const auto& cam = lsCB.CameraPosAdjust;
		const float dx = boundXY.x - cam.x;
		const float dy = boundXY.y - cam.y;
		if (std::sqrt(dx * dx + dy * dy) - boundRadius < cfg.shadowTessReach)
			want = LsMode::kTess;
	}
	LsEntry* entry = nullptr;
	if (want != LsMode::kNone) {
		entry = &ls.entries[state->modifiedVertexDescriptor];
		if (entry->state == 0) {
			entry->pending = std::async(std::launch::async, LsCompile, SIE::ShaderCache::GetDefinesString(*shader, state->modifiedVertexDescriptor));
			entry->state = 1;
		}
		if (entry->state == 1 && entry->pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
			entry->shaders = entry->pending.get();
			const auto& sh = entry->shaders;
			entry->state = (sh.vsRaise && sh.vs && sh.hs && sh.ds) ? 2 : 3;
			if (entry->state == 2)
				logger::info("[SNOW DEFORMATION] Landscape snow {:X}: ready in {:.0f} ms", state->modifiedVertexDescriptor, sh.compileMs);
			else
				logger::warn("[SNOW DEFORMATION] Landscape snow {:X}: shader compile FAILED, those draws stay unraised; defines: {}", state->modifiedVertexDescriptor, sh.defines);
		}
		if (entry->state != 2)
			want = LsMode::kNone;
	}

	if (want != LsMode::kNone) {
		if (!ls.drawCB)
			ls.drawCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<LsCBData>(), "SnowDeformation::LandscapeSnowCB");
		const auto& t = a_pass->geometry->world.translate;
		const auto& grid = lsCB.GridOrigin;
		LsCBData data{
			{ float(double(t.x) - double(grid.x)), float(double(t.y) - double(grid.y)), t.z, std::max(cfg.lift, 0.0f) },
			{ 0.0f, std::clamp(cfg.maxFactor, 1.0f, 64.0f), cfg.gate ? float(std::clamp(cfg.gateMask, 0, 31)) : 0.0f, cfg.cull ? 1.0f : 0.0f },
			{ std::max(cfg.guard, 0.0f), 0.0f, 0.0f, 0.0f }
		};
		if (want == LsMode::kTess && (uint32_t(data.tess[2]) & 16) != 0)
			ls.shadeFrame.raiseExcl.Add(LsFillSmallExclusions(data, staticExclusions, boundXY, boundRadius, grid));
		ls.drawCB->Update(data);
		ID3D11Buffer* own[2] = { ls.drawCB->CB(), ls.shellCB->CB() };

		ID3D11VertexShader* prevVS = nullptr;
		context->VSGetShader(&prevVS, nullptr, nullptr);
		ls.savedVS.attach(prevVS);
		if (want == LsMode::kTess) {
			ID3D11Buffer* cbs[13]{};
			context->VSGetConstantBuffers(0, 13, cbs);
			context->HSSetConstantBuffers(0, 13, cbs);
			context->DSSetConstantBuffers(0, 13, cbs);
			for (auto* b : cbs)
				if (b)
					b->Release();
			context->HSSetConstantBuffers(11, 1, &own[0]);
			context->DSSetConstantBuffers(11, 1, &own[0]);
			context->HSSetConstantBuffers(13, 1, &own[1]);
			context->DSSetConstantBuffers(13, 1, &own[1]);
			context->HSSetShaderResources(0, kLsSRVCount, ls.srvs);
			context->DSSetShaderResources(0, kLsSRVCount, ls.srvs);
			context->DSSetSamplers(0, 1, &ls.sampler);
			context->VSSetShader(entry->shaders.vs.get(), nullptr, 0);
			context->HSSetShader(entry->shaders.hs.get(), nullptr, 0);
			context->DSSetShader(entry->shaders.ds.get(), nullptr, 0);
			globals::game::shadowState->GetRuntimeData().topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			globals::game::stateUpdateFlags->reset(RE::BSGraphics::ShaderFlags::DIRTY_PRIMITIVE_TOPO);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
			slot.tess[cat]++;
			entry->tessAttaches++;
		} else {
			context->VSGetShaderResources(0, kLsSRVCount, ls.savedVSSRVs);
			context->VSGetConstantBuffers(11, 1, &ls.savedVSCB[0]);
			context->VSGetConstantBuffers(13, 1, &ls.savedVSCB[1]);
			context->VSSetShaderResources(0, kLsSRVCount, ls.srvs);
			context->VSSetConstantBuffers(11, 1, &own[0]);
			context->VSSetConstantBuffers(13, 1, &own[1]);
			context->VSSetShader(entry->shaders.vsRaise.get(), nullptr, 0);
			slot.raise[cat]++;
			entry->raiseAttaches++;
		}
		ls.mode = want;
	}

	if (!cfg.measure || !slot.open)
		return;
	uint32_t targetIndex = kLsMaxTargets;
	if (!a_lighting) {
		targetIndex = LsTargetIndex(cat);
		if (targetIndex < kLsMaxTargets) {
			slot.targetDraws[targetIndex]++;
			if (triShape)
				slot.targetTris[targetIndex] += triShape->GetTrishapeRuntimeData().triangleCount;
		}
	}
	if (slot.drawCount >= kLsMaxDraws) {
		slot.overflow++;
		return;
	}
	const uint32_t i = slot.drawCount;
	auto* device = globals::d3d::device;
	while (slot.ts.size() < 2 * (i + 1)) {
		D3D11_QUERY_DESC qd{ D3D11_QUERY_TIMESTAMP, 0 };
		winrt::com_ptr<ID3D11Query> q;
		device->CreateQuery(&qd, q.put());
		slot.ts.push_back(q);
	}
	if (cfg.stats) {
		while (slot.stats.size() < i + 1) {
			D3D11_QUERY_DESC qd{ D3D11_QUERY_PIPELINE_STATISTICS, 0 };
			winrt::com_ptr<ID3D11Query> q;
			device->CreateQuery(&qd, q.put());
			slot.stats.push_back(q);
		}
	}
	if (!slot.ts[2 * i] || !slot.ts[2 * i + 1])
		return;
	slot.cat[i] = uint8_t(cat);
	slot.target[i] = uint8_t(targetIndex);
	slot.hasStats[i] = cfg.stats && slot.stats.size() > i && slot.stats[i] ? 1 : 0;
	context->End(slot.ts[2 * i].get());
	if (slot.hasStats[i])
		context->Begin(slot.stats[i].get());
	slot.drawCount++;
	ls.curDraw = int(i);
}

void SnowDeformation::NoteMainViewport()
{
	auto* state = globals::state;
	if (mainViewportFrame == state->frameCount || !loaded || !settings.EnableSnowDeformation || !state->inWorld || !globals::deferred->deferredPass ||
		(state->permutationData.ExtraShaderDescriptor & static_cast<uint32_t>(State::ExtraShaderDescriptors::IsReflections)))
		return;
	UINT count = 1;
	D3D11_VIEWPORT vp{};
	globals::d3d::context->RSGetViewports(&count, &vp);
	if (!count)
		return;
	mainViewportMinDepth = vp.MinDepth;
	mainViewportMaxDepth = vp.MaxDepth;
	mainViewportFrame = state->frameCount;
}

void SnowDeformation::LandscapeSnowRestore()
{
	if (ls.capturePending)
		LsShadeCapture();
	if (ls.curDraw >= 0) {
		auto* context = globals::d3d::context;
		auto& slot = ls.slots[ls.slotIndex];
		const uint32_t i = uint32_t(ls.curDraw);
		context->End(slot.ts[2 * i + 1].get());
		if (slot.hasStats[i])
			context->End(slot.stats[i].get());
		ls.curDraw = -1;
	}
	if (ls.mode != LsMode::kNone)
		LsDetach();
}

