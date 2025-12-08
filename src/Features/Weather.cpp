#include "Weather.h"

#include "Deferred.h"
#include "State.h"
#include "Util.h"
#include "WeatherManager.h"

#include "Editor/EditorWindow.h"

int8_t LerpInt8_t(const int8_t oldValue, const int8_t newVal, const float lerpValue)
{
	int lerpedValue = (int)std::lerp(oldValue, newVal, lerpValue);
	return (int8_t)std::clamp(lerpedValue, -128, 127);
}

uint8_t LerpUint8_t(const uint8_t oldValue, const uint8_t newVal, const float lerpValue)
{
	int lerpedValue = (int)std::lerp(oldValue, newVal, lerpValue);
	return (uint8_t)std::clamp(lerpedValue, 0, 255);
}

void LerpColor(const RE::TESWeather::Data::Color3& oldColor, RE::TESWeather::Data::Color3& newColor, const float changePct)
{
	newColor.red = (int8_t)std::lerp(-128, 127, changePct);
	newColor.green = (int8_t)std::lerp(oldColor.green, newColor.green, changePct);
	newColor.blue = LerpInt8_t(oldColor.blue, newColor.blue, changePct);
}

void LerpColor(const RE::Color& oldColor, RE::Color& newColor, const float changePct)
{
	newColor.red = LerpUint8_t(oldColor.red, newColor.red, changePct);
	newColor.green = LerpUint8_t(oldColor.green, newColor.green, changePct);
	newColor.blue = LerpUint8_t(oldColor.blue, newColor.blue, changePct);
}

void LerpDirectional(RE::BGSDirectionalAmbientLightingColors::Directional& oldColor, RE::BGSDirectionalAmbientLightingColors::Directional& newColor, const float changePct)
{
	LerpColor(oldColor.x.max, newColor.x.max, changePct);
	LerpColor(oldColor.x.min, newColor.x.min, changePct);
	LerpColor(oldColor.y.max, newColor.y.max, changePct);
	LerpColor(oldColor.y.min, newColor.y.min, changePct);
	LerpColor(oldColor.z.max, newColor.z.max, changePct);
	LerpColor(oldColor.z.min, newColor.z.min, changePct);
}

void WeatherEditor::DrawSettings()
{
	ImGui::TextWrapped("Weather Editor provides controls for saving settings and opening the weather editor window.");
	ImGui::Spacing();

	if (ImGui::BeginTable("##WeatherEditorButtons", 2, ImGuiTableFlags_SizingStretchSame)) {
		ImGui::TableNextColumn();
		if (ImGui::Button("Open Editor", { -1, 0 })) {
			EditorWindow::GetSingleton()->open = true;
		}

		ImGui::TableNextColumn();
		if (ImGui::Button("Save Settings", { -1, 0 })) {
			State::GetSingleton()->Save();
		}

		ImGui::EndTable();
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	DrawWeatherStatusPanel();

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	DrawQuickWeatherSpawner();
}

void WeatherEditor::Bind()
{
	if (loaded) {
		auto& context = globals::d3d::context;

		// Set PS shader resource
		{
			ID3D11ShaderResourceView* srv = diffuseIBLTexture->srv.get();
			context->PSSetShaderResources(80, 1, &srv);
		}
	}
}

void WeatherEditor::Prepass()
{
	auto& context = globals::d3d::context;

	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	auto& reflections = renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGET_CUBEMAP::kREFLECTIONS];

	std::array<ID3D11ShaderResourceView*, 1> srvs = { reflections.SRV };
	std::array<ID3D11UnorderedAccessView*, 1> uavs = { diffuseIBLTexture->uav.get() };
	std::array<ID3D11SamplerState*, 1> samplers = { Deferred::GetSingleton()->linearSampler };

	// Unset PS shader resource
	{
		ID3D11ShaderResourceView* srv = nullptr;
		context->PSSetShaderResources(80, 1, &srv);
	}

	// IBL
	{
		samplers[0] = Deferred::GetSingleton()->linearSampler;

		context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(GetDiffuseIBLCS(), nullptr, 0);
		context->Dispatch(1, 1, 1);
	}

	// Reset
	{
		srvs.fill(nullptr);
		uavs.fill(nullptr);
		samplers.fill(nullptr);

		context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(nullptr, nullptr, 0);
	}

	// Set PS shader resource
	{
		ID3D11ShaderResourceView* srv = diffuseIBLTexture->srv.get();
		context->PSSetShaderResources(100, 1, &srv);
	}
}

