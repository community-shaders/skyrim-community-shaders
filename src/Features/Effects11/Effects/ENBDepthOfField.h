#pragma once

#include "ExtendedEffect.h"

class ENBDepthOfField : public EffectBase
{
public:
	virtual std::string GetName() const override { return "enbdepthoffield.fx"; }

	/** @brief Loads the effect, resets focus history and caches the image technique used when no UIName technique exists. */
	virtual bool Apply() override;
	virtual void Execute() override;
	virtual void UpdateEffectVariables() override;

	/** @brief This frame's aperture texture for other effects, or null if DOF did not run this frame. */
	ID3D11ShaderResourceView* GetApertureSRV() const;

protected:
	void CreateEffectTextures() override;

private:
	uint32_t idApertureTime = 0xFFFFFFFF;
	uint32_t idFocusingTime = 0xFFFFFFFF;
	uint32_t idEnableAdaptation = 0xFFFFFFFF;
	bool idsCached = false;
	bool historyValid = false;
	std::string fallbackTechnique;

	ID3D11ShaderResourceView* apertureSRV = nullptr;
	uint32_t apertureFrame = UINT32_MAX;
};
