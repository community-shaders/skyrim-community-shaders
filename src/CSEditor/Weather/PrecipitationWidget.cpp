#include "PrecipitationWidget.h"
#include "../../I18n/I18n.h"
#include "../EditorWindow.h"
#include "../WeatherUtils.h"
#include "Globals.h"
#include "RE/B/BSShaderManager.h"
#include "RE/N/NiSourceTexture.h"
#include "Utils/Game.h"

#include <format>

#define I18N_KEY_PREFIX "cs_editor."

namespace
{
	namespace PrecipitationTab
	{
		constexpr const char* kParticle = "Particle";
		constexpr const char* kPosition = "Position";
		constexpr const char* kTexture = "Texture";
	}

	namespace PrecipitationSetting
	{
		constexpr const char* kType = "Type";
		constexpr const char* kSizeX = "Size X";
		constexpr const char* kSizeY = "Size Y";
		constexpr const char* kGravityVelocity = "Gravity Velocity";
		constexpr const char* kRotationVelocity = "Rotation Velocity";
		constexpr const char* kCenterOffsetMin = "Center Offset Min";
		constexpr const char* kCenterOffsetMax = "Center Offset Max";
		constexpr const char* kStartRotationRange = "Start Rotation Range";
		constexpr const char* kBoxSize = "Box Size";
		constexpr const char* kParticleDensity = "Particle Density";
		constexpr const char* kNumSubtexturesX = "Num Subtextures X";
		constexpr const char* kNumSubtexturesY = "Num Subtextures Y";
		constexpr const char* kParticleTexture = "Particle Texture";
	}
}

