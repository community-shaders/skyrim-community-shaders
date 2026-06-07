#include "ShadowmapCascadeRasterizerFix.h"

void ShadowmapRasterizerFix::Install()
{
	// This function is called once per cascade to begin the updating and rendering process
	stl::write_thunk_call<BSShadowDirectionalLight_RenderShadowmaps_RenderCascade>(REL::RelocationID(101495, 108489).address() + REL::Relocate(0xC6, 0xC6));

	gRasterStates = reinterpret_cast<RasterStateArray*>(REL::RelocationID(524748, 411363).address());

	numCascades = static_cast<uint>(Util::GetGameSettingValue<std::int32_t>("iNumSplits:Display", Settings.at("iNumSplits:Display")));
}

void ShadowmapRasterizerFix::BSShadowDirectionalLight_RenderShadowmaps_RenderCascade::thunk(RE::BSShadowDirectionalLight* light, void* arg1, void* arg2, uint32_t flags)
{
	static uint cascade = 0;

	static bool initialized = false;
	if (!initialized) {
		//Backup
		if (cascade == 0) {
			std::memcpy(backupGameRasterStates, *gRasterStates, sizeof(RasterStateArray));
			numCascades = std::min(numCascades, maxCascades);
		}

		//Clone
		CloneRasterStates(gRasterStates, cascade);

		initialized = cascade == numCascades - 1;
	}

	//Emplace
	std::memcpy(*gRasterStates, shadowmapRasterStates[cascade], sizeof(RasterStateArray));

	func(light, arg1, arg2, flags);

	//Restore
	if (cascade == numCascades - 1)
		std::memcpy(*gRasterStates, backupGameRasterStates, sizeof(RasterStateArray));

	cascade = ++cascade < numCascades ? cascade : 0;
}

void ShadowmapRasterizerFix::GetUpdatedRasterDesc(D3D11_RASTERIZER_DESC& outputDesc, ShadowMapRasterizerDescriptor shadowmapDesc)
{
	outputDesc.DepthBias = shadowmapDesc.rasterDepthBias;
	outputDesc.DepthBiasClamp = shadowmapDesc.rasterDepthBiasClamp;
	outputDesc.SlopeScaledDepthBias = shadowmapDesc.rasterSlopeScaleBias;
}

// Since state objects are shared globally across the pipeline we make duplicate arrays that cover the same range of states the game does
void ShadowmapRasterizerFix::CloneRasterStates(RasterStateArray* inputArray, int cascade)
{
	for (int fill = 0; fill < 2; fill++) {
		for (int cull = 0; cull < 3; cull++) {
			for (int depth = 0; depth < 12; depth++) {
				for (int scissor = 0; scissor < 2; scissor++) {
					if (auto* gRasterizer = (*inputArray)[fill][cull][depth][scissor]) {
						D3D11_RASTERIZER_DESC desc{};
						gRasterizer->GetDesc(&desc);

						GetUpdatedRasterDesc(desc, cascadeDescriptors[cascade]);

						DX::ThrowIfFailed(globals::d3d::device->CreateRasterizerState(&desc, &shadowmapRasterStates[cascade][fill][cull][depth][scissor]));
					}
				}
			}
		}
	}
}