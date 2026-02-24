#include "PrecipitationWidget.h"
#include "../EditorWindow.h"
#include "../WeatherUtils.h"

void PrecipitationWidget::DrawWidget()
{
	WeatherUtils::SetCurrentWidget(this);
	ImGui::SetNextWindowSizeConstraints(ImVec2(600, 0), ImVec2(FLT_MAX, FLT_MAX));
	if (ImGui::Begin(GetEditorID().c_str(), &open, ImGuiWindowFlags_NoSavedSettings | kStickyHeaderFlags)) {
		DrawWidgetHeader("##PrecipitationSearch", true, true);

		bool changed = false;

		if (ImGui::BeginTabBar("PrecipitationTabs")) {
			if (ImGui::BeginTabItem("Particle")) {
				BeginScrollableContent("##ParticleScroll");
				ImGui::SeparatorText("Particle Type");
				const char* types[] = { "Rain", "Snow" };
				int currentType = static_cast<int>(settings.particleType);
				if (ImGui::Combo("Type", &currentType, types, IM_ARRAYSIZE(types))) {
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
	}
	ImGui::End();
}

void PrecipitationWidget::LoadSettings()
{
	if (!precipitation)
		return;

	if (!js.empty()) {
		settings = vanillaSettings;
		try {
			if (js.contains("gravityVelocity"))
				settings.gravityVelocity = js["gravityVelocity"];
			if (js.contains("rotationVelocity"))
				settings.rotationVelocity = js["rotationVelocity"];
			if (js.contains("particleSizeX"))
				settings.particleSizeX = js["particleSizeX"];
			if (js.contains("particleSizeY"))
				settings.particleSizeY = js["particleSizeY"];
			if (js.contains("centerOffsetMin"))
				settings.centerOffsetMin = js["centerOffsetMin"];
			if (js.contains("centerOffsetMax"))
				settings.centerOffsetMax = js["centerOffsetMax"];
			if (js.contains("startRotationRange"))
				settings.startRotationRange = js["startRotationRange"];
			if (js.contains("numSubtexturesX"))
				settings.numSubtexturesX = js["numSubtexturesX"];
			if (js.contains("numSubtexturesY"))
				settings.numSubtexturesY = js["numSubtexturesY"];
			if (js.contains("particleType"))
				settings.particleType = js["particleType"];
			if (js.contains("boxSize"))
				settings.boxSize = js["boxSize"];
			if (js.contains("particleDensity"))
				settings.particleDensity = js["particleDensity"];
			if (js.contains("particleTexture"))
				settings.particleTexture = js["particleTexture"].get<std::string>();
		} catch (const std::exception& e) {
			logger::error("Precipitation {}: Failed to load from JSON: {}", GetEditorID(), e.what());
			settings = vanillaSettings;
		}
	} else {
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

	auto& runtime = precipitation->GetRuntimeData();

	settings.gravityVelocity = precipitation->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kGravityVelocity).f;
	settings.rotationVelocity = precipitation->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kRotationVelocity).f;
	settings.particleSizeX = precipitation->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kParticleSizeX).f;
	settings.particleSizeY = precipitation->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kParticleSizeY).f;
	settings.centerOffsetMin = precipitation->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kCenterOffsetMin).f;
	settings.centerOffsetMax = precipitation->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kCenterOffsetMax).f;
	settings.startRotationRange = precipitation->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kStartRotationRange).f;
	settings.numSubtexturesX = precipitation->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kNumSubtexturesX).i;
	settings.numSubtexturesY = precipitation->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kNumSubtexturesY).i;
	settings.particleType = precipitation->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kParticleType).i;
	settings.boxSize = precipitation->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kBoxSize).f;
	settings.particleDensity = precipitation->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kParticleDensity).f;
	settings.particleTexture = runtime.particleTexture.textureName.c_str();
}

void PrecipitationWidget::SaveSettings()
{
	js["gravityVelocity"] = settings.gravityVelocity;
	js["rotationVelocity"] = settings.rotationVelocity;
	js["particleSizeX"] = settings.particleSizeX;
	js["particleSizeY"] = settings.particleSizeY;
	js["centerOffsetMin"] = settings.centerOffsetMin;
	js["centerOffsetMax"] = settings.centerOffsetMax;
	js["startRotationRange"] = settings.startRotationRange;
	js["numSubtexturesX"] = settings.numSubtexturesX;
	js["numSubtexturesY"] = settings.numSubtexturesY;
	js["particleType"] = settings.particleType;
	js["boxSize"] = settings.boxSize;
	js["particleDensity"] = settings.particleDensity;
	js["particleTexture"] = settings.particleTexture;
	originalSettings = settings;
}

void PrecipitationWidget::ApplyChanges()
{
	if (!precipitation)
		return;

	auto& runtime = precipitation->GetRuntimeData();

	runtime.data[(uint32_t)RE::BGSShaderParticleGeometryData::DataID::kGravityVelocity].f = settings.gravityVelocity;
	runtime.data[(uint32_t)RE::BGSShaderParticleGeometryData::DataID::kRotationVelocity].f = settings.rotationVelocity;
	runtime.data[(uint32_t)RE::BGSShaderParticleGeometryData::DataID::kParticleSizeX].f = settings.particleSizeX;
	runtime.data[(uint32_t)RE::BGSShaderParticleGeometryData::DataID::kParticleSizeY].f = settings.particleSizeY;
	runtime.data[(uint32_t)RE::BGSShaderParticleGeometryData::DataID::kCenterOffsetMin].f = settings.centerOffsetMin;
	runtime.data[(uint32_t)RE::BGSShaderParticleGeometryData::DataID::kCenterOffsetMax].f = settings.centerOffsetMax;
	runtime.data[(uint32_t)RE::BGSShaderParticleGeometryData::DataID::kStartRotationRange].f = settings.startRotationRange;
	runtime.data[(uint32_t)RE::BGSShaderParticleGeometryData::DataID::kNumSubtexturesX].i = settings.numSubtexturesX;
	runtime.data[(uint32_t)RE::BGSShaderParticleGeometryData::DataID::kNumSubtexturesY].i = settings.numSubtexturesY;
	runtime.data[(uint32_t)RE::BGSShaderParticleGeometryData::DataID::kParticleType].i = settings.particleType;
	runtime.data[(uint32_t)RE::BGSShaderParticleGeometryData::DataID::kBoxSize].f = settings.boxSize;
	runtime.data[(uint32_t)RE::BGSShaderParticleGeometryData::DataID::kParticleDensity].f = settings.particleDensity;
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
