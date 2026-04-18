#include "PrecipitationWidget.h"
#include "../EditorWindow.h"
#include "../WeatherUtils.h"
#include "Globals.h"
#include "RE/B/BSShaderManager.h"
#include "RE/N/NiSourceTexture.h"

void PrecipitationWidget::DrawWidget()
{
	WeatherUtils::SetCurrentWidget(this);
	if (BeginWidgetWindow()) {
		DrawWidgetHeader("##PrecipitationSearch", true, true);
		DrawSearchDropdown();

		bool changed = false;

		if (ImGui::BeginTabBar("PrecipitationTabs")) {
			const ImGuiTabItemFlags particleFlags = GetTabFlagsForOverride("Particle");
			const ImGuiTabItemFlags positionFlags = GetTabFlagsForOverride("Position");
			const ImGuiTabItemFlags textureFlags = GetTabFlagsForOverride("Texture");

			if (ImGui::BeginTabItem("Particle", nullptr, particleFlags)) {
				BeginScrollableContent("##ParticleScroll");
				if (MatchesSearch("Type")) {
					ImGui::SeparatorText("Particle Type");
					const char* types[] = { "Rain", "Snow" };
					int currentType = static_cast<int>(settings.particleType);
					if (ImGui::Combo("Type", &currentType, types, IM_ARRAYSIZE(types))) {
						settings.particleType = static_cast<uint32_t>(currentType);
						changed = true;
					}
				}
				if (MatchesSearch("Size X") || MatchesSearch("Size Y")) {
					ImGui::SeparatorText("Particle Size");
					if (MatchesSearch("Size X") && WeatherUtils::DrawSliderFloat("Size X", settings.particleSizeX, 0.0f, 200.0f))
						changed = true;
					if (MatchesSearch("Size Y") && WeatherUtils::DrawSliderFloat("Size Y", settings.particleSizeY, 0.0f, 200.0f))
						changed = true;
				}
				if (MatchesSearch("Gravity Velocity") || MatchesSearch("Rotation Velocity")) {
					ImGui::SeparatorText("Velocity");
					if (MatchesSearch("Gravity Velocity") && WeatherUtils::DrawSliderFloat("Gravity Velocity", settings.gravityVelocity, 0.0f, 10000.0f))
						changed = true;
					if (MatchesSearch("Rotation Velocity") && WeatherUtils::DrawSliderFloat("Rotation Velocity", settings.rotationVelocity, 0.0f, 10000.0f))
						changed = true;
				}
				EndScrollableContent();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Position", nullptr, positionFlags)) {
				BeginScrollableContent("##PositionScroll");
				if (MatchesSearch("Center Offset Min") || MatchesSearch("Center Offset Max") || MatchesSearch("Start Rotation Range")) {
					ImGui::SeparatorText("Offset");
					if (MatchesSearch("Center Offset Min") && WeatherUtils::DrawSliderFloat("Center Offset Min", settings.centerOffsetMin, 0.0f, 200.0f))
						changed = true;
					if (MatchesSearch("Center Offset Max") && WeatherUtils::DrawSliderFloat("Center Offset Max", settings.centerOffsetMax, 0.0f, 200.0f))
						changed = true;
					if (MatchesSearch("Start Rotation Range") && WeatherUtils::DrawSliderFloat("Start Rotation Range", settings.startRotationRange, 0.0f, 360.0f))
						changed = true;
				}
				if (MatchesSearch("Box Size") || MatchesSearch("Particle Density")) {
					ImGui::SeparatorText("Volume");
					if (MatchesSearch("Box Size") && WeatherUtils::DrawSliderFloat("Box Size", settings.boxSize, 0.0f, 1000.0f))
						changed = true;
					if (MatchesSearch("Particle Density") && WeatherUtils::DrawSliderFloat("Particle Density", settings.particleDensity, 0.0f, 1000.0f))
						changed = true;
				}
				EndScrollableContent();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Texture", nullptr, textureFlags)) {
				BeginScrollableContent("##TextureScroll");
				if (MatchesSearch("Num Subtextures X") || MatchesSearch("Num Subtextures Y")) {
					ImGui::SeparatorText("Subtextures");
					int numX = static_cast<int>(settings.numSubtexturesX);
					int numY = static_cast<int>(settings.numSubtexturesY);
					if (MatchesSearch("Num Subtextures X") && ImGui::InputInt("Num Subtextures X", &numX)) {
						settings.numSubtexturesX = std::max(1, numX);
						changed = true;
					}
					if (MatchesSearch("Num Subtextures Y") && ImGui::InputInt("Num Subtextures Y", &numY)) {
						settings.numSubtexturesY = std::max(1, numY);
						changed = true;
					}
				}
				if (MatchesSearch("Particle Texture")) {
				ImGui::SeparatorText("Texture Path");
				if (ImGui::InputText("Particle Texture", textureBuffer, sizeof(textureBuffer))) {
					std::string_view buf(textureBuffer);
					if (buf != lastCheckedBuffer) {
						lastCheckedExists = WeatherUtils::TexturePath::ExistsOnDisk(buf);
						lastCheckedBuffer = std::string(buf);
					}
					if (lastCheckedExists) {
						settings.particleTexture = lastCheckedBuffer;
						changed = true;
					}
				}
				if (std::string_view buf(textureBuffer); settings.particleTexture != buf) {
					if (!buf.empty() && !WeatherUtils::TexturePath::HasDdsExtension(buf))
						ImGui::TextColored(globals::menu->GetTheme().StatusPalette.Error, "Path must end with '.dds'");
					else if (!buf.empty()) {
						if (buf != lastCheckedBuffer) {
							lastCheckedExists = WeatherUtils::TexturePath::ExistsOnDisk(buf);
							lastCheckedBuffer = std::string(buf);
						}
						if (!lastCheckedExists)
							ImGui::TextColored(globals::menu->GetTheme().StatusPalette.Error, "Texture file not found under Data/textures/.");
					}
				}
				} // MatchesSearch("Particle Texture")

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
	ApplyLiveParticleTexture(settings.particleTexture);
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
		{ "Particle", { "Type", "Size X", "Size Y", "Gravity Velocity", "Rotation Velocity" } },
		{ "Position", { "Center Offset Min", "Center Offset Max", "Start Rotation Range", "Box Size", "Particle Density" } },
		{ "Texture", { "Num Subtextures X", "Num Subtextures Y", "Particle Texture" } },
	};

	std::vector<SearchResult> results;
	for (const auto& [tab, names] : entries) {
		for (const auto& name : names) {
			results.push_back({ name, tab, name });
		}
	}
	return results;
}
