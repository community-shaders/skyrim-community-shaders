#include "Features/ScreenshotFeature.h"
#include "Features/Upscaling.h"
#include "Features/VR.h"
#include "Globals.h"
#include "Hooks.h"
#include "Menu.h"
#include "State.h"
#include "Util.h"
#include "Utils/VRUtils.h"
#include <DirectXMath.h>
#include <SimpleMath.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <imgui_impl_dx11.h>
#include <limits>
#include <mutex>
#include <utility>

using namespace DirectX;
using namespace DirectX::SimpleMath;

using AttachMode = VR::Settings::OverlayAttachMode;

//=============================================================================
// IN-SCENE OVERLAY RENDERING VIA SUBMIT HOOK
//=============================================================================

namespace
{
	// Publish the cycle token and its promotion-sensitive cooldown policy as one
	// atomic snapshot. Bit zero is the cycle-start cooldown; the remaining bits
	// are the OpenVR Submit-cycle token.
	constexpr uint64_t kOpenVRCycleCooldownBit = 1u;
	constexpr uint64_t kOpenVRCycleTokenMax =
		std::numeric_limits<uint64_t>::max() >> 1u;
	std::atomic<uint64_t> g_openVRSubmitCycleState{ 0 };
	std::mutex g_openVRSubmitCyclePublishMutex;
	std::mutex g_vrPostLoadCompositorSubmitMutex;

	enum class VRNativeRestoreCyclePresentationPath : uint8_t
	{
		Unset,
		Native,
		BlackKeepalive,
		Rejected
	};

	struct VRNativeRestoreCyclePresentationState
	{
		uint64_t guardEpoch = 0;
		uint64_t compositorCycleToken = 0;
		uint32_t contractGeneration = 0;
		uint64_t postLoadKeepaliveToken = 0;
		VRNativeRestoreCyclePresentationPath path =
			VRNativeRestoreCyclePresentationPath::Unset;
		vr::EVRCompositorError rejectionResult =
			vr::VRCompositorError_RequestFailed;
		vr::Texture_t texture{};
		bool textureHasPose = false;
		vr::VRTextureWithPose_t textureWithPose{};
		vr::VRTextureBounds_t nativeBounds{};
		vr::EVREye nativeSourceEye = vr::Eye_Left;
		vr::EVRSubmitFlags nativeSubmitFlags = vr::Submit_Default;
		winrt::com_ptr<ID3D11Texture2D> lifetime;

		void Reset()
		{
			guardEpoch = 0;
			compositorCycleToken = 0;
			contractGeneration = 0;
			postLoadKeepaliveToken = 0;
			path = VRNativeRestoreCyclePresentationPath::Unset;
			rejectionResult = vr::VRCompositorError_RequestFailed;
			texture = {};
			textureHasPose = false;
			textureWithPose = {};
			nativeBounds = {};
			nativeSourceEye = vr::Eye_Left;
			nativeSubmitFlags = vr::Submit_Default;
			lifetime = nullptr;
		}
	};

	// Access is serialized by Upscaling's recursive presentation/relatch queue
	// lease. Once one eye establishes native or black ownership, the peer eye in
	// that exact OpenVR cycle must use the same presentation class.
	VRNativeRestoreCyclePresentationState
		g_vrNativeRestoreCyclePresentationState{};

