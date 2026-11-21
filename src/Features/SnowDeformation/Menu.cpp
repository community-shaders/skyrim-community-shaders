#include "Features/SnowDeformation.h"

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include "State.h"
#include "Utils/Game.h"
#include "Utils/UI.h"

#define I18N_KEY_PREFIX "feature.snow_deformation."

namespace
{
	void WrapText(const char* a_text, const char* a_end = nullptr)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
			return;
		if (!a_end)
			a_end = a_text + std::strlen(a_text);
		ImGuiContext& g = *GImGui;
		const ImVec2 origin(window->DC.CursorPos.x, window->DC.CursorPos.y + window->DC.CurrLineTextBaseOffset);
		const float right = window->DC.CursorPos.x + ImGui::GetContentRegionAvail().x;
		const float hang = g.FontSize * 1.5f;
		float x = origin.x;
		float y = origin.y;
		float maxX = origin.x;
		const char* s = a_text;
		do {
			const char* lineEnd = static_cast<const char*>(std::memchr(s, '\n', a_end - s));
			if (!lineEnd)
				lineEnd = a_end;
			const char* cut = g.Font->CalcWordWrapPosition(g.FontSize, s, lineEnd, ImMax(right - x, g.FontSize));
			if (cut <= s)
				cut = lineEnd;
			ImGui::RenderText({ x, y }, s, cut, false);
			maxX = ImMax(maxX, x + ImGui::CalcTextSize(s, cut).x);
			y += g.FontSize;
			s = cut;
			while (s < a_end && *s == ' ')
				s++;
			if (s < a_end && *s == '\n')
				s++;
			x = origin.x + hang;
		} while (s < a_end);
		const ImVec2 size(maxX - origin.x, y - origin.y);
		ImGui::ItemSize(size, 0.0f);
		ImGui::ItemAdd(ImRect(origin.x, origin.y, origin.x + size.x, origin.y + size.y), 0);
	}

	std::string FormatV(const char* a_fmt, va_list a_args)
	{
		va_list copy;
		va_copy(copy, a_args);
		const int n = std::vsnprintf(nullptr, 0, a_fmt, copy);
		va_end(copy);
		if (n <= 0)
			return {};
		std::string out(static_cast<size_t>(n), '\0');
		std::vsnprintf(out.data(), out.size() + 1, a_fmt, a_args);
		return out;
	}

	void WrapTextF(const char* a_fmt, ...)
	{
		va_list args;
		va_start(args, a_fmt);
		const std::string text = FormatV(a_fmt, args);
		va_end(args);
		WrapText(text.data(), text.data() + text.size());
	}
}

namespace
{
	struct QualityPreset
	{
		uint32_t mapDim;
		float trenchesM;
		float actorTrenchesM;
		float parallaxDepth;
		float parallaxShadow;
		bool sparkle;
		bool contactShadows;
		bool selfShadowing;
		bool castsShadows;
	};
	constexpr QualityPreset kQualityPresets[SnowDeformation::kQualityPresetCount] = {
		{ 1024u, 29.0f, 10.0f, 0.0f, 0.0f, false, false, false, false },
		{ 2048u, 115.0f, 105.0f, 0.25f, 0.13f, false, false, true, true },
		{ 4096u, 200.0f, 200.0f, 0.5f, 0.25f, true, true, true, true },
	};
}

void SnowDeformation::ApplyQualityPreset(int a_preset)
{
	const auto& p = kQualityPresets[std::clamp(a_preset, 0, kQualityPresetCount - 1)];
	if (settings.DeformMapResolution != p.mapDim) {
		settings.DeformMapResolution = p.mapDim;
		deformMapDimDirty = true;
	}
	if (settings.RangeTrenchesM != p.trenchesM) {
		settings.RangeTrenchesM = p.trenchesM;
		trenchRangeDirty = true;
	}
	settings.RangeActorTrenchesM = p.actorTrenchesM;
	settings.ParallaxDepth = p.parallaxDepth;
	settings.ParallaxShadowStrength = p.parallaxShadow;
	settings.SnowGlints = p.sparkle;
	settings.SnowScreenSpaceShadows = p.contactShadows;
	settings.ShellHorizonMarch = p.selfShadowing;
	settings.SnowCastsShadows = p.castsShadows;
}

