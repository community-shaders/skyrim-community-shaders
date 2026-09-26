#pragma once

#include "Utils/ColorSpace.h"

struct alignas(16) SharedDataCB
{
	float4 WaterData[25];
	float4 DirLightDirection;
	float4 DirLightColor;
	float4 SunDirection;
	float4 SunColor;
	float4 MasserDirection;
	float4 MasserColor;
	float4 SecundaDirection;
	float4 SecundaColor;
	float4 CameraData;
	float4 BufferDim;
	float Timer;
	uint FrameCount;
	uint FrameCountAlwaysActive;
	uint InInterior;
	uint HasDirectionalShadows;
	uint InMapMenu;
	uint HideSky;
	float MipBias;
	float WaterSystemHeight;  // TES::GetWaterHeight in camera-relative Z; -NI_INFINITY when no water body found
	uint PostWaterComposite;
	uint ResetHistory;
	float pad0;
	float4 AmbientSHR;
	float4 AmbientSHG;
	float4 AmbientSHB;
	float4 HDRData;  // xyz + menu scene encoding in w — see HDRDisplay::GetSharedDataHDR
};
STATIC_ASSERT_ALIGNAS_16(SharedDataCB);

struct SharedLighting
{
	const RE::NiLight* directionalLight = nullptr;
	Util::ColorSpace::LightColor directional;
	Util::ColorSpace::LightColor sun;
	Util::ColorSpace::LightColor masser;
	Util::ColorSpace::LightColor secunda;
};
