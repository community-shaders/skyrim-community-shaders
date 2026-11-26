#include "Border.h"

#include "Features/PostProcessing.h"
#include "Features/Upscaling.h"
#include "I18n/I18n.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Border::Settings,
	BorderColor,
	DepthThreshold,
	Scale)

void Border::DrawSettings()
{
	ImGui::ColorEdit3(T("feature.post_processing.border.border_color", "Border Color"), reinterpret_cast<float*>(&settings.BorderColor));
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("feature.post_processing.border.the_color_of_the_border", "The color of the border."));

	ImGui::SliderFloat(T("feature.post_processing.border.depth_threshold", "Depth Threshold"), &settings.DepthThreshold, 0.f, 1.f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("feature.post_processing.border.the_depth_threshold_for_the_border_effect", "The depth threshold for the border effect."));

	ImGui::SliderFloat4(T("feature.post_processing.border.scale_top_down_left_right", "Scale (Top, Down, Left, Right)"), reinterpret_cast<float*>(&settings.Scale), 0.f, 0.5f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("feature.post_processing.border.the_scale_of_the_border_on_each_side", "The scale of the border on each side of the screen."));
}

void Border::RestoreDefaultSettings()
{
	settings = {};
}

void Border::LoadSettings(json& o_json)
{
	settings = o_json;
}

void Border::SaveSettings(json& o_json)
{
	o_json = settings;
}

void Border::SetupResources()
{
	logger::debug("Creating buffers...");
	{
		borderCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<BorderCB>(), "PostProcessing::Border::Constants");
	}

	CompileComputeShaders();
}

void Border::ClearShaderCache()
{
	BumpShaderGeneration();
	const auto shaderPtrs = std::array{
		&borderCS,
		&borderClearMVCS
	};

	{
		std::lock_guard lock(shaderMutex);
		for (auto shader : shaderPtrs)
			if ((*shader)) {
				(*shader)->Release();
				shader->detach();
			}
	}

	CompileComputeShaders();
}

void Border::CompileComputeShaders()
{
	const std::vector<ComputeShaderCompileInfo> shaderInfos = {
		{ &borderCS, "border.cs.hlsl" },
		{ &borderClearMVCS, "border_clear_mv.cs.hlsl" },
	};

	CompileComputeShadersAsync(L"Data\\Shaders\\PostProcessing\\Border", shaderInfos);
}

void Border::ClearMotionVectorsForFrameGen()
{
	if (!borderCB || !AllShadersReady({ &borderClearMVCS }))
		return;

	// Only run when there's an actual border to clear
	if (settings.Scale.x <= 0.f && settings.Scale.y <= 0.f && settings.Scale.z <= 0.f && settings.Scale.w <= 0.f)
		return;

	const auto& swapChain = Upscaling::dx12SwapChain;
	const auto* depth = swapChain.depthBufferShared12;
	const auto* motion = swapChain.motionVectorBufferShared12;
	if (!depth || !depth->srv || !motion || !motion->uav)
		return;

	auto context = globals::d3d::context;

	float2 screenSize{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
	auto dynResDim = Util::ConvertToDynamic(screenSize, true);

	BorderCB data = {
		.BorderColor = float4(settings.BorderColor.x, settings.BorderColor.y, settings.BorderColor.z, settings.DepthThreshold),
		.Scale = settings.Scale,
		.BufferDim = float4(dynResDim.x, dynResDim.y, 1.f / dynResDim.x, 1.f / dynResDim.y)
	};
	borderCB->Update(data);

	ID3D11ShaderResourceView* srvs[1] = { depth->srv };
	context->CSSetShaderResources(0, 1, srvs);
	ID3D11UnorderedAccessView* uavs[1] = { motion->uav };
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
	ID3D11Buffer* cb = borderCB->CB();
	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetShader(borderClearMVCS.get(), nullptr, 0);

	context->Dispatch(((uint)dynResDim.x + 7) >> 3, ((uint)dynResDim.y + 7) >> 3, 1);

	srvs[0] = nullptr;
	uavs[0] = nullptr;
	cb = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
	context->CSSetShaderResources(0, 1, srvs);
	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetShader(nullptr, nullptr, 0);
}

void Border::Draw(TextureInfo& inout_tex)
{
	if (!owner || !AllShadersReady({ &borderCS }))
		return;

	auto* texOutput = owner->GetColorTarget(inout_tex.tex);
	if (!texOutput)
		return;

	globals::profiler->BeginPass("PostProcessing::Border");
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	float2 res = { (float)texOutput->desc.Width, (float)texOutput->desc.Height };
	res = Util::ConvertToDynamic(res);

	BorderCB data = {
		.BorderColor = float4(settings.BorderColor.x, settings.BorderColor.y, settings.BorderColor.z, settings.DepthThreshold),
		.Scale = settings.Scale,
		.BufferDim = float4(res.x, res.y, 1.f / res.x, 1.f / res.y)
	};
	borderCB->Update(data);

	auto* depthSRV = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].depthSRV;
	if (!depthSRV) {
		globals::profiler->EndPass();
		return;
	}
	ID3D11ShaderResourceView* srvs[2] = { inout_tex.srv, depthSRV };
	context->CSSetShaderResources(0, 2, srvs);
	ID3D11UnorderedAccessView* uav = texOutput->uav.get();
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	ID3D11Buffer* cb = borderCB->CB();
	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetShader(borderCS.get(), nullptr, 0);

	context->Dispatch(((uint)res.x + 7) >> 3, ((uint)res.y + 7) >> 3, 1);

	srvs[0] = nullptr;
	srvs[1] = nullptr;
	uav = nullptr;
	cb = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	context->CSSetShaderResources(0, 2, srvs);
	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetShader(nullptr, nullptr, 0);

	inout_tex = { texOutput->resource.get(), texOutput->srv.get() };
	globals::profiler->EndPass();
}