void WeatherEditor::SetupResources()
{
	GetDiffuseIBLCS();

	{
		D3D11_TEXTURE2D_DESC texDesc{
			.Width = 3,
			.Height = 1,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
			.SampleDesc = { 1, 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = texDesc.MipLevels }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		diffuseIBLTexture = new Texture2D(texDesc);
		diffuseIBLTexture->CreateSRV(srvDesc);
		diffuseIBLTexture->CreateUAV(uavDesc);
	}
}

void WeatherEditor::ClearShaderCache()
{
	if (diffuseIBLCS)
		diffuseIBLCS->Release();
	diffuseIBLCS = nullptr;
}

ID3D11ComputeShader* WeatherEditor::GetDiffuseIBLCS()
{
	if (!diffuseIBLCS)
		diffuseIBLCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Weather\\DiffuseIBLCS.hlsl", {}, "cs_5_0"));
	return diffuseIBLCS;
}

void WeatherEditor::LerpWeather(RE::TESWeather* oldWeather, RE::TESWeather* newWeather, float currentWeatherPct)
{
	//// Precipitation
	newWeather->data.precipitationBeginFadeIn = LerpInt8_t(oldWeather->data.precipitationBeginFadeIn, newWeather->data.precipitationBeginFadeIn, currentWeatherPct);
	newWeather->data.precipitationEndFadeOut = LerpInt8_t(oldWeather->data.precipitationEndFadeOut, newWeather->data.precipitationEndFadeOut, currentWeatherPct);

	//// Sun
	newWeather->data.sunDamage = LerpInt8_t(oldWeather->data.sunDamage, newWeather->data.sunDamage, currentWeatherPct);
	newWeather->data.sunGlare = LerpInt8_t(oldWeather->data.sunGlare, newWeather->data.sunGlare, currentWeatherPct);

	//// Lightning
	newWeather->data.thunderLightningBeginFadeIn = LerpInt8_t(oldWeather->data.thunderLightningBeginFadeIn, newWeather->data.thunderLightningBeginFadeIn, currentWeatherPct);
	newWeather->data.thunderLightningEndFadeOut = LerpInt8_t(oldWeather->data.thunderLightningEndFadeOut, newWeather->data.thunderLightningEndFadeOut, currentWeatherPct);
	newWeather->data.thunderLightningFrequency = LerpInt8_t(oldWeather->data.thunderLightningFrequency, newWeather->data.thunderLightningFrequency, currentWeatherPct);
	LerpColor(oldWeather->data.lightningColor, newWeather->data.lightningColor, currentWeatherPct);

	//// Trans delta
	newWeather->data.transDelta = LerpInt8_t(oldWeather->data.transDelta, newWeather->data.transDelta, currentWeatherPct);

	//// Visual Effects
	newWeather->data.visualEffectBegin = LerpInt8_t(oldWeather->data.visualEffectBegin, newWeather->data.visualEffectBegin, currentWeatherPct);
	newWeather->data.visualEffectEnd = LerpInt8_t(oldWeather->data.visualEffectEnd, newWeather->data.visualEffectEnd, currentWeatherPct);

	//// Wind
	newWeather->data.windDirection = LerpInt8_t(oldWeather->data.windDirection, newWeather->data.windDirection, currentWeatherPct);
	newWeather->data.windDirectionRange = LerpInt8_t(oldWeather->data.windDirectionRange, newWeather->data.windDirectionRange, currentWeatherPct);
	newWeather->data.windSpeed = LerpUint8_t(oldWeather->data.windSpeed, newWeather->data.windSpeed, currentWeatherPct);

	//// Fog
	newWeather->fogData.dayFar = std::lerp(oldWeather->fogData.dayFar, newWeather->fogData.dayFar, currentWeatherPct);
	newWeather->fogData.dayMax = std::lerp(oldWeather->fogData.dayMax, newWeather->fogData.dayMax, currentWeatherPct);
	newWeather->fogData.dayNear = std::lerp(oldWeather->fogData.dayNear, newWeather->fogData.dayNear, currentWeatherPct);
	newWeather->fogData.dayPower = std::lerp(oldWeather->fogData.dayPower, newWeather->fogData.dayPower, currentWeatherPct);

	newWeather->fogData.nightFar = std::lerp(oldWeather->fogData.nightFar, newWeather->fogData.nightFar, currentWeatherPct);
	newWeather->fogData.nightMax = std::lerp(oldWeather->fogData.nightMax, newWeather->fogData.nightMax, currentWeatherPct);
	newWeather->fogData.nightNear = std::lerp(oldWeather->fogData.nightNear, newWeather->fogData.nightNear, currentWeatherPct);
	newWeather->fogData.nightPower = std::lerp(oldWeather->fogData.nightPower, newWeather->fogData.nightPower, currentWeatherPct);

	//// Weather colors
	for (size_t i = 0; i < RE::TESWeather::ColorTypes::kTotal; i++) {
		for (size_t j = 0; j < RE::TESWeather::ColorTime::kTotal; j++) {
			LerpColor(oldWeather->colorData[i][j], newWeather->colorData[i][j], currentWeatherPct);
		}
	}

	//// DALC
	for (size_t i = 0; i < RE::TESWeather::ColorTime::kTotal; i++) {
		auto& newDALC = newWeather->directionalAmbientLightingColors[i];
		auto& oldDALC = oldWeather->directionalAmbientLightingColors[i];

		LerpColor(oldDALC.specular, newDALC.specular, currentWeatherPct);
		newWeather->directionalAmbientLightingColors[i].fresnelPower = std::lerp(oldDALC.fresnelPower, newDALC.fresnelPower, currentWeatherPct);
		LerpDirectional(oldDALC.directional, newDALC.directional, currentWeatherPct);
	}

	//// Clouds
	for (size_t i = 0; i < RE::TESWeather::kTotalLayers; i++) {
		for (size_t j = 0; j < RE::TESWeather::ColorTime::kTotal; j++) {
			LerpColor(oldWeather->cloudColorData[i][j], newWeather->cloudColorData[i][j], currentWeatherPct);
			newWeather->cloudAlpha[i][j] = std::lerp(oldWeather->cloudAlpha[i][j], newWeather->cloudAlpha[i][j], currentWeatherPct);
		}

		newWeather->cloudLayerSpeedY[i] = LerpInt8_t(oldWeather->cloudLayerSpeedY[i], newWeather->cloudLayerSpeedY[i], currentWeatherPct);
		newWeather->cloudLayerSpeedX[i] = LerpInt8_t(oldWeather->cloudLayerSpeedX[i], newWeather->cloudLayerSpeedX[i], currentWeatherPct);
	}
}

void WeatherEditor::DrawWeatherStatusPanel()
{
	if (ImGui::CollapsingHeader("Current Weather Status", ImGuiTreeNodeFlags_DefaultOpen)) {
		auto weatherManager = WeatherManager::GetSingleton();
		auto currentWeathers = weatherManager->GetCurrentWeathers();

		if (currentWeathers.currentWeather) {
			ImGui::Text("Current Weather: %s",
				currentWeathers.currentWeather->GetFormEditorID() ?
					currentWeathers.currentWeather->GetFormEditorID() :
					std::format("{:08X}", currentWeathers.currentWeather->GetFormID()).c_str());

			if (currentWeathers.lastWeather && currentWeathers.lerpFactor < 1.0f) {
				ImGui::Text("Transitioning From: %s",
					currentWeathers.lastWeather->GetFormEditorID() ?
						currentWeathers.lastWeather->GetFormEditorID() :
						std::format("{:08X}", currentWeathers.lastWeather->GetFormID()).c_str());

				ImGui::ProgressBar(currentWeathers.lerpFactor, ImVec2(-1, 0),
					std::format("Transition: {:.1f}%%", currentWeathers.lerpFactor * 100.0f).c_str());
			} else {
				ImGui::Text("Transition: Complete (100%%)");
			}

			// Show if weather has custom settings
			if (weatherManager->HasWeatherSettings(currentWeathers.currentWeather)) {
				ImGui::TextColored({ 0.0f, 1.0f, 0.0f, 1.0f }, "Has Custom Settings");
			} else {
				ImGui::TextColored({ 0.7f, 0.7f, 0.7f, 1.0f }, "Using Default Settings");
			}
		} else {
			ImGui::TextColored({ 1.0f, 0.5f, 0.0f, 1.0f }, "No Active Weather");
		}
	}
}

void WeatherEditor::DrawQuickWeatherSpawner()
{
	if (ImGui::CollapsingHeader("Quick Weather Spawner")) {
		ImGui::TextWrapped("Quickly test different weather conditions. Note: Weather changes may take time to transition.");
		ImGui::Spacing();

		static char weatherFilter[128] = "";
		ImGui::InputTextWithHint("##WeatherFilter", "Search weathers...", weatherFilter, sizeof(weatherFilter));

		if (ImGui::BeginChild("WeatherList", ImVec2(0, 200), true)) {
			auto dataHandler = RE::TESDataHandler::GetSingleton();
			if (dataHandler) {
				auto& weatherArray = dataHandler->GetFormArray<RE::TESWeather>();

				for (auto* weather : weatherArray) {
					if (!weather)
						continue;

					const char* editorID = weather->GetFormEditorID();
					std::string displayName = editorID ? editorID : std::format("{:08X}", weather->GetFormID());

					// Filter
					if (weatherFilter[0] != '\0' &&
						displayName.find(weatherFilter) == std::string::npos) {
						continue;
					}

					if (ImGui::Selectable(displayName.c_str())) {
						// Force weather change
						auto sky = RE::Sky::GetSingleton();
						if (sky) {
							sky->ForceWeather(weather, false);
						}
					}

					// Show if this weather has custom settings
					if (WeatherManager::GetSingleton()->HasWeatherSettings(weather)) {
						ImGui::SameLine();
						ImGui::TextColored({ 0.0f, 0.8f, 0.0f, 1.0f }, "[Modified]");
					}
				}
			}
		}
		ImGui::EndChild();

		if (ImGui::Button("Reset to Natural Weather", ImVec2(-1, 0))) {
			auto sky = RE::Sky::GetSingleton();
			if (sky) {
				sky->ReleaseWeatherOverride();
			}
		}
	}
}
