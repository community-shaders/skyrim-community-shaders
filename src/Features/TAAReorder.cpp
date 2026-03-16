/**
 * @brief TAA Periphery Reordering for VR DLSS Viewport Scaling
 *
 * This implementation follows the approach pioneered by PureDark's Skyrim Upscaler
 * (https://github.com/PureDark/Skyrim-Upscaler/tree/VR), which demonstrated how to
 * reorder Skyrim's post-processing pipeline to run vanilla TAA on the periphery while
 * DLSS processes a cropped center region. No code was copied; the approach was used as
 * a reference for the conductor/hook architecture.
 *
 * PureDark's Skyrim Upscaler is licensed under the MIT License:
 *   Copyright (c) 2022 PureDark
 *   https://github.com/PureDark/Skyrim-Upscaler/blob/VR/LICENSE
 */
#include "TAAReorder.h"

#include "Globals.h"
#include "Upscaling.h"
#include <algorithm>
#include <d3d11.h>

namespace TAAReorder
{
	bool ShouldReorderTAA()
	{
		if (!g_initialized)
			return false;
		auto& upscaling = globals::features::upscaling;
		return globals::game::isVR &&
		       upscaling.settings.vrPeripheryTAA &&
		       upscaling.settings.vrDlssViewportScale < 1.0f &&
		       upscaling.GetUpscaleMethod() == Upscaling::UpscaleMethod::kDLSS;
	}

	// ─── Setter A: Force TAA (pass-through) ───
	void ForceTAASetter::thunk()
	{
		func();
	}

	// ─── Setter B: TAA State Machine (pass-through) ───
	void TAAStateMachine::thunk()
	{
		func();
	}

	// ─── EnsurePostPPCopy: create/resize staging texture matching source ───
	void EnsurePostPPCopy(ID3D11Texture2D* sourceTex)
	{
		D3D11_TEXTURE2D_DESC srcDesc;
		sourceTex->GetDesc(&srcDesc);

		if (g_postPPCopy) {
			D3D11_TEXTURE2D_DESC existingDesc;
			g_postPPCopy->GetDesc(&existingDesc);
			if (existingDesc.Width == srcDesc.Width && existingDesc.Height == srcDesc.Height &&
				existingDesc.Format == srcDesc.Format)
				return;
		}

		D3D11_TEXTURE2D_DESC desc = srcDesc;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.MiscFlags = 0;
		g_postPPCopy = nullptr;
		g_postPPCopySRV = nullptr;
		globals::d3d::device->CreateTexture2D(&desc, nullptr, g_postPPCopy.put());

		if (g_postPPCopy) {
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = desc.Format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;
			globals::d3d::device->CreateShaderResourceView(g_postPPCopy.get(), &srvDesc, g_postPPCopySRV.put());
			Util::SetResourceName(g_postPPCopy.get(), "TAAReorder_PostPPCopy");
		}
	}

	// ─── Helper: set up common fullscreen rendering state ───
	static void SetupFullscreenState(ID3D11DeviceContext* context, float vpX, float vpY, float vpW, float vpH)
	{
		D3D11_VIEWPORT viewport = {};
		viewport.TopLeftX = vpX;
		viewport.TopLeftY = vpY;
		viewport.Width = vpW;
		viewport.Height = vpH;
		viewport.MaxDepth = 1.0f;

		auto& upscaling = globals::features::upscaling;
		context->RSSetViewports(1, &viewport);
		context->IASetInputLayout(nullptr);
		context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(upscaling.GetUpscaleVS(), nullptr, 0);
		context->RSSetState(upscaling.upscaleRasterizerState.get());
		context->OMSetBlendState(upscaling.upscaleBlendState.get(), nullptr, 0xffffffff);
	}

	// ─── Helper: draw fullscreen triangle (point-sample format-converting copy) ───
	void DrawFullscreenCopy(ID3D11ShaderResourceView* srcSRV, ID3D11RenderTargetView* dstRTV,
		float vpX, float vpY, float vpW, float vpH)
	{
		auto& upscaling = globals::features::upscaling;
		auto context = globals::d3d::context;

		SetupFullscreenState(context, vpX, vpY, vpW, vpH);
		context->PSSetShader(upscaling.GetDlssCompositePS(), nullptr, 0);

		ID3D11ShaderResourceView* srvs[] = { srcSRV };
		context->PSSetShaderResources(0, 1, srvs);

		ID3D11RenderTargetView* rtvs[] = { dstRTV };
		context->OMSetRenderTargets(1, rtvs, nullptr);

		context->Draw(3, 0);
	}

