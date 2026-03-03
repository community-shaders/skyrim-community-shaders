#include "PrecipitationWidget.h"
#include "../EditorWindow.h"
#include "../WeatherUtils.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	PrecipitationWidget::Settings,
	gravityVelocity,
	rotationVelocity,
	particleSizeX,
	particleSizeY,
	centerOffsetMin,
	centerOffsetMax,
	startRotationRange,
	numSubtexturesX,
	numSubtexturesY,
	particleType,
	boxSize,
	particleDensity,
	particleTexture)

void PrecipitationWidget::DrawWidget()
{
	WeatherUtils::SetCurrentWidget(this);
	ImGui::SetNextWindowSizeConstraints(ImVec2(600, 0), ImVec2(FLT_MAX, FLT_MAX));
	if (!ImGui::Begin(GetEditorID().c_str(), &open, ImGuiWindowFlags_NoSavedSettings | kStickyHeaderFlags)) {
		ImGui::End();
		return;
	}
	DrawWidgetHeader("##PrecipitationSearch", true, true);

	bool changed = false;

	if (ImGui::BeginTabBar("PrecipitationTabs")) {
		if (ImGui::BeginTabItem("Particle")) {
			BeginScrollableContent("##ParticleScroll");
			ImGui::SeparatorText("Particle Type");
			static constexpr const char* kParticleTypes[] = { "Rain", "Snow" };
			int currentType = static_cast<int>(settings.particleType);
			if (ImGui::Combo("Type", &currentType, kParticleTypes, IM_ARRAYSIZE(kParticleTypes))) {
				settings.particleType = static_cast<uint32_t>(currentType);
				changed = true;
			}

			ImGui::SeparatorText("Particle Size");
			if (WeatherUtils::DrawSliderFloat("Size X", settings.particleSizeX, 0.0f, 10.0f))
				changed = true;
			if (WeatherUtils::DrawSliderFloat("Size Y", settings.particleSizeY, 0.0f, 10.0f))
				changed = true;

			ImGui::SeparatorText("Velocity");
			if (WeatherUtils::DrawSliderFloat("Gravity Velocity", settings.gravityVelocity, -100.0f, 100.0f))
				changed = true;
			if (WeatherUtils::DrawSliderFloat("Rotation Velocity", settings.rotationVelocity, -360.0f, 360.0f))
				changed = true;

			EndScrollableContent();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Position")) {
			BeginScrollableContent("##PositionScroll");
			ImGui::SeparatorText("Offset");
			if (WeatherUtils::DrawSliderFloat("Center Offset Min", settings.centerOffsetMin, -1000.0f, 1000.0f))
				changed = true;
			if (WeatherUtils::DrawSliderFloat("Center Offset Max", settings.centerOffsetMax, -1000.0f, 1000.0f))
				changed = true;
			if (WeatherUtils::DrawSliderFloat("Start Rotation Range", settings.startRotationRange, 0.0f, 360.0f))
				changed = true;

			ImGui::SeparatorText("Volume");
			if (WeatherUtils::DrawSliderFloat("Box Size", settings.boxSize, 0.0f, 10000.0f))
				changed = true;
			if (WeatherUtils::DrawSliderFloat("Particle Density", settings.particleDensity, 0.0f, 10.0f))
				changed = true;

			EndScrollableContent();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Texture")) {
			BeginScrollableContent("##TextureScroll");
			ImGui::SeparatorText("Subtextures");
			int numX = static_cast<int>(settings.numSubtexturesX);
			int numY = static_cast<int>(settings.numSubtexturesY);
			if (ImGui::InputInt("Num Subtextures X", &numX)) {
				settings.numSubtexturesX = std::max(1, numX);
				changed = true;
			}
			if (ImGui::InputInt("Num Subtextures Y", &numY)) {
				settings.numSubtexturesY = std::max(1, numY);
				changed = true;
			}

			ImGui::SeparatorText("Texture Path");
			if (ImGui::InputText("Particle Texture", textureBuffer, sizeof(textureBuffer))) {
				settings.particleTexture = textureBuffer;
				changed = true;
			}

			EndScrollableContent();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges) {
		ApplyChanges();
	}
	ImGui::End();
}

void PrecipitationWidget::LoadSettings()
{
	if (!precipitation)
		return;

	try {
		if (!js.empty()) {
			settings = js;
		} else {
			settings = vanillaSettings;
		}
	} catch (const std::exception& e) {
		logger::error("Precipitation {}: Failed to load from JSON: {}", GetEditorID(), e.what());
		settings = vanillaSettings;
	}

	originalSettings = settings;
	strncpy_s(textureBuffer, sizeof(textureBuffer), settings.particleTexture.c_str(), _TRUNCATE);
	ApplyChanges();
}

void PrecipitationWidget::LoadFromGameSettings()
{
	if (!precipitation)
		return;

	using DataID = RE::BGSShaderParticleGeometryData::DataID;
	auto& runtime = precipitation->GetRuntimeData();

	settings.gravityVelocity = precipitation->GetSettingValue(DataID::kGravityVelocity).f;
	settings.rotationVelocity = precipitation->GetSettingValue(DataID::kRotationVelocity).f;
	settings.particleSizeX = precipitation->GetSettingValue(DataID::kParticleSizeX).f;
	settings.particleSizeY = precipitation->GetSettingValue(DataID::kParticleSizeY).f;
	settings.centerOffsetMin = precipitation->GetSettingValue(DataID::kCenterOffsetMin).f;
	settings.centerOffsetMax = precipitation->GetSettingValue(DataID::kCenterOffsetMax).f;
	settings.startRotationRange = precipitation->GetSettingValue(DataID::kStartRotationRange).f;
	settings.numSubtexturesX = precipitation->GetSettingValue(DataID::kNumSubtexturesX).i;
	settings.numSubtexturesY = precipitation->GetSettingValue(DataID::kNumSubtexturesY).i;
	settings.particleType = precipitation->GetSettingValue(DataID::kParticleType).i;
	settings.boxSize = precipitation->GetSettingValue(DataID::kBoxSize).f;
	settings.particleDensity = precipitation->GetSettingValue(DataID::kParticleDensity).f;
	settings.particleTexture = runtime.particleTexture.textureName.c_str();
}

void PrecipitationWidget::SaveSettings()
{
	js = settings;
	originalSettings = settings;
}

void PrecipitationWidget::ApplyChanges()
{
	if (!precipitation)
		return;

	using DataID = RE::BGSShaderParticleGeometryData::DataID;
	auto& runtime = precipitation->GetRuntimeData();

	runtime.data[static_cast<uint32_t>(DataID::kGravityVelocity)].f = settings.gravityVelocity;
	runtime.data[static_cast<uint32_t>(DataID::kRotationVelocity)].f = settings.rotationVelocity;
	runtime.data[static_cast<uint32_t>(DataID::kParticleSizeX)].f = settings.particleSizeX;
	runtime.data[static_cast<uint32_t>(DataID::kParticleSizeY)].f = settings.particleSizeY;
	runtime.data[static_cast<uint32_t>(DataID::kCenterOffsetMin)].f = settings.centerOffsetMin;
	runtime.data[static_cast<uint32_t>(DataID::kCenterOffsetMax)].f = settings.centerOffsetMax;
	runtime.data[static_cast<uint32_t>(DataID::kStartRotationRange)].f = settings.startRotationRange;
	runtime.data[static_cast<uint32_t>(DataID::kNumSubtexturesX)].i = settings.numSubtexturesX;
	runtime.data[static_cast<uint32_t>(DataID::kNumSubtexturesY)].i = settings.numSubtexturesY;
	runtime.data[static_cast<uint32_t>(DataID::kParticleType)].i = settings.particleType;
	runtime.data[static_cast<uint32_t>(DataID::kBoxSize)].f = settings.boxSize;
	runtime.data[static_cast<uint32_t>(DataID::kParticleDensity)].f = settings.particleDensity;
	runtime.particleTexture.textureName = settings.particleTexture.c_str();
}

void PrecipitationWidget::RevertChanges()
{
	settings = vanillaSettings;
	strncpy_s(textureBuffer, sizeof(textureBuffer), settings.particleTexture.c_str(), _TRUNCATE);
	ApplyChanges();
}

bool PrecipitationWidget::HasUnsavedChanges() const
{
	return !(settings == originalSettings);
}