void PrecipitationWidget::DrawWidget()
{
	WeatherUtils::SetCurrentWidget(this);
	if (BeginWidgetWindow()) {
		DrawWidgetHeader("##PrecipitationSearch", true, true);
		DrawSearchDropdown();

		bool changed = false;

		if (ImGui::BeginTabBar("PrecipitationTabs")) {
			const ImGuiTabItemFlags particleFlags = GetTabFlagsForOverride(PrecipitationTab::kParticle);
			const ImGuiTabItemFlags positionFlags = GetTabFlagsForOverride(PrecipitationTab::kPosition);
			const ImGuiTabItemFlags textureFlags = GetTabFlagsForOverride(PrecipitationTab::kTexture);

			if (ImGui::BeginTabItem(T(TKEY("tab_particle"), "Particle"), nullptr, particleFlags)) {
				BeginScrollableContent("##ParticleScroll");
				if (DrawIfMatchesSearch(PrecipitationSetting::kType, [&](const char* label) {
						ImGui::SeparatorText(T(TKEY("particle_type"), "Particle Type"));
						const char* types[] = { T(TKEY("rain"), "Rain"), T(TKEY("snow"), "Snow") };
						int currentType = static_cast<int>(settings.particleType);
						bool comboChanged = DrawWithHighlight(label, [&]() {
							return ImGui::Combo(std::format("{}##{}", T(TKEY("type"), "Type"), PrecipitationSetting::kType).c_str(), &currentType, types, IM_ARRAYSIZE(types));
						});
						if (comboChanged) {
							settings.particleType = static_cast<uint32_t>(currentType);
							return true;
						}
						return false;
					}))
					changed = true;
				if (MatchesAnySearch({ PrecipitationSetting::kSizeX, PrecipitationSetting::kSizeY })) {
					ImGui::SeparatorText(T(TKEY("particle_size"), "Particle Size"));
					changed |= WeatherUtils::DrawSliderFloat(PrecipitationSetting::kSizeX, settings.particleSizeX, 0.0f, 200.0f);
					changed |= WeatherUtils::DrawSliderFloat(PrecipitationSetting::kSizeY, settings.particleSizeY, 0.0f, 200.0f);
				}
				if (MatchesAnySearch({ PrecipitationSetting::kGravityVelocity, PrecipitationSetting::kRotationVelocity })) {
					ImGui::SeparatorText(T(TKEY("velocity"), "Velocity"));
					changed |= WeatherUtils::DrawSliderFloat(PrecipitationSetting::kGravityVelocity, settings.gravityVelocity, 0.0f, 10000.0f);
					changed |= WeatherUtils::DrawSliderFloat(PrecipitationSetting::kRotationVelocity, settings.rotationVelocity, 0.0f, 10000.0f);
				}
				EndScrollableContent();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem(T(TKEY("tab_position"), "Position"), nullptr, positionFlags)) {
				BeginScrollableContent("##PositionScroll");
				if (MatchesAnySearch({ PrecipitationSetting::kCenterOffsetMin, PrecipitationSetting::kCenterOffsetMax, PrecipitationSetting::kStartRotationRange })) {
					ImGui::SeparatorText(T(TKEY("offset"), "Offset"));
					changed |= WeatherUtils::DrawSliderFloat(PrecipitationSetting::kCenterOffsetMin, settings.centerOffsetMin, 0.0f, 200.0f);
					changed |= WeatherUtils::DrawSliderFloat(PrecipitationSetting::kCenterOffsetMax, settings.centerOffsetMax, 0.0f, 200.0f);
					changed |= WeatherUtils::DrawSliderFloat(PrecipitationSetting::kStartRotationRange, settings.startRotationRange, 0.0f, 360.0f);
				}
				if (MatchesAnySearch({ PrecipitationSetting::kBoxSize, PrecipitationSetting::kParticleDensity })) {
					ImGui::SeparatorText(T(TKEY("volume"), "Volume"));
					changed |= WeatherUtils::DrawSliderFloat(PrecipitationSetting::kBoxSize, settings.boxSize, 0.0f, 1000.0f);
					changed |= WeatherUtils::DrawSliderFloat(PrecipitationSetting::kParticleDensity, settings.particleDensity, 0.0f, 1000.0f);
				}
				EndScrollableContent();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem(T(TKEY("tab_texture"), "Texture"), nullptr, textureFlags)) {
				BeginScrollableContent("##TextureScroll");
				if (MatchesAnySearch({ PrecipitationSetting::kNumSubtexturesX, PrecipitationSetting::kNumSubtexturesY })) {
					ImGui::SeparatorText(T(TKEY("subtextures"), "Subtextures"));
					int numX = static_cast<int>(settings.numSubtexturesX);
					int numY = static_cast<int>(settings.numSubtexturesY);
					if (DrawIfMatchesSearch(PrecipitationSetting::kNumSubtexturesX, [&](const char* label) {
							return DrawWithHighlight(label, [&]() {
								return ImGui::InputInt(std::format("{}##{}", T(TKEY("num_subtextures_x"), "Num Subtextures X"), PrecipitationSetting::kNumSubtexturesX).c_str(), &numX);
							});
						})) {
						settings.numSubtexturesX = std::max(1, numX);
						changed = true;
					}
					if (DrawIfMatchesSearch(PrecipitationSetting::kNumSubtexturesY, [&](const char* label) {
							return DrawWithHighlight(label, [&]() {
								return ImGui::InputInt(std::format("{}##{}", T(TKEY("num_subtextures_y"), "Num Subtextures Y"), PrecipitationSetting::kNumSubtexturesY).c_str(), &numY);
							});
						})) {
						settings.numSubtexturesY = std::max(1, numY);
						changed = true;
					}
				}
				DrawSearchSectionIfMatches(PrecipitationSetting::kParticleTexture, [&](const char* label) {
					ImGui::SeparatorText(T(TKEY("texture_path"), "Texture Path"));
					const bool inputChanged = DrawWithHighlight(label, [&]() {
						return ImGui::InputText(std::format("{}##{}", T(TKEY("particle_texture_label"), "Particle Texture"), PrecipitationSetting::kParticleTexture).c_str(), textureBuffer, sizeof(textureBuffer));
					});
					std::string_view buf(textureBuffer);
					if (buf != lastCheckedBuffer) {
						lastCheckedBuffer = std::string(buf);
						lastCheckedExists = WeatherUtils::TexturePath::ExistsOnDisk(buf);
					}
					if (inputChanged && WeatherUtils::TexturePath::HasDdsExtension(buf) && lastCheckedExists) {
						settings.particleTexture = lastCheckedBuffer;
						changed = true;
					}
					if (settings.particleTexture != buf && !buf.empty()) {
						if (!WeatherUtils::TexturePath::HasDdsExtension(buf))
							ImGui::TextColored(globals::menu->GetTheme().StatusPalette.Error, "%s", T(TKEY("path_must_end_dds"), "Path must end with '.dds'"));
						else if (!lastCheckedExists)
							ImGui::TextColored(globals::menu->GetTheme().StatusPalette.Error, "%s", T(TKEY("texture_file_not_found"), "Texture file not found under Data/textures/."));
					}
				});

				EndScrollableContent();
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}

		if (changed && EditorWindow::GetSingleton()->settings.autoApplyChanges)
			ApplyChanges();
	}
	ImGui::End();
}

#undef I18N_KEY_PREFIX

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
			if (js.contains("particleTexture")) {
				if (!js["particleTexture"].is_string()) {
					logger::warn("Precipitation {}: particleTexture is not a string, skipping", GetEditorID());
				} else {
					auto texPath = js["particleTexture"].get<std::string>();
					if (!WeatherUtils::TexturePath::HasDdsExtension(texPath)) {
						logger::warn("Precipitation {}: ignoring malformed texture path '{}'", GetEditorID(), texPath);
					} else {
						settings.particleTexture = texPath;
						if (!WeatherUtils::TexturePath::ExistsOnDisk(texPath))
							logger::warn("Precipitation {}: saved texture path '{}' not found on disk", GetEditorID(), texPath);
					}
				}
			}
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
	auto& particleTexture = precipitation->GetRuntimeData().particleTexture;
	settings.particleTexture = particleTexture.textureName.c_str();
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

	using DataID = RE::BGSShaderParticleGeometryData::DataID;

	precipitation->GetSettingRef(DataID::kGravityVelocity).f = settings.gravityVelocity;
	precipitation->GetSettingRef(DataID::kRotationVelocity).f = settings.rotationVelocity;
	precipitation->GetSettingRef(DataID::kParticleSizeX).f = settings.particleSizeX;
	precipitation->GetSettingRef(DataID::kParticleSizeY).f = settings.particleSizeY;
	precipitation->GetSettingRef(DataID::kCenterOffsetMin).f = settings.centerOffsetMin;
	precipitation->GetSettingRef(DataID::kCenterOffsetMax).f = settings.centerOffsetMax;
	precipitation->GetSettingRef(DataID::kStartRotationRange).f = settings.startRotationRange;
	precipitation->GetSettingRef(DataID::kNumSubtexturesX).i = settings.numSubtexturesX;
	precipitation->GetSettingRef(DataID::kNumSubtexturesY).i = settings.numSubtexturesY;
	precipitation->GetSettingRef(DataID::kParticleType).i = settings.particleType;
	precipitation->GetSettingRef(DataID::kBoxSize).f = settings.boxSize;
	precipitation->GetSettingRef(DataID::kParticleDensity).f = settings.particleDensity;
	auto& particleTexture = precipitation->GetRuntimeData().particleTexture;
	particleTexture.textureName = settings.particleTexture.c_str();
	ApplyLiveParticleTexture(settings.particleTexture);
	Widget::ForceCurrentWeatherReinit();
}

