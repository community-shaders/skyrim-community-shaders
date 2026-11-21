#ifndef __SNOW_COLOR_HLSLI__
#define __SNOW_COLOR_HLSLI__

#include "Common/ColorManagement.hlsli"
#include "Common/SharedData.hlsli"

#if defined(PSHADER) || defined(CSHADER) || defined(COMPUTESHADER)
namespace SnowColor
{
	float3 SetAlbedo(float3 texel, bool isLinear)
	{
		return isLinear ? Color::LinearToPBRMaterial(ColorManagement::DiffuseToWorking(texel, true)) : ColorManagement::DiffuseToWorking(texel);
	}
}
#endif

#endif
