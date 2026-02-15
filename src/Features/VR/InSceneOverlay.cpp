#include "Features/VR.h"
#include "Globals.h"
#include "Hooks.h"
#include "Menu.h"
#include "Util.h"
#include "Utils/VRUtils.h"
#include <DirectXMath.h>
#include <SimpleMath.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <imgui_impl_dx11.h>

using namespace DirectX;
using namespace DirectX::SimpleMath;

using AttachMode = VR::Settings::OverlayAttachMode;

// Helper to create aspect-corrected scale matrix for overlay
inline Matrix CreateOverlayScaleMatrix(float scale)
{
	return Matrix::CreateScale(scale, scale * VR::Config::kOverlayAspect, scale);
}

//=============================================================================
// IN-SCENE OVERLAY RENDERING VIA SUBMIT HOOK
//=============================================================================

namespace
{
	struct IVRCompositor_Submit
	{
		static vr::EVRCompositorError thunk(vr::IVRCompositor* _this, vr::EVREye eEye, const vr::Texture_t* pTexture, const vr::VRTextureBounds_t* pBounds, vr::EVRSubmitFlags nSubmitFlags)
		{
			auto& vr = globals::features::vr;
			// Only process DirectX textures - skip OpenGL/Vulkan to avoid undefined behavior
			if (pTexture && pTexture->handle && pTexture->eType == vr::TextureType_DirectX) {
				vr.RenderInSceneOverlay(eEye, (ID3D11Texture2D*)pTexture->handle, pBounds);
			}
			return func(_this, eEye, pTexture, pBounds, nSubmitFlags);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

void VR::InitInSceneResources()
{
	if (inSceneResources.initialized)
		return;

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

	inSceneResources.vs.attach(vs);
	inSceneResources.ps.attach(ps);
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

	if (FAILED(device->CreateInputLayout(polygonLayout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), inSceneResources.inputLayout.put()))) {
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
	if (FAILED(device->CreateBuffer(&vertexBufferDesc, &vertexData, inSceneResources.vb.put()))) {
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
	if (FAILED(device->CreateBuffer(&indexBufferDesc, &indexData, inSceneResources.ib.put()))) {
		logger::error("VR: Failed to create index buffer");
		return;
	}

	D3D11_BUFFER_DESC matrixBufferDesc = {};
	matrixBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	matrixBufferDesc.ByteWidth = sizeof(InSceneCB);
	matrixBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	matrixBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	if (FAILED(device->CreateBuffer(&matrixBufferDesc, nullptr, inSceneResources.cb.put()))) {
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
	if (FAILED(device->CreateBlendState(&blendDesc, inSceneResources.blendState.put()))) {
		logger::error("VR: Failed to create blend state");
		return;
	}

	D3D11_DEPTH_STENCIL_DESC depthDesc = {};
	depthDesc.DepthEnable = FALSE;  // Always on top
	depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	depthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
	if (FAILED(device->CreateDepthStencilState(&depthDesc, inSceneResources.depthState.put()))) {
		logger::error("VR: Failed to create depth stencil state");
		return;
	}

	D3D11_RASTERIZER_DESC rasterDesc = {};
	rasterDesc.FillMode = D3D11_FILL_SOLID;
	rasterDesc.CullMode = D3D11_CULL_NONE;
	rasterDesc.FrontCounterClockwise = FALSE;
	rasterDesc.DepthClipEnable = TRUE;
	if (FAILED(device->CreateRasterizerState(&rasterDesc, inSceneResources.rasterizerState.put()))) {
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
	if (FAILED(device->CreateSamplerState(&samplerDesc, inSceneResources.sampler.put()))) {
		logger::error("VR: Failed to create sampler state");
		return;
	}

	inSceneResources.initialized = true;
	logger::debug("VR: In-Scene Overlay resources initialized.");
}

void VR::RenderInSceneOverlay(vr::EVREye eye, ID3D11Texture2D* targetTexture, const vr::VRTextureBounds_t* bounds)
{
	auto context = globals::d3d::context;
	winrt::com_ptr<ID3DUserDefinedAnnotation> perf;
	context->QueryInterface(__uuidof(ID3DUserDefinedAnnotation), perf.put_void());

	std::wstring eventName = L"VR In-Scene Overlay (Eye " + std::to_wstring((int)eye) + L")";
	if (perf)
		perf->BeginEvent(eventName.c_str());

	if (!inSceneResources.initialized)
		InitInSceneResources();
	if (!inSceneResources.initialized) {
		if (perf)
			perf->EndEvent();
		return;
	}

	// Only render if overlay should be visible
	if (!(globals::menu->IsEnabled || globals::menu->overlayVisible || settings.kAutoHideSeconds > 0)) {
		if (perf)
			perf->EndEvent();
		return;
	}
	if (!menuTexture) {
		if (perf)
			perf->EndEvent();
		return;
	}

	// Skip rendering when attach mode is None (disabled)
	if (settings.attachMode == AttachMode::None) {
		if (perf)
			perf->EndEvent();
		return;
	}

	// We can't render if we don't have HMD pose
	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	if (!openvr || !openvr->vrSystem) {
		if (perf)
			perf->EndEvent();
		return;
	}

	// Get HMD Pose and Eye matrices
	vr::TrackedDevicePose_t hmdPose;
	vr::TrackedDevicePose_t renderPose[vr::k_unMaxTrackedDeviceCount];

	RE::BSOpenVR::GetIVRCompositor()->GetLastPoses(renderPose, vr::k_unMaxTrackedDeviceCount, nullptr, 0);
	hmdPose = renderPose[vr::k_unTrackedDeviceIndex_Hmd];
	if (!hmdPose.bPoseIsValid) {
		if (perf)
			perf->EndEvent();
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
		Matrix eyeToWorld = hmdWorld * eyeToHead;
		vpWorldSpace = eyeToWorld.Invert() * proj;
	}

	// Use standard 3D rendering path
	bool force2D = false;

	// Create RTV for the target texture
	winrt::com_ptr<ID3D11RenderTargetView> rtv;
	D3D11_TEXTURE2D_DESC texDesc;
	targetTexture->GetDesc(&texDesc);

	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = texDesc.Format;

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

	HRESULT hr = globals::d3d::device->CreateRenderTargetView(targetTexture, &rtvDesc, rtv.put());

	if (FAILED(hr)) {
		logger::error("VR: Failed to create RTV for eye texture (Format: {}, Samples: {}). HRESULT: {:x}",
			(uint32_t)texDesc.Format, texDesc.SampleDesc.Count, (uint32_t)hr);
		if (perf)
			perf->EndEvent();
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

	// Setup Render
	ID3D11RenderTargetView* rtvPtr = rtv.get();
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

	// Log texture and viewport details once per session
	static bool textureInfoLogged = false;
	if (!textureInfoLogged) {
		logger::debug("VR Submit Texture Info:");
		logger::debug("  Texture Size: {}x{}, Format: {}, ArraySize: {}, SampleCount: {}",
			texDesc.Width, texDesc.Height, (uint32_t)texDesc.Format, texDesc.ArraySize, texDesc.SampleDesc.Count);
		if (bounds) {
			logger::debug("  Bounds for Eye {}: uMin={:.3f}, vMin={:.3f}, uMax={:.3f}, vMax={:.3f}",
				(int)eye, bounds->uMin, bounds->vMin, bounds->uMax, bounds->vMax);
			logger::debug("  Viewport: X={:.0f}, Y={:.0f}, W={:.0f}, H={:.0f}",
				vpDesc.TopLeftX, vpDesc.TopLeftY, vpDesc.Width, vpDesc.Height);
		} else {
			logger::debug("  No bounds provided (full texture per eye, or texture array)");
			logger::debug("  Viewport: X={:.0f}, Y={:.0f}, W={:.0f}, H={:.0f}",
				vpDesc.TopLeftX, vpDesc.TopLeftY, vpDesc.Width, vpDesc.Height);
		}
		logger::debug("  RTV Dimension: {}",
			rtvDesc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2DARRAY   ? "Texture2DArray (per-eye slice)" :
			rtvDesc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D        ? "Texture2D (single)" :
			rtvDesc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2DMS      ? "Texture2DMS" :
			rtvDesc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY ? "Texture2DMSArray" :
																			"Unknown");
		// Log again for the other eye
		if (eye == vr::Eye_Right)
			textureInfoLogged = true;
	}

	// Helper to draw the overlay quad with a given WVP matrix
	auto drawOverlayQuad = [&](ID3D11DeviceContext* ctx, const InSceneCB& cbData) {
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

		// Cache SRV to avoid creating every frame
		if (menuTexture.get() != inSceneResources.cachedMenuTexture) {
			inSceneResources.menuSRV = nullptr;
			if (FAILED(globals::d3d::device->CreateShaderResourceView(menuTexture.get(), nullptr, inSceneResources.menuSRV.put()))) {
				logger::error("VR: Failed to create menu texture SRV");
				return;
			}
			inSceneResources.cachedMenuTexture = menuTexture.get();
		}
		ID3D11ShaderResourceView* srvPtr = inSceneResources.menuSRV.get();
		ctx->PSSetShaderResources(0, 1, &srvPtr);

		ID3D11SamplerState* sampler = inSceneResources.sampler.get();
		ctx->PSSetSamplers(0, 1, &sampler);

		ctx->DrawIndexed(6, 0, 0);
	};

	// --- Render HMD Overlay ---
	if ((settings.attachMode == AttachMode::HMDOnly || settings.attachMode == AttachMode::Both) && menuTexture) {
		InSceneCB cbData;

		if (force2D) {
			// Screen-space overlay with stereo convergence

			float scale = settings.VRMenuScale * 0.5f;
			float aspect = 16.0f / 9.0f;

			// Get actual IPD from HMD for proper convergence
			static float cachedIPD = -1.0f;
			static bool ipdLogged = false;
			if (cachedIPD < 0.0f) {
				cachedIPD = Util::GetIPDFromHMD();

				// Log IPD information once
				if (!ipdLogged) {
					// Get eye transform data for logging
					vr::HmdMatrix34_t leftEye = openvr->vrSystem->GetEyeToHeadTransform(vr::Eye_Left);
					vr::HmdMatrix34_t rightEye = openvr->vrSystem->GetEyeToHeadTransform(vr::Eye_Right);

					logger::debug("VR IPD Detection:");
					logger::debug("  Detected IPD: {:.4f} meters ({:.2f} mm)", cachedIPD, cachedIPD * 1000.0f);
					logger::debug("  Left Eye Transform X: {:.4f}, Y: {:.4f}, Z: {:.4f}",
						leftEye.m[0][3], leftEye.m[1][3], leftEye.m[2][3]);
					logger::debug("  Right Eye Transform X: {:.4f}, Y: {:.4f}, Z: {:.4f}",
						rightEye.m[0][3], rightEye.m[1][3], rightEye.m[2][3]);
					logger::debug("  Eye Separation (calc): {:.4f} meters",
						std::abs(leftEye.m[0][3] - rightEye.m[0][3]));

					ipdLogged = true;
				}
			}

			// Convergence formula for screen-space SBS rendering:
			//
			// We render a quad into clip space [-1,+1] which the viewport maps
			// to one eye's half of an SBS texture (8688x4615, each eye 4344 wide).
			// There is NO projection matrix -- WVP is just affine (scale + translate).
			//
			// To make the overlay appear at virtual depth `d` meters, we shift
			// each eye's image in clip space by:
			//
			//   stereoShift = (IPD / 2) / (d * tan(hFOV/2))
			//
			// where tan(hFOV/2) comes from the HMD's projection matrix.
			//
			// This shift is CONSTANT for a given depth, regardless of quad scale.
			// Scale changes the apparent size. Depth changes convergence.
			// They are independent.
			//
			// VRMenuScale controls apparent size (how much of the view the menu fills).
			// VRMenuOffsetZ controls depth (negative = in front, determines convergence).
			// VRMenuOffsetX/Y control horizontal/vertical positioning.

			// Get projection parameters (Lens center offset and FOV)
			static float cachedTanHalfFOV = 0.0f;
			static float cachedLensOffsetX = 0.0f;
			if (cachedTanHalfFOV == 0.0f) {
				vr::HmdMatrix44_t projMatrix = openvr->vrSystem->GetProjectionMatrix(vr::Eye_Left, 0.1f, 1000.0f);
				cachedTanHalfFOV = 1.0f / projMatrix.m[0][0];
				cachedLensOffsetX = projMatrix.m[0][2];  // Horizontal offset of lens center in clip space
				logger::debug("VR Projection: projMatrix[0][0]={:.4f} (tanFOV={:.4f}), projMatrix[0][2]={:.4f} (Lens Center)",
					projMatrix.m[0][0], cachedTanHalfFOV, cachedLensOffsetX);
			}

			// Virtual depth in meters (positive = in front of viewer)
			// Depth is fixed at 1.0m to provide a stable convergence point regardless of scale
			const float virtualDepth = 1.0f;

			// Stereo shift in clip-space units -- independent of scale
			float stereoShift = (cachedIPD / 2.0f) / (virtualDepth * cachedTanHalfFOV);

			// Log convergence parameters periodically (debug only, every 60 frames, left eye only)
			static int frameCount = 0;
			if (eye == vr::Eye_Left && (frameCount++ % 60 == 0)) {
				logger::debug("VR Convergence Parameters:");
				logger::debug("  Scale: {:.2f}, Depth: {:.2f} m (Fixed)", settings.VRMenuScale, virtualDepth);
				logger::debug("  IPD: {:.4f} m, tan(hFOV/2): {:.4f}", cachedIPD, cachedTanHalfFOV);
				logger::debug("  Lens Center Offset: {:.4f} (Base)", cachedLensOffsetX);
				logger::debug("  Stereo Shift: {:.6f} (Convergence)", stereoShift);
				logger::debug("  Total Offset Left: {:.4f}", cachedLensOffsetX + stereoShift);
				logger::debug("  Viewport: X={:.0f}, W={:.0f}", vpDesc.TopLeftX, vpDesc.Width);
				logger::debug("  Offset: ({:.2f}, {:.2f}, {:.2f}), Scale: {:.2f}",
					settings.VRMenuOffsetX, settings.VRMenuOffsetY, settings.VRMenuOffsetZ, settings.VRMenuScale);
				logger::debug("  Formula: shift = (IPD/2) / (depth * tan(hFOV/2)) = {:.4f} / ({:.2f} * {:.4f})",
					cachedIPD / 2.0f, virtualDepth, cachedTanHalfFOV);
			}

			// Apply stereo offset: left eye gets positive, right eye gets negative
			// Account for lens center offset (proj[0][2]) to ensure "Infinity" is actually parallel
			// Left Eye (Lens < 0): Move Right (+) to converge from lens center
			// Right Eye (Lens > 0): Move Left (-) to converge from lens center
			float lensOffset = (eye == vr::Eye_Left) ? cachedLensOffsetX : -cachedLensOffsetX;
			float convergenceOffset = (eye == vr::Eye_Left) ? stereoShift : -stereoShift;

			float eyeOffsetX = lensOffset + convergenceOffset;

			// Position offset for X/Y only (Z is used for depth/convergence above)
			Matrix screenSpace = Matrix::CreateScale(scale, scale / aspect, 1.0f) *
			                     Matrix::CreateTranslation(settings.VRMenuOffsetX + eyeOffsetX, settings.VRMenuOffsetY, 0.5f);

			cbData.wvp = screenSpace.Transpose();
		} else {
			Matrix modelMatrix;
			Matrix vp;
			if (settings.VRMenuPositioningMethod == 1) {  // Fixed World Position
				modelMatrix = CreateOverlayScaleMatrix(settings.VRMenuScale) * fixedWorldOverlayPosition.m;
				vp = vpWorldSpace;
			} else {  // HMD Relative
				Matrix offset = Matrix::CreateTranslation(settings.VRMenuOffsetX, settings.VRMenuOffsetY, settings.VRMenuOffsetZ);
				modelMatrix = CreateOverlayScaleMatrix(settings.VRMenuScale) * offset;
				vp = vpHeadSpace;
			}
			cbData.wvp = (modelMatrix * vp).Transpose();
		}

		drawOverlayQuad(context, cbData);
	}

	// --- Render Controller Overlay ---
	if ((settings.attachMode == AttachMode::ControllerOnly || settings.attachMode == AttachMode::Both) && menuTexture) {
		vr::TrackedDeviceIndex_t attachIndex = Util::GetControllerIndexForDevice(settings.VRMenuAttachController, lastKnownLeftHandedMode);
		if (attachIndex != vr::k_unTrackedDeviceIndexInvalid && attachIndex < vr::k_unMaxTrackedDeviceCount) {
			vr::TrackedDevicePose_t controllerPose = renderPose[attachIndex];
			if (controllerPose.bPoseIsValid) {
				Matrix controllerWorld = Util::HmdMatrix34ToMatrix(controllerPose.mDeviceToAbsoluteTracking);
				Matrix offset = Matrix::CreateTranslation(settings.VRMenuControllerOffsetX, settings.VRMenuControllerOffsetY, settings.VRMenuControllerOffsetZ);
				Matrix modelMatrix = CreateOverlayScaleMatrix(settings.VRMenuScale) * offset * controllerWorld;

				// Backface culling: hide overlay when viewed from behind
				// Use the unscaled controller+offset transform for correct normal direction
				Matrix overlayTransform = offset * controllerWorld;
				Vector3 overlayNormal(overlayTransform._31, overlayTransform._32, overlayTransform._33);
				overlayNormal.Normalize();
				Matrix eyeWorld = hmdWorld * eyeToHead;
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
					drawOverlayQuad(context, cbData);
				}
			}
		}
	}

	// Restore State
	context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRTVs, oldDSV);
	context->RSSetViewports(numViewports, oldViewports);
	if (oldRS) {
		context->RSSetState(oldRS);
		oldRS->Release();
	}
	for (int i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
		if (oldRTVs[i])
			oldRTVs[i]->Release();
	if (oldDSV)
		oldDSV->Release();

	if (perf)
		perf->EndEvent();
}

void VR::InstallSubmitHook()
{
	static bool installed = false;
	if (installed)
		return;

	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	if (openvr && RE::BSOpenVR::GetIVRCompositor()) {
		logger::info("VR: Installing IVRCompositor::Submit hook for in-scene overlay rendering");

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

		// IVRCompositor::Submit is index 5
		stl::detour_vfunc<5, IVRCompositor_Submit>(RE::BSOpenVR::GetIVRCompositor());
		installed = true;

		logger::info("VR: In-scene overlay initialized");
	} else {
		logger::warn("VR: Failed to install IVRCompositor::Submit hook - Interface not available");
	}
}
