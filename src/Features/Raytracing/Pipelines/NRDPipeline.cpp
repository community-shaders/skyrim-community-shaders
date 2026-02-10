#include "NRDPipeline.h"

void NRDPipeline::CompileShaders()
{

}

void NRDPipeline::SetupResources(ID3D12Device5* device)
{
	// Initialize NRD: REBLUR, RELAX and SIGMA in one instance
	const nrd::DenoiserDesc denoisersDescs[] = {

#if (NRD_MODE == OCCLUSION)
#	if (NRD_COMBINED == 1)
		{ NRD_ID(REBLUR_DIFFUSE_SPECULAR_OCCLUSION), nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR_OCCLUSION },
#	else
		{ NRD_ID(REBLUR_DIFFUSE_OCCLUSION), nrd::Denoiser::REBLUR_DIFFUSE_OCCLUSION },
		{ NRD_ID(REBLUR_SPECULAR_OCCLUSION), nrd::Denoiser::REBLUR_SPECULAR_OCCLUSION },
#	endif
#elif (NRD_MODE == SH)
#	if (NRD_COMBINED == 1)
		{ NRD_ID(REBLUR_DIFFUSE_SPECULAR_SH), nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR_SH },
#	else
		{ NRD_ID(REBLUR_DIFFUSE_SH), nrd::Denoiser::REBLUR_DIFFUSE_SH },
		{ NRD_ID(REBLUR_SPECULAR_SH), nrd::Denoiser::REBLUR_SPECULAR_SH },
#	endif
#elif (NRD_MODE == DIRECTIONAL_OCCLUSION)
		{ NRD_ID(REBLUR_DIFFUSE_DIRECTIONAL_OCCLUSION), nrd::Denoiser::REBLUR_DIFFUSE_DIRECTIONAL_OCCLUSION },
#else
#	if (NRD_COMBINED == 1)
		{ NRD_ID(REBLUR_DIFFUSE_SPECULAR), nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR },
#	else
		{ NRD_ID(REBLUR_DIFFUSE), nrd::Denoiser::REBLUR_DIFFUSE },
		{ NRD_ID(REBLUR_SPECULAR), nrd::Denoiser::REBLUR_SPECULAR },
#	endif
#endif

	// RELAX
#if (NRD_MODE == SH)
#	if (NRD_COMBINED == 1)
		{ NRD_ID(RELAX_DIFFUSE_SPECULAR_SH), nrd::Denoiser::RELAX_DIFFUSE_SPECULAR_SH },
#	else
		{ NRD_ID(RELAX_DIFFUSE_SH), nrd::Denoiser::RELAX_DIFFUSE_SH },
		{ NRD_ID(RELAX_SPECULAR_SH), nrd::Denoiser::RELAX_SPECULAR_SH },
#	endif
#else
#	if (NRD_COMBINED == 1)
		{ NRD_ID(RELAX_DIFFUSE_SPECULAR), nrd::Denoiser::RELAX_DIFFUSE_SPECULAR },
#	else
		{ NRD_ID(RELAX_DIFFUSE), nrd::Denoiser::RELAX_DIFFUSE },
		{ NRD_ID(RELAX_SPECULAR), nrd::Denoiser::RELAX_SPECULAR },
#	endif
#endif

	// SIGMA
#if (NRD_MODE < OCCLUSION)
		{ NRD_ID(SIGMA_SHADOW), SIGMA_VARIANT },
#endif

		// REFERENCE
		{ NRD_ID(REFERENCE), nrd::Denoiser::REFERENCE },
	};

	nrd::InstanceCreationDesc instanceCreationDesc = {};
	instanceCreationDesc.denoisers = denoisersDescs;
	instanceCreationDesc.denoisersNum = _countof(denoisersDescs);

	nrd::Instance* instance = nullptr;
	nrd::Result res = nrd::CreateInstance(instanceCreationDesc, instance);

	if (res != nrd::Result::SUCCESS) {
		assert(!"Failed to create NRD instance");
	}

	const nrd::InstanceDesc* instanceDesc = nrd::GetInstanceDesc(*instance);

	eastl::vector<CD3DX12_STATIC_SAMPLER_DESC> samplers;
	for (uint32_t samplerIndex = 0; samplerIndex < instanceDesc->samplersNum; samplerIndex++) {
		const nrd::Sampler& samplerMode = instanceDesc->samplers[samplerIndex];

		D3D12_FILTER filter;
		D3D12_TEXTURE_ADDRESS_MODE address;

		switch (samplerMode) {
		case nrd::Sampler::NEAREST_CLAMP:
			filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
			address = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			break;
		case nrd::Sampler::LINEAR_CLAMP:
			filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			address = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			break;
		default:
			assert(!"Unknown NRD sampler mode");
			break;
		}

		samplers.emplace_back(
			instanceDesc->samplersBaseRegisterIndex + samplerIndex, 
			filter, 
			address, 
			address, 
			address, 
			0.0f, 
			16u, 
			D3D12_COMPARISON_FUNC_LESS_EQUAL, 
			D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
			0.0f,
			D3D12_FLOAT32_MAX,
			D3D12_SHADER_VISIBILITY_ALL,
			instanceDesc->constantBufferAndSamplersSpaceIndex);
	}

	pipelines.resize(instanceDesc->pipelinesNum);
	for (uint32_t pipelineIndex = 0; pipelineIndex < instanceDesc->pipelinesNum; pipelineIndex++) {
		const nrd::PipelineDesc& nrdPipelineDesc = instanceDesc->pipelines[pipelineIndex];
		const nrd::ComputeShaderDesc& nrdComputeShader = nrdPipelineDesc.computeShaderDXIL;

		eastl::unique_ptr<NDRSubPipeline> pipeline = eastl::make_unique<NDRSubPipeline>();

		// Root Signature
		eastl::vector<CD3DX12_DESCRIPTOR_RANGE1> srvRanges;
		eastl::vector<CD3DX12_DESCRIPTOR_RANGE1> uavRanges;
		eastl::vector<CD3DX12_ROOT_PARAMETER1> rootParameters;

		uint32_t srvBaseRegister = instanceDesc->resourcesBaseRegisterIndex;
		uint32_t uavBaseRegister = instanceDesc->resourcesBaseRegisterIndex;

        for (uint32_t resourceRangeIndex = 0; resourceRangeIndex < nrdPipelineDesc.resourceRangesNum; resourceRangeIndex++) {
			const nrd::ResourceRangeDesc& nrdResourceRange = nrdPipelineDesc.resourceRanges[resourceRangeIndex];

			bool srv = nrdResourceRange.descriptorType == nrd::DescriptorType::TEXTURE;

			CD3DX12_DESCRIPTOR_RANGE1 descRange;
			descRange.Init(
				srv ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV : D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
				nrdResourceRange.descriptorsNum,
				srv ? srvBaseRegister : uavBaseRegister,
				instanceDesc->resourcesSpaceIndex);

			if (srv) {
				srvRanges.push_back(descRange);
				srvBaseRegister += nrdResourceRange.descriptorsNum;
			} else {
				uavRanges.push_back(descRange);
				uavBaseRegister += nrdResourceRange.descriptorsNum;
			}
		}

		if (srvRanges.empty()) {
			rootParameters.emplace_back().InitAsDescriptorTable(0, nullptr);
		} else {
			rootParameters.emplace_back().InitAsDescriptorTable(
				(UINT)srvRanges.size(),
				srvRanges.data(),
				D3D12_SHADER_VISIBILITY_ALL);
		}

		if (uavRanges.empty()) {
			rootParameters.emplace_back().InitAsDescriptorTable(0, nullptr);
		} else {
			rootParameters.emplace_back().InitAsDescriptorTable(
				(UINT)uavRanges.size(),
				uavRanges.data(),
				D3D12_SHADER_VISIBILITY_ALL);
		}

		CD3DX12_ROOT_PARAMETER1 constantRootParam;
		constantRootParam.InitAsConstantBufferView(instanceDesc->constantBufferRegisterIndex, instanceDesc->constantBufferAndSamplersSpaceIndex);
		rootParameters.push_back(constantRootParam);

		auto flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS |
		             D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		             D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS;

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
		rootSigDesc.Init_1_1(
			(uint)rootParameters.size(),
			rootParameters.data(),
			(uint)samplers.size(),
			samplers.data(),
			flags);

		winrt::com_ptr<ID3DBlob> serializedRootSig = nullptr;
		winrt::com_ptr<ID3DBlob> errorBlob = nullptr;

		DX::ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, serializedRootSig.put(), errorBlob.put()));
		DX::ThrowIfFailed(device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(pipeline->rootSignature.put())));
		DX::ThrowIfFailed(pipeline->rootSignature->SetName(std::format(L"Compute Root Signature - NRD {}", pipelineIndex).c_str()));

		// Shader and Pipeline State
		D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc = {};
		computeDesc.pRootSignature = pipeline->rootSignature.get();
		computeDesc.CS = { nrdComputeShader.bytecode, nrdComputeShader.size };

		DX::ThrowIfFailed(device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(pipeline->pipelineState.put())));
		DX::ThrowIfFailed(pipeline->pipelineState->SetName(std::format(L"Compute Pipeline - NRD {}", pipelineIndex).c_str()));

		pipelines[pipelineIndex] = eastl::move(pipeline);
	}
}

void NRDPipeline::SetupTextureResources([[maybe_unused]] uint2 size)
{

}

void NRDPipeline::Denoise() const
{

}