	vr::VRTextureBounds_t GetCombinedStereoEyeBounds(
		vr::EVREye a_eye,
		vr::EVREye a_sourceEye,
		const vr::VRTextureBounds_t& a_sourceBounds)
	{
		if (a_eye == a_sourceEye)
			return a_sourceBounds;

		const bool reverseU =
			a_sourceBounds.uMin > a_sourceBounds.uMax;
		float uMin = a_eye == vr::Eye_Right ? 0.5f : 0.0f;
		float uMax = a_eye == vr::Eye_Right ? 1.0f : 0.5f;
		if (reverseU)
			std::swap(uMin, uMax);
		return {
			uMin,
			a_sourceBounds.vMin,
			uMax,
			a_sourceBounds.vMax
		};
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	std::atomic_bool g_openVRSubmitProcessingEnabled{ false };
#endif

	bool ShouldRenderInSceneMenu(const VR& vr)
	{
		return vr.ShouldUseInSceneOverlay() &&
		       vr.ShouldPresentOverlayInHeadset() &&
		       vr.menuTexture &&
		       vr.settings.attachMode != AttachMode::None;
	}

	bool MatchesSubmitCopyDesc(const D3D11_TEXTURE2D_DESC& lhs, const D3D11_TEXTURE2D_DESC& rhs)
	{
		return lhs.Width == rhs.Width &&
		       lhs.Height == rhs.Height &&
		       lhs.MipLevels == rhs.MipLevels &&
		       lhs.ArraySize == rhs.ArraySize &&
		       lhs.Format == rhs.Format &&
		       lhs.SampleDesc.Count == rhs.SampleDesc.Count &&
		       lhs.SampleDesc.Quality == rhs.SampleDesc.Quality &&
		       lhs.Usage == rhs.Usage &&
		       lhs.BindFlags == rhs.BindFlags &&
		       lhs.CPUAccessFlags == rhs.CPUAccessFlags &&
		       lhs.MiscFlags == rhs.MiscFlags;
	}

	DXGI_FORMAT GetRenderTargetViewFormat(DXGI_FORMAT format)
	{
		switch (format) {
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
			return DXGI_FORMAT_R8G8B8A8_UNORM;
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
			return DXGI_FORMAT_B8G8R8A8_UNORM;
		case DXGI_FORMAT_B8G8R8X8_TYPELESS:
			return DXGI_FORMAT_B8G8R8X8_UNORM;
		case DXGI_FORMAT_R10G10B10A2_TYPELESS:
			return DXGI_FORMAT_R10G10B10A2_UNORM;
		case DXGI_FORMAT_R16G16B16A16_TYPELESS:
			return DXGI_FORMAT_R16G16B16A16_FLOAT;
		case DXGI_FORMAT_R32G32B32A32_TYPELESS:
			return DXGI_FORMAT_R32G32B32A32_FLOAT;
		default:
			return format;
		}
	}

	bool SupportsRenderTargetView(ID3D11Device* device, DXGI_FORMAT format)
	{
		if (!device || format == DXGI_FORMAT_UNKNOWN) {
			return false;
		}

		UINT support = 0;
		if (FAILED(device->CheckFormatSupport(format, &support))) {
			return false;
		}
		return (support & D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0;
	}

	bool SupportsUnorderedAccessView(ID3D11Device* device, DXGI_FORMAT format)
	{
		if (!device || format == DXGI_FORMAT_UNKNOWN) {
			return false;
		}

		UINT support = 0;
		if (FAILED(device->CheckFormatSupport(format, &support))) {
			return false;
		}
		return (support & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW) != 0;
	}

	bool EnsureMenuTextureSRV(
		ID3D11Texture2D* texture,
		winrt::com_ptr<ID3D11ShaderResourceView>& srv,
		ID3D11Texture2D*& cachedTexture,
		const char* label)
	{
		if (!texture || !globals::d3d::device) {
			return false;
		}

		if (texture != cachedTexture || !srv) {
			srv = nullptr;
			if (FAILED(globals::d3d::device->CreateShaderResourceView(texture, nullptr, srv.put()))) {
				logger::error("VR: Failed to create {} menu texture SRV", label);
				cachedTexture = nullptr;
				return false;
			}
			cachedTexture = texture;
		}

		return true;
	}

	winrt::com_ptr<ID3D11Texture2D> ResolveSubmitTexture2D(void* handle)
	{
		winrt::com_ptr<ID3D11Texture2D> texture;
		if (!handle) {
			return texture;
		}

		auto* unknown = static_cast<IUnknown*>(handle);
		if (SUCCEEDED(unknown->QueryInterface(__uuidof(ID3D11Texture2D), texture.put_void()))) {
			return texture;
		}

		winrt::com_ptr<ID3D11Resource> resource;
		if (SUCCEEDED(unknown->QueryInterface(__uuidof(ID3D11Resource), resource.put_void()))) {
			resource->QueryInterface(__uuidof(ID3D11Texture2D), texture.put_void());
			if (texture) {
				return texture;
			}
		}

		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		if (SUCCEEDED(unknown->QueryInterface(__uuidof(ID3D11ShaderResourceView), srv.put_void()))) {
			resource = nullptr;
			srv->GetResource(resource.put());
			if (resource) {
				resource->QueryInterface(__uuidof(ID3D11Texture2D), texture.put_void());
				if (texture) {
					return texture;
				}
			}
		}

		winrt::com_ptr<ID3D11RenderTargetView> rtv;
		if (SUCCEEDED(unknown->QueryInterface(__uuidof(ID3D11RenderTargetView), rtv.put_void()))) {
			resource = nullptr;
			rtv->GetResource(resource.put());
			if (resource) {
				resource->QueryInterface(__uuidof(ID3D11Texture2D), texture.put_void());
			}
		}

		return texture;
	}

	constexpr char kSubmitCompositeCS[] = R"(
cbuffer OverlayCompositeCB : register(b0)
{
	uint2 TargetSize;
	uint2 DispatchOrigin;
	uint2 DispatchSize;
	uint2 Padding;
	float4 QuadPixels01;
	float4 QuadPixels23;
	float4 QuadInvW;
};

Texture2D<float4> MenuTexture : register(t0);
SamplerState MenuSampler : register(s0);
RWTexture2D<float4> Target : register(u0);

bool Barycentric(float2 p, float2 a, float2 b, float2 c, out float3 bary)
{
	float2 v0 = b - a;
	float2 v1 = c - a;
	float2 v2 = p - a;
	float denom = v0.x * v1.y - v1.x * v0.y;
	if (abs(denom) < 1e-5f) {
		bary = 0.0f;
		return false;
	}

	float invDenom = rcp(denom);
	float u = (v2.x * v1.y - v1.x * v2.y) * invDenom;
	float v = (v0.x * v2.y - v2.x * v0.y) * invDenom;
	float w = 1.0f - u - v;
	bary = float3(w, u, v);
	return u >= 0.0f && v >= 0.0f && w >= 0.0f;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	if (dispatchThreadID.x >= DispatchSize.x || dispatchThreadID.y >= DispatchSize.y) {
		return;
	}

	uint2 targetPixel = DispatchOrigin + dispatchThreadID.xy;
	if (targetPixel.x >= TargetSize.x || targetPixel.y >= TargetSize.y) {
		return;
	}

	float2 p = float2(targetPixel) + 0.5f;
	float2 q0 = QuadPixels01.xy;
	float2 q1 = QuadPixels01.zw;
	float2 q2 = QuadPixels23.xy;
	float2 q3 = QuadPixels23.zw;

	float3 bary = 0.0f;
	float2 uv = 0.0f;
	if (Barycentric(p, q0, q1, q2, bary)) {
		float invW = bary.x * QuadInvW.x + bary.y * QuadInvW.y + bary.z * QuadInvW.z;
		if (abs(invW) < 1e-6f) {
			return;
		}
		uv = (bary.x * float2(0.0f, 1.0f) * QuadInvW.x +
		      bary.y * float2(0.0f, 0.0f) * QuadInvW.y +
		      bary.z * float2(1.0f, 0.0f) * QuadInvW.z) / invW;
	} else if (Barycentric(p, q0, q2, q3, bary)) {
		float invW = bary.x * QuadInvW.x + bary.y * QuadInvW.z + bary.z * QuadInvW.w;
		if (abs(invW) < 1e-6f) {
			return;
		}
		uv = (bary.x * float2(0.0f, 1.0f) * QuadInvW.x +
		      bary.y * float2(1.0f, 0.0f) * QuadInvW.z +
		      bary.z * float2(1.0f, 1.0f) * QuadInvW.w) / invW;
	} else {
		return;
	}

	uv = saturate(uv);

	float4 menuColor = MenuTexture.SampleLevel(MenuSampler, uv, 0.0f);
	menuColor.a = saturate(menuColor.a);
	if (menuColor.a <= 0.001f) {
		return;
	}

	float4 sceneColor = Target[targetPixel];
	Target[targetPixel] = float4(lerp(sceneColor.rgb, menuColor.rgb, menuColor.a), sceneColor.a);
}
)";

	struct IVRCompositor_WaitGetPoses
	{
		static vr::EVRCompositorError thunk(
			vr::IVRCompositor* _this,
			vr::TrackedDevicePose_t* pRenderPoseArray,
			uint32_t unRenderPoseArrayCount,
			vr::TrackedDevicePose_t* pGamePoseArray,
			uint32_t unGamePoseArrayCount)
		{
			const auto result = func(
				_this,
				pRenderPoseArray,
				unRenderPoseArrayCount,
				pGamePoseArray,
				unGamePoseArrayCount);
			{
				const std::scoped_lock cyclePublishLock(
					g_openVRSubmitCyclePublishMutex);
				const uint64_t previousCycleState =
					g_openVRSubmitCycleState.load(std::memory_order_acquire);
				const uint64_t previousCompositorCycleToken =
					previousCycleState >> 1u;
				const uint64_t compositorCycleToken =
					previousCompositorCycleToken == kOpenVRCycleTokenMax ?
						1u :
						previousCompositorCycleToken + 1u;
				auto& upscaling = globals::features::upscaling;
				// Service promotion before atomically publishing the token and
				// resulting policy. A concurrent Submit keeps the complete old
				// snapshot; the next one gets the complete new snapshot.
				upscaling.NotifyVRPostLoadCompositorCycleStarted(
					compositorCycleToken,
					result == vr::VRCompositorError_None);
				const bool cycleCooldownActive =
					upscaling.submitStageVendorResumeFrame.load(
						std::memory_order_acquire) != 0;
				g_openVRSubmitCycleState.store(
					(compositorCycleToken << 1u) |
						static_cast<uint64_t>(cycleCooldownActive),
					std::memory_order_release);
			}
			return result;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct IVRCompositor_Submit
	{
		static vr::EVRCompositorError thunk(vr::IVRCompositor* _this, vr::EVREye eEye, const vr::Texture_t* pTexture, const vr::VRTextureBounds_t* pBounds, vr::EVRSubmitFlags nSubmitFlags)
		{
			auto& vr = globals::features::vr;
			auto& upscaling = globals::features::upscaling;
			uint64_t compositorCycleState =
				g_openVRSubmitCycleState.load(std::memory_order_acquire);
			uint64_t compositorCycleToken = compositorCycleState >> 1u;
			bool submitStageVendorResumeCooldownAtCycleStart =
				(compositorCycleState & kOpenVRCycleCooldownBit) != 0;
			const uint64_t postLoadSubmitScopeEpoch =
				upscaling.BeginVRPostLoadCompositorSubmitScope(
					compositorCycleToken);
			const SKSE::stl::scope_exit endPostLoadSubmitScope([&]() noexcept {
				upscaling.EndVRPostLoadCompositorSubmitScope(
					postLoadSubmitScopeEpoch);
			});
			const bool postLoadHoldActiveAtHookEntry =
				upscaling.IsVRPostLoadCompositorHoldActive();
			const bool quarantinedAtHookEntry =
				upscaling.ShouldQuarantineVRPostLoadCompositorCycle(
					compositorCycleToken);
			std::unique_lock postLoadSubmitLock(
				g_vrPostLoadCompositorSubmitMutex,
				std::defer_lock);
			if (postLoadSubmitScopeEpoch != 0 ||
				postLoadHoldActiveAtHookEntry ||
				quarantinedAtHookEntry) {
				// Keep startup hold/quarantine eye decisions serialized without
				// holding a project mutex across ordinary OpenVR calls.
				postLoadSubmitLock.lock();
				compositorCycleState =
					g_openVRSubmitCycleState.load(std::memory_order_acquire);
				compositorCycleToken = compositorCycleState >> 1u;
				submitStageVendorResumeCooldownAtCycleStart =
					(compositorCycleState & kOpenVRCycleCooldownBit) != 0;
			}
			// Keep the exact policy decision, all vendor/D3D work, and the
			// selected compositor submission in one relatch transaction. This
			// also makes an accepted first-eye cycle visible before quarantine
			// arbitration for its peer.
			const auto renderScalePresentationCommitLock =
				upscaling.AcquireVRRenderScalePresentationCommitLock();
			const bool acceptedNativeRestoreCycleAtHookEntry =
				g_vrNativeRestoreCyclePresentationState
						.compositorCycleToken ==
					compositorCycleToken &&
				(g_vrNativeRestoreCyclePresentationState.path ==
						VRNativeRestoreCyclePresentationPath::Native ||
					g_vrNativeRestoreCyclePresentationState.path ==
						VRNativeRestoreCyclePresentationPath::
							BlackKeepalive);
			const auto rejectQuarantinedSubmit = [&](
													 const vr::Texture_t* a_texture,
													 const vr::VRTextureBounds_t* a_bounds) {
#ifdef DEVBENCH_BRIDGE_ENABLED
				const uint64_t probeSequence =
					upscaling.BeginVRLoadPresentationProbeSubmit(
						"compositor-quarantine",
						eEye,
						a_texture,
						a_bounds,
						nSubmitFlags,
						compositorCycleToken,
						false,
						nullptr);
				upscaling.CompleteVRLoadPresentationProbeSubmit(
					probeSequence,
					vr::VRCompositorError_RequestFailed);
#endif
				Upscaling::TraceVRMenuPresentationOpenVRSubmit(
					"post-load-cycle-quarantine",
					eEye,
					a_texture,
					a_bounds,
					nSubmitFlags,
					vr::VRCompositorError_RequestFailed);
				return vr::VRCompositorError_RequestFailed;
			};
			if (upscaling.ShouldQuarantineVRPostLoadCompositorCycle(
					compositorCycleToken) &&
				!acceptedNativeRestoreCycleAtHookEntry)
				return rejectQuarantinedSubmit(pTexture, pBounds);
			const Upscaling::VRRenderScalePresentationObservation* probePresentationObservation = nullptr;
			auto submit = [&](const char* a_path,
							  const vr::Texture_t* a_texture,
							  const vr::VRTextureBounds_t* a_bounds,
							  vr::EVRSubmitFlags a_submitFlags,
							  uint64_t a_postLoadReleaseToken = 0,
							  uint64_t a_postLoadKeepaliveToken = 0,
							  const Upscaling::VRRenderScalePresentationObservation* a_probeObservation = nullptr,
							  Upscaling::VRPostLoadCompositorKeepaliveDisposition* a_keepaliveDisposition = nullptr,
							  bool a_allowPostLoadScopeRebase = false,
							  bool a_allowScreenshotCapture = true) {
				(void)a_probeObservation;
#ifdef DEVBENCH_BRIDGE_ENABLED
				const uint64_t probeSequence = upscaling.BeginVRLoadPresentationProbeSubmit(
					a_path,
					eEye,
					a_texture,
					a_bounds,
					a_submitFlags,
					compositorCycleToken,
					true,
					a_probeObservation);
#endif
				vr::EVRCompositorError result;
				winrt::com_ptr<ID3D11Texture2D> submitTextureLifetime;
				const bool observeScreenshot =
					a_allowScreenshotCapture &&
					globals::features::screenshotFeature.HasPendingCapture();
				{
					const std::shared_lock renderTargetReadLock(
						Hooks::GetRenderTargetRecreationMutex());
					if (observeScreenshot &&
						a_texture &&
						a_texture->handle &&
						a_texture->eType == vr::TextureType_DirectX) {
						submitTextureLifetime = ResolveSubmitTexture2D(a_texture->handle);
					}
					result = func(
						_this,
						eEye,
						a_texture,
						a_bounds,
						a_submitFlags);
					if (result == vr::VRCompositorError_None &&
						observeScreenshot &&
						submitTextureLifetime &&
						globals::features::screenshotFeature.HasPendingCapture()) {
						globals::features::screenshotFeature.ObserveAcceptedVRSubmit(
							compositorCycleToken,
							eEye,
							submitTextureLifetime.get(),
							a_bounds,
							a_texture->eColorSpace);
					}
					// The desktop-window observer, at the SAME acceptance
					// boundary and independent of whether a screenshot is
					// pending. Only an eye OpenVR actually accepted may be shown
					// on the desktop - the existence of an intermediate texture
					// proves nothing about its currency, and it can be retired or
					// recreated underneath us.
					if (result == vr::VRCompositorError_None &&
						a_texture &&
						a_texture->handle &&
						a_texture->eType == vr::TextureType_DirectX) {
						upscaling.ObserveAcceptedVRSubmitForDesktop(
							compositorCycleToken,
							eEye,
							static_cast<ID3D11Texture2D*>(a_texture->handle));
					}
				}
				uint64_t completionScopeEpoch =
					postLoadSubmitScopeEpoch;
				if (a_allowPostLoadScopeRebase &&
					completionScopeEpoch != 0 &&
					!upscaling.IsVRInitialLoadPresentationProtectionActive() &&
					!upscaling.IsVRPostLoadCompositorHoldActive()) {
					// ShouldSuppress may deliberately retire the entry scope
					// before this exact native-restore Submit. Do not let that
					// stale, now-inactive epoch quarantine the accepted eye.
					// A newly armed/active hold remains non-rebasable and is
					// conservatively quarantined by Complete.
					completionScopeEpoch = 0;
				}
				const auto keepaliveDisposition =
					upscaling.CompleteVRPostLoadCompositorSubmit(
						eEye,
						result,
						a_postLoadReleaseToken,
						a_postLoadKeepaliveToken,
						compositorCycleToken,
						completionScopeEpoch);
				if (a_keepaliveDisposition)
					*a_keepaliveDisposition = keepaliveDisposition;
#ifdef DEVBENCH_BRIDGE_ENABLED
				upscaling.CompleteVRLoadPresentationProbeSubmit(probeSequence, result);
#endif
				Upscaling::TraceVRMenuPresentationOpenVRSubmit(
					a_path,
					eEye,
					a_texture,
					a_bounds,
					a_submitFlags,
					result);
				return result;
			};
			auto suppressPostLoadSubmit = [&](
											  const char* a_candidatePath,
											  const vr::Texture_t* a_texture,
											  const vr::VRTextureBounds_t* a_bounds,
											  const Upscaling::VRPostLoadCompositorKeepaliveSubmission& a_keepalive,
											  bool a_recordCandidateObservationOnFallback,
											  bool a_allowCandidateFallback = true) {
				if (a_keepalive.quarantine)
					return rejectQuarantinedSubmit(a_texture, a_bounds);
#ifdef DEVBENCH_BRIDGE_ENABLED
				// Queue the candidate for inspection, but deliberately leave its
				// compositor result unknown because OpenVR is not called.
				(void)upscaling.BeginVRLoadPresentationProbeSubmit(
					"compositor-hold",
					eEye,
					a_texture,
					a_bounds,
					nSubmitFlags,
					compositorCycleToken,
					false,
					probePresentationObservation);
#endif
				const auto submitCandidateFallback = [&]() {
					const auto fallbackResult = submit(
						a_candidatePath,
						a_texture,
						a_bounds,
						nSubmitFlags,
						0,
						0,
						probePresentationObservation);
					if (fallbackResult == vr::VRCompositorError_None &&
						a_recordCandidateObservationOnFallback &&
						probePresentationObservation &&
						probePresentationObservation->valid) {
						upscaling.RecordVRRenderScalePresentationObservation(
							*probePresentationObservation);
					}
					return fallbackResult;
				};

				// ShouldSuppress supplies either an explicit one-cycle
				// quarantine or a real black submission. Preserve continuity if
				// neither invariant is satisfied instead of manufacturing a
				// successful no-submit result.
				if (!a_keepalive.IsValid()) {
					return a_allowCandidateFallback ?
					           submitCandidateFallback() :
					           vr::VRCompositorError_RequestFailed;
				}

				Upscaling::VRPostLoadCompositorKeepaliveDisposition
					keepaliveDisposition =
						Upscaling::VRPostLoadCompositorKeepaliveDisposition::
							NotApplicable;
				const auto keepaliveResult = submit(
					"compositor-keepalive",
					&a_keepalive.texture,
					&a_keepalive.bounds,
					vr::Submit_Default,
					0,
					a_keepalive.token,
					nullptr,
					&keepaliveDisposition,
					false,
					false);
				switch (keepaliveDisposition) {
				case Upscaling::VRPostLoadCompositorKeepaliveDisposition::
					Accepted:
					return keepaliveResult;
				case Upscaling::VRPostLoadCompositorKeepaliveDisposition::
					AlreadySatisfied:
					return vr::VRCompositorError_None;
				case Upscaling::VRPostLoadCompositorKeepaliveDisposition::
					RejectedCanFallback:
					// OpenVR rejected the keepalive before either eye was
					// accepted. Fail open once to the exact candidate.
					return a_allowCandidateFallback ?
					           submitCandidateFallback() :
					           keepaliveResult;
				default:
					// Transient, peer-satisfied, and interface-level failures
					// must not issue a second, visually different Submit.
					return keepaliveResult;
				}
			};

			// DevBench may install the interception before the production render
			// path needs it so startup/loading submissions can be observed. Until
			// the ordinary call site enables processing, preserve those submits
			// unchanged and use the hook only as a probe boundary.
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (!g_openVRSubmitProcessingEnabled.load(
					std::memory_order_acquire) &&
				!acceptedNativeRestoreCycleAtHookEntry)
				return submit("original", pTexture, pBounds, nSubmitFlags);
#endif

			Upscaling::VRRenderScalePresentationObservation
				presentationObservation{};
			bool nativeRestoreCandidate =
				upscaling.PrepareVRNativeRestorePresentationObservation(
					eEye,
					compositorCycleToken,
					pTexture,
					pBounds,
					presentationObservation);
			auto originalSubmitDecision =
				upscaling.ClassifyVRRenderScaleOriginalSubmitFallback(
					eEye,
					pTexture,
					pBounds);
			bool nativeRestoreGuardActive =
				originalSubmitDecision.IsNativeRestoreGuarded();
			bool nativeRestoreContinuityCandidate =
				originalSubmitDecision.IsNativeRestoreContinuity();
			uint64_t nativeRestoreGuardEpoch =
				originalSubmitDecision.nativeRestoreGuardEpoch;
			const auto refreshOriginalSubmitDecision = [&]() {
				originalSubmitDecision =
					upscaling.ClassifyVRRenderScaleOriginalSubmitFallback(
						eEye,
						pTexture,
						pBounds);
				nativeRestoreGuardActive =
					originalSubmitDecision.IsNativeRestoreGuarded();
				nativeRestoreContinuityCandidate =
					originalSubmitDecision.IsNativeRestoreContinuity();
				nativeRestoreGuardEpoch =
					originalSubmitDecision.nativeRestoreGuardEpoch;
				if (nativeRestoreGuardActive) {
					Upscaling::VRRenderScalePresentationObservation
						freshNativeObservation{};
					nativeRestoreCandidate =
						upscaling.PrepareVRNativeRestorePresentationObservation(
							eEye,
							compositorCycleToken,
							pTexture,
							pBounds,
							freshNativeObservation);
					// Replace the prior evidence even when the fresh candidate is
					// invalid. A guard may have armed after vendor evaluation, and
					// stale vendor/native evidence must not cross that epoch
					// boundary into post-load arbitration or stabilization.
					presentationObservation = freshNativeObservation;
				} else {
					nativeRestoreCandidate = false;
				}
			};
			auto& nativeRestoreCycle =
				g_vrNativeRestoreCyclePresentationState;
			const auto nativeRestoreTransition =
				upscaling.GetVRRenderScaleTransitionSnapshot();
			const uint32_t nativeRestoreContractGeneration =
				nativeRestoreGuardActive &&
						nativeRestoreTransition.applied.valid &&
						nativeRestoreTransition.applied.transitionEpoch ==
							nativeRestoreGuardEpoch ?
					nativeRestoreTransition.applied.contractGeneration :
					0u;
			const bool currentCycleAlreadyOwned =
				nativeRestoreCycle.path !=
					VRNativeRestoreCyclePresentationPath::Unset &&
				nativeRestoreCycle.compositorCycleToken ==
					compositorCycleToken;
			if (!currentCycleAlreadyOwned) {
				nativeRestoreCycle.Reset();
				if (nativeRestoreGuardActive) {
					nativeRestoreCycle.guardEpoch =
						nativeRestoreGuardEpoch;
					nativeRestoreCycle.compositorCycleToken =
						compositorCycleToken;
					nativeRestoreCycle.contractGeneration =
						nativeRestoreContractGeneration;
				}
			}
			const auto rejectNativeRestoreCycle = [&](
													  vr::EVRCompositorError a_result,
													  uint64_t a_postLoadKeepaliveToken = 0) {
				nativeRestoreCycle.path =
					VRNativeRestoreCyclePresentationPath::Rejected;
				nativeRestoreCycle.rejectionResult =
					a_result == vr::VRCompositorError_None ?
						vr::VRCompositorError_RequestFailed :
						a_result;
				nativeRestoreCycle.postLoadKeepaliveToken =
					a_postLoadKeepaliveToken;
				nativeRestoreCycle.texture = {};
				nativeRestoreCycle.textureHasPose = false;
				nativeRestoreCycle.textureWithPose = {};
				nativeRestoreCycle.nativeBounds = {};
				nativeRestoreCycle.nativeSourceEye = vr::Eye_Left;
				nativeRestoreCycle.nativeSubmitFlags =
					vr::Submit_Default;
				nativeRestoreCycle.lifetime = nullptr;
				return nativeRestoreCycle.rejectionResult;
			};
			const auto commitAcceptedNativeRestoreCycle =
				[&](
					VRNativeRestoreCyclePresentationPath a_path,
					const winrt::com_ptr<ID3D11Texture2D>& a_lifetime,
					const vr::Texture_t& a_texture,
					const vr::VRTextureBounds_t& a_bounds,
					vr::EVREye a_sourceEye,
					vr::EVRSubmitFlags a_submitFlags,
					uint64_t a_postLoadKeepaliveToken = 0,
					const vr::VRTextureWithPose_t* a_textureWithPose =
						nullptr) {
					const bool textureHasPose =
						(static_cast<uint32_t>(a_submitFlags) &
							static_cast<uint32_t>(
								vr::Submit_TextureWithPose)) != 0;
					if ((a_path !=
								VRNativeRestoreCyclePresentationPath::
									Native &&
							a_path !=
								VRNativeRestoreCyclePresentationPath::
									BlackKeepalive) ||
						!a_lifetime ||
						!a_texture.handle ||
						(textureHasPose && !a_textureWithPose)) {
						return false;
					}

					nativeRestoreCycle.path = a_path;
					nativeRestoreCycle.rejectionResult =
						vr::VRCompositorError_RequestFailed;
					nativeRestoreCycle.postLoadKeepaliveToken =
						a_postLoadKeepaliveToken;
					nativeRestoreCycle.lifetime = a_lifetime;
					nativeRestoreCycle.texture = a_texture;
					nativeRestoreCycle.texture.handle =
						nativeRestoreCycle.lifetime.get();
					nativeRestoreCycle.textureHasPose =
						textureHasPose;
					nativeRestoreCycle.textureWithPose =
						textureHasPose ?
							*a_textureWithPose :
							vr::VRTextureWithPose_t{};
					if (textureHasPose) {
						nativeRestoreCycle.textureWithPose.handle =
							nativeRestoreCycle.lifetime.get();
					}
					nativeRestoreCycle.nativeBounds = a_bounds;
					nativeRestoreCycle.nativeSourceEye = a_sourceEye;
					nativeRestoreCycle.nativeSubmitFlags =
						a_submitFlags;
					return true;
				};
			const auto recordStrictNativeRestoreObservationIfCurrent =
				[&](
					uint64_t a_expectedGuardEpoch,
					uint32_t a_expectedContractGeneration,
					ID3D11Texture2D* a_expectedTexture) {
					if (a_expectedGuardEpoch == 0 ||
						!a_expectedTexture ||
						upscaling.GetVRNativeRestorePresentationGuardActiveEpoch() !=
							a_expectedGuardEpoch ||
						upscaling.IsVRInitialLoadPresentationProtectionActive() ||
						upscaling.IsVRPostLoadCompositorHoldActive() ||
						upscaling.ShouldQuarantineVRPostLoadCompositorCycle(
							compositorCycleToken)) {
						return;
					}

					winrt::com_ptr<ID3D11Texture2D> currentTexture;
					if (pTexture &&
						pTexture->handle &&
						pTexture->eType ==
							vr::TextureType_DirectX) {
						currentTexture =
							ResolveSubmitTexture2D(
								pTexture->handle);
					}
					if (!currentTexture ||
						currentTexture.get() !=
							a_expectedTexture) {
						return;
					}

					Upscaling::VRRenderScalePresentationObservation
						freshObservation{};
					if (!upscaling.PrepareVRNativeRestorePresentationObservation(
							eEye,
							compositorCycleToken,
							pTexture,
							pBounds,
							freshObservation) ||
						!freshObservation.valid ||
						freshObservation.transitionEpoch !=
							a_expectedGuardEpoch ||
						freshObservation.contractGeneration !=
							a_expectedContractGeneration ||
						upscaling.GetVRNativeRestorePresentationGuardActiveEpoch() !=
							a_expectedGuardEpoch ||
						upscaling.IsVRInitialLoadPresentationProtectionActive() ||
						upscaling.IsVRPostLoadCompositorHoldActive() ||
						upscaling.ShouldQuarantineVRPostLoadCompositorCycle(
							compositorCycleToken)) {
						return;
					}
					(void)upscaling
						.RecordVRNativeRestorePresentationObservationIfUnprotected(
							freshObservation);
				};
			const auto submitLatchedNativeRestoreCycle = [&](
															 const char* a_path,
															 bool a_recordStrictObservation = false) {
				if (nativeRestoreCycle.path ==
					VRNativeRestoreCyclePresentationPath::Rejected) {
					Upscaling::TraceVRMenuPresentationOpenVRSubmit(
						"native-restore-cycle-rejected",
						eEye,
						pTexture,
						pBounds,
						nSubmitFlags,
						nativeRestoreCycle.rejectionResult);
					return nativeRestoreCycle.rejectionResult;
				}
				if (!nativeRestoreCycle.lifetime ||
					!nativeRestoreCycle.texture.handle ||
					(nativeRestoreCycle.path !=
							VRNativeRestoreCyclePresentationPath::
								Native &&
						nativeRestoreCycle.path !=
							VRNativeRestoreCyclePresentationPath::
								BlackKeepalive)) {
					return vr::VRCompositorError_RequestFailed;
				}
				const vr::VRTextureBounds_t bounds =
					nativeRestoreCycle.path ==
							VRNativeRestoreCyclePresentationPath::Native ?
						GetCombinedStereoEyeBounds(
							eEye,
							nativeRestoreCycle.nativeSourceEye,
							nativeRestoreCycle.nativeBounds) :
						nativeRestoreCycle.nativeBounds;
				const auto submitFlags =
					nativeRestoreCycle.path ==
							VRNativeRestoreCyclePresentationPath::Native ?
						nativeRestoreCycle.nativeSubmitFlags :
						vr::Submit_Default;
				const vr::Texture_t* retainedSubmitTexture =
					nativeRestoreCycle.textureHasPose ?
						static_cast<const vr::Texture_t*>(
							&nativeRestoreCycle.textureWithPose) :
						&nativeRestoreCycle.texture;
				const auto result = submit(
					a_path,
					retainedSubmitTexture,
					&bounds,
					submitFlags,
					0,
					nativeRestoreCycle.postLoadKeepaliveToken,
					a_recordStrictObservation ?
						&presentationObservation :
						nullptr,
					nullptr,
					nativeRestoreCycle.postLoadKeepaliveToken == 0,
					nativeRestoreCycle.path == VRNativeRestoreCyclePresentationPath::Native);
				if (result == vr::VRCompositorError_None &&
					a_recordStrictObservation &&
					presentationObservation.valid) {
					recordStrictNativeRestoreObservationIfCurrent(
						nativeRestoreCycle.guardEpoch,
						nativeRestoreCycle.contractGeneration,
						nativeRestoreCycle.lifetime.get());
				} else if (
					result != vr::VRCompositorError_None &&
					nativeRestoreCycle.path ==
						VRNativeRestoreCyclePresentationPath::Native &&
					nativeRestoreCycle.guardEpoch != 0) {
					upscaling.RecordVRNativeRestorePresentationRejection(
						compositorCycleToken,
						nativeRestoreCycle.guardEpoch,
						"OpenVR rejected the retained native peer eye");
				}
				return result;
			};
			const auto submitNativeRestoreKeepalive = [&]() {
				if (nativeRestoreCycle.path ==
						VRNativeRestoreCyclePresentationPath::Native ||
					nativeRestoreCycle.path ==
						VRNativeRestoreCyclePresentationPath::
							BlackKeepalive ||
					nativeRestoreCycle.path ==
						VRNativeRestoreCyclePresentationPath::Rejected) {
					return submitLatchedNativeRestoreCycle(
						nativeRestoreCycle.path ==
								VRNativeRestoreCyclePresentationPath::
									Native ?
							"native-restore-cycle-native" :
							"native-restore-cycle-black");
				}
				Upscaling::VRNativeRestoreCompositorKeepaliveSubmission
					keepalive{};
				if (!upscaling.PrepareVRNativeRestoreCompositorKeepalive(
						nativeRestoreGuardEpoch,
						compositorCycleToken,
						pTexture,
						pBounds,
						keepalive)) {
					Upscaling::TraceVRMenuPresentationOpenVRSubmit(
						"native-restore-keepalive-unavailable",
						eEye,
						pTexture,
						pBounds,
						nSubmitFlags,
						vr::VRCompositorError_RequestFailed);
					return rejectNativeRestoreCycle(
						vr::VRCompositorError_RequestFailed);
				}
				const auto result = submit(
					"native-restore-keepalive",
					&keepalive.texture,
					&keepalive.bounds,
					vr::Submit_Default,
					0,
					0,
					nullptr,
					nullptr,
					true,
					false);
				if (result != vr::VRCompositorError_None) {
					rejectNativeRestoreCycle(result);
					upscaling.RecordVRNativeRestorePresentationRejection(
						compositorCycleToken,
						nativeRestoreGuardEpoch,
						"OpenVR rejected the first native-restore black keepalive eye");
					return result;
				}
				if (!commitAcceptedNativeRestoreCycle(
						VRNativeRestoreCyclePresentationPath::
							BlackKeepalive,
						keepalive.lifetime,
						keepalive.texture,
						keepalive.bounds,
						eEye,
						vr::Submit_Default)) {
					rejectNativeRestoreCycle(
						vr::VRCompositorError_RequestFailed);
					upscaling.RecordVRNativeRestorePresentationRejection(
						compositorCycleToken,
						nativeRestoreGuardEpoch,
						"accepted black keepalive eye could not establish retained cycle ownership");
				}
				return result;
			};
			const auto submitNewNativeRestoreCycle = [&](
														 bool a_recordStrictObservation) {
				if (!pTexture ||
					!pTexture->handle ||
					pTexture->eType != vr::TextureType_DirectX ||
					!pBounds) {
					upscaling.RecordVRNativeRestorePresentationRejection(
						compositorCycleToken,
						nativeRestoreGuardEpoch,
						"native candidate could not be retained for cycle ownership");
					return submitNativeRestoreKeepalive();
				}
				auto lifetime =
					ResolveSubmitTexture2D(
						pTexture->handle);
				if (!lifetime) {
					upscaling.RecordVRNativeRestorePresentationRejection(
						compositorCycleToken,
						nativeRestoreGuardEpoch,
						"native candidate texture identity could not be retained");
					return submitNativeRestoreKeepalive();
				}

				vr::Texture_t retainedTexture = *pTexture;
				retainedTexture.handle = lifetime.get();
				const bool textureHasPose =
					(static_cast<uint32_t>(nSubmitFlags) &
						static_cast<uint32_t>(
							vr::Submit_TextureWithPose)) != 0;
				vr::VRTextureWithPose_t retainedTextureWithPose{};
				const vr::Texture_t* retainedSubmitTexture =
					&retainedTexture;
				if (textureHasPose) {
					retainedTextureWithPose =
						*static_cast<const vr::VRTextureWithPose_t*>(
							pTexture);
					retainedTextureWithPose.handle =
						lifetime.get();
					retainedSubmitTexture =
						static_cast<const vr::Texture_t*>(
							&retainedTextureWithPose);
				}
				const auto result = submit(
					"native-restore-cycle-native",
					retainedSubmitTexture,
					pBounds,
					nSubmitFlags,
					0,
					0,
					a_recordStrictObservation ?
						&presentationObservation :
						nullptr,
					nullptr,
					true);
				if (result != vr::VRCompositorError_None) {
					rejectNativeRestoreCycle(result);
					upscaling.RecordVRNativeRestorePresentationRejection(
						compositorCycleToken,
						nativeRestoreGuardEpoch,
						"OpenVR rejected the first guarded native eye");
					return result;
				}
				if (!commitAcceptedNativeRestoreCycle(
						VRNativeRestoreCyclePresentationPath::Native,
						lifetime,
						retainedTexture,
						*pBounds,
						eEye,
						nSubmitFlags,
						0,
						textureHasPose ?
							&retainedTextureWithPose :
							nullptr)) {
					rejectNativeRestoreCycle(
						vr::VRCompositorError_RequestFailed);
					upscaling.RecordVRNativeRestorePresentationRejection(
						compositorCycleToken,
						nativeRestoreGuardEpoch,
						"accepted native eye could not establish retained cycle ownership");
					return result;
				}
				if (a_recordStrictObservation &&
					presentationObservation.valid) {
					recordStrictNativeRestoreObservationIfCurrent(
						nativeRestoreCycle.guardEpoch,
						nativeRestoreCycle.contractGeneration,
						nativeRestoreCycle.lifetime.get());
				}
				return result;
			};
			if (nativeRestoreCycle.path ==
				VRNativeRestoreCyclePresentationPath::Rejected) {
				if (nativeRestoreGuardActive &&
					nativeRestoreCycle.postLoadKeepaliveToken == 0 &&
					!upscaling.IsVRInitialLoadPresentationProtectionActive() &&
					!upscaling.IsVRPostLoadCompositorHoldActive() &&
					!upscaling.ShouldQuarantineVRPostLoadCompositorCycle(
						compositorCycleToken)) {
					upscaling.RecordVRNativeRestorePresentationRejection(
						compositorCycleToken,
						nativeRestoreGuardEpoch,
						"native restore cycle has an unproven first-eye submission");
				}
				return submitLatchedNativeRestoreCycle(
					"native-restore-cycle-rejected");
			}
			if (nativeRestoreCycle.path ==
				VRNativeRestoreCyclePresentationPath::
					BlackKeepalive) {
				if (nativeRestoreGuardActive &&
					nativeRestoreCycle.postLoadKeepaliveToken == 0 &&
					!upscaling.IsVRInitialLoadPresentationProtectionActive() &&
					!upscaling.IsVRPostLoadCompositorHoldActive() &&
					!upscaling.ShouldQuarantineVRPostLoadCompositorCycle(
						compositorCycleToken)) {
					upscaling.RecordVRNativeRestorePresentationRejection(
						compositorCycleToken,
						nativeRestoreGuardEpoch,
						"native restore cycle is already owned by black keepalive");
				}
				return submitLatchedNativeRestoreCycle(
					"native-restore-cycle-black");
			}
			if (nativeRestoreCycle.path ==
				VRNativeRestoreCyclePresentationPath::Native) {
				winrt::com_ptr<ID3D11Texture2D> currentTexture;
				if (nativeRestoreContinuityCandidate &&
					pTexture &&
					pTexture->handle &&
					pTexture->eType ==
						vr::TextureType_DirectX) {
					currentTexture =
						ResolveSubmitTexture2D(
							pTexture->handle);
				}
				const bool sameNativeCycleSource =
					nativeRestoreContinuityCandidate &&
					currentTexture &&
					currentTexture.get() ==
						nativeRestoreCycle.lifetime.get();
				if (!sameNativeCycleSource) {
					if (nativeRestoreGuardActive) {
						upscaling.RecordVRNativeRestorePresentationRejection(
							compositorCycleToken,
							nativeRestoreGuardEpoch,
							"native restore peer did not match the cycle-owned native source");
					}
				}
				const bool recordStrictObservation =
					sameNativeCycleSource &&
					nativeRestoreGuardActive &&
					nativeRestoreCandidate &&
					presentationObservation.valid &&
					presentationObservation.transitionEpoch ==
						nativeRestoreCycle.guardEpoch &&
					presentationObservation.contractGeneration ==
						nativeRestoreCycle.contractGeneration;
				return submitLatchedNativeRestoreCycle(
					"native-restore-cycle-native",
					recordStrictObservation);
			}
			if (!upscaling.IsVRPostLoadCompositorHoldActive() &&
				!upscaling.IsVRRenderScaleModeLatched() &&
				!upscaling.IsPresentationUpscalingActive() &&
				!ShouldRenderInSceneMenu(vr) &&
				!nativeRestoreGuardActive) {
				return submit("original", pTexture, pBounds, nSubmitFlags);
			}

			uint64_t postLoadReleaseToken = 0;
			Upscaling::VRPostLoadCompositorKeepaliveSubmission postLoadKeepalive{};

			// Only process DirectX textures - skip OpenGL/Vulkan to avoid undefined behavior
			if (pTexture && pTexture->handle && pTexture->eType == vr::TextureType_DirectX) {
				vr::Texture_t upscaledTexture{};
				vr::VRTextureBounds_t upscaledBounds{};
				if (!presentationObservation.valid &&
					upscaling.SubmitVRUpscaledFrame(eEye, compositorCycleToken, submitStageVendorResumeCooldownAtCycleStart, pTexture, pBounds, upscaledTexture, upscaledBounds, presentationObservation)) {
					refreshOriginalSubmitDecision();
					if (!nativeRestoreGuardActive) {
						probePresentationObservation = &presentationObservation;
						if (upscaling.ShouldSuppressVRPostLoadCompositorSubmit(
								eEye,
								&upscaledTexture,
								&upscaledBounds,
								&presentationObservation,
								compositorCycleToken,
								postLoadReleaseToken,
								postLoadKeepalive)) {
							return suppressPostLoadSubmit(
								"upscaled",
								&upscaledTexture,
								&upscaledBounds,
								postLoadKeepalive,
								true);
						}
						refreshOriginalSubmitDecision();
						if (!nativeRestoreGuardActive) {
							bool inSceneOverlayComposited = false;
							if (postLoadReleaseToken == 0 &&
								ShouldRenderInSceneMenu(vr) &&
								upscaledTexture.handle &&
								upscaledTexture.eType == vr::TextureType_DirectX) {
								vr.RenderInSceneOverlay(
									eEye,
									static_cast<ID3D11Texture2D*>(upscaledTexture.handle),
									&upscaledBounds,
									nullptr,
									&inSceneOverlayComposited);
							}
							const auto result = submit(
								"upscaled",
								&upscaledTexture,
								&upscaledBounds,
								nSubmitFlags,
								postLoadReleaseToken,
								0,
								&presentationObservation);
							if (result == vr::VRCompositorError_None) {
								upscaling.RecordVRRenderScalePresentationObservation(presentationObservation);
								if (inSceneOverlayComposited) {
									vr.MarkAutoHideOverlayPresented();
								}
							}
							return result;
						}
					}

					// A native-restore guard armed while the vendor work was in
					// flight. Discard that output and validate the exact original
					// candidate under the guarded fallback policy below.
					if (!nativeRestoreCandidate)
						presentationObservation = {};
				}

				// Submit-stage work can span a relatch boundary. Classify the
				// exact original candidate again at the fallback commit point
				// and use this one epoch-tagged decision for every downstream
				// suppression, overlay, and watchdog action.
				refreshOriginalSubmitDecision();

				probePresentationObservation =
					presentationObservation.valid ? &presentationObservation : nullptr;
				if (upscaling.ShouldSuppressVRPostLoadCompositorSubmit(
						eEye,
						pTexture,
						pBounds,
						presentationObservation.valid ? &presentationObservation : nullptr,
						compositorCycleToken,
						postLoadReleaseToken,
						postLoadKeepalive)) {
					const auto suppressionResult =
						suppressPostLoadSubmit(
							"original",
							pTexture,
							pBounds,
							postLoadKeepalive,
							presentationObservation.path ==
									Upscaling::VRRenderScalePresentationPath::BoundsMismatchOriginalFallback ||
								presentationObservation.path ==
									Upscaling::VRRenderScalePresentationPath::NativeOriginal,
							!nativeRestoreGuardActive);
					if (!nativeRestoreGuardActive)
						return suppressionResult;

					// During native restore, post-load arbitration may only
					// establish the black class. Candidate fallback is disabled
					// above so an accepted result is unambiguously the retained
					// keepalive (including AlreadySatisfied, whose black
					// occupancy was proven by the post-load controller).
					if (suppressionResult ==
							vr::VRCompositorError_None &&
						postLoadKeepalive.IsValid() &&
						commitAcceptedNativeRestoreCycle(
							VRNativeRestoreCyclePresentationPath::
								BlackKeepalive,
							postLoadKeepalive.lifetime,
							postLoadKeepalive.texture,
							postLoadKeepalive.bounds,
							eEye,
							vr::Submit_Default,
							postLoadKeepalive.token)) {
						return suppressionResult;
					}

					rejectNativeRestoreCycle(
						suppressionResult,
						postLoadKeepalive.token);
					upscaling.RecordVRNativeRestorePresentationRejection(
						compositorCycleToken,
						nativeRestoreGuardEpoch,
						"post-load arbitration did not prove first-eye black ownership");
					return suppressionResult;
				}

				if (postLoadReleaseToken == 0 &&
					!nativeRestoreGuardActive &&
					upscaling.IsSubmitStageDeviceLost() &&
					upscaling.IsVRRenderScaleModeActive()) {
					static std::atomic_bool loggedDeviceLostFallback{ false };
					if (!loggedDeviceLostFallback.exchange(true, std::memory_order_relaxed)) {
						logger::warn(
							"[VRRenderScale] Submit-stage device loss left no final-size output while render-scale is active; submitting the current original texture so the compositor cannot reuse a stale frame.");
					}
					// OpenComposite ASW reprojects the last accepted texture when an
					// application omits Submit. Never synthesize success here: make a
					// real submission and propagate the compositor's actual result.
					return submit(
						"current-original-device-lost-fallback",
						pTexture,
						pBounds,
						nSubmitFlags);
				}

				// Post-load arbitration may itself span a controller change.
				// Refresh at the final original/overlay commit point.
				refreshOriginalSubmitDecision();
				if (postLoadReleaseToken == 0 &&
					nativeRestoreGuardActive &&
					upscaling.ShouldQuarantineVRPostLoadCompositorCycle(
						compositorCycleToken)) {
					rejectNativeRestoreCycle(
						vr::VRCompositorError_RequestFailed);
					return rejectQuarantinedSubmit(
						pTexture,
						pBounds);
				}
				const bool suppressOriginalFallback =
					postLoadReleaseToken == 0 &&
					originalSubmitDecision.ShouldSuppress();
				if (suppressOriginalFallback) {
					if (nativeRestoreGuardActive) {
						upscaling.RecordVRNativeRestorePresentationRejection(
							compositorCycleToken,
							nativeRestoreGuardEpoch,
							"native output identity, dimensions, bounds, or content proof rejected");
						return submitNativeRestoreKeepalive();
					}
					static std::atomic_bool loggedRenderScaleCurrentFallback{ false };
					if (!loggedRenderScaleCurrentFallback.exchange(true, std::memory_order_relaxed)) {
						logger::warn(
							"[VRRenderScale] Final-size presentation is unavailable during a transition or menu transaction; truthfully submitting the current reduced candidate as a lower-fidelity fail-open.");
					}
					// OpenVR accepts normalized bounds independently of the texture's pixel
					// dimensions. A real submission of the current DirectX candidate is a
					// truthful, lower-fidelity fail-open; omitting Submit instead asks the
					// compositor/ASW to reuse an unrelated stale frame.
					return submit(
						"reduced-candidate-fail-open",
						pTexture,
						pBounds,
						nSubmitFlags,
						0,
						0,
						presentationObservation.valid ?
							&presentationObservation :
							nullptr);
				}

				if (postLoadReleaseToken == 0 &&
					!nativeRestoreGuardActive &&
					!upscaling.IsVRProtectedFullSizeSubmitTexture(pTexture) &&
					!upscaling.ShouldSuppressVRInSceneOverlaySubmit()) {
					vr::Texture_t overlayTexture{};
					if (vr.PrepareInSceneOverlaySubmitTexture(eEye, pTexture, pBounds, overlayTexture)) {
						const auto result = submit(
							"in-scene-overlay",
							&overlayTexture,
							pBounds,
							nSubmitFlags);
						if (result == vr::VRCompositorError_None) {
							vr.MarkAutoHideOverlayPresented();
						}
						return result;
					}
				}
			}
			if (postLoadReleaseToken == 0 &&
				nativeRestoreGuardActive &&
				upscaling.ShouldQuarantineVRPostLoadCompositorCycle(
					compositorCycleToken)) {
				rejectNativeRestoreCycle(
					vr::VRCompositorError_RequestFailed);
				return rejectQuarantinedSubmit(
					pTexture,
					pBounds);
			}
			if (nativeRestoreGuardActive && !nativeRestoreCandidate) {
				upscaling.RecordVRNativeRestorePresentationRejection(
					compositorCycleToken,
					nativeRestoreGuardEpoch,
					nativeRestoreContinuityCandidate ?
						"native continuity candidate is not yet strict stabilization evidence" :
						"native restore candidate is not a validated DirectX output");
				if (postLoadReleaseToken == 0 &&
					!nativeRestoreContinuityCandidate) {
					return submitNativeRestoreKeepalive();
				}
			}
			if (nativeRestoreGuardActive &&
				postLoadReleaseToken == 0) {
				if (!nativeRestoreContinuityCandidate) {
					upscaling.RecordVRNativeRestorePresentationRejection(
						compositorCycleToken,
						nativeRestoreGuardEpoch,
						"native restore candidate could not establish exact continuity");
					return submitNativeRestoreKeepalive();
				}
				return submitNewNativeRestoreCycle(
					nativeRestoreCandidate);
			}
			probePresentationObservation =
				presentationObservation.valid ? &presentationObservation : nullptr;
			const auto result = submit(
				"original",
				pTexture,
				pBounds,
				nSubmitFlags,
				postLoadReleaseToken,
				0,
				presentationObservation.valid ? &presentationObservation : nullptr);
			if (result == vr::VRCompositorError_None &&
				presentationObservation.path ==
					Upscaling::VRRenderScalePresentationPath::
						NativeOriginal) {
				winrt::com_ptr<ID3D11Texture2D>
					acceptedNativeTexture;
				if (pTexture &&
					pTexture->handle &&
					pTexture->eType ==
						vr::TextureType_DirectX) {
					acceptedNativeTexture =
						ResolveSubmitTexture2D(
							pTexture->handle);
				}
				recordStrictNativeRestoreObservationIfCurrent(
					presentationObservation.transitionEpoch,
					presentationObservation.contractGeneration,
					acceptedNativeTexture.get());
			} else if (
				result == vr::VRCompositorError_None &&
				presentationObservation.path ==
					Upscaling::VRRenderScalePresentationPath::
						BoundsMismatchOriginalFallback) {
				upscaling.RecordVRRenderScalePresentationObservation(
					presentationObservation);
			} else if (nativeRestoreGuardActive &&
					   result != vr::VRCompositorError_None) {
				upscaling.RecordVRNativeRestorePresentationRejection(
					compositorCycleToken,
					nativeRestoreGuardEpoch,
					"OpenVR rejected guarded native presentation");
			}
			return result;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

void VR::InitInSceneResources()
{
	if (inSceneResources.initialized)
		return;

	InSceneResources temp = {};

	auto device = globals::d3d::device;

	// 1. Compile shaders - compile VS to get bytecode for input layout, PS separately
	ID3DBlob* vsBlob = nullptr;
	ID3DBlob* psBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;

	// Compile vertex shader
	if (FAILED(D3DCompileFromFile(L"Data\\Shaders\\VR\\InSceneOverlay.vs.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
			"main", "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &errorBlob))) {
		if (errorBlob) {
			logger::error("VR InScene VS compile error: {}", (char*)errorBlob->GetBufferPointer());
			errorBlob->Release();
		}
		return;
	}
	if (errorBlob) {
		errorBlob->Release();
		errorBlob = nullptr;
	}

	// Compile pixel shader
	if (FAILED(D3DCompileFromFile(L"Data\\Shaders\\VR\\InSceneOverlay.ps.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
			"main", "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psBlob, &errorBlob))) {
		if (errorBlob) {
			logger::error("VR InScene PS compile error: {}", (char*)errorBlob->GetBufferPointer());
			errorBlob->Release();
		}
		if (vsBlob)
			vsBlob->Release();
		return;
	}
	if (errorBlob) {
		errorBlob->Release();
		errorBlob = nullptr;
	}

	// Create shader objects from bytecode
	ID3D11VertexShader* vs = nullptr;
	ID3D11PixelShader* ps = nullptr;
	if (FAILED(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs)) ||
		FAILED(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps))) {
		logger::error("VR: Failed to create shader objects");
		if (vs)
			vs->Release();
		if (ps)
			ps->Release();
		if (vsBlob)
			vsBlob->Release();
		if (psBlob)
			psBlob->Release();
		return;
	}

	temp.vs.attach(vs);
	temp.ps.attach(ps);
	if (psBlob)
		psBlob->Release();  // Don't need PS blob anymore

	// 2. Input Layout
	D3D11_INPUT_ELEMENT_DESC polygonLayout[2];
	polygonLayout[0].SemanticName = "POSITION";
	polygonLayout[0].SemanticIndex = 0;
	polygonLayout[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	polygonLayout[0].InputSlot = 0;
	polygonLayout[0].AlignedByteOffset = 0;
	polygonLayout[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout[0].InstanceDataStepRate = 0;

	polygonLayout[1].SemanticName = "TEXCOORD";
	polygonLayout[1].SemanticIndex = 0;
	polygonLayout[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	polygonLayout[1].InputSlot = 0;
	polygonLayout[1].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	polygonLayout[1].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout[1].InstanceDataStepRate = 0;

	if (FAILED(device->CreateInputLayout(polygonLayout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), temp.inputLayout.put()))) {
		logger::error("VR: Failed to create input layout");
		vsBlob->Release();
		return;
	}

	vsBlob->Release();

	// 3. Buffers
	// Quad Vertices (XY plane, z=0, size=1)
	struct VertexType
	{
		XMFLOAT3 position;
		XMFLOAT2 texture;
	};
	VertexType vertices[4] = {
		{ XMFLOAT3(-0.5f, -0.5f, 0.0f), XMFLOAT2(0.0f, 1.0f) },  // Bottom Left
		{ XMFLOAT3(-0.5f, 0.5f, 0.0f), XMFLOAT2(0.0f, 0.0f) },   // Top Left
		{ XMFLOAT3(0.5f, 0.5f, 0.0f), XMFLOAT2(1.0f, 0.0f) },    // Top Right
		{ XMFLOAT3(0.5f, -0.5f, 0.0f), XMFLOAT2(1.0f, 1.0f) }    // Bottom Right
	};

	D3D11_BUFFER_DESC vertexBufferDesc = {};
	vertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	vertexBufferDesc.ByteWidth = sizeof(VertexType) * 4;
	vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	D3D11_SUBRESOURCE_DATA vertexData = {};
	vertexData.pSysMem = vertices;
	if (FAILED(device->CreateBuffer(&vertexBufferDesc, &vertexData, temp.vb.put()))) {
		logger::error("VR: Failed to create vertex buffer");
		return;
	}

	unsigned long indices[6] = { 0, 1, 2, 0, 2, 3 };
	D3D11_BUFFER_DESC indexBufferDesc = {};
	indexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	indexBufferDesc.ByteWidth = sizeof(unsigned long) * 6;
	indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	D3D11_SUBRESOURCE_DATA indexData = {};
	indexData.pSysMem = indices;
	if (FAILED(device->CreateBuffer(&indexBufferDesc, &indexData, temp.ib.put()))) {
		logger::error("VR: Failed to create index buffer");
		return;
	}

	D3D11_BUFFER_DESC matrixBufferDesc = {};
	matrixBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	matrixBufferDesc.ByteWidth = sizeof(InSceneCB);
	matrixBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	matrixBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	if (FAILED(device->CreateBuffer(&matrixBufferDesc, nullptr, temp.cb.put()))) {
		logger::error("VR: Failed to create constant buffer");
		return;
	}

	// 4. States
	D3D11_BLEND_DESC blendDesc = {};
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = 0x0F;
	if (FAILED(device->CreateBlendState(&blendDesc, temp.blendState.put()))) {
		logger::error("VR: Failed to create blend state");
		return;
	}

	D3D11_DEPTH_STENCIL_DESC depthDesc = {};
	depthDesc.DepthEnable = FALSE;  // Always on top
	depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	depthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
	if (FAILED(device->CreateDepthStencilState(&depthDesc, temp.depthState.put()))) {
		logger::error("VR: Failed to create depth stencil state");
		return;
	}

	D3D11_RASTERIZER_DESC rasterDesc = {};
	rasterDesc.FillMode = D3D11_FILL_SOLID;
	rasterDesc.CullMode = D3D11_CULL_NONE;
	rasterDesc.FrontCounterClockwise = FALSE;
	rasterDesc.DepthClipEnable = TRUE;
	if (FAILED(device->CreateRasterizerState(&rasterDesc, temp.rasterizerState.put()))) {
		logger::error("VR: Failed to create rasterizer state");
		return;
	}

	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	if (FAILED(device->CreateSamplerState(&samplerDesc, temp.sampler.put()))) {
		logger::error("VR: Failed to create sampler state");
		return;
	}
	Util::SetResourceName(temp.sampler.get(), "VR::InSceneOverlaySampler");

	ID3DBlob* csBlob = nullptr;
	if (FAILED(D3DCompile(kSubmitCompositeCS, sizeof(kSubmitCompositeCS) - 1, "VRSubmitMenuCompositeCS", nullptr, nullptr,
			"main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &csBlob, &errorBlob))) {
		if (errorBlob) {
			logger::error("VR submit menu composite CS compile error: {}", static_cast<char*>(errorBlob->GetBufferPointer()));
			errorBlob->Release();
		}
		return;
	}
	if (errorBlob) {
		errorBlob->Release();
		errorBlob = nullptr;
	}
	if (FAILED(device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, temp.submitCompositeCS.put()))) {
		logger::error("VR: Failed to create submit menu composite compute shader");
		csBlob->Release();
		return;
	}
	csBlob->Release();
	Util::SetResourceName(temp.submitCompositeCS.get(), "VR::SubmitMenuCompositeCS");

	D3D11_BUFFER_DESC submitCompositeCBDesc = {};
	submitCompositeCBDesc.Usage = D3D11_USAGE_DYNAMIC;
	submitCompositeCBDesc.ByteWidth = sizeof(SubmitCompositeCB);
	submitCompositeCBDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	submitCompositeCBDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	if (FAILED(device->CreateBuffer(&submitCompositeCBDesc, nullptr, temp.submitCompositeCB.put()))) {
		logger::error("VR: Failed to create submit menu composite constant buffer");
		return;
	}
	Util::SetResourceName(temp.submitCompositeCB.get(), "VR::SubmitMenuCompositeCB");

	inSceneResources = std::move(temp);
	inSceneResources.initialized = true;
	logger::debug("VR: In-Scene Overlay resources initialized.");
}

void VR::RenderInSceneOverlay(vr::EVREye eye, ID3D11Texture2D* targetTexture, const vr::VRTextureBounds_t* bounds, ID3D11RenderTargetView* targetRTV, bool* overlayComposited)
{
	if (overlayComposited) {
		*overlayComposited = false;
	}
	auto context = globals::d3d::context;
	if (!context || !targetTexture) {
		return;
	}

	if (!inSceneResources.initialized)
		InitInSceneResources();
	if (!inSceneResources.initialized) {
		return;
	}

	// Only render if overlay should be visible
	if (!ShouldRenderInSceneMenu(*this)) {
		return;
	}

	// We can't render if we don't have HMD pose
	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	if (!openvr || !openvr->vrSystem) {
		return;
	}

	// Get HMD Pose and Eye matrices
	const bool hasState = globals::state != nullptr;
	const uint32_t currentFrame = hasState ? globals::state->frameCount : 0;
	const bool shouldRefreshPoses =
		!hasState ||
		!inSceneResources.cachedPosesValid ||
		inSceneResources.cachedPoseFrame != currentFrame;

	if (shouldRefreshPoses) {
		auto* compositor = RE::BSOpenVR::GetIVRCompositor();
		if (!compositor) {
			compositor = openvr->vrContext.vrCompositor;
		}
		if (!compositor) {
			return;
		}

		auto compositorError = compositor->GetLastPoses(
			inSceneResources.cachedRenderPoses,
			vr::k_unMaxTrackedDeviceCount,
			nullptr,
			0);
		if (compositorError != vr::VRCompositorError_None) {
			return;
		}

		inSceneResources.cachedPoseFrame = currentFrame;
		inSceneResources.cachedPosesValid = true;
	}

	const vr::TrackedDevicePose_t& hmdPose = inSceneResources.cachedRenderPoses[vr::k_unTrackedDeviceIndex_Hmd];
	if (!hmdPose.bPoseIsValid) {
		return;
	}

	Matrix hmdWorld = Matrix::Identity;
	Matrix eyeToHead = Matrix::Identity;
	Matrix proj = Matrix::Identity;
	Matrix vpHeadSpace = Matrix::Identity;   // For HMD-relative rendering (head space)
	Matrix vpWorldSpace = Matrix::Identity;  // For world/controller rendering (world space)

	// Always get Eye and Projection matrices
	eyeToHead = Util::HmdMatrix34ToMatrix(openvr->vrSystem->GetEyeToHeadTransform(eye));

	// Use GetProjectionRaw to build a DirectX-compatible projection matrix (Depth [0, 1])
	// IMPORTANT: OpenVR GetProjectionRaw has a known bug (Valve issue #110, open since 2016):
	// The 3rd parameter (named "pTop") actually returns the BOTTOM tangent, and
	// the 4th parameter (named "pBottom") actually returns the TOP tangent.
	// We name our variables to match the ACTUAL values, not the misleading parameter names.
	float left, right, bottom, top;
	openvr->vrSystem->GetProjectionRaw(eye, &left, &right, &bottom, &top);
	float nearZ = 0.1f;
	float farZ = 1000.0f;

	proj = DirectX::XMMatrixPerspectiveOffCenterRH(left * nearZ, right * nearZ, bottom * nearZ, top * nearZ, nearZ, farZ);

	// Log projection values once per eye
	static bool projLogged[2] = { false, false };
	if (!projLogged[(int)eye]) {
		logger::debug("VR Projection Eye {}: L={:.4f} R={:.4f} B={:.4f} T={:.4f}, EyeX={:.4f}",
			(int)eye, left, right, bottom, top, eyeToHead._41);
		projLogged[(int)eye] = true;
	}

	// Head-space VP (for HMD-relative mode)
	vpHeadSpace = eyeToHead.Invert() * proj;

	// World-space VP (for controller attach and fixed world position modes)
	if (hmdPose.bPoseIsValid) {
		hmdWorld = Util::HmdMatrix34ToMatrix(hmdPose.mDeviceToAbsoluteTracking);
		// SimpleMath uses row-vector transforms, so compose local-to-world as
		// eye -> head -> tracking world. Reversing this leaves the eye offset in
		// tracking axes and breaks stereo when the HMD is rotated.
		Matrix eyeToWorld = eyeToHead * hmdWorld;
		vpWorldSpace = eyeToWorld.Invert() * proj;
	}

	// Get or create cached RTV for the target texture
	D3D11_TEXTURE2D_DESC texDesc;
	targetTexture->GetDesc(&texDesc);

	int eyeIdx = (int)eye;
	if (eyeIdx < 0 || eyeIdx >= 2) {
		return;
	}

	ID3D11RenderTargetView* rtvPtr = targetRTV;
	if (!rtvPtr) {
		auto& cachedRTV = inSceneResources.cachedEyeRTVs[eyeIdx];
		if (cachedRTV.texture != targetTexture) {
			cachedRTV.rtv = nullptr;
			cachedRTV.texture = nullptr;

			winrt::com_ptr<ID3D11Device> targetDevice;
			targetTexture->GetDevice(targetDevice.put());
			auto* rtvDevice = targetDevice.get() ? targetDevice.get() : globals::d3d::device;
			if (!rtvDevice) {
				return;
			}

			const DXGI_FORMAT rtvFormat = GetRenderTargetViewFormat(texDesc.Format);
			if (!SupportsRenderTargetView(rtvDevice, rtvFormat)) {
				logger::error("VR: Eye texture format cannot be used as an RTV ({}x{}, Format: {}, RTVFormat: {}, ArraySize: {}, Samples: {}, BindFlags: 0x{:X})",
					texDesc.Width,
					texDesc.Height,
					(uint32_t)texDesc.Format,
					(uint32_t)rtvFormat,
					texDesc.ArraySize,
					texDesc.SampleDesc.Count,
					texDesc.BindFlags);
				return;
			}

			D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
			rtvDesc.Format = rtvFormat;

			if (texDesc.ArraySize > 1) {
				if (texDesc.SampleDesc.Count > 1) {
					rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
					rtvDesc.Texture2DMSArray.FirstArraySlice = (UINT)eye;
					rtvDesc.Texture2DMSArray.ArraySize = 1;
				} else {
					rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
					rtvDesc.Texture2DArray.FirstArraySlice = (UINT)eye;
					rtvDesc.Texture2DArray.ArraySize = 1;
					rtvDesc.Texture2DArray.MipSlice = 0;
				}
			} else if (texDesc.SampleDesc.Count > 1) {
				rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
			} else {
				rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
				rtvDesc.Texture2D.MipSlice = 0;
			}

			HRESULT hr = rtvDevice->CreateRenderTargetView(targetTexture, &rtvDesc, cachedRTV.rtv.put());
			if (FAILED(hr)) {
				logger::error("VR: Failed to create RTV for eye texture ({}x{}, Format: {}, RTVFormat: {}, ArraySize: {}, Samples: {}, BindFlags: 0x{:X}). HRESULT: 0x{:08X}",
					texDesc.Width,
					texDesc.Height,
					(uint32_t)texDesc.Format,
					(uint32_t)rtvFormat,
					texDesc.ArraySize,
					texDesc.SampleDesc.Count,
					texDesc.BindFlags,
					(uint32_t)hr);
				return;
			}
			cachedRTV.texture = targetTexture;
		}
		rtvPtr = cachedRTV.rtv.get();
	}
	if (!rtvPtr) {
		return;
	}

	// Save State
	ID3D11RenderTargetView* oldRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
	ID3D11DepthStencilView* oldDSV;
	context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRTVs, &oldDSV);

	D3D11_VIEWPORT oldViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
	UINT numViewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
	context->RSGetViewports(&numViewports, oldViewports);

	ID3D11RasterizerState* oldRS = nullptr;
	context->RSGetState(&oldRS);

	ID3D11BlendState* oldBlend = nullptr;
	FLOAT oldBlendFactor[4];
	UINT oldSampleMask;
	context->OMGetBlendState(&oldBlend, oldBlendFactor, &oldSampleMask);

	ID3D11DepthStencilState* oldDepth = nullptr;
	UINT oldStencilRef;
	context->OMGetDepthStencilState(&oldDepth, &oldStencilRef);

	// Setup Render
	context->OMSetRenderTargets(1, &rtvPtr, nullptr);  // No DSV

	// Viewport: Use bounds if provided (for SBS textures), otherwise use full texture
	D3D11_VIEWPORT vpDesc = {};
	if (bounds) {
		vpDesc.TopLeftX = bounds->uMin * texDesc.Width;
		vpDesc.TopLeftY = bounds->vMin * texDesc.Height;
		vpDesc.Width = (bounds->uMax - bounds->uMin) * texDesc.Width;
		vpDesc.Height = (bounds->vMax - bounds->vMin) * texDesc.Height;
	} else {
		vpDesc.TopLeftX = 0.0f;
		vpDesc.TopLeftY = 0.0f;
		vpDesc.Width = (float)texDesc.Width;
		vpDesc.Height = (float)texDesc.Height;
	}
	vpDesc.MinDepth = 0.0f;
	vpDesc.MaxDepth = 1.0f;
	context->RSSetViewports(1, &vpDesc);

	// Log texture and viewport details once per eye per session
	static bool textureInfoLogged[2] = { false, false };
	if (!textureInfoLogged[eyeIdx]) {
		logger::debug("VR Submit Texture Info (Eye {}):", eyeIdx);
		logger::debug("  Texture Size: {}x{}, Format: {}, ArraySize: {}, SampleCount: {}",
			texDesc.Width, texDesc.Height, (uint32_t)texDesc.Format, texDesc.ArraySize, texDesc.SampleDesc.Count);
		if (bounds) {
			logger::debug("  Bounds: uMin={:.3f}, vMin={:.3f}, uMax={:.3f}, vMax={:.3f}",
				bounds->uMin, bounds->vMin, bounds->uMax, bounds->vMax);
			logger::debug("  Viewport: X={:.0f}, Y={:.0f}, W={:.0f}, H={:.0f}",
				vpDesc.TopLeftX, vpDesc.TopLeftY, vpDesc.Width, vpDesc.Height);
		} else {
			logger::debug("  No bounds provided (full texture per eye, or texture array)");
			logger::debug("  Viewport: X={:.0f}, Y={:.0f}, W={:.0f}, H={:.0f}",
				vpDesc.TopLeftX, vpDesc.TopLeftY, vpDesc.Width, vpDesc.Height);
		}
		logger::debug("  RTV Dimension: {}",
			(texDesc.ArraySize > 1 && texDesc.SampleDesc.Count > 1) ? "Texture2DMSArray" :
			(texDesc.ArraySize > 1)                                 ? "Texture2DArray (per-eye slice)" :
			(texDesc.SampleDesc.Count > 1)                          ? "Texture2DMS" :
																	  "Texture2D (single)");
		textureInfoLogged[eyeIdx] = true;
	}

	bool overlayDrawn = false;

	// Helper to draw the overlay quad with a given WVP matrix
	auto drawOverlayQuad = [&](ID3D11DeviceContext* ctx,
							   const InSceneCB& cbData,
							   ID3D11Texture2D* texture,
							   winrt::com_ptr<ID3D11ShaderResourceView>& srv,
							   ID3D11Texture2D*& cachedTexture,
							   const char* label) -> bool {
		D3D11_MAPPED_SUBRESOURCE mappedResource;
		if (SUCCEEDED(ctx->Map(inSceneResources.cb.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource))) {
			memcpy(mappedResource.pData, &cbData, sizeof(InSceneCB));
			ctx->Unmap(inSceneResources.cb.get(), 0);
		}

		ctx->VSSetShader(inSceneResources.vs.get(), nullptr, 0);
		ctx->PSSetShader(inSceneResources.ps.get(), nullptr, 0);
		ID3D11Buffer* cb = inSceneResources.cb.get();
		ctx->VSSetConstantBuffers(0, 1, &cb);

		struct VT
		{
			XMFLOAT3 p;
			XMFLOAT2 t;
		};
		UINT stride = sizeof(VT);
		UINT offset = 0;
		ID3D11Buffer* vb = inSceneResources.vb.get();
		ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
		ctx->IASetIndexBuffer(inSceneResources.ib.get(), DXGI_FORMAT_R32_UINT, 0);
		ctx->IASetInputLayout(inSceneResources.inputLayout.get());
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		ctx->OMSetBlendState(inSceneResources.blendState.get(), nullptr, 0xFFFFFFFF);
		ctx->OMSetDepthStencilState(inSceneResources.depthState.get(), 0);
		ctx->RSSetState(inSceneResources.rasterizerState.get());

		if (!EnsureMenuTextureSRV(texture, srv, cachedTexture, label)) {
			return false;
		}
		ID3D11ShaderResourceView* srvPtr = srv.get();
		ctx->PSSetShaderResources(0, 1, &srvPtr);

		ID3D11SamplerState* sampler = inSceneResources.sampler.get();
		ctx->PSSetSamplers(0, 1, &sampler);

		ctx->DrawIndexed(6, 0, 0);
		return true;
	};

	// --- Render HMD Overlay ---
	if ((settings.attachMode == AttachMode::HMDOnly || settings.attachMode == AttachMode::Both) && menuTexture) {
		InSceneCB cbData;

		Matrix modelMatrix;
		Matrix vp;
		if (settings.VRMenuPositioningMethod == 1) {  // Fixed World Position
			modelMatrix = VR::Config::CreateHMDOverlayScaleMatrix(settings.VRMenuScale) * fixedWorldOverlayPosition.m;
			vp = vpWorldSpace;
		} else {  // HMD Relative
			Matrix offset = Matrix::CreateTranslation(settings.VRMenuOffsetX, settings.VRMenuOffsetY, settings.VRMenuOffsetZ);
			modelMatrix = VR::Config::CreateHMDOverlayScaleMatrix(settings.VRMenuScale) * offset;
			vp = vpHeadSpace;
		}
		cbData.wvp = (modelMatrix * vp).Transpose();

		overlayDrawn = drawOverlayQuad(
						   context,
						   cbData,
						   menuTexture.get(),
						   inSceneResources.menuSRV,
						   inSceneResources.cachedMenuTexture,
						   "HMD") ||
		               overlayDrawn;
	}

	// --- Render Controller Overlay ---
	if ((settings.attachMode == AttachMode::ControllerOnly || settings.attachMode == AttachMode::Both) && (menuControllerTexture || menuTexture)) {
		vr::TrackedDeviceIndex_t attachIndex = Util::GetControllerIndexForDevice(settings.VRMenuAttachController, lastKnownLeftHandedMode);
		if (attachIndex != vr::k_unTrackedDeviceIndexInvalid && attachIndex < vr::k_unMaxTrackedDeviceCount) {
			const vr::TrackedDevicePose_t& controllerPose = inSceneResources.cachedRenderPoses[attachIndex];
			if (controllerPose.bPoseIsValid) {
				Matrix controllerWorld = Util::HmdMatrix34ToMatrix(controllerPose.mDeviceToAbsoluteTracking);
				Matrix offset = Matrix::CreateTranslation(settings.VRMenuControllerOffsetX, settings.VRMenuControllerOffsetY, settings.VRMenuControllerOffsetZ);
				Matrix modelMatrix = VR::Config::CreateOverlayScaleMatrix(settings.VRMenuScale) * offset * controllerWorld;

				// Backface culling: hide overlay when viewed from behind
				// Use the unscaled controller+offset transform for correct normal direction
				Matrix overlayTransform = offset * controllerWorld;
				Vector3 overlayNormal(overlayTransform._31, overlayTransform._32, overlayTransform._33);
				overlayNormal.Normalize();
				Matrix eyeWorld = eyeToHead * hmdWorld;
				Vector3 eyePos = eyeWorld.Translation();
				Vector3 overlayPos = overlayTransform.Translation();
				Vector3 toEye = eyePos - overlayPos;
				toEye.Normalize();
				// Quad front face is +Z in local space (D3D default CW winding).
				// Render when eye is on the +Z side of the overlay (dot > 0).
				float dot = overlayNormal.Dot(toEye);
				if (dot > 0.0f) {
					InSceneCB cbData;
					cbData.wvp = (modelMatrix * vpWorldSpace).Transpose();
					if (menuControllerTexture) {
						overlayDrawn = drawOverlayQuad(
										   context,
										   cbData,
										   menuControllerTexture.get(),
										   inSceneResources.menuControllerSRV,
										   inSceneResources.cachedMenuControllerTexture,
										   "controller") ||
						               overlayDrawn;
					} else {
						overlayDrawn = drawOverlayQuad(
										   context,
										   cbData,
										   menuTexture.get(),
										   inSceneResources.menuSRV,
										   inSceneResources.cachedMenuTexture,
										   "HMD") ||
						               overlayDrawn;
					}
				}
			}
		}
	}

	// Restore State
	context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRTVs, oldDSV);
	context->RSSetViewports(numViewports, oldViewports);
	context->OMSetBlendState(oldBlend, oldBlendFactor, oldSampleMask);
	context->OMSetDepthStencilState(oldDepth, oldStencilRef);
	if (oldRS) {
		context->RSSetState(oldRS);
		oldRS->Release();
	}
	if (oldBlend)
		oldBlend->Release();
	if (oldDepth)
		oldDepth->Release();
	for (int i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
		if (oldRTVs[i])
			oldRTVs[i]->Release();
	if (oldDSV)
		oldDSV->Release();
	if (overlayComposited) {
		*overlayComposited = overlayDrawn;
	}
}

void VR::CompositeInSceneOverlaySubmitTexture(vr::EVREye eye, ID3D11Texture2D* targetTexture, ID3D11UnorderedAccessView* targetUAV, const D3D11_TEXTURE2D_DESC& targetDesc, const vr::VRTextureBounds_t* bounds, bool* overlayComposited)
{
	if (overlayComposited) {
		*overlayComposited = false;
	}
	auto* context = globals::d3d::context;
	auto* device = globals::d3d::device;
	if (!context || !device || !targetTexture || !targetUAV || !menuTexture || !inSceneResources.initialized || !inSceneResources.submitCompositeCS || !inSceneResources.submitCompositeCB || !inSceneResources.sampler) {
		return;
	}

	const float targetWidth = static_cast<float>(targetDesc.Width);
	const float targetHeight = static_cast<float>(targetDesc.Height);
	float viewX = 0.0f;
	float viewY = 0.0f;
	float viewW = targetWidth;
	float viewH = targetHeight;
	if (bounds) {
		viewX = std::clamp(bounds->uMin, 0.0f, 1.0f) * targetWidth;
		viewY = std::clamp(bounds->vMin, 0.0f, 1.0f) * targetHeight;
		viewW = std::max(1.0f, (std::clamp(bounds->uMax, 0.0f, 1.0f) - std::clamp(bounds->uMin, 0.0f, 1.0f)) * targetWidth);
		viewH = std::max(1.0f, (std::clamp(bounds->vMax, 0.0f, 1.0f) - std::clamp(bounds->vMin, 0.0f, 1.0f)) * targetHeight);
	}

	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	if (!openvr || !openvr->vrSystem) {
		return;
	}

	const bool hasState = globals::state != nullptr;
	const uint32_t currentFrame = hasState ? globals::state->frameCount : 0;
	const bool shouldRefreshPoses =
		!hasState ||
		!inSceneResources.cachedPosesValid ||
		inSceneResources.cachedPoseFrame != currentFrame;

	if (shouldRefreshPoses) {
		auto* compositor = RE::BSOpenVR::GetIVRCompositor();
		if (!compositor) {
			compositor = openvr->vrContext.vrCompositor;
		}
		if (!compositor) {
			return;
		}

		auto compositorError = compositor->GetLastPoses(
			inSceneResources.cachedRenderPoses,
			vr::k_unMaxTrackedDeviceCount,
			nullptr,
			0);
		if (compositorError != vr::VRCompositorError_None) {
			return;
		}

		inSceneResources.cachedPoseFrame = currentFrame;
		inSceneResources.cachedPosesValid = true;
	}

	const vr::TrackedDevicePose_t& hmdPose = inSceneResources.cachedRenderPoses[vr::k_unTrackedDeviceIndex_Hmd];
	if (!hmdPose.bPoseIsValid) {
		return;
	}

	Matrix hmdWorld = Util::HmdMatrix34ToMatrix(hmdPose.mDeviceToAbsoluteTracking);
	Matrix eyeToHead = Util::HmdMatrix34ToMatrix(openvr->vrSystem->GetEyeToHeadTransform(eye));

	float left, right, bottom, top;
	openvr->vrSystem->GetProjectionRaw(eye, &left, &right, &bottom, &top);
	const float nearZ = 0.1f;
	const float farZ = 1000.0f;
	Matrix proj = DirectX::XMMatrixPerspectiveOffCenterRH(left * nearZ, right * nearZ, bottom * nearZ, top * nearZ, nearZ, farZ);
	Matrix vpHeadSpace = eyeToHead.Invert() * proj;
	Matrix eyeToWorld = eyeToHead * hmdWorld;
	Matrix vpWorldSpace = eyeToWorld.Invert() * proj;

	Matrix modelMatrix = Matrix::Identity;
	Matrix viewProjection = vpHeadSpace;
	const bool showOnHMD = settings.attachMode == AttachMode::HMDOnly || settings.attachMode == AttachMode::Both;
	const bool showOnController = settings.attachMode == AttachMode::ControllerOnly;
	if (showOnHMD) {
		if (settings.VRMenuPositioningMethod == 1) {
			modelMatrix = VR::Config::CreateHMDOverlayScaleMatrix(settings.VRMenuScale) * fixedWorldOverlayPosition.m;
			viewProjection = vpWorldSpace;
		} else {
			Matrix offset = Matrix::CreateTranslation(settings.VRMenuOffsetX, settings.VRMenuOffsetY, settings.VRMenuOffsetZ);
			modelMatrix = VR::Config::CreateHMDOverlayScaleMatrix(settings.VRMenuScale) * offset;
			viewProjection = vpHeadSpace;
		}
	} else if (showOnController) {
		vr::TrackedDeviceIndex_t attachIndex = Util::GetControllerIndexForDevice(settings.VRMenuAttachController, lastKnownLeftHandedMode);
		if (attachIndex == vr::k_unTrackedDeviceIndexInvalid || attachIndex >= vr::k_unMaxTrackedDeviceCount) {
			return;
		}
		const vr::TrackedDevicePose_t& controllerPose = inSceneResources.cachedRenderPoses[attachIndex];
		if (!controllerPose.bPoseIsValid) {
			return;
		}

		Matrix controllerWorld = Util::HmdMatrix34ToMatrix(controllerPose.mDeviceToAbsoluteTracking);
		Matrix offset = Matrix::CreateTranslation(settings.VRMenuControllerOffsetX, settings.VRMenuControllerOffsetY, settings.VRMenuControllerOffsetZ);
		modelMatrix = VR::Config::CreateOverlayScaleMatrix(settings.VRMenuScale) * offset * controllerWorld;
		viewProjection = vpWorldSpace;
	} else {
		return;
	}

	ID3D11ShaderResourceView* overlaySRV = nullptr;
	if (showOnHMD) {
		if (!EnsureMenuTextureSRV(menuTexture.get(), inSceneResources.menuSRV, inSceneResources.cachedMenuTexture, "HMD")) {
			return;
		}
		overlaySRV = inSceneResources.menuSRV.get();
	} else if (showOnController) {
		if (menuControllerTexture) {
			if (!EnsureMenuTextureSRV(
					menuControllerTexture.get(),
					inSceneResources.menuControllerSRV,
					inSceneResources.cachedMenuControllerTexture,
					"controller")) {
				return;
			}
			overlaySRV = inSceneResources.menuControllerSRV.get();
		} else {
			if (!EnsureMenuTextureSRV(menuTexture.get(), inSceneResources.menuSRV, inSceneResources.cachedMenuTexture, "HMD")) {
				return;
			}
			overlaySRV = inSceneResources.menuSRV.get();
		}
	}

	const Matrix worldViewProjection = modelMatrix * viewProjection;
	const XMFLOAT3 vertices[4] = {
		XMFLOAT3(-0.5f, -0.5f, 0.0f),
		XMFLOAT3(-0.5f, 0.5f, 0.0f),
		XMFLOAT3(0.5f, 0.5f, 0.0f),
		XMFLOAT3(0.5f, -0.5f, 0.0f)
	};

	SubmitCompositeCB cbData{};
	cbData.targetSize[0] = targetDesc.Width;
	cbData.targetSize[1] = targetDesc.Height;

	float minX = std::numeric_limits<float>::max();
	float minY = std::numeric_limits<float>::max();
	float maxX = std::numeric_limits<float>::lowest();
	float maxY = std::numeric_limits<float>::lowest();
	for (size_t i = 0; i < 4; ++i) {
		const XMVECTOR local = XMVectorSet(vertices[i].x, vertices[i].y, vertices[i].z, 1.0f);
		const XMVECTOR clip = XMVector4Transform(local, worldViewProjection);
		const float w = XMVectorGetW(clip);
		if (std::abs(w) < 1e-5f) {
			return;
		}
		cbData.quadInvW[i] = 1.0f / w;

		const float ndcX = XMVectorGetX(clip) / w;
		const float ndcY = XMVectorGetY(clip) / w;
		if (!std::isfinite(ndcX) || !std::isfinite(ndcY)) {
			return;
		}

		const float pixelX = viewX + (ndcX * 0.5f + 0.5f) * viewW;
		const float pixelY = viewY + (0.5f - ndcY * 0.5f) * viewH;
		cbData.quadPixels[i * 2] = pixelX;
		cbData.quadPixels[i * 2 + 1] = pixelY;
		minX = std::min(minX, pixelX);
		minY = std::min(minY, pixelY);
		maxX = std::max(maxX, pixelX);
		maxY = std::max(maxY, pixelY);
	}

	const int dispatchLeft = std::clamp(static_cast<int>(std::floor(minX)) - 1, 0, static_cast<int>(targetDesc.Width));
	const int dispatchTop = std::clamp(static_cast<int>(std::floor(minY)) - 1, 0, static_cast<int>(targetDesc.Height));
	const int dispatchRight = std::clamp(static_cast<int>(std::ceil(maxX)) + 1, 0, static_cast<int>(targetDesc.Width));
	const int dispatchBottom = std::clamp(static_cast<int>(std::ceil(maxY)) + 1, 0, static_cast<int>(targetDesc.Height));
	if (dispatchRight <= dispatchLeft || dispatchBottom <= dispatchTop) {
		return;
	}

	cbData.dispatchOrigin[0] = static_cast<uint32_t>(dispatchLeft);
	cbData.dispatchOrigin[1] = static_cast<uint32_t>(dispatchTop);
	cbData.dispatchSize[0] = static_cast<uint32_t>(dispatchRight - dispatchLeft);
	cbData.dispatchSize[1] = static_cast<uint32_t>(dispatchBottom - dispatchTop);

	D3D11_MAPPED_SUBRESOURCE mapped{};
	if (FAILED(context->Map(inSceneResources.submitCompositeCB.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
		return;
	}
	std::memcpy(mapped.pData, &cbData, sizeof(cbData));
	context->Unmap(inSceneResources.submitCompositeCB.get(), 0);

	ID3D11ComputeShader* oldCS = nullptr;
	ID3D11ShaderResourceView* oldSRV = nullptr;
	ID3D11UnorderedAccessView* oldUAV = nullptr;
	ID3D11SamplerState* oldSampler = nullptr;
	ID3D11Buffer* oldCB = nullptr;
	context->CSGetShader(&oldCS, nullptr, nullptr);
	context->CSGetShaderResources(0, 1, &oldSRV);
	context->CSGetUnorderedAccessViews(0, 1, &oldUAV);
	context->CSGetSamplers(0, 1, &oldSampler);
	context->CSGetConstantBuffers(0, 1, &oldCB);

	ID3D11ShaderResourceView* srv = overlaySRV;
	ID3D11UnorderedAccessView* uav = targetUAV;
	ID3D11SamplerState* sampler = inSceneResources.sampler.get();
	ID3D11Buffer* cb = inSceneResources.submitCompositeCB.get();
	context->CSSetShader(inSceneResources.submitCompositeCS.get(), nullptr, 0);
	context->CSSetConstantBuffers(0, 1, &cb);
	context->CSSetShaderResources(0, 1, &srv);
	context->CSSetSamplers(0, 1, &sampler);
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	context->Dispatch((cbData.dispatchSize[0] + 7) / 8, (cbData.dispatchSize[1] + 7) / 8, 1);
	if (overlayComposited) {
		*overlayComposited = true;
	}

	ID3D11ShaderResourceView* nullSRV = nullptr;
	ID3D11UnorderedAccessView* nullUAV = nullptr;
	context->CSSetShaderResources(0, 1, &nullSRV);
	context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	context->CSSetShader(oldCS, nullptr, 0);
	context->CSSetConstantBuffers(0, 1, &oldCB);
	context->CSSetShaderResources(0, 1, &oldSRV);
	context->CSSetSamplers(0, 1, &oldSampler);
	context->CSSetUnorderedAccessViews(0, 1, &oldUAV, nullptr);

	if (oldCS)
		oldCS->Release();
	if (oldSRV)
		oldSRV->Release();
	if (oldUAV)
		oldUAV->Release();
	if (oldSampler)
		oldSampler->Release();
	if (oldCB)
		oldCB->Release();
}

void VR::EnsureInSceneOverlaySubmitCopyResources()
{
	auto* device = globals::d3d::device;
	if (!device) {
		return;
	}

	for (int eyeIdx = 0; eyeIdx < 2; ++eyeIdx) {
		auto& submitCopy = inSceneResources.submitCopies[eyeIdx];
		if (!submitCopy.pendingCreate) {
			continue;
		}

		const auto sourceDesc = submitCopy.pendingSourceDesc;
		if (sourceDesc.ArraySize != 1 || sourceDesc.SampleDesc.Count != 1) {
			logger::error("VR: Cannot composite in-scene menu into submit texture with array={} samples={}", sourceDesc.ArraySize, sourceDesc.SampleDesc.Count);
			submitCopy.pendingCreate = false;
			continue;
		}

		const DXGI_FORMAT viewFormat = GetRenderTargetViewFormat(sourceDesc.Format);
		if (!SupportsUnorderedAccessView(device, viewFormat)) {
			logger::error("VR: Cannot create in-scene menu submit copy UAV (eye={}, {}x{}, Format: {}, ViewFormat: {}, ArraySize: {}, Samples: {}, BindFlags: 0x{:X})",
				eyeIdx,
				sourceDesc.Width,
				sourceDesc.Height,
				(uint32_t)sourceDesc.Format,
				(uint32_t)viewFormat,
				sourceDesc.ArraySize,
				sourceDesc.SampleDesc.Count,
				sourceDesc.BindFlags);
			submitCopy.pendingCreate = false;
			continue;
		}

		D3D11_TEXTURE2D_DESC copyDesc = sourceDesc;
		copyDesc.Usage = D3D11_USAGE_DEFAULT;
		copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		copyDesc.CPUAccessFlags = 0;
		copyDesc.MiscFlags = 0;

		winrt::com_ptr<ID3D11Texture2D> texture;
		HRESULT hr = device->CreateTexture2D(&copyDesc, nullptr, texture.put());
		if (FAILED(hr)) {
			logger::error("VR: Failed to create in-scene menu submit copy texture (eye={}, {}x{}, Format: {}, ArraySize: {}, Samples: {}, HRESULT: 0x{:08X})",
				eyeIdx,
				copyDesc.Width,
				copyDesc.Height,
				(uint32_t)copyDesc.Format,
				copyDesc.ArraySize,
				copyDesc.SampleDesc.Count,
				(uint32_t)hr);
			submitCopy.pendingCreate = false;
			continue;
		}

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = viewFormat;
		if (copyDesc.ArraySize > 1) {
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
			uavDesc.Texture2DArray.FirstArraySlice = (UINT)eyeIdx;
			uavDesc.Texture2DArray.ArraySize = 1;
			uavDesc.Texture2DArray.MipSlice = 0;
		} else {
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;
		}

		winrt::com_ptr<ID3D11UnorderedAccessView> uav;
		hr = device->CreateUnorderedAccessView(texture.get(), &uavDesc, uav.put());
		if (FAILED(hr)) {
			logger::error("VR: Failed to create in-scene menu submit copy UAV (eye={}, {}x{}, Format: {}, ViewFormat: {}, ArraySize: {}, Samples: {}, HRESULT: 0x{:08X})",
				eyeIdx,
				copyDesc.Width,
				copyDesc.Height,
				(uint32_t)copyDesc.Format,
				(uint32_t)viewFormat,
				copyDesc.ArraySize,
				copyDesc.SampleDesc.Count,
				(uint32_t)hr);
			submitCopy.pendingCreate = false;
			continue;
		}

		submitCopy.texture = std::move(texture);
		submitCopy.uav = std::move(uav);
		submitCopy.sourceDesc = sourceDesc;
		submitCopy.pendingCreate = false;
		inSceneResources.cachedEyeRTVs[eyeIdx].texture = nullptr;
		inSceneResources.cachedEyeRTVs[eyeIdx].rtv = nullptr;
		Util::SetResourceName(submitCopy.texture.get(), eyeIdx == 0 ? "VR::InSceneOverlaySubmitCopyLeft" : "VR::InSceneOverlaySubmitCopyRight");
		Util::SetResourceName(submitCopy.uav.get(), eyeIdx == 0 ? "VR::InSceneOverlaySubmitCopyLeft UAV" : "VR::InSceneOverlaySubmitCopyRight UAV");
		logger::debug("VR: Created in-scene menu submit copy for eye {} ({}x{}, format={}, array={}, samples={})",
			eyeIdx,
			copyDesc.Width,
			copyDesc.Height,
			(uint32_t)copyDesc.Format,
			copyDesc.ArraySize,
			copyDesc.SampleDesc.Count);
	}
}

bool VR::PrepareInSceneOverlaySubmitTexture(vr::EVREye eye, const vr::Texture_t* inputTexture, const vr::VRTextureBounds_t* bounds, vr::Texture_t& outputTexture)
{
	if (!inputTexture || !inputTexture->handle || inputTexture->eType != vr::TextureType_DirectX || !ShouldRenderInSceneMenu(*this)) {
		return false;
	}

	auto sourceTexture = ResolveSubmitTexture2D(inputTexture->handle);
	auto* context = globals::d3d::context;
	if (!sourceTexture || !context) {
		logger::error("VR: OpenVR submit handle is not a D3D11 texture; skipping in-scene menu compositing");
		return false;
	}

	const int eyeIdx = static_cast<int>(eye);
	if (eyeIdx < 0 || eyeIdx >= 2) {
		return false;
	}

	D3D11_TEXTURE2D_DESC sourceDesc{};
	sourceTexture->GetDesc(&sourceDesc);

	auto& submitCopy = inSceneResources.submitCopies[eyeIdx];
	if (!submitCopy.texture || !submitCopy.uav || !MatchesSubmitCopyDesc(submitCopy.sourceDesc, sourceDesc)) {
		submitCopy.texture = nullptr;
		submitCopy.uav = nullptr;
		submitCopy.pendingSourceDesc = sourceDesc;
		submitCopy.pendingCreate = true;
		return false;
	}

	context->CopyResource(submitCopy.texture.get(), sourceTexture.get());
	bool overlayComposited = false;
	CompositeInSceneOverlaySubmitTexture(
		eye,
		submitCopy.texture.get(),
		submitCopy.uav.get(),
		sourceDesc,
		bounds,
		&overlayComposited);
	if (!overlayComposited) {
		return false;
	}

	outputTexture = *inputTexture;
	outputTexture.handle = submitCopy.texture.get();
	return true;
}

bool VR::InstallSubmitHook(bool a_enableProcessing)
{
	static bool installed = false;
	static bool warnedUnavailable = false;
	if (installed) {
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (a_enableProcessing)
			g_openVRSubmitProcessingEnabled.store(true, std::memory_order_release);
#else
		(void)a_enableProcessing;
#endif
		inSceneResources.submitHookInstalled = true;
		return true;
	}

	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	auto* compositor = openvr ? RE::BSOpenVR::GetIVRCompositor() : nullptr;
	if (!compositor && openvr) {
		compositor = openvr->vrContext.vrCompositor;
	}

	if (openvr && compositor) {
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (a_enableProcessing)
			logger::info("VR: Installing IVRCompositor::Submit hook for in-scene overlay rendering");
		else
			logger::debug("[VRLoadPresentationProbe] Installing observer-only IVRCompositor::Submit interception");
#else
		logger::info("VR: Installing IVRCompositor::Submit hook for in-scene overlay rendering");
#endif

		// Log comprehensive VR system parameters (debug only)
		logger::debug("=== VR System Configuration ===");

		// Get and log IPD
		float ipd = Util::GetIPDFromHMD();
		logger::debug("IPD: {:.4f} meters ({:.2f} mm)", ipd, ipd * 1000.0f);

		// Get and log eye transforms
		if (openvr->vrSystem) {
			vr::HmdMatrix34_t leftEye = openvr->vrSystem->GetEyeToHeadTransform(vr::Eye_Left);
			vr::HmdMatrix34_t rightEye = openvr->vrSystem->GetEyeToHeadTransform(vr::Eye_Right);

			logger::debug("Left Eye Transform:");
			logger::debug("  Translation: X={:.4f}, Y={:.4f}, Z={:.4f}",
				leftEye.m[0][3], leftEye.m[1][3], leftEye.m[2][3]);
			logger::debug("Right Eye Transform:");
			logger::debug("  Translation: X={:.4f}, Y={:.4f}, Z={:.4f}",
				rightEye.m[0][3], rightEye.m[1][3], rightEye.m[2][3]);
			logger::debug("Calculated Eye Separation: {:.4f} meters ({:.2f} mm)",
				std::abs(leftEye.m[0][3] - rightEye.m[0][3]),
				std::abs(leftEye.m[0][3] - rightEye.m[0][3]) * 1000.0f);

			// Get projection matrices
			vr::HmdMatrix44_t leftProj = openvr->vrSystem->GetProjectionMatrix(vr::Eye_Left, 0.1f, 1000.0f);
			vr::HmdMatrix44_t rightProj = openvr->vrSystem->GetProjectionMatrix(vr::Eye_Right, 0.1f, 1000.0f);

			logger::debug("Projection Matrices (near=0.1, far=1000.0):");
			logger::debug("  Left [0][0]={:.4f}, [1][1]={:.4f}, [0][2]={:.4f}",
				leftProj.m[0][0], leftProj.m[1][1], leftProj.m[0][2]);
			logger::debug("  Right [0][0]={:.4f}, [1][1]={:.4f}, [0][2]={:.4f}",
				rightProj.m[0][0], rightProj.m[1][1], rightProj.m[0][2]);
		}

		logger::debug("Convergence Formula Info:");
		logger::debug("  Formula: stereoShift = (IPD/2) / (depth * tan(hFOV/2))");
		logger::debug("  - Shift is independent of scale (scale only controls size)");
		logger::debug("  - Depth is controlled by OffsetZ (negative = in front)");
		float halfIPD = ipd / 2.0f;
		if (openvr->vrSystem) {
			vr::HmdMatrix44_t proj = openvr->vrSystem->GetProjectionMatrix(vr::Eye_Left, 0.1f, 1000.0f);
			float tanHFOV = 1.0f / proj.m[0][0];
			logger::debug("  tan(hFOV/2) = {:.4f}", tanHFOV);
			logger::debug("  Example: At depth 1.0m, shift={:.6f}", halfIPD / (1.0f * tanHFOV));
			logger::debug("  Example: At depth 2.0m, shift={:.6f}", halfIPD / (2.0f * tanHFOV));
			logger::debug("  Example: At depth 5.0m, shift={:.6f}", halfIPD / (5.0f * tanHFOV));
		}
		logger::debug("================================");

		// WaitGetPoses (index 2) provides the exact OpenVR Submit-cycle
		// boundary; Submit itself is index 5. Install both hooks in one checked
		// transaction: Submit suppression must never run without its cycle
		// boundary, or a quarantine could become permanent.
		auto* compositorVtable =
			*reinterpret_cast<std::uintptr_t**>(compositor);
		if (!compositorVtable ||
			compositorVtable[2] == 0 ||
			compositorVtable[5] == 0) {
			logger::error(
				"VR: Failed to install OpenVR compositor hooks: required vtable entries are unavailable");
			return false;
		}
		IVRCompositor_WaitGetPoses::func = compositorVtable[2];
		IVRCompositor_Submit::func = compositorVtable[5];
		LONG hookResult = DetourTransactionBegin();
		const bool transactionStarted = hookResult == NO_ERROR;
		if (hookResult == NO_ERROR)
			hookResult = DetourUpdateThread(GetCurrentThread());
		if (hookResult == NO_ERROR) {
			hookResult = DetourAttach(
				reinterpret_cast<PVOID*>(&IVRCompositor_WaitGetPoses::func),
				reinterpret_cast<PVOID>(IVRCompositor_WaitGetPoses::thunk));
		}
		if (hookResult == NO_ERROR) {
			hookResult = DetourAttach(
				reinterpret_cast<PVOID*>(&IVRCompositor_Submit::func),
				reinterpret_cast<PVOID>(IVRCompositor_Submit::thunk));
		}
		if (hookResult != NO_ERROR) {
			if (transactionStarted)
				DetourTransactionAbort();
			logger::error(
				"VR: Failed to queue the atomic OpenVR compositor hooks (error {})",
				hookResult);
			return false;
		}
		hookResult = DetourTransactionCommit();
		if (hookResult != NO_ERROR) {
			logger::error(
				"VR: Failed to commit the atomic OpenVR compositor hooks (error {})",
				hookResult);
			return false;
		}
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (a_enableProcessing)
			g_openVRSubmitProcessingEnabled.store(true, std::memory_order_release);
#endif
		installed = true;
		inSceneResources.submitHookInstalled = true;

#ifdef DEVBENCH_BRIDGE_ENABLED
		if (a_enableProcessing)
			logger::info("VR: In-scene overlay initialized");
		else
			logger::debug("[VRLoadPresentationProbe] Observer-only OpenVR submit interception initialized");
#else
		logger::info("VR: In-scene overlay initialized");
#endif
		return true;
	} else if (!warnedUnavailable) {
		logger::warn("VR: Failed to install IVRCompositor::Submit hook - Interface not available");
		warnedUnavailable = true;
	}
	return false;
}
