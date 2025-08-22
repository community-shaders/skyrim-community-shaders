#pragma once

#include "Effect.h"
#include <d3d11.h>
#include <unordered_map>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class ENBBloom : public Effect
{
public:
	virtual std::string GetName() const override { return "enbbloom.fx"; }

	virtual void Execute() override;
	virtual void UpdateEffectVariables() override;
};