	// ─── ExecutePass hook: capture Phase 2A output, detect Phase 5 ───
	void ExecutePassHook::thunk(void* manager, void* passObj, int srcTech, int dstTech, void* extraData, uint8_t flag)
	{
		bool isPeripheryTAA = ShouldReorderTAA();
		bool shouldLog = (g_diagCounter == 0);

		// Compute pass index for Phase 2A / Phase 5 detection
		int passIndex = -1;
		if (isPeripheryTAA || shouldLog) {
			uintptr_t managerAddr = (uintptr_t)manager;
			uintptr_t passArrayBase = *(uintptr_t*)(managerAddr + 0x28);
			if (passArrayBase) {
				for (int i = 0; i < 40; i++) {
					if (*(uintptr_t*)(passArrayBase + i * 8) == (uintptr_t)passObj) {
						passIndex = i;
						break;
					}
				}
			}
		}

		if (shouldLog)
			logger::info("[TAAReorder] ExecutePass: src=0x{:X} dst=0x{:X} flag={} passIdx={}",
				srcTech, dstTech, flag, passIndex);

		// Execute the original pass
		func(manager, passObj, srcTech, dstTech, extraData, flag);

		// After Phase 2A: copy output RT to g_postPPCopy for DLSS to process
		if (isPeripheryTAA && passIndex == 30 && dstTech == 0x29) {
			ID3D11RenderTargetView* postRTV = nullptr;
			globals::d3d::context->OMGetRenderTargets(1, &postRTV, nullptr);
			if (postRTV) {
				ID3D11Resource* res = nullptr;
				postRTV->GetResource(&res);
				if (res) {
					ID3D11Texture2D* postTex = nullptr;
					res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&postTex);
					if (postTex) {
						EnsurePostPPCopy(postTex);
						globals::d3d::context->CopyResource(g_postPPCopy.get(), postTex);
						g_postPPReady = true;
						if (shouldLog) {
							D3D11_TEXTURE2D_DESC desc;
							postTex->GetDesc(&desc);
							logger::info("[TAAReorder] Phase 2A output: {}x{} fmt={} → copied to g_postPPCopy",
								desc.Width, desc.Height, (uint32_t)desc.Format);
						}
						postTex->Release();
					}
					res->Release();
				}
				postRTV->Release();
			}
		}