void PrecipitationWidget::ApplyLiveParticleTexture(const std::string& path)
{
	if (path.empty())
		return;
	if (!WeatherUtils::TexturePath::ExistsOnDisk(path)) {
		if (path != lastInvalidTexture) {
			logger::warn("Precipitation {}: invalid texture path '{}', must end with '.dds'", GetEditorID(), path);
			lastInvalidTexture = path;
		}
		return;
	}

	auto* sky = globals::game::sky;
	if (!sky || !sky->precip)
		return;

	if (path == lastAppliedTexture &&
		sky->precip->currentPrecip == lastAppliedPrecip &&
		sky->precip->lastPrecip == lastAppliedPrecip)
		return;

	RE::NiPointer<RE::NiTexture> tex;
	RE::BSShaderManager::GetTexture(path.c_str(), true, tex, false);
	if (!tex || tex->GetRTTI() != globals::rtti::NiSourceTextureRTTI.get())
		return;

	auto* sourceTex = static_cast<RE::NiSourceTexture*>(tex.get());
	if (!sourceTex->rendererTexture || !sourceTex->rendererTexture->texture)
		return;

	RE::BSGeometry* precipObjects[] = { sky->precip->currentPrecip.get(), sky->precip->lastPrecip.get() };
	for (auto* precipObject : precipObjects) {
		if (!precipObject)
			continue;
		if (auto* shaderProp = netimmerse_cast<RE::BSParticleShaderProperty*>(precipObject->GetGeometryRuntimeData().shaderProperty.get()))
			shaderProp->particleShaderTexture = RE::NiPointer(sourceTex);
	}

	lastAppliedTexture = path;
	lastAppliedPrecip = sky->precip->currentPrecip;
}

void PrecipitationWidget::RevertChanges()
{
	settings = vanillaSettings;
	strncpy_s(textureBuffer, sizeof(textureBuffer), settings.particleTexture.c_str(), _TRUNCATE);
	lastAppliedTexture.clear();
	lastAppliedPrecip.reset();
	lastInvalidTexture.clear();
	lastCheckedBuffer.clear();
	lastCheckedExists = false;
	ApplyChanges();
}

bool PrecipitationWidget::HasUnsavedChanges() const
{
	return !(settings == originalSettings);
}

std::vector<Widget::SearchResult> PrecipitationWidget::CollectSearchableSettings() const
{
	const std::vector<std::pair<std::string, std::vector<std::string>>> entries = {
		{ PrecipitationTab::kParticle, { PrecipitationSetting::kType, PrecipitationSetting::kSizeX, PrecipitationSetting::kSizeY, PrecipitationSetting::kGravityVelocity, PrecipitationSetting::kRotationVelocity } },
		{ PrecipitationTab::kPosition, { PrecipitationSetting::kCenterOffsetMin, PrecipitationSetting::kCenterOffsetMax, PrecipitationSetting::kStartRotationRange, PrecipitationSetting::kBoxSize, PrecipitationSetting::kParticleDensity } },
		{ PrecipitationTab::kTexture, { PrecipitationSetting::kNumSubtexturesX, PrecipitationSetting::kNumSubtexturesY, PrecipitationSetting::kParticleTexture } },
	};

	std::vector<SearchResult> results;
	for (const auto& [tab, names] : entries) {
		for (const auto& name : names) {
			results.push_back({ WeatherUtils::TranslateControlLabel(name), tab, name });
		}
	}
	return results;
}