int SnowDeformation::MatchingQualityPreset() const
{
	for (int i = 0; i < kQualityPresetCount; i++) {
		const auto& p = kQualityPresets[i];
		if (settings.DeformMapResolution == p.mapDim && settings.RangeTrenchesM == p.trenchesM &&
			settings.RangeActorTrenchesM == p.actorTrenchesM &&
			settings.ParallaxDepth == p.parallaxDepth && settings.ParallaxShadowStrength == p.parallaxShadow &&
			settings.SnowGlints == p.sparkle && settings.SnowScreenSpaceShadows == p.contactShadows && settings.ShellHorizonMarch == p.selfShadowing &&
			settings.SnowCastsShadows == p.castsShadows)
			return i;
	}
	return -1;
}

void SnowDeformation::DrawSettings()
{
	ImGui::TextDisabled("%s", T(TKEY("credit"), "Snow Deformation - by josefplyr1"));
	if (auto _ttCredit = Util::HoverTooltipWrapper())
		ImGui::Text("%s", kAttribution);
	ImGui::Separator();

	ImGui::Checkbox(T(TKEY("enable"), "Enable Snow Deformation"), &settings.EnableSnowDeformation);

	{
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(T(TKEY("quality_presets"), "Quality Preset:"));
		if (auto _ttPresets = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("quality_presets_tooltip"), "Sets the snow's performance in one click. High, the default: every effect on, trails drawn out to 200 m and a 4096 trail map. Medium: no sparkle or contact shadows on the snow, half the parallax, trails out to 115 m (NPCs 105 m) and a 2048 map. Low: no sparkle, snow shadows, self-shadowing or parallax, trails out to 29 m (NPCs 10 m) and a 1024 map. Custom means the saved settings match no preset. Changing the map size or the trail range clears the trails."));
		const char* presetLabels[kQualityPresetCount] = { T(TKEY("preset_low"), "Low"), T(TKEY("preset_medium"), "Medium"), T(TKEY("preset_high"), "High") };
		const int activePreset = MatchingQualityPreset();
		for (int i = 0; i < kQualityPresetCount; i++) {
			ImGui::SameLine();
			if (i == activePreset)
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			if (ImGui::Button(presetLabels[i]))
				ApplyQualityPreset(i);
			if (i == activePreset)
				ImGui::PopStyleColor();
		}
		if (activePreset < 0) {
			ImGui::SameLine();
			ImGui::TextDisabled("%s", T(TKEY("preset_custom"), "(Custom)"));
		}
		if (deformMapVramCap && deformMapDim < settings.DeformMapResolution)
			ImGui::TextDisabled("%s %u", T(TKEY("vram_aware_map_active"), "Video memory over budget this session, map running at"), deformMapDim);
	}

	ImGui::InputText(T(TKEY("snow_texture_path"), "Shell Snow Texture"), &settings.SnowTexturePath);
	if (auto _ttTex = Util::HoverTooltipWrapper())
		ImGui::Text("%s", T(TKEY("snow_texture_path_tooltip"), "The ground snow's texture: a DDS path relative to Data. Point it at your modlist's snow texture, then press Reload."));
	ImGui::SameLine();
	if (ImGui::Button(T(TKEY("reload_texture"), "Reload"))) {
		shellSnowDiffuseSRV = nullptr;
		shellSnowNormalSRV = nullptr;
		shellSnowRmaosSRV = nullptr;
		shellSnowTextureIsPBR = false;
		shellSnowTextureAttempted = false;
	}

	ImGui::SliderFloat(T(TKEY("refill_rate"), "Snow Refill Rate"), &settings.RefillRateMultiplier, 0.0f, 10.0f, "%.1fx");
	if (auto _ttRefill = Util::HoverTooltipWrapper())
		ImGui::Text("%s", T(TKEY("refill_rate_tooltip"), "How fast trails fill back in. They fill only while it snows, faster in heavier snowfall. At 1.0x, typical snowfall fills a trail in about 12 minutes; 0 stops refilling."));

	ImGui::Checkbox(T(TKEY("enable_accumulation"), "Snow Accumulation"), &settings.EnableSnowAccumulation);
	if (auto _ttAccumEnable = Util::HoverTooltipWrapper())
		ImGui::Text("%s", T(TKEY("enable_accumulation_tooltip"), "Snowfall deepens the snow: heavy snow builds it up to 1.5 times its normal depth over about one game hour, and clear weather settles it back over about one game hour. Off keeps the snow at its normal depth in every weather."));

	ImGui::SliderFloat(T(TKEY("snow_resistance"), "Snow Resistance"), &settings.SnowResistance, 0.0f, 100.0f, "%.0f %%");
	if (auto _ttResist = Util::HoverTooltipWrapper())
		ImGui::Text("%s", T(TKEY("snow_resistance_tooltip"), "How much of a body's push the snow holds back. Feet stand on the ground beneath the snow, so at 0% every step cuts to the bottom and anything low - a long skirt, a hand, a blade - cuts as deep as it reaches. At 50% one pass carves half of that: legs sink into the snow and the trench behind stays shallow. Every new pass over the same snow carves the same share of what is left, so busy paths sink deeper the more they are walked. Affects new trenches only."));

	if (trenchDetailAttempted && !trenchDetailNormalSRV)
		ImGui::TextDisabled("%s", T(TKEY("trench_detail_missing"), "Trench detail textures not found: Shaders\\SnowDeformation\\TrenchDetailN.dds and TrenchDetailH.dds."));

	if (ImGui::TreeNodeEx(T(TKEY("texture_depths"), "Snow Depth by Landscape Texture"), ImGuiTreeNodeFlags_Framed)) {
		if (auto _ttTextures = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("texture_depths_tooltip"), "Every landscape texture the game has loaded this session, each with its own snow depth. A texture starts at its family's depth; moving it here pins it to its own value, saved by texture path so load order cannot rebind it. Use this to give snow to textures the families lump in with bare ground (snowy stone, frozen marsh), or to hold one texture back."));

		std::vector<LandTextureEntry> textures;
		{
			const std::shared_lock lock(landTextureMutex);
			textures = landTextures;
		}

		ImGui::InputText(T(TKEY("texture_filter"), "Filter"), &textureFilter);
		std::string filter = textureFilter;
		std::transform(filter.begin(), filter.end(), filter.begin(),
			[](unsigned char c) { return (char)std::tolower(c); });

		std::vector<uint16_t> order(textures.size());
		std::iota(order.begin(), order.end(), (uint16_t)0);
		std::sort(order.begin(), order.end(), [&](uint16_t a, uint16_t b) {
			if (textures[a].classIndex != textures[b].classIndex)
				return textures[a].classIndex < textures[b].classIndex;
			return textures[a].label < textures[b].label;
		});

		WrapTextF("%zu textures loaded this session", textures.size());

		for (uint16_t textureI : order) {
			const auto& entry = textures[textureI];
			if (!filter.empty() && entry.path.find(filter) == std::string::npos)
				continue;

			ImGui::PushID((int)textureI);
			float depth = entry.depth;
			if (ImGui::SliderFloat(entry.label.c_str(), &depth, -20.0f, 64.0f, "%.0f units")) {
				SetLandTextureOverride(entry.path, depth);
				shellDataDirty.store(true, std::memory_order_release);
			}
			if (auto _ttEntry = Util::HoverTooltipWrapper()) {
				ImGui::Text(T(TKEY("texture_entry_tooltip"), "%s\nFamily: %s (%.0f units)"), entry.path.c_str(),
					kSnowClasses[entry.classIndex].label, kSnowClassDepths[entry.classIndex]);
				if (entry.overridden)
					ImGui::Text("%s", T(TKEY("texture_pinned"), "Pinned to its own value; Reset hands it back."));
				else if (entry.shipped)
					ImGui::Text("%s", T(TKEY("texture_shipped"), "Ships with its own default depth."));
			}
			if (entry.overridden) {
				ImGui::SameLine();
				if (ImGui::SmallButton(T(TKEY("texture_reset"), "Reset"))) {
					SetLandTextureOverride(entry.path, std::nullopt);
					shellDataDirty.store(true, std::memory_order_release);
				}
			}
			ImGui::PopID();
		}

		ImGui::TreePop();
	}
}
