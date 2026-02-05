#pragma once

#include "PCH.h"

#include <d3d12.h>

#include "Features/Raytracing/Core/Model.h"
#include "Features/Raytracing/Core/Shape.h"

#include "Features/Raytracing/Pipelines/SkinningPipeline.h"

struct Instance
{
	enum State : uint8_t
	{
		Hidden = 1 << 0,
		Detached = 1 << 1
	};

	Instance(RE::FormID formID, eastl::string filename) :
		formID(formID), filename(filename) {};

	void SetDetached(bool detach);

	bool IsDetached() const;

	static uint UpdateInterval(float distance)
	{
		float t = std::log2((distance - 25.0f) + 1.0f) * 0.3f;
		return std::clamp(static_cast<uint>(t), 0u, 30u);
	}
	//

	bool SkipUpdate(RE::NiAVObject* node, const RE::NiPoint3& cameraPosition);

	// Checks for skinned and dynamic trishapes update
	void Update(RE::NiAVObject* node, const RE::NiPoint3& cameraPosition, const eastl::pair<eastl::string, Model*>& modelPair, SkinningPipeline* skinningPipeline);

	// Instance form id
	RE::FormID formID;

	// What model this instance references
	eastl::string filename;

	// Used for BLAS instance
	float3x4 transform;

	// Makes sure we only update once per frame
	uint64_t lastUpdate = 0;

private:
	bool detached = false;
};