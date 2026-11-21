#pragma once

#include "EngineFix.h"

struct WaterReflectionCubemapFix : EngineFix
{
	std::string GetName() override { return "Water Reflection Cubemap Fix"; }

	void Install() override;

private:
	static void ResetCubemapOnSceneChange();
	/** @brief Renders the reflection cubemap on sky-lit exterior frames where no water reflection rendered it. */
	static void RenderExteriorCubemap();

	/** @brief CS's own dynamic reflection, built like an exterior water's; it lives for the session. */
	static inline RE::TESWaterReflections* exteriorReflections = nullptr;
	/** @brief Frame in which a reflection cubemap face last rendered. */
	static inline uint lastCubemapFrame = UINT32_MAX;

	struct Main_RenderWaterEffects
	{
		static void thunk();
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSWaterShader_SetupGeometry
	{
		static void thunk(RE::BSShader* shader, RE::BSRenderPass* pass, uint32_t renderFlags);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSCubeMapCamera_RenderCubemap
	{
		static void thunk(RE::NiAVObject* camera, int faces, bool a3, bool a4, bool a5);
		static inline REL::Relocation<decltype(thunk)> func;
	};
};
