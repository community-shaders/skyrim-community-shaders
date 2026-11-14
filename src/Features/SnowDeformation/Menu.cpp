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
	settings.Tessellation = true;
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
			settings.Tessellation && settings.ParallaxDepth == p.parallaxDepth && settings.ParallaxShadowStrength == p.parallaxShadow &&
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
			ImGui::Text("%s", T(TKEY("quality_presets_tooltip"), "Sets the performance settings in one click. High is the default: every effect on, every reach at its maximum and a 4096 Deformation Map. Medium keeps the snow's shadows (Snow Casts Shadows, Snow Self-Shadowing) and turns off the close-up extras (Snow Sparkle, Snow Contact Shadows), with Trenches at 115 m and NPC Trenches at 105 m. Low turns all four off, and parallax too, with a 1024 Deformation Map, Trenches at 29 m and NPC Trenches at 10 m. Every preset turns Tessellate Trenches on and the experimental Volume Snow off. Everything else keeps its value; change any of these afterwards and the preset shows as Custom. Applying can clear existing trenches (a resolution or Trenches-range change does); remembered trenches are re-injected."));
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
	}

	if (ImGui::TreeNodeEx(T(TKEY("general_settings"), "General Settings"), ImGuiTreeNodeFlags_Framed)) {
		ImGui::InputText(T(TKEY("snow_texture_path"), "Shell Snow Texture"), &settings.SnowTexturePath);
		if (auto _ttTex = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("snow_texture_path_tooltip"), "DDS path (relative to Data) for the shell's snow diffuse. Point it at the modlist's snow texture, then press Reload."));
		ImGui::SameLine();
		if (ImGui::Button(T(TKEY("reload_texture"), "Reload"))) {
			shellSnowDiffuseSRV = nullptr;
			shellSnowNormalSRV = nullptr;
			shellSnowRmaosSRV = nullptr;
			shellSnowTextureIsPBR = false;
			shellSnowTextureAttempted = false;
		}

		if (ImGui::InputText(T(TKEY("snow_pbr_config"), "Snow PBR Config"), &settings.SnowPBRConfig)) {
			shellSnowTextureAttempted = false;
		}
		if (auto _ttPbrCfg = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("snow_pbr_config_tooltip"), "The shell takes its roughness, specular level and sparkle from a TruePBR texture set, matched by the snow texture's file name. A modlist often holds several sets whose name contains that word - a blend variant, a glacier, a grass edge - and some of them carry no sparkle settings at all, which leaves the snow flat and dull. Leave this empty to take the best automatic match, or type a set's name (its JSON file name, without the extension) to pin it. The set in use is shown beside this box and updates as you type."));
		ImGui::SameLine();
		if (snowPBRSetName.empty())
			ImGui::TextDisabled("%s", T(TKEY("snow_pbr_config_none"), "(no match - built-in snow)"));
		else
			ImGui::TextDisabled("%s%s", T(TKEY("snow_pbr_config_using"), "using: "), snowPBRSetName.c_str());

		ImGui::Checkbox(T(TKEY("snow_glints"), "Snow Sparkle"), &settings.SnowGlints);
		if (auto _ttGlints = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("snow_glints_tooltip"), "The glitter of light catching single snow crystals, on the ground snow, using Community Shaders' TruePBR glints (needs TruePBR). The Medium and Low presets turn it off."));
		ImGui::Checkbox(T(TKEY("snow_screen_space_shadows"), "Snow Contact Shadows"), &settings.SnowScreenSpaceShadows);
		if (auto _ttSnowSSS = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("snow_screen_space_shadows_tooltip"), "Community Shaders' Screen-Space Shadows on the ground snow: the small, sharp shadows where grass, feet and objects meet the snow (needs Screen-Space Shadows). Off, the snow is shaded by the sun's shadows and its own self-shadowing only. The Medium and Low presets turn it off."));
		ImGui::Checkbox(T(TKEY("snow_casts_shadows"), "Snow Casts Shadows"), &settings.SnowCastsShadows);
		if (auto _ttSnowCasts = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("snow_casts_shadows_tooltip"), "The ground snow is drawn into the sun's shadows, so deep snow, banks and trench walls cast shadows onto the world and onto the snow itself. Off, the snow still receives shadows but casts none. The Low preset turns it off."));

		ImGui::PushID("general_settings");
		if (ImGui::TreeNodeEx(T(TKEY("menu_advanced"), "Advanced"))) {
			ImGui::Checkbox(T(TKEY("camera_above_snow"), "Keep Camera Above the Snow"), &settings.CameraAboveSnow);
			if (auto _ttCam = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T(TKEY("camera_above_snow_tooltip"), "The game's camera collides with the ground, not with the snow, so a low third-person camera, or the camera while you are knocked down, can slip under the sheet and show its underside. On, the camera is pulled in toward you when that happens, the way it is against a hillside, and lifted clear of the snow while you are down."));

			ImGui::Checkbox(T(TKEY("shell_horizon_march"), "Snow Self-Shadowing"), &settings.ShellHorizonMarch);
			if (auto _ttHm = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T(TKEY("shell_horizon_march_tooltip"), "The snow shades itself: bumps, drifts, berms and trench rims cast soft shadows onto the snow behind them, found by a short march toward the sun over the snow's height, on the ground snow. It fades out as the sun nears the horizon, where the game's own shadows take over. Off, the snow is shaded by the game's shadows and Snow Contact Shadows only. The Low preset turns it off."));
			ImGui::SliderFloat(T(TKEY("shelter_max_height"), "Shelter Max Height"), &settings.ShelterMaxHeight, 100.0f, 1000.0f, "%.0f units");
			if (auto _ttShelterMax = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T(TKEY("shelter_max_height_tooltip"), "Snow under a roof, bridge or archway thins out, because the structure keeps the snowfall off. A structure whose underside is higher above the ground than this no longer counts: a wide archway with plenty of air under it is as open as the sky, and the snow beneath it keeps its full depth. Fades out over the next 100 units. 70 units is about a metre."));
			ImGui::SliderFloat(T(TKEY("shelter_drift_angle"), "Snow Drift Angle"), &settings.ShelterDriftAngle, 5.0f, 70.0f, "%.0f deg");
			if (auto _ttShelterAngle = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T(TKEY("shelter_drift_angle_tooltip"), "Cover higher than Shelter Max Height (up to about 250 units) still keeps the snow off where the spot is enclosed - cover all around it, like the inside of a tent; a spot open along one side, like the strip under an overhang, keeps its snow. This angle is how far from straight down the snow blows in, which sets how much cover a spot needs around it to count as enclosed. Higher = snow reaches further in, so only deep cover shelters; lower = a smaller roof is enough. Default 30."));
			ImGui::SliderFloat(T(TKEY("workspace_clearing_size"), "Workspace Clearing Size"), &settings.TrampleZoneScale, 0.25f, 2.0f, "%.2fx");
			if (auto _ttWcs = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T(TKEY("workspace_clearing_size_tooltip"), "Radius multiplier for the snow bowls around workstations, smelters, forges, stalls, wells and shrines. Applies within a second."));

			ImGui::SliderFloat(T(TKEY("workspace_clearing_height"), "Workspace Clearing Height"), &settings.TrampleZoneHeight, 0.0f, 100.0f, "%.0f%%");
			if (auto _ttWch = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T(TKEY("workspace_clearing_height_tooltip"), "Snow height remaining in a workspace bowl, as a percent of the surrounding depth. 0 melts to the floor, 100 disables the clearing. Applies within a second."));

			ImGui::TreePop();
		}
		ImGui::PopID();

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx(T(TKEY("model_depths"), "Object Snow"), ImGuiTreeNodeFlags_Framed)) {
		if (auto _ttModels = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("model_depths_tooltip"), "Object snow: the recolor of the game's projected snow."));

		ImGui::Checkbox(T(TKEY("proj_snow_match"), "Recolor Projected Snow"), &settings.ProjSnowMatch);
		if (auto _ttPsm = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("proj_snow_match_tooltip"), "Makes the game's painted-on projected snow look like this mod's snow: inside the object's own shader the projection's texture and material are swapped for the snow shell's set, which works from every angle, overhangs included. Every pixel the game paints solidly (its own half blend and up) takes the full snow, all or nothing; a faint dusting stays bare. Only draws whose projected material really is snow are touched, so sand and moss projections keep their look."));

		ImGui::Checkbox(T(TKEY("snow_textured_recolor"), "Recolor Snow-Textured Meshes"), &settings.SnowTexturedRecolor);
		if (auto _ttStr = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("snow_textured_recolor_tooltip"), "Some meshes wear snow as a plain texture rather than the game's projected paint: dirt cliffs with a snow01 top, rock piles and their skirts, a season mod's winter variants, the vanilla drifts. The projected-snow recolor never sees those, so they keep their own snow texture and mismatch the shell. This gives them the shell's snow in the object's own shader, on the texels that read as snow; a plain ground snow texture (snow01, snowrocks01) takes it on every texel where it lies flat, so the stones in a rock pile's ground turn to snow too. Needs Recolor Projected Snow."));

		ImGui::Checkbox(T(TKEY("proj_snow_normals"), "Recolor Projected Snow Normals"), &settings.ProjSnowNormals);
		if (auto _ttProjNormals = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("proj_snow_normals_tooltip"), "Recolored projected snow also takes the snow's own surface relief. Without it the game's projected snow keeps the plank's or the stone's own bump - the game never sends its own snow normal to the deferred buffer - and the snow reads as paint on the surface."));

		ImGui::Checkbox(T(TKEY("snow_follows_parallax"), "Snow Follows Parallax"), &settings.SnowFollowsParallax);
		if (auto _ttFollowParallax = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("snow_follows_parallax_tooltip"), "On objects drawn with Extended Materials parallax (PBR displacement and complex parallax), the game's recolored projected snow lies on the surface the parallax draws instead of on the flat mesh under it: its edge and its own texture move with the stone as the camera moves, so the stone no longer slides beneath the snow and the snow stops looking like it floats. With Flatten Parallax Under Snow also on, it follows the relief the flatten leaves. Objects on the older vanilla parallax shader are not affected. On by default, together with the other one."));

		ImGui::Checkbox(T(TKEY("flatten_parallax_under_snow"), "Flatten Parallax Under Snow"), &settings.FlattenParallaxUnderSnow);
		if (auto _ttFlattenParallax = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("flatten_parallax_under_snow_tooltip"), "The other answer to snow floating over parallax objects: on an object with projected snow, its Extended Materials parallax (PBR displacement and complex parallax) fades out as the snow covers the surface, the way real snow fills cracks and crevices, and the shadows the relief casts on itself fade with it. Where the snow lies solid the rock is flat, so the snow sits on a surface that does not move; where it lies in patches part of the relief stays; bare rock keeps all of it. With Snow Follows Parallax also on, the patches follow the relief that stays. Objects on the older vanilla parallax shader carry no projected snow and are not affected. On by default, together with the other one."));

		ImGui::SliderFloat(T(TKEY("seasons_snow_max_angle"), "Seasons Snow Max Angle"), &settings.SeasonsSnowMaxAngle, 0.0f, 90.0f, "%.0f deg");
		if (auto _ttSeasons = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("seasons_snow_max_angle_tooltip"), "Steepest slope that keeps snow on objects Seasons of Skyrim paints at runtime. Those projections come with the record default of 90 degrees and no painted mask; 90 keeps Seasons' own coverage, which the recolor then follows exactly, and lower cuts the snow off steeper faces for them only. Authored snow objects keep their own angle and mask."));

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx(T(TKEY("distant_snow"), "Distant Snow"), ImGuiTreeNodeFlags_Framed)) {
		if (auto _ttDs = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("distant_snow_tooltip"), "Snow on far terrain the game hasn't loaded: heights come from the worldspace heightmap (shipped with Community Shaders), and snow placement follows the game's own distant LOD textures — where the LOD is painted snowy, our snow appears. Loaded terrain always uses its real snow textures instead. The sliders here also set how far each snow system reaches; higher = more VRAM and GPU cost."));
		bool distantChanged = false;

		ImGui::Checkbox(T(TKEY("horizon_snow"), "Horizon Snow"), &settings.HorizonSnow);
		if (auto _ttHs = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("horizon_snow_tooltip"), "Recolors the game's distant LOD terrain with the shell's own snow material wherever its bake reads as snow, so snow appearance stays consistent from your feet to the horizon. The snow shell ends at the loaded-cell boundary and this takes over from there, out to the edge of the world."));

		ImGui::Checkbox(T(TKEY("lod_object_snow"), "Recolor Baked LOD Snow"), &settings.LODObjectSnow);
		if (auto _ttLodObj = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("lod_object_snow_tooltip"), "Distant objects the game draws as LOD without its projected-snow flags - snow drifts, snowy roads, piles - keep their own baked snow texture, which reads far brighter than the shell. This applies the Horizon Snow recolor to those pixels too, with the same detection slider below."));
		distantChanged |= ImGui::SliderFloat(T(TKEY("lod_snow_sensitivity"), "LOD Snow Detection"), &settings.LODSnowSensitivity, 0.0f, 1.0f, "%.2f");
		if (auto _ttLss = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("lod_snow_sensitivity_tooltip"), "How eagerly a distant LOD texture pixel counts as snow. The scale was widened: the old best-at-1.0 now sits near 0.5. Low = only bright white; high = pale gray rock starts counting too. The same setting drives the Horizon Snow recolor, so it decides where snow sits on the far terrain as well as how the shell reads it."));

		ImGui::SliderFloat(T(TKEY("range_trenches"), "Trenches"), &settings.RangeTrenchesM, 29.0f, 200.0f, "%.0f m");
		if (ImGui::IsItemDeactivatedAfterEdit())
			trenchRangeDirty = true;
		if (auto _ttRt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("range_trenches_tooltip"), "Deformation window radius: you carve anywhere inside it, NPCs out to NPC Trenches. Applying a change CLEARS existing trenches. NOT a GPU setting: GPU cost follows Deformation Map Resolution, not range. Texel detail is range divided by resolution, so a smaller range at the same resolution means sharper footprints over a shorter reach."));

		ImGui::SliderFloat(T(TKEY("range_actor_trenches"), "NPC Trenches"), &settings.RangeActorTrenchesM, 10.0f, 200.0f, "%.0f m");
		if (auto _ttRat = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("range_actor_trenches_tooltip"), "How far from the camera NPCs, creatures and corpses still carve trenches. Past it they leave no prints, and the work of following their bodies is skipped. You always carve. It never reaches past the Trenches range. Lower saves CPU time where many people or animals are on the move. Applies live."));

		ImGui::SliderFloat(T(TKEY("object_raster_reach"), "Object Height Map Range"), &settings.ObjectRasterReachM, 29.0f, 234.0f, "%.0f m");
		if (auto _ttOrr = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("object_raster_reach_tooltip"), "How far around you the top-down height map of the objects reaches. Inside it, snow thins under roofs, bridges and archways, and Volume Snow finds what shelters it. Past it there is no shelter under cover. The map is a fixed 2048 texels however far it reaches, so this trades detail against range: a texel is about 7 world units at the default 100 m, 4 at 58 m. Costs nothing extra to raise; it coarsens the map near you instead."));

		if (distantChanged)
			shellDataDirty.store(true, std::memory_order_release);

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx(T(TKEY("snow_refill"), "Snow Refill"), ImGuiTreeNodeFlags_Framed)) {
		if (auto _ttRefillTree = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("snow_refill_tooltip"), "How compressed snow recovers, and how deep the layer grows while it snows."));
		ImGui::Checkbox(T(TKEY("refill_only_snowing"), "Refill Only While Snowing"), &settings.RefillOnlyWhenSnowing);
		if (auto _ttRefillSnow = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("refill_only_snowing_tooltip"), "Compressed snow only recovers while the current weather is snowing, faster in denser snowfall. Trails and trenches persist through clear weather. Off: snow recovers at the baseline rate in any weather."));

		ImGui::SliderFloat(T(TKEY("refill_rate"), "Snow Refill Rate"), &settings.RefillRateMultiplier, 0.0f, 10.0f, "%.1fx");
		if (auto _ttRefill = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("refill_rate_tooltip"), "Multiplier on the snowfall-driven refill rate. At 1.0x, typical snowfall recovers compressed snow in about 12 minutes. 0 disables refilling."));

		ImGui::SeparatorText(T(TKEY("snow_accumulation"), "Snow Accumulation"));
		if (auto _ttAccumCat = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("snow_accumulation_tooltip"), "The snow layer deepens while it snows and settles back when it stops, so a long storm leaves the world deeper than it found it. Landscape snow only."));

		ImGui::Checkbox(T(TKEY("enable_accumulation"), "Snow Accumulation"), &settings.EnableSnowAccumulation);
		if (auto _ttAccumEnable = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("enable_accumulation_tooltip"), "Let snowfall deepen the snow. Off holds every kind of ground at its set depth whatever the weather does, which is how the mod behaved before this existed - useful for comparing the two."));

		ImGui::SliderFloat(T(TKEY("accumulation_peak"), "Accumulation Peak"), &settings.AccumulationPeak, 1.0f, 2.0f, "%.2fx");
		if (auto _ttAccumPeak = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("accumulation_peak_tooltip"), "How deep the snow gets after a long storm, as a multiple of its normal depth. Every kind of ground grows by the same proportion, so paths and roads stay lower than the fields around them - in fact the gap between them widens as it snows, which keeps a road readable. 1.00x means snowfall never deepens anything."));

		ImGui::SliderFloat(T(TKEY("accumulation_hours"), "Accumulation Time"), &settings.AccumulationHours, 1.0f, 10.0f, "%.0f game hours");
		if (auto _ttAccumHours = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("accumulation_hours_tooltip"), "How long heavy snowfall takes to build the layer from its normal depth up to the peak. Lighter snow takes proportionally longer, so a thin flurry barely moves it."));

		ImGui::SliderFloat(T(TKEY("accumulation_melt_hours"), "Melt Time"), &settings.AccumulationMeltHours, 1.0f, 10.0f, "%.0f game hours");
		if (auto _ttAccumMelt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("accumulation_melt_hours_tooltip"), "How long clear weather takes to settle the layer back down from the peak. By default the same as Accumulation Time; set it longer and a snowy stretch stays deep between storms."));

		ImGui::SliderFloat(T(TKEY("accumulation_fade"), "Accumulated Snow Fade"), &settings.AccumulationFadeDays, 0.0f, 14.0f, "%.0f days");
		if (auto _ttAccumFade = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("accumulation_fade_tooltip"), "A slow settling that runs in all weather, snowfall included, so the world always finds its way back to its normal depth instead of climbing for ever through an endless winter. Because it never stops, it also makes clear weather settle somewhat faster than Melt Time alone would. 0 turns it off and leaves clear weather as the only thing that brings the snow back down."));

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx(T(TKEY("undulation"), "Snow Undulation"), ImGuiTreeNodeFlags_Framed)) {
		if (auto _ttUnd = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("undulation_tooltip"), "Wind-worked waves in deep snow. They fade out automatically over thin cover, class borders and carved trench floors."));
		ImGui::SliderFloat(T(TKEY("undulation_strength"), "Undulation Strength"), &settings.UndulationStrength, 0.0f, 8.0f, "%.1f units");
		if (auto _ttUs = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("undulation_strength_tooltip"), "Wave height. 0 flattens deep snow into a smooth sheet."));

		ImGui::SliderFloat(T(TKEY("undulation_spacing"), "Undulation Spacing"), &settings.UndulationSpacing, 0.5f, 4.0f, "%.1fx");
		if (auto _ttUsp = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("undulation_spacing_tooltip"), "Stretches the wave pattern: larger = broader, calmer dunes instead of a spike carpet."));

		ImGui::SliderFloat(T(TKEY("parallax_depth"), "Parallax Depth"), &settings.ParallaxDepth, 0.0f, 2.0f, "%.2fx");
		if (auto _ttPd = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("parallax_depth_tooltip"), "Parallax occlusion mapping on the landscape shell: marches the view ray through the snow texture's displacement map and shades from where it hits, so grain occludes grain and the surface reads as thick instead of merely lit. It moves no vertices, and the depth it resolves is by construction the depth of the grain being drawn. A multiplier on the PBR config's displacementScale - 1.0 is exactly the slab depth PBR ground gets. 0 skips the march. It also sets how strongly the trench detail shades itself from the sun: 0 turns that off, 1 or more gives it in full."));

		ImGui::SliderFloat(T(TKEY("parallax_shadow_strength"), "Parallax Shadow"), &settings.ParallaxShadowStrength, 0.0f, 2.0f, "%.2fx");
		if (auto _ttPss = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("parallax_shadow_strength_tooltip"), "Self-shadowing of the snow's own grain, the same term PBR ground receives from Extended Materials: four taps along the sun through the displacement map, so the micro-relief casts into itself under low sun instead of reading flat. Needs the PBR snow set's _p map. 0 skips the taps entirely (and is the A/B for their cost). It scales the trench detail's own sun shadow the same way: 0 turns that off too."));

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx(T(TKEY("class_depths"), "Snow Depth by Texture Class"), ImGuiTreeNodeFlags_Framed)) {
		if (auto _ttClasses = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("class_depths_tooltip"), "Starting height for each snow texture family (classified by the vanilla LTEX filenames every retexture mod overrides). This is the DEFAULT a texture uses until it is given its own value below. Negative values submerge the shell below the surface. Retunes live from cached data."));
		ImGui::SeparatorText(T(TKEY("roads_group"), "Roads"));
		ImGui::Checkbox(T(TKEY("city_shells"), "Landscape Snow In Cities"), &settings.CityShells);
		if (auto _ttCity = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("city_shells_tooltip"), "Draws the landscape shell inside the walled cities (their own worldspaces). Off, cities keep object snow only; the ground there stays as the game draws it."));

		ImGui::SeparatorText(T(TKEY("landscape_class_group"), "Landscape Texture Class"));
		bool classDepthsChanged = false;
		for (uint32_t classI = 0; classI < kSnowClassCount; ++classI)
			classDepthsChanged |= ImGui::SliderFloat(kSnowClasses[classI].label, &settings.SnowClassDepths[classI], -20.0f, 64.0f, "%.0f units");
		if (classDepthsChanged) {
			RefreshLandTextureDepths();
			shellDataDirty.store(true, std::memory_order_release);
		}
		ImGui::SliderFloat(T(TKEY("slope_drape"), "Slope Drape"), &settings.SlopeDrape, 0.0f, 1.0f, "%.2f");
		if (auto _ttDrape = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("slope_drape_tooltip"), "How the ground snow stands off steep ground. At 0 it is always lifted straight up, so on a near-vertical face the cover thins to almost nothing and the rock shows through in diagonal gaps. At 1 the lift turns toward the surface as the ground steepens (from 30 degrees, fully by 60), so the layer keeps its full depth in front of the face - the way snow plasters a mountainside rather than only sitting on top of it. Snow on objects is not affected."));
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx(T(TKEY("texture_depths"), "Snow Depth by Landscape Texture"), ImGuiTreeNodeFlags_Framed)) {
		if (auto _ttTextures = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("texture_depths_tooltip"), "Every landscape texture the game has loaded this session, each with its own shell height. A texture starts at its family's default above; moving it here pins it to its own value, saved by texture path so load order cannot rebind it. Use this to give snow to textures the families lump in with bare ground (snowy stone, frozen marsh), or to hold one texture back."));

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
					kSnowClasses[entry.classIndex].label, settings.SnowClassDepths[entry.classIndex]);
				if (entry.overridden)
					ImGui::Text("%s", T(TKEY("texture_pinned"), "Pinned to its own value; Reset hands it back."));
				else if (entry.shipped)
					ImGui::Text("%s", T(TKEY("texture_shipped"), "Ships with its own default, so the family slider does not reach it."));
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

	if (ImGui::TreeNodeEx(T(TKEY("snow_trenches"), "Snow Trenches"), ImGuiTreeNodeFlags_Framed)) {
		if (auto _ttTd = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("snow_trenches_tooltip"), "Everything about the ground being walked through: how long a trench is remembered, how wide and how sharply it cuts, and the look of the disturbed snow around it. Untouched snow is never affected."));

		{
			static const uint kMapDims[] = { 1024u, 2048u, 4096u };
			int dimIndex = settings.DeformMapResolution <= 1024u ? 0 : (settings.DeformMapResolution >= 4096u ? 2 : 1);
			if (ImGui::Combo(T(TKEY("map_resolution"), "Deformation Map Resolution"), &dimIndex, "1024\0" "2048\0" "4096\0")) {
				settings.DeformMapResolution = kMapDims[dimIndex];
				deformMapDimDirty = true;
			}
			if (auto _ttRes = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T(TKEY("map_resolution_tooltip"), "Resolution of the map every trench, melt mark and berm lives in. The main performance dial for this section: cost scales with the square of it, so each step down roughly quarters the trench passes' GPU time. Detail follows the Trenches range too - at the default range, 2048 gives roughly 12 cm per texel, 1024 roughly 24 cm (footprints soften but trails stay). 4096 halves the texel again and costs roughly four times 2048's trench-pass time and video memory; the High preset uses it. Changing it clears the map; remembered trenches are re-injected from the store."));
			if (ImGui::Checkbox(T(TKEY("vram_aware_map"), "VRAM-Aware Map Size"), &settings.VRAMAwareTrenchMap) && !settings.VRAMAwareTrenchMap && deformMapVramCap) {
				deformMapVramCap = 0;
				deformMapDimDirty = true;
			}
			if (auto _ttVram = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T(TKEY("vram_aware_map_tooltip"), "When the graphics card's free video memory budget gets smaller than what 4096 costs over 2048 (about 190 MB), the map runs at 2048 for the rest of the session: past the budget the driver moves textures to system memory and frames stall. The resolution above keeps its value. The map clears once; remembered trenches are re-injected from the store."));
			if (deformMapVramCap && deformMapDim < settings.DeformMapResolution)
				ImGui::TextDisabled("%s %u", T(TKEY("vram_aware_map_active"), "Video memory over budget this session, map running at"), deformMapDim);
		}

		ImGui::Checkbox(T(TKEY("tessellation"), "Tessellate Trenches"), &settings.Tessellation);
		if (auto _ttTess = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("tessellation_tooltip"), "Adds vertex density to the ground snow near the camera, keyed off the deformation map, so carves resolve as smooth walls instead of following the ground's own triangles. This is what trench smoothness actually depends on. Off leaves every trench as angular as the triangles beneath it."));

		ImGui::SeparatorText(T(TKEY("trench_detail_group"), "Trench Detail"));
		if (auto _ttDetail = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("trench_detail_group_tooltip"), "The shape and surface of disturbed snow: the raised berm along trench edges, the broken snow in and around trenches and the broken rim."));

		ImGui::SliderFloat(T(TKEY("berm_height"), "Berm Height"), &settings.BermHeight, 0.0f, 1.0f, "%.2fx");
		if (auto _ttBh = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("berm_height_tooltip"), "Height of the pushed-aside snow ridge along trench edges, as a fraction of the local snow depth. 0 removes the berm."));

		ImGui::SliderFloat(T(TKEY("berm_steepness"), "Berm Steepness"), &settings.BermSteepness, 0.0f, 2.0f, "%.2f");
		if (auto _ttBs = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("berm_steepness_tooltip"), "How tightly the berm hugs the trench, at the same crest height. 1.00 = the default apron, reaching about 40 units out; 0 spreads it twice as wide and gentle, 2 packs it into half the width - a steep ridge."));

		ImGui::SliderFloat(T(TKEY("trench_detail_relief"), "Detail Relief"), &settings.TrenchDetailRelief, 0.0f, 2.0f, "%.2fx");
		if (auto _ttTdRelief = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("trench_detail_relief_tooltip"), "Height of the broken snow in and around trenches. It is drawn with parallax occlusion: each pixel follows its view ray into the clods, crumbs and slabs, so nearer ones hide what lies behind them and shade it from the sun, while the snow keeps your snow texture's colour, sparkle and lighting. It eases off on thin snow and on trampled floors (Flattening). Disturbed snow wears the powder detail in every weather; where something hits it hard - a sprint, a dodge, a landing, a body knocked down, lightning, a Force shout - it turns to the impact detail, the harder the more, and rises up to 1.5x. Snow fire has melted wears the impact detail too. 1 = as authored, 0 = off."));

		ImGui::SliderFloat(T(TKEY("trench_detail_roughness"), "Detail Roughness"), &settings.TrenchDetailRoughness, 0.3f, 1.5f, "%.2fx");
		if (auto _ttTdRough = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("trench_detail_roughness_tooltip"), "Snow roughness where the detail applies, as a multiplier. Lower is glossier, for wet snow; 1 leaves the snow set's own roughness."));

		ImGui::SliderFloat(T(TKEY("trench_detail_coverage"), "Detail Coverage"), &settings.TrenchDetailCoverage, 0.0f, 2.5f, "%.2f");
		if (auto _ttTdCover = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("trench_detail_coverage_tooltip"), "How far across a trench the detail reaches. 0 = the trench floor only, 1 = the floor and walls to just past the rim, 2 = out over the berm, 2.5 = the berm's whole apron. The edge fades softly at every setting and every snow depth."));

		ImGui::SliderFloat(T(TKEY("impact_reach"), "Impact Reach"), &settings.ImpactReach, 0.5f, 4.0f, "%.2fx");
		if (auto _ttImpactReach = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("impact_reach_tooltip"), "How far the impact detail spreads around a hard hit - a sprint, a dodge, a landing, a body knocked down - as a share of what hit the snow. At 1 a foot's impact covers its own print and a thin ring around it, and a body knocked down an area about its own size; 2 is twice as wide. The snow it throws is Speed Spoil's. Applies to new impacts."));

		ImGui::Checkbox(T(TKEY("speed_spoil"), "Speed Spoil"), &settings.SpeedSpoil);
		if (auto _ttSpoil = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("speed_spoil_tooltip"), "A foot leaving its print faster than a run throws snow back from it in a wedge - the faster, and the heavier the creature, the longer and stronger - and the impact detail shows that thrown snow, past the trench too, until snowfall buries it. It carves nothing; it shows which way someone was running."));
		if (settings.SpeedSpoil) {
			ImGui::SliderFloat(T(TKEY("speed_spoil_size"), "Spoil Size"), &settings.SpeedSpoilSize, 0.1f, 2.0f, "%.2fx");
			if (auto _ttSpoilSize = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T(TKEY("speed_spoil_size_tooltip"), "How far the thrown snow reaches, length and width together. At 1 a sprint throws it about a metre back from each print; 0.5 is half that. Applies to new wedges."));
			ImGui::TextDisabled("%s %llu", T(TKEY("speed_spoil_count"), "Wedges thrown since load:"), static_cast<unsigned long long>(speedSpoilWedges));
		}

		if (trenchDetailAttempted && !trenchDetailNormalSRV)
			ImGui::TextDisabled("%s", T(TKEY("trench_detail_missing"), "Trench detail textures not found: Shaders\\SnowDeformation\\TrenchDetailN.dds and TrenchDetailH.dds."));

		ImGui::SliderFloat(T(TKEY("rim_lip"), "Rim Lip"), &settings.RimLip, 0.0f, 0.3f, "%.2f");
		if (auto _ttLip = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("rim_lip_tooltip"), "The trench rim rolls UP slightly before it drops - the cornice look of cut snow. Height as a fraction of local snow depth; deep snow only (shallow dimples stay smooth). 0 = off."));

		ImGui::SliderFloat(T(TKEY("rim_teeth"), "Rim Teeth"), &settings.RimTeeth, 0.0f, 1.0f, "%.2f");
		if (auto _ttTeeth = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("rim_teeth_tooltip"), "Breaks the trench edge into irregular teeth and blocks instead of a clean curve, from a ragged noise fixed to the ground. Deep snow only. Too high eats the trench's readable width - back off if trails start looking chewed. 0 = off."));

		ImGui::SeparatorText(T(TKEY("snow_condition_group"), "Snow Condition"));

		ImGui::SliderFloat(T(TKEY("snow_resistance"), "Snow Resistance"), &settings.SnowResistance, 0.0f, 100.0f, "%.0f %%");
		if (auto _ttResist = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("snow_resistance_tooltip"), "How much of a body's push the snow holds back. Feet stand on the ground beneath the snow, so at 0% every step cuts to the bottom and anything low - a long skirt, a hand, a blade - cuts as deep as it reaches. At 50% one pass carves half of that: legs sink into the snow and the trench behind stays shallow. Every new pass over the same snow carves the same share of what is left, so busy paths sink deeper the more they are walked. Dropped items, spells and melt keep their own depths. Affects new trenches only."));

		ImGui::SeparatorText(T(TKEY("trampled_group"), "Trampled Ground"));

		ImGui::SliderFloat(T(TKEY("trampled_flattening"), "Flattening"), &settings.TrampledFlattening, 0.0f, 1.0f, "%.2fx");
		if (auto _ttTrampleFlat = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("trampled_flattening_tooltip"), "Snow walked over again and again is packed down, and the broken snow on the trench floor is walked flatter: this multiplies Detail Relief on floors deeper than one pass leaves them, easing in the deeper the traffic has packed them. At 0.50x a Detail Relief of 1 becomes 0.5. The berms and a trail walked once are never touched. 1.00x = off."));

		ImGui::PushID("snow_trenches");
		if (ImGui::TreeNodeEx(T(TKEY("menu_advanced"), "Advanced"))) {
			ImGui::SliderFloat(T(TKEY("snow_slumping"), "Snow Slumping"), &settings.SlumpRate, 0.0f, 1.0f, "%.2f");
			if (auto _ttSlump = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T(TKEY("snow_slumping_tooltip"), "Snow dug away on BOTH sides loses its support and settles about halfway into a low uneven bump, so heavy traffic reads as one churned channel instead of a comb of full-height fins. Trench walls and open snow never move. 0 = off; higher = faster settling."));

			ImGui::Checkbox(T(TKEY("no_carve_floating"), "Floating Actors Leave No Trench"), &settings.NoCarveFloatingActors);
			if (auto _ttFloat = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T(TKEY("no_carve_floating_tooltip"), "Far field only. Inside the contact window (about 44 m) an actor carves by its render mesh, and anything that hovers reaches no snow by construction; this gate covers the bone path beyond it. Stops things that never touch the ground from digging it: atronachs, wisps, ghosts, anything that hovers. Nothing is named - an actor is judged by whether its own lowest part ever comes down to its footing, so modded levitators are covered too."));

			ImGui::SliderFloat(T(TKEY("floating_band"), "Floating Actor Clearance"), &settings.FloatingActorBand, 4.0f, 80.0f, "%.0f units");
			if (auto _ttFloatBand = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T(TKEY("floating_band_tooltip"), "Far field only (the bone path beyond the contact window, about 44 m). How far an actor's lowest part may sit above its footing and still count as standing on it. Lower values catch things that only just hover, at the risk of dropping a normal creature's tracks mid-stride."));

			{
				std::string incorporealModes;
				incorporealModes += T(TKEY("incorporeal_off"), "Off");
				incorporealModes += '\0';
				incorporealModes += T(TKEY("incorporeal_translucent"), "See-through bodies");
				incorporealModes += '\0';
				incorporealModes += T(TKEY("incorporeal_flag"), "Actors marked Ghost");
				incorporealModes += '\0';
				incorporealModes += T(TKEY("incorporeal_either"), "Either");
				incorporealModes += '\0';
				ImGui::Combo(T(TKEY("incorporeal_mode"), "Ghosts Leave No Trench"), &settings.IncorporealMode, incorporealModes.c_str());
			}
			if (auto _ttIncorp = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T(TKEY("incorporeal_mode_tooltip"), "Stops things with no substance from digging the snow. This is a separate question from hovering, and has to be: a ghost stands with its feet on the ground like the Nord it otherwise is, so no clearance measurement will ever catch one. See-through bodies judges an actor by whether it is drawn solid, which needs no list and covers modded ghosts; actors marked Ghost reads the flag on the record instead, which never misses a ghost but also catches anything the game made unkillable rather than incorporeal."));

			ImGui::TreePop();
		}
		ImGui::PopID();

		ImGui::TreePop();
	}
}
