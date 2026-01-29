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

	static uint UpdateRate(float distance)
	{
		if (distance < 25)
			return 0;

		if (distance < 100)
			return 1;

		if (distance < 250)
			return 2;

		if (distance < 500)
			return 3;

		return 4;
	}

	bool ShouldUpdate(RE::NiAVObject* node, RE::NiPoint3 cameraPosition);

	// Checks for skinned and dynamic trishapes update
	void Update(RE::NiAVObject* node, RE::NiPoint3 cameraPosition, const eastl::pair<eastl::string, Model*>& modelPair, SkinningPipeline* skinningPipeline);

	// Instance form id
	RE::FormID formID;

	// What model this instance references
	eastl::string filename;

	// Used for BLAS instance
	float3x4 transform;

	// Makes sure we only update once per frame
	uint lastUpdate = 0;

private:
	bool detached = false;
};