		// Detect Phase 5 completion
		if (isPeripheryTAA && passIndex == 35) {
			g_phase5Complete = true;
			if (shouldLog)
				logger::info("[TAAReorder] Phase 5 complete (passIdx=35)");
		}
	}

	// ─── BSImagespaceShader hook: DLSS eval + paste after pipeline completes ───
	// Wraps call at 0x132C827 (write_thunk_call). func() encompasses the
	// conductor (Phase 2A) but NOT Phase 5 (TAA+DRS) — Phase 5 runs after us.
	// We evaluate DLSS on the captured Phase 2A output and paste the center
	// via CopySubresourceRegion onto the submit texture.
	void BSImagespaceShaderHook::thunk(void* a_this, uint64_t a_param)
	{
		func(a_this, a_param);

		if (!ShouldReorderTAA())
			return;

		bool shouldLog = (g_diagCounter == 0);
		auto context = globals::d3d::context;
		auto& upscaling = globals::features::upscaling;

		// Get submit texture from bound RT after pipeline stage completes
		ID3D11RenderTargetView* submitRTV = nullptr;
		context->OMGetRenderTargets(1, &submitRTV, nullptr);
		ID3D11Texture2D* submitTex = nullptr;
		if (submitRTV) {
			ID3D11Resource* res = nullptr;
			submitRTV->GetResource(&res);
			if (res) {
				res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&submitTex);
				res->Release();
			}
			submitRTV->Release();
		}

		if (shouldLog) {
			if (submitTex) {
				D3D11_TEXTURE2D_DESC desc;
				submitTex->GetDesc(&desc);
				logger::info("[TAAReorder] BSImagespaceShaderHook: submitTex=0x{:X} {}x{} fmt={} bind=0x{:X} postPPReady={} phase5={}",
					(uintptr_t)submitTex, desc.Width, desc.Height, (uint32_t)desc.Format,
					desc.BindFlags, g_postPPReady, g_phase5Complete);
			} else {
				logger::info("[TAAReorder] BSImagespaceShaderHook: no submitTex bound");
			}
		}

		// Step 1: Evaluate DLSS on the captured post-PP intermediate
		if (g_postPPReady && g_postPPCopy) {
			if (shouldLog)
				logger::info("[TAAReorder] BSImagespaceShaderHook: evaluating DLSS on g_postPPCopy...");

			upscaling.Upscale(g_postPPCopy.get());
			g_dlssReady = true;

			if (shouldLog)
				logger::info("[TAAReorder] BSImagespaceShaderHook: DLSS evaluation complete");
		} else if (shouldLog) {
			logger::info("[TAAReorder] BSImagespaceShaderHook: skip DLSS (postPPReady={} postPPCopy={})",
				g_postPPReady, (void*)g_postPPCopy.get());
		}

		// Step 2: Paste DLSS center from g_postPPCopy onto submit texture per-eye
		if (g_dlssReady && submitTex && g_postPPCopy) {
			auto screenSize = globals::state->screenSize;
			uint32_t eyeW = (uint32_t)(screenSize.x / 2);
			uint32_t eyeH = (uint32_t)screenSize.y;
			float vpScale = upscaling.settings.vrDlssViewportScale;
			uint32_t centerW = (uint32_t)(eyeW * vpScale);
			uint32_t centerH = (uint32_t)(eyeH * vpScale);
			uint32_t baseCenterX = (eyeW - centerW) / 2;
			uint32_t centerY = (eyeH - centerH) / 2;

			// Apply nasal offset (in display resolution space, matching FinalizePerEyeOutputs)
			int32_t nasalShift = (int32_t)(upscaling.settings.vrDlssCropOffsetX * eyeW);

			float featherWidth = upscaling.settings.vrDlssFeatherWidth;
			float featherPixels = featherWidth * eyeW;

			// Feathered blend path: use FeatheredCompositePS with hardware alpha blending
			bool useFeathered = featherPixels > 0.0f && upscaling.vrFeatheredCompositePS && upscaling.vrFeatheredCompositeBlendState;
			if (useFeathered) {
				// Re-acquire submitRTV (we released it above, need it for render target binding)
				ID3D11RenderTargetView* pasteRTV = nullptr;
				context->OMGetRenderTargets(1, &pasteRTV, nullptr);

				if (pasteRTV) {
					// Save current pipeline state
					ID3D11BlendState* oldBlendState = nullptr;
					float oldBlendFactor[4];
					UINT oldSampleMask;
					context->OMGetBlendState(&oldBlendState, oldBlendFactor, &oldSampleMask);

					ID3D11VertexShader* oldVS = nullptr;
					context->VSGetShader(&oldVS, nullptr, nullptr);
					ID3D11PixelShader* oldPS = nullptr;
					context->PSGetShader(&oldPS, nullptr, nullptr);

					UINT oldNumVPs = 1;
					D3D11_VIEWPORT oldVP;
					context->RSGetViewports(&oldNumVPs, &oldVP);

					ID3D11ShaderResourceView* oldPSSRV = nullptr;
					context->PSGetShaderResources(0, 1, &oldPSSRV);
					ID3D11SamplerState* oldPSSampler = nullptr;
					context->PSGetSamplers(0, 1, &oldPSSampler);
					ID3D11Buffer* oldPSCB = nullptr;
					context->PSGetConstantBuffers(0, 1, &oldPSCB);

					// Ensure CB exists (lazy create, matching Upscaling.cpp pattern)
					if (!upscaling.vrFeatheredCompositeCB) {
						D3D11_BUFFER_DESC cbDesc = {};
						cbDesc.ByteWidth = 48;
						cbDesc.Usage = D3D11_USAGE_DYNAMIC;
						cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
						cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
						DX::ThrowIfFailed(globals::d3d::device->CreateBuffer(&cbDesc, nullptr, upscaling.vrFeatheredCompositeCB.put()));
					}

					// Set shared state: VS, PS, IA, blend
					context->IASetInputLayout(nullptr);
					context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
					context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					context->VSSetShader(upscaling.GetUpscaleVS(), nullptr, 0);
					context->PSSetShader(upscaling.vrFeatheredCompositePS.get(), nullptr, 0);
					context->RSSetState(upscaling.upscaleRasterizerState.get());

					float blendFactor[4] = { 0, 0, 0, 0 };
					context->OMSetBlendState(upscaling.vrFeatheredCompositeBlendState.get(), blendFactor, 0xFFFFFFFF);

					// Bind g_postPPCopy SRV as crop source at t0
					ID3D11ShaderResourceView* srvs[1] = { g_postPPCopySRV.get() };
					context->PSSetShaderResources(0, 1, srvs);

					// Bind render target
					ID3D11RenderTargetView* rtvs[1] = { pasteRTV };
					context->OMSetRenderTargets(1, rtvs, nullptr);

					// Create/use linear sampler (use Upscaling's if available)
					if (!upscaling.vrLinearSampler) {
						D3D11_SAMPLER_DESC sampDesc = {};
						sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
						sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
						sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
						sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
						globals::d3d::device->CreateSamplerState(&sampDesc, upscaling.vrLinearSampler.put());
					}
					ID3D11SamplerState* samplers[1] = { upscaling.vrLinearSampler.get() };
					context->PSSetSamplers(0, 1, samplers);

					for (uint32_t i = 0; i < 2; i++) {
						uint32_t eyeOffset = i * eyeW;
						int32_t eyeNasalShift = (i == 0) ? nasalShift : -nasalShift;
						uint32_t offsetCenterX = (uint32_t)std::clamp((int32_t)baseCenterX + eyeNasalShift, 0, (int32_t)(eyeW - centerW));

						// Set viewport to this eye region within the SBS submit texture
						D3D11_VIEWPORT vp = {};
						vp.TopLeftX = (float)eyeOffset;
						vp.TopLeftY = 0.0f;
						vp.Width = (float)eyeW;
						vp.Height = (float)eyeH;
						vp.MinDepth = 0.0f;
						vp.MaxDepth = 1.0f;
						context->RSSetViewports(1, &vp);

						// Update constant buffer with crop rect in SCREEN-SPACE pixel coordinates.
						// SV_Position in the pixel shader is in screen space (not viewport-relative):
						// for eye 0, x ranges [0, eyeW); for eye 1, x ranges [eyeW, 2*eyeW).
						// CropOrigin must therefore include the eye offset so distance calculations
						// in FeatheredCompositePS work correctly for both eyes.
						// SrcUVOrigin/Scale remap crop-local [0,1] UV to the correct eye region
						// within the full SBS g_postPPCopy texture.
						uint32_t fullW = eyeW * 2;
						uint32_t fullH = eyeH;
						float srcUVOriginX = (float)(eyeOffset + offsetCenterX) / (float)fullW;
						float srcUVOriginY = (float)centerY / (float)fullH;
						float srcUVScaleX = (float)centerW / (float)fullW;
						float srcUVScaleY = (float)centerH / (float)fullH;

						D3D11_MAPPED_SUBRESOURCE mapped{};
						context->Map(upscaling.vrFeatheredCompositeCB.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
						struct
						{
							float originX, originY;
							float sizeX, sizeY;
							float featherWidth;
							float pad0;
							float srcUVOriginX, srcUVOriginY;
							float srcUVScaleX, srcUVScaleY;
							float pad1[2];
						} cbData = {
							(float)(eyeOffset + offsetCenterX), (float)centerY,
							(float)centerW, (float)centerH,
							featherPixels, 0.0f,
							srcUVOriginX, srcUVOriginY,
							srcUVScaleX, srcUVScaleY,
							{}
						};
						memcpy(mapped.pData, &cbData, sizeof(cbData));
						context->Unmap(upscaling.vrFeatheredCompositeCB.get(), 0);

						ID3D11Buffer* cbs[1] = { upscaling.vrFeatheredCompositeCB.get() };
						context->PSSetConstantBuffers(0, 1, cbs);

						context->Draw(3, 0);
					}

					if (shouldLog)
						logger::info("[TAAReorder] BSImagespaceShaderHook: feathered composite {}x{} at ({},{}) feather={:.1f}px nasalShift={} per-eye onto submit",
							centerW, centerH, baseCenterX, centerY, featherPixels, nasalShift);

					// Restore pipeline state
					context->OMSetBlendState(oldBlendState, oldBlendFactor, oldSampleMask);
					context->RSSetViewports(1, &oldVP);
					context->VSSetShader(oldVS, nullptr, 0);
					context->PSSetShader(oldPS, nullptr, 0);
					context->PSSetShaderResources(0, 1, &oldPSSRV);
					context->PSSetSamplers(0, 1, &oldPSSampler);
					context->PSSetConstantBuffers(0, 1, &oldPSCB);

					if (oldBlendState) oldBlendState->Release();
					if (oldVS) oldVS->Release();
					if (oldPS) oldPS->Release();
					if (oldPSSRV) oldPSSRV->Release();
					if (oldPSSampler) oldPSSampler->Release();
					if (oldPSCB) oldPSCB->Release();

					pasteRTV->Release();
				} else {
					useFeathered = false;  // fall through to hard copy
					if (shouldLog)
						logger::warn("[TAAReorder] BSImagespaceShaderHook: feathered path - could not re-acquire submitRTV, falling back to hard copy");
				}
			}
			if (!useFeathered) {
				// Hard edge path: CopySubresourceRegion (feather disabled or resources not ready)
				for (uint32_t i = 0; i < 2; i++) {
					uint32_t eyeOffset = i * eyeW;
					int32_t eyeNasalShift = (i == 0) ? nasalShift : -nasalShift;
					uint32_t offsetCenterX = (uint32_t)std::clamp((int32_t)baseCenterX + eyeNasalShift, 0, (int32_t)(eyeW - centerW));

					D3D11_BOX srcBox = {
						eyeOffset + offsetCenterX, centerY, 0,
						eyeOffset + offsetCenterX + centerW, centerY + centerH, 1
					};
					context->CopySubresourceRegion(submitTex, 0,
						eyeOffset + offsetCenterX, centerY, 0,
						g_postPPCopy.get(), 0, &srcBox);
				}

				if (shouldLog)
					logger::info("[TAAReorder] BSImagespaceShaderHook: hard-copy pasted DLSS crop {}x{} at ({},{}) nasalShift={} per-eye onto submit",
						centerW, centerH, baseCenterX, centerY, nasalShift);
			}

			g_dlssPasteComplete = true;
		} else if (shouldLog) {
			logger::info("[TAAReorder] BSImagespaceShaderHook: skip paste (dlssReady={} submitTex={} postPPCopy={})",
				g_dlssReady, (void*)submitTex, (void*)g_postPPCopy.get());
		}

		if (submitTex)
			submitTex->Release();
	}

	// ─── Depth/stencil registration hook: diagnostic logging ───
	// Tracks dimensions per slot and logs whenever they change.
	// data[0]=width, data[1]=height based on initial analysis.
	void DepthStencilRegHook::thunk(void* manager, uint32_t slot, void* desc)
	{
		if (desc && slot < 32) {
			auto* data = reinterpret_cast<uint32_t*>(desc);
			static uint32_t lastWidth[32] = {};
			static uint32_t lastHeight[32] = {};
			static uint32_t callCount[32] = {};

			callCount[slot]++;
			bool dimsChanged = (data[0] != lastWidth[slot] || data[1] != lastHeight[slot]);
			if (dimsChanged) {
				logger::info("[TAAReorder] DepthStencilReg: slot={} {}x{} → {}x{} (call #{}) data[2..7]= {} {} {} {} {} {}",
					slot, lastWidth[slot], lastHeight[slot], data[0], data[1], callCount[slot],
					data[2], data[3], data[4], data[5], data[6], data[7]);
				lastWidth[slot] = data[0];
				lastHeight[slot] = data[1];
			}
		}

		func(manager, slot, desc);
	}

	// ─── Hidden area mesh render hook: pass-through ───
	// HAM renders normally. Previous "frozen frame" artifacts at the HAM boundary
	// were caused by the depth upscaler's conservative blending (GatherRed + lerp)
	// leaking depth=0 mask values into valid depth. Fixed in DepthUpscalePS.hlsl
	// by switching to pure point sampling.
	// HiddenAreaMeshHook removed — the passthrough hook was breaking HAM
	// by corrupting the original function via Detours on an unverified RVA.

	// ─── BSOpenVR::Submit hook: diagnostic logging ───
	void SubmitHook::thunk(void* thisPtr, void* textureHandle)
	{
		if (g_diagCounter == 0 && textureHandle) {
			auto tex2d = static_cast<ID3D11Texture2D*>(textureHandle);
			D3D11_TEXTURE2D_DESC desc = {};
			tex2d->GetDesc(&desc);
			auto base = REL::Module::get().base();
			auto retAddr = reinterpret_cast<uintptr_t>(_ReturnAddress());
			logger::info("[TAAReorder] Submit: tex=0x{:X} {}x{} fmt={} dlssPasted={} callerRVA=0x{:X}",
				(uintptr_t)textureHandle, desc.Width, desc.Height, (uint32_t)desc.Format,
				g_dlssPasteComplete, retAddr - base);
		}

		func(thisPtr, textureHandle);
	}

	// ─── Post-processing conductor call hook: pass-through (tracking only) ───
	// Inner conductor call at 0x1325086 inside BSImagespaceShader::Render.
	// Only tracks g_insideConductor state. DLSS logic is in BSImagespaceShaderHook.
	void ConductorCallHook::thunk(void* a1, void* a2, void* a3, void* a4)
	{
		g_insideConductor = true;
		func(a1, a2, a3, a4);
		g_insideConductor = false;
	}

	void InitEarly()
	{
		auto base = REL::Module::get().base();

		// ─── Hook: DepthStencilRegistration (RVA 0x00DC79D0) ───
		// Must be installed before renderer initialization (which registers depth/stencil targets).
		// Called from Upscaling::Load(), before D3D device creation.
		DepthStencilRegHook::func = reinterpret_cast<RegisterDepthStencil_t>(base + 0x00DC79D0);
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach(reinterpret_cast<PVOID*>(&DepthStencilRegHook::func), reinterpret_cast<PVOID>(DepthStencilRegHook::thunk));
		DetourTransactionCommit();

		logger::info("[TAAReorder] InitEarly: DepthStencil registration hooked at RVA 0x00DC79D0");
	}

	void Init()
	{
		auto base = REL::Module::get().base();

		// ─── Core pointers ───
		g_pRendererSingleton = reinterpret_cast<uintptr_t*>(base + 0x034234C0);

		// ─── Hook: ForceTAASetter (RVA 0x005C8EE0) ───
		ForceTAASetter::func = base + 0x005C8EE0;
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach(reinterpret_cast<PVOID*>(&ForceTAASetter::func), reinterpret_cast<PVOID>(ForceTAASetter::thunk));
		DetourTransactionCommit();

		// ─── Hook: TAAStateMachine (RVA 0x005C8F10) ───
		TAAStateMachine::func = base + 0x005C8F10;
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach(reinterpret_cast<PVOID*>(&TAAStateMachine::func), reinterpret_cast<PVOID>(TAAStateMachine::thunk));
		DetourTransactionCommit();

		// ─── Hook: ExecutePass (RVA 0x012D2540) ───
		ExecutePassHook::func = reinterpret_cast<ExecutePass_t>(base + 0x012D2540);
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach(reinterpret_cast<PVOID*>(&ExecutePassHook::func), reinterpret_cast<PVOID>(ExecutePassHook::thunk));
		DetourTransactionCommit();

		// ─── Hook: BSOpenVR::Submit (RVA 0x00C53920) ───
		SubmitHook::func = reinterpret_cast<BSOpenVRSubmit_t>(base + 0x00C53920);
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach(reinterpret_cast<PVOID*>(&SubmitHook::func), reinterpret_cast<PVOID>(SubmitHook::thunk));
		DetourTransactionCommit();

		// ─── Hook: BSImagespaceShader via write_thunk_call at RVA 0x132C827 ───
		// Wraps BSImagespaceShader::Render from the Orchestrator level.
		// func() encompasses conductor (Phase 2A) + Phase 5 (TAA+DRS) + Submit.
		// After func(): DLSS eval + paste. Matches PureDark's BSImagespaceShader_Hook_VR.
		stl::write_thunk_call<BSImagespaceShaderHook>(base + 0x132C827);

		// ─── Hook: Inner conductor call via write_thunk_call at RVA 0x1325086 ───
		// Pass-through, only tracks g_insideConductor state.
		stl::write_thunk_call<ConductorCallHook>(base + 0x1325086);

		g_initialized = true;

		logger::info("[TAAReorder] Initialized — base=0x{:X}", base);
		logger::info("[TAAReorder] Post-pipeline DLSS mode (periphery TAA)");
		logger::info("[TAAReorder] BSImagespaceShader hooked via write_thunk_call at RVA 0x132C827 (DLSS eval + paste)");
		logger::info("[TAAReorder] Inner conductor hooked via write_thunk_call at RVA 0x1325086 (tracking only)");
		logger::info("[TAAReorder] BSOpenVR::Submit hooked at RVA 0x00C53920");
	}
}
