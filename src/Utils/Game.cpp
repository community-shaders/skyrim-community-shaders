#include "Game.h"

#include "State.h"

namespace Util
{
	void StoreTransform3x4NoScale(DirectX::XMFLOAT3X4& Dest, const RE::NiTransform& Source)
	{
		//
		// Shove a Matrix3+Point3 directly into a float[3][4] with no modifications
		//
		// Dest[0][#] = Source.m_Rotate.m_pEntry[0][#];
		// Dest[0][3] = Source.m_Translate.x;
		// Dest[1][#] = Source.m_Rotate.m_pEntry[1][#];
		// Dest[1][3] = Source.m_Translate.x;
		// Dest[2][#] = Source.m_Rotate.m_pEntry[2][#];
		// Dest[2][3] = Source.m_Translate.x;
		//
		static_assert(sizeof(RE::NiTransform::rotate) == 3 * 3 * sizeof(float));  // NiMatrix3
		static_assert(sizeof(RE::NiTransform::translate) == 3 * sizeof(float));   // NiPoint3
		static_assert(offsetof(RE::NiTransform, translate) > offsetof(RE::NiTransform, rotate));

		_mm_store_ps(Dest.m[0], _mm_loadu_ps(Source.rotate.entry[0]));
		_mm_store_ps(Dest.m[1], _mm_loadu_ps(Source.rotate.entry[1]));
		_mm_store_ps(Dest.m[2], _mm_loadu_ps(Source.rotate.entry[2]));
		Dest.m[0][3] = Source.translate.x;
		Dest.m[1][3] = Source.translate.y;
		Dest.m[2][3] = Source.translate.z;
	}

	float4 TryGetWaterData(float offsetX, float offsetY)
	{
		if (globals::game::shadowState) {
			if (auto tes = globals::game::tes) {
				auto position = GetEyePosition(0);
				position.x += offsetX;
				position.y += offsetY;
				if (auto cell = tes->GetCell(position)) {
					float4 data = float4(1.0f, 1.0f, 1.0f, -FLT_MAX);

					bool extraCellWater = false;

					if (auto extraCellWaterType = cell->extraList.GetByType<RE::ExtraCellWaterType>()) {
						if (auto water = extraCellWaterType->water) {
							{
								data = { float(water->data.deepWaterColor.red) + float(water->data.shallowWaterColor.red),
									float(water->data.deepWaterColor.green) + float(water->data.shallowWaterColor.green),
									float(water->data.deepWaterColor.blue) + float(water->data.shallowWaterColor.blue) };

								data.x /= 255.0f;
								data.y /= 255.0f;
								data.z /= 255.0f;

								data.x *= 0.5;
								data.y *= 0.5;
								data.z *= 0.5;
								extraCellWater = true;
							}
						}
					}

					if (!extraCellWater) {
						if (auto worldSpace = tes->GetRuntimeData2().worldSpace) {
							if (auto water = worldSpace->worldWater) {
								data = { float(water->data.deepWaterColor.red) + float(water->data.shallowWaterColor.red),
									float(water->data.deepWaterColor.green) + float(water->data.shallowWaterColor.green),
									float(water->data.deepWaterColor.blue) + float(water->data.shallowWaterColor.blue) };

								data.x /= 255.0f;
								data.y /= 255.0f;
								data.z /= 255.0f;

								data.x *= 0.5;
								data.y *= 0.5;
								data.z *= 0.5;
							}
						}
					}

					if (auto sky = globals::game::sky) {
						const auto& color = sky->skyColor[RE::TESWeather::ColorTypes::kWaterMultiplier];
						data.x *= color.red;
						data.y *= color.green;
						data.z *= color.blue;
					}

					data.w = cell->GetExteriorWaterHeight() - position.z;

					return data;
				}
			}
		}
		return float4(1.0f, 1.0f, 1.0f, -FLT_MAX);
	}

	RE::NiPoint3 GetAverageEyePosition()
	{
		auto shadowState = globals::game::shadowState;
		if (!REL::Module::IsVR())
			return shadowState->GetRuntimeData().posAdjust.getEye();
		return (shadowState->GetVRRuntimeData().posAdjust.getEye(0) + shadowState->GetVRRuntimeData().posAdjust.getEye(1)) * 0.5f;
	}

	RE::NiPoint3 GetEyePosition(int eyeIndex)
	{
		auto shadowState = globals::game::shadowState;
		if (!REL::Module::IsVR())
			return shadowState->GetRuntimeData().posAdjust.getEye();
		return shadowState->GetVRRuntimeData().posAdjust.getEye(eyeIndex);
	}

	RE::BSGraphics::ViewData GetCameraData(int eyeIndex)
	{
		auto shadowState = globals::game::shadowState;
		if (!REL::Module::IsVR()) {
			return shadowState->GetRuntimeData().cameraData.getEye();
		}
		return shadowState->GetVRRuntimeData().cameraData.getEye(eyeIndex);
	}

	float4 GetCameraData()
	{
		static float& cameraNear = (*(float*)(REL::RelocationID(517032, 403540).address() + 0x40));
		static float& cameraFar = (*(float*)(REL::RelocationID(517032, 403540).address() + 0x44));

		float4 cameraData{};
		cameraData.x = cameraFar;
		cameraData.y = cameraNear;
		cameraData.z = cameraFar - cameraNear;
		cameraData.w = cameraFar * cameraNear;

		return cameraData;
	}

	bool GetTemporal()
	{
		auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
		return (!REL::Module::IsVR() ? imageSpaceManager->GetRuntimeData().BSImagespaceShaderISTemporalAA->taaEnabled : imageSpaceManager->GetVRRuntimeData().BSImagespaceShaderISTemporalAA->taaEnabled) || globals::state->upscalerLoaded;
	}

	// https://github.com/PureDark/Skyrim-Upscaler/blob/fa057bb088cf399e1112c1eaba714590c881e462/src/SkyrimUpscaler.cpp#L88
	float GetVerticalFOVRad()
	{
		static float& fac = (*(float*)(REL::RelocationID(513786, 388785).address()));
		const auto base = fac;
		const auto x = base / 1.30322540f;
		auto state = globals::state;
		const auto vFOV = 2 * atan(x / (state->screenSize.x / state->screenSize.y));
		return vFOV;
	}

	float2 ConvertToDynamic(float2 a_size)
	{
		auto viewport = globals::game::graphicsState;

		return float2(
			a_size.x * viewport->GetRuntimeData().dynamicResolutionWidthRatio,
			a_size.y * viewport->GetRuntimeData().dynamicResolutionHeightRatio);
	}

	DispatchCount GetScreenDispatchCount(bool a_dynamic)
	{
		float2 resolution = globals::state->screenSize;

		if (a_dynamic)
			ConvertToDynamic(resolution);

		uint dispatchX = (uint)std::ceil(resolution.x / 8.0f);
		uint dispatchY = (uint)std::ceil(resolution.y / 8.0f);

		return { dispatchX, dispatchY };
	}

	bool IsDynamicResolution()
	{
		const static auto address = REL::RelocationID{ 508794, 380760 }.address();
		bool* bDynamicResolution = reinterpret_cast<bool*>(address);
		return *bDynamicResolution;
	}

	bool FrameChecker::IsNewFrame()
	{
		return IsNewFrame(globals::state->frameCount);
	}
}  // namespace Util
