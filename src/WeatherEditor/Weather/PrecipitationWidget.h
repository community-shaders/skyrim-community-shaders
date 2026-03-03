#pragma once

#include "../Widget.h"

class PrecipitationWidget : public Widget
{
public:
	PrecipitationWidget(RE::BGSShaderParticleGeometryData* a_precipitation) :
		precipitation(a_precipitation)
	{
		form = a_precipitation;
		if (precipitation) {
			LoadFromGameSettings();
			vanillaSettings = settings;
			originalSettings = settings;
			strncpy_s(textureBuffer, sizeof(textureBuffer), settings.particleTexture.c_str(), _TRUNCATE);
		}
	}

	~PrecipitationWidget() override = default;

	void DrawWidget() override;
	void LoadSettings() override;
	void SaveSettings() override;
	void ApplyChanges() override;
	void RevertChanges() override;
	bool HasUnsavedChanges() const override;

	// Public type required by NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT macro
	struct Settings
	{
		float gravityVelocity = 0.0f;
		float rotationVelocity = 0.0f;
		float particleSizeX = 1.0f;
		float particleSizeY = 1.0f;
		float centerOffsetMin = 0.0f;
		float centerOffsetMax = 0.0f;
		float startRotationRange = 0.0f;
		uint32_t numSubtexturesX = 1;
		uint32_t numSubtexturesY = 1;
		uint32_t particleType = 0;  // 0 = Rain, 1 = Snow
		float boxSize = 1.0f;
		float particleDensity = 1.0f;
		std::string particleTexture = "";
		bool operator==(const Settings&) const = default;
	};

private:
	void LoadFromGameSettings();

	RE::BGSShaderParticleGeometryData* precipitation = nullptr;

	Settings settings;
	Settings vanillaSettings;
	Settings originalSettings;
	char textureBuffer[256] = {};
};
