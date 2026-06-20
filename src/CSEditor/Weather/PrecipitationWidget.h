#pragma once

#include "../Widget.h"

/**
 * @brief Widget for editing precipitation particle geometry data (rain/snow).
 *
 * Covers particle size, velocity, density, subtexture layout, and the
 * particle texture path. Requires manual apply because texture swaps
 * trigger a weather reinit.
 */
class PrecipitationWidget : public Widget
{
public:
	/**
	 * @brief Constructs a precipitation widget for the given particle data form.
	 * @param a_precipitation The BGSShaderParticleGeometryData form to edit.
	 */
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

	/** @brief Renders the precipitation editor UI with Particle, Position, and Texture tabs. */
	void DrawWidget() override;

	/** @brief Returns the human-readable widget type name for window sizing. */
	const char* GetWidgetTypeName() const override { return "Precipitation"; }

	/** @brief Returns true because texture changes require an explicit apply and weather reinit. */
	bool RequiresManualApply() const override { return true; }

	/** @brief Deserializes precipitation settings from the stored JSON blob. */
	void LoadSettings() override;

	/** @brief Serializes current precipitation settings to the JSON blob. */
	void SaveSettings() override;

	/** @brief Writes the current settings into the game's particle data and reloads the live texture. */
	void ApplyChanges() override;

	/** @brief Reverts settings to vanilla values, clears texture caches, and re-applies. */
	void RevertChanges() override;

	/** @brief Returns true if the current settings differ from the last saved state. */
	bool HasUnsavedChanges() const override;

	/** @brief Collects all searchable precipitation settings for the search dropdown. */
	std::vector<SearchResult> CollectSearchableSettings() const override;

	RE::BGSShaderParticleGeometryData* precipitation = nullptr;

private:
	void LoadFromGameSettings();

	// Swaps the live precipitation particle texture (Sky → precip → BSParticleShaderProperty::particleShaderTexture).
	// Needed because updating BGSShaderParticleGeometryData::particleTexture.textureName alone doesn't reload the GPU texture.
	void ApplyLiveParticleTexture(const std::string& path);

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

	Settings settings;
	Settings vanillaSettings;
	Settings originalSettings;
	char textureBuffer[256] = {};
	std::string lastAppliedTexture;
	std::string lastInvalidTexture;
	RE::NiPointer<RE::BSGeometry> lastAppliedPrecip;
	std::string lastCheckedBuffer;
	bool lastCheckedExists = false;
};
