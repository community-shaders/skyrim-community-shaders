#define COMPUTESHADER
#include "Common/ViewAtmosphere.hlsli"
#include "PhysicalSky/Common.hlsli"
#include "PhysicalSky/Lightning.hlsli"
#define SKYLIGHTING_PROBE_REGISTER t50
#include "Skylighting/Skylighting.hlsli"

Texture3D<float4> MediumSource : register(t0);
RWTexture3D<float4> IntegratedLuminance : register(u0);
RWTexture3D<float4> IntegratedTransmittance : register(u1);
cbuffer MediumDispatch : register(b0)
{
	uint MediumLevel;
	uint3 MediumPadding;
};

[numthreads(8, 8, 1)] void main(uint2 coord : SV_DispatchThreadID) {
	const ViewAtmosphere::Grid grid = ViewAtmosphere::Data[0].grids[MediumLevel];
	if (any(coord >= grid.size.xy))
		return;
	float3 light = 0.0;
	float3 tr = 1.0;
	IntegratedLuminance[uint3(coord, 0) + grid.offset.xyz] = 0.0;
	IntegratedTransmittance[uint3(coord, 0) + grid.offset.xyz] = 1.0;
	[loop] for (uint layer = 0u; layer < grid.size.z; ++layer)
	{
		const float4 source = MediumSource[uint3(coord, layer) + grid.offset.xyz];
		const float3 position = ViewAtmosphere::Position(grid, uint3(coord, layer), 0.5);
		float3 rayleigh, mie, extinction;
		PhysSky::GetMediumCoefficients(position, rayleigh, mie, extinction);
		extinction = extinction * SharedData::physSkyData.apTrMix + source.a;
		const float step = (ViewAtmosphere::Distance(grid, layer + 1u) - ViewAtmosphere::Distance(grid, layer)) * (GAME_UNIT_TO_M / 1000.0);
		const float3 tau = max(extinction * step, 0.0);
		const float3 stepTr = exp(-tau);
		const float3 factor = float3(
			tau.x < 1e-3 ? step * (1.0 - 0.5 * tau.x) : (1.0 - stepTr.x) / extinction.x,
			tau.y < 1e-3 ? step * (1.0 - 0.5 * tau.y) : (1.0 - stepTr.y) / extinction.y,
			tau.z < 1e-3 ? step * (1.0 - 0.5 * tau.z) : (1.0 - stepTr.z) / extinction.z);
		float3 transient = 0.0;
		if (SharedData::physSkyData.lightningColorIntensity.w > 0.0) {
			const float3 worldPosition = position + FrameBuffer::CameraPosAdjust.xyz;
			sh2 visibilitySH = Skylighting::UNIT_SH;
			if (ViewAtmosphere::Data[0].flags.z & 8u)
				visibilitySH = Skylighting::Sample(position, 0.0);
			[unroll] for (uint emitter = 0u; emitter < 2u; ++emitter)
			{
				const float3 direction = normalize(Lightning::Emitter(emitter).xyz - worldPosition);
				const float cosine = dot(normalize(position), direction);
				float visibility = SharedData::InInterior ? 0.0 : 1.0;
				if (ViewAtmosphere::Data[0].flags.z & 8u)
					visibility *= Skylighting::EvaluateDiffuse(visibilitySH, direction, Skylighting::GetFadeOutFactor(position));
				transient += Lightning::IncidentFrom(emitter, worldPosition) * visibility *
				             (rayleigh * PhysSky::Phase::Rayleigh(cosine) + mie * PhysSky::Phase::CornetteShanks(cosine, SharedData::physSkyData.aerosolPhaseG) + source.a * (1.0 / (4.0 * 3.14159265)));
			}
		}
		light += tr * (source.rgb + transient) * factor;
		tr *= stepTr;
		IntegratedLuminance[uint3(coord, layer + 1u) + grid.offset.xyz] = float4(min(light, 65504.0), 0);
		IntegratedTransmittance[uint3(coord, layer + 1u) + grid.offset.xyz] = float4(tr, 1);
	}
}
