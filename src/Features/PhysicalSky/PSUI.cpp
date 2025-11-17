#include "../PhysicalSky.h"

#include "PSCommon.h"

#include "Util.h"

#include <imgui_stdlib.h>

void OrbitEdit(Orbit& orbit)
{
	ImGui::SliderAngle("Azimuth", &orbit.azimuth, -180, 180, "%.1f deg");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Clockwise orientation of where sun rise/set. 0 is east-west.");
	ImGui::SliderAngle("Zenith", &orbit.zenith, -90, 90, "%.1f deg");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(
			"Inclination of the orbit. At 0 azimuth, positive values tilt the midday sun northward.\n"
			"On an earth-like planet, this is the latitude, positive in the south, negative in the north.");
	ImGui::SliderAngle("Drift", &orbit.drift, -90, 90, "%.1f deg");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(
			"Offset of the whole orbit. At 0 azimuth, positive values move the orbit northward.\n"
			"On an earth-like planet, this is the tilt of its axis, positive in summer, negative in winter.");
}

void TrajectoryEdit(Trajectory& traj)
{
	if (ImGui::TreeNodeEx("Orbit A (Winter)", ImGuiTreeNodeFlags_DefaultOpen)) {
		OrbitEdit(traj.minima);
		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx("Orbit B (Summer)", ImGuiTreeNodeFlags_DefaultOpen)) {
		OrbitEdit(traj.maxima);
		ImGui::TreePop();
	}

	ImGui::InputFloat("Orbital Period", &traj.period_orbital, 0, 0, "%.3f day(s)");
	traj.period_orbital = std::max(1e-6f, traj.period_orbital);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("The time it takes to go one circle.");
	ImGui::SliderFloat("Orbital Period Offset", &traj.offset_orbital, -traj.period_orbital, traj.period_orbital, "%.3f day(s)");

	ImGui::InputFloat("Drift Period", &traj.period_long), 0, 0, "%.1f day(s)";
	traj.period_long = std::max(1e-6f, traj.period_long);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("The time it takes for the orbit to drift from A to B and back.");
	ImGui::SliderFloat("Drift Period Offset", &traj.offset_long, -traj.period_long, traj.period_long, "%.1f day(s)");
}

void TextureCombo(const char* label, std::string& target, const TextureManager& manager)
{
	auto curr_path = manager.Query(target) ? target : "Choose texture..."s;
	if (ImGui::BeginCombo(label, curr_path.c_str())) {
		for (auto& [tex, srv] : manager.tex_list) {
			if (!srv.get())
				continue;
			bool is_selected = (target == tex);
			if (ImGui::Selectable(tex.c_str(), is_selected))
				target = tex;
			if (is_selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
}

void CloudLayerEdit(CloudLayerSettings& cloud)
{
	ImGui::SeparatorText("Placement");

	ImGui::SliderFloat("Layer Bottom", &cloud.layer.bottom, 0.f, 2.f, "%.2f km");
	ImGui::SliderFloat("Layer Thickness", &cloud.layer.thickness, 0.05f, 2.f, "%.2f km");

	ImGui::SeparatorText("Optics");

	ImGui::ColorEdit3("Scatter", &cloud.layer.scatter.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
	ImGui::ColorEdit3("Absorption", &cloud.layer.absorption.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);

	ImGui::SeparatorText("Composition");

	ImGui::SliderFloat2("NDF Scale", &cloud.layer.ndf_scale_or_freq.x, 1.f, 50.f, "%.2f km");

	ImGui::SliderFloat("Noise Scale", &cloud.layer.noise_scale_or_freq, 0.01f, 5.f, "%.3f km");
	ImGui::SliderFloat3("Noise Velocity", &cloud.layer.noise_offset_or_speed.x, -30.f, 30.f, "%.3f m/s");

	ImGui::SliderFloat("Post Power", &cloud.layer.power, 0.2f, 5, "%.3f");

	ImGui::SeparatorText("Lighting");

	ImGui::SliderFloat("Average Density", &cloud.layer.average_density, 0.f, 0.1f, "%.3f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("For approximating shadowing on far away clouds.");

	ImGui::SliderFloat("Multiscatter Mult", &cloud.layer.ms_mult, 0.1f, 10.f, "%.2f");
	ImGui::SliderFloat("Multiscatter Transmittance Power", &cloud.layer.ms_transmittance_power, 0.1f, 1.f, "%.2f");
	ImGui::SliderFloat("Multiscatter Altitude Power", &cloud.layer.ms_height_power, 0.2f, 5.f, "%.2f");

	ImGui::SliderFloat("Ambient Strength", &cloud.layer.ambient_mult, 0.f, 5.f);
}

void PhysicalSky::DrawSettings()
{
	if (ImGui::BeginTabBar("##PHYSSKY")) {
		if (ImGui::BeginTabItem("General")) {
			SettingsGeneral();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Worldspace")) {
			SettingsWorldspace();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Lighting")) {
			SettingsLighting();
			ImGui::EndTabItem();
		}

		// if (ImGui::BeginTabItem("Clouds")) {
		// 	SettingsClouds();
		// 	ImGui::EndTabItem();
		// }

		if (ImGui::BeginTabItem("Celestials")) {
			SettingsCelestials();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Atmosphere")) {
			SettingsAtmosphere();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Volumetric Layers")) {
			SettingsLayers();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Textures")) {
			SettingsTextures();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Debug")) {
			SettingsDebug();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}
}

void PhysicalSky::SettingsGeneral()
{
	if (!CheckComputeShaders())
		ImGui::TextColored({ 1, .1f, .1f, 1 }, "Shader compilation failed!");

	ImGui::Checkbox("Enable Physcial Sky", &settings.enable_sky);

	if (ImGui::CollapsingHeader("Performance")) {
		ImGui::DragScalar("Transmittance Steps", ImGuiDataType_U32, &settings.transmittance_step);
		ImGui::DragScalar("Multiscatter Steps", ImGuiDataType_U32, &settings.multiscatter_step);
		ImGui::DragScalar("Multiscatter Sqrt Samples", ImGuiDataType_U32, &settings.multiscatter_sqrt_samples);
		ImGui::DragScalar("Sky View Steps", ImGuiDataType_U32, &settings.skyview_step);
		ImGui::SliderFloat("Aerial Perspective Max Dist", &settings.aerial_perspective_max_dist, 0, settings.atmos_thickness, "%.3f km");
		ImGui::SliderFloat("Shadow Volume Range", &settings.shadow_volume_range, 0, 16, "%.1f km");

		ImGui::SliderFloat("Ray March Range", &settings.ray_march_range, 0, 64, "%.1f km");
		uint min_step = 1, max_step = 120;
		ImGui::SliderScalar("Fog Max Steps", ImGuiDataType_U32, &settings.fog_max_step, &min_step, &max_step);
		ImGui::SliderScalar("Cloud Max Steps", ImGuiDataType_U32, &settings.cloud_max_step, &min_step, &max_step);
	}

	if (ImGui::CollapsingHeader("Scale")) {
		ImGui::SliderFloat("Planet Radius", &settings.planet_radius, 0.f, 1e4f, "%.1f km");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("The supposed radius of the planet Nirn, or whatever rock you are on.");
		ImGui::SliderFloat("Atmosphere Thickness", &settings.atmos_thickness, 0.f, 200.f, "%.1f km");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("The thickness of atmosphere that contributes to lighting.");
	}
}

void PhysicalSky::SettingsWorldspace()
{
	ImGui::TextWrapped("Register worldspaces that has physical sky enabled.");

	if (ImGui::BeginTable("Worldspace List", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
		ImGui::TableSetupColumn("Editor ID");
		ImGui::TableSetupColumn("Bottom Z");
		ImGui::TableSetupColumn("Action");
		ImGui::TableHeadersRow();

		int i = 0;
		std::optional<int> mark_delete = std::nullopt;
		for (auto& worldspace : settings.worldspace_whitelist) {
			ImGui::PushID(i);

			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputText("##name", &worldspace.name);

			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputFloat("##Bottom Z", &worldspace.bottom_z, 0, 0, "%.1f game unit");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text(
					"The lowest elevation of the worldspace you can reach. "
					"You can check it by standing at sea level and using \"getpos z\" console command.");

			ImGui::TableNextColumn();
			if (ImGui::Button("Delete"))
				mark_delete = i;

			ImGui::PopID();

			i++;
		}
		if (mark_delete.has_value())
			settings.worldspace_whitelist.erase(settings.worldspace_whitelist.begin() + mark_delete.value());

		ImGui::TableNextColumn();
		if (ImGui::Selectable("Add New Entry"))
			settings.worldspace_whitelist.push_back({});

		ImGui::EndTable();
	}
}

void PhysicalSky::SettingsLighting()
{
	ImGui::TextWrapped("How the sky is lit, as well as everything under the sun (or moon).");

	if (ImGui::CollapsingHeader("Syncing", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Override Light Direction", &settings.override_dirlight_dir);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(
				"Directional light will use directions specified by trajectories in Celestials tab.\n"
				"Needs game running to update.");
		ImGui::Indent();
		ImGui::Checkbox("Moonlight Follows Secunda", &settings.moonlight_follows_secunda);
		ImGui::Unindent();

		ImGui::Checkbox("Override Light Color", &settings.override_dirlight_color);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(
				"The physical sky is lit differently from vanilla objects, because it needs values from outer space and is much brighter. "
				"With this on, everything will be lit by colors specified below.");
	}

	if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::ColorEdit3("Sunlight Color", &settings.sunlight_color.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);

		ImGui::BeginDisabled(settings.phase_dep_moonlight);
		ImGui::ColorEdit3("Moonlight Color", &settings.moonlight_color.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		ImGui::EndDisabled();

		ImGui::Checkbox("Moon Phase Affects Moonlight", &settings.phase_dep_moonlight);
		ImGui::BeginDisabled(!settings.phase_dep_moonlight);
		ImGui::Indent();
		{
			ImGui::ColorEdit3("Masser", &settings.masser_moonlight_color.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
			Util::PercentageSlider("Masser Min Brightness", &settings.masser_moonlight_min);
			ImGui::ColorEdit3("Secunda", &settings.secunda_moonlight_color.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
			Util::PercentageSlider("Secunda Min Brightness", &settings.secunda_moonlight_min);
		}
		ImGui::Unindent();
		ImGui::EndDisabled();

		ImGui::SliderAngle("Sun/Moon Transition Start", &settings.light_transition_angles.x, -30, 0);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("When the sun dips this much below the horizon, the sky will gradually transition to being lit by moonlight.");
		ImGui::SliderAngle("Sun/Moon Transition End", &settings.light_transition_angles.y, -30, 0);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("When the sun dips this much below the horizon, the sky will completely transition to being lit by moonlight.");
	}

	if (ImGui::CollapsingHeader("Misc")) {
		ImGui::BeginDisabled(settings.override_dirlight_color);
		{
			ImGui::SliderFloat("Light Transmittance Mix", &settings.dirlight_transmittance_mix, 0.f, 1.f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Applying the filtering effect of light going through the atmosphere.");
		}
		ImGui::EndDisabled();

		ImGui::ColorEdit3("Ground Albedo", &settings.ground_albedo.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_DisplayHSV);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("How much light gets reflected from the ground far away.");
			ImGui::Text("A neat chart from wiki and other sources:");
			// http://www.climatedata.info/forcing/albedo/
			if (ImGui::BeginTable("Albedo Table", 2)) {
				ImGui::TableNextColumn();
				ImGui::Text("Open ocean");
				ImGui::TableNextColumn();
				ImGui::Text("0.06");

				ImGui::TableNextColumn();
				ImGui::Text("Conifer forest, summer");
				ImGui::TableNextColumn();
				ImGui::Text("0.08 to 0.15");

				ImGui::TableNextColumn();
				ImGui::Text("Deciduous forest");
				ImGui::TableNextColumn();
				ImGui::Text("0.15 to 0.18");

				ImGui::TableNextColumn();
				ImGui::Text("Bare soil");
				ImGui::TableNextColumn();
				ImGui::Text("0.17");

				ImGui::TableNextColumn();
				ImGui::Text("Tundra");
				ImGui::TableNextColumn();
				ImGui::Text("0.20");

				ImGui::TableNextColumn();
				ImGui::Text("Green grass");
				ImGui::TableNextColumn();
				ImGui::Text("0.25");

				ImGui::TableNextColumn();
				ImGui::Text("Desert sand");
				ImGui::TableNextColumn();
				ImGui::Text("0.40");

				ImGui::TableNextColumn();
				ImGui::Text("Old/melting snow");
				ImGui::TableNextColumn();
				ImGui::Text("0.40 to 0.80");

				ImGui::TableNextColumn();
				ImGui::Text("Ocean ice");
				ImGui::TableNextColumn();
				ImGui::Text("0.50 to 0.70");

				ImGui::TableNextColumn();
				ImGui::Text("Fresh snow");
				ImGui::TableNextColumn();
				ImGui::Text("0.80");

				ImGui::EndTable();
			}
		}
	}
}

void PhysicalSky::SettingsClouds()
{
	ImGui::TextWrapped("Little fluffy clouds.");

	if (ImGui::CollapsingHeader("Vanilla Clouds")) {
		ImGui::Checkbox("Enable Vanilla Clouds", &settings.enable_vanilla_clouds);

		if (!(settings.enable_vanilla_clouds && settings.override_dirlight_color)) {
			ImGui::TextDisabled("Below options require Override Light Color.");
			ImGui::BeginDisabled();
		}

		ImGui::SliderFloat("Height", &settings.cloud_height, 0.f, 20.f, "%.2f km");
		ImGui::SliderFloat("Saturation", &settings.cloud_saturation, 0.f, 1.f, "%.2f");
		ImGui::SliderFloat("Brightness", &settings.cloud_mult, 0.f, 2.f, "%.2f");
		ImGui::SliderFloat("Atmosphere Scattering", &settings.cloud_atmos_scatter, 0.f, 5.f, "%.2f");

		if (ImGui::TreeNodeEx("Phase Function", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SliderFloat("Forward Asymmetry", &settings.cloud_phase_g0, 0, 1, "%.2f");
			ImGui::SliderFloat("Backward Asymmetry", &settings.cloud_phase_g1, -1, 0, "%.2f");
			ImGui::SliderFloat("Backward Weight", &settings.cloud_phase_w, 0, 1, "%.2f");
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Scattering Heuristics", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SliderFloat("Alpha", &settings.cloud_alpha_heuristics, -.5, 1, "%.2f");
			ImGui::SliderFloat("Value", &settings.cloud_color_heuristics, -.5, 1, "%.2f");
			ImGui::TreePop();
		}

		if (!(settings.enable_vanilla_clouds && settings.override_dirlight_color))
			ImGui::EndDisabled();
	}
}

void PhysicalSky::SettingsCelestials()
{
	auto& celestials = settings.celestials;

	ImGui::TextWrapped("How celestials look and move.");

	if (ImGui::CollapsingHeader("Sun Disc", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::PushID("Sun");
		ImGui::ColorEdit3("Color", &celestials.sun_disc_color.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		ImGui::SliderAngle("Angular Radius", &celestials.sun_angular_radius, 0.05f, 5, "%.2f deg", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Ours is quite small (only 0.25 deg)");

		ImGui::Checkbox("Custom Trajectory", &celestials.override_sun_traj);
		if (ImGui::TreeNodeEx("Trajectory")) {
			if (!celestials.override_sun_traj)
				ImGui::BeginDisabled();

			TrajectoryEdit(celestials.sun_trajectory);

			if (!celestials.override_sun_traj)
				ImGui::EndDisabled();

			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	if (ImGui::CollapsingHeader("Masser", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::PushID("Masser");
		ImGui::SliderFloat("Brightness", &celestials.masser_brightness, 0.f, 10.f, "%.2f");
		ImGui::SliderAngle("Angular Radius", &celestials.masser_angular_radius, 0.05f, 30, "%.2f deg", ImGuiSliderFlags_AlwaysClamp);

		ImGui::Checkbox("Custom Trajectory", &celestials.override_masser_traj);
		if (ImGui::TreeNodeEx("Trajectory")) {
			if (!celestials.override_masser_traj)
				ImGui::BeginDisabled();

			TrajectoryEdit(celestials.masser_trajectory);

			if (!celestials.override_masser_traj)
				ImGui::EndDisabled();

			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	if (ImGui::CollapsingHeader("Secunda", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::PushID("Secunda");
		ImGui::SliderFloat("Brightness", &celestials.secunda_brightness, 0.f, 10.f, "%.2f");
		ImGui::SliderAngle("Angular Radius", &celestials.secunda_angular_radius, 0.05f, 30, "%.2f deg", ImGuiSliderFlags_AlwaysClamp);

		ImGui::Checkbox("Custom Trajectory", &celestials.override_secunda_traj);
		if (ImGui::TreeNodeEx("Trajectory")) {
			if (!celestials.override_masser_traj)
				ImGui::BeginDisabled();

			TrajectoryEdit(celestials.secunda_trajectory);

			if (!celestials.override_masser_traj)
				ImGui::EndDisabled();

			ImGui::TreePop();
		}
		ImGui::PopID();
	}
}

void PhysicalSky::SettingsAtmosphere()
{
	ImGui::TextWrapped("The composition and physical properties of the atmosphere.");

	ImGui::SliderFloat("Atmosphere Thickness", &settings.atmos_thickness, 0.f, 200.f, "%.1f km");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("The thickness of atmosphere that contributes to lighting.");

	if (ImGui::CollapsingHeader("Air Molecules (Rayleigh)")) {
		ImGui::PushID("Rayleigh");
		ImGui::TextWrapped(
			"Particles much smaller than the wavelength of light. They have almost complete symmetry in forward and backward scattering (Rayleigh Scattering). "
			"On earth, they are what makes the sky blue and, at sunset, red. Usually needs no extra change.");

		ImGui::ColorEdit3("Scatter", &settings.rayleigh_scatter.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		ImGui::ColorEdit3("Absorption", &settings.rayleigh_absorption.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Usually zero.");
		ImGui::SliderFloat("Height Decay", &settings.rayleigh_decay, 0.f, 2.f);
		ImGui::PopID();
	}

	if (ImGui::CollapsingHeader("Aerosol (Mie)")) {
		ImGui::PushID("Mie");
		ImGui::TextWrapped(
			"Solid and liquid particles greater than 1/10 of the light wavelength but not too much, like dust. Strongly anisotropic (Mie Scattering). "
			"They contributes to the aureole around bright celestial bodies.");

		ImGui::SliderFloat("Anisotropy", &settings.aerosol_phase_func_g, -1, 1);
		ImGui::ColorEdit3("Scatter", &settings.aerosol_scatter.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		ImGui::ColorEdit3("Absorption", &settings.aerosol_absorption.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Usually 1/9 of scatter coefficient. Dust/pollution is lower, fog is higher.");
		ImGui::SliderFloat("Height Decay", &settings.aerosol_decay, 0.f, 2.f);
		ImGui::PopID();
	}

	if (ImGui::CollapsingHeader("Ozone")) {
		ImGui::PushID("Ozone");
		ImGui::TextWrapped(
			"The ozone layer high up in the sky that mainly absorbs light of certain wavelength. "
			"It keeps the zenith sky blue, especially at sunrise or sunset.");

		ImGui::ColorEdit3("Absorption", &settings.ozone_absorption.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		ImGui::DragFloat("Layer Altitude", &settings.ozone_altitude, .1f, 0.f, 100.f, "%.3f km");
		ImGui::DragFloat("Layer Thickness", &settings.ozone_thickness, .1f, 0.f, 50.f, "%.3f km");
		ImGui::PopID();
	}
}

void PhysicalSky::SettingsLayers()
{
	ImGui::TextWrapped("Volumetric layers. Cloud, fog, mist, smog, toxic volcanic ash cloud, etc.");

	if (ImGui::CollapsingHeader("Fog")) {
		ImGui::ColorEdit3("Scatter", &settings.fog_scatter.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		ImGui::ColorEdit3("Absorption", &settings.fog_absorption.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		ImGui::SliderFloat("Height Decay", &settings.fog_decay, 0.1f, 30.f);
		ImGui::SliderFloat("Layer Offset", &settings.fog_bottom, 0.f, 2.f, "%.3f km");
		ImGui::SliderFloat("Layer Height", &settings.fog_thickness, 0.f, 1.f, "%.3f km");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("For optimization purposes.");
		ImGui::SliderFloat("Ambient Strength", &settings.fog_ambient_mult, 0.f, 5.f);
	}

	if (ImGui::CollapsingHeader("Vanilla Clouds")) {
		ImGui::Checkbox("Enable Vanilla Clouds", &settings.enable_vanilla_clouds);
	}

	if (ImGui::CollapsingHeader("Cloud")) {
		ImGui::PushID("Cloud");
		CloudLayerEdit(settings.cloud_layer);
		ImGui::PopID();
	}
}

void PhysicalSky::SettingsTextures()
{
	if (ImGui::Button("Load NDF Textures", { -FLT_MIN, 0 }))
		LoadNDFTextures();
	if (ndf_tex_srv && cloud_top_lut_srv && cloud_bottom_lut_srv)
		ImGui::Text("NDF Tex Status: Loaded");
	else
		ImGui::Text("NDF Tex Status: Incomplete");

	if (ImGui::TreeNodeEx("Noise Generator")) {
		constexpr auto noise_types = std::array{
			"Worley",
			"Alligator"
		};

		static std::string filename = "Data\\Textures\\PhysicalSky\\noise.dds";
		static uint type = 0;
		static float base_freq = 2.f;
		static uint octaves = 6;
		static float persistence = 0.5f;
		static float lacunarity = 2.f;
		static uint seed = 5555;
		static int state = -1;  // -1 - nothing, 0 - failed, 1 - success

		ImGui::InputText("Texture Save Path", &filename);
		ImGui::Combo("Noise Type", reinterpret_cast<int*>(&type), noise_types.data(), (int)noise_types.size());
		ImGui::SliderFloat("Base Frequency", &base_freq, 1.f, 10.f, "%.1f");
		ImGui::SliderInt("Octaves", reinterpret_cast<int*>(&octaves), 1, 10, "%d");
		ImGui::SliderFloat("Persistence", &persistence, 0.f, 1.f, "%.2f");
		ImGui::SliderFloat("Lacunarity", &lacunarity, 1.f, 4.f, "%.2f");
		ImGui::InputScalar("Seed", ImGuiDataType_U32, &seed);

		if (ImGui::Button("Generate"))
			state = GenerateNoise({ filename }, type, base_freq, octaves, persistence, lacunarity, seed);
		if (state == 0) {
			ImGui::SameLine();
			ImGui::TextColored({ 1.0, 0.2f, 0.2f, 1.0 }, "Failed!");
		} else if (state == 1) {
			ImGui::SameLine();
			ImGui::TextColored({ 0.2f, 1.0, 0.2f, 1.0 }, "Success!");
		}

		ImGui::TreePop();
	}

	// ImGui::SeparatorText("Texture List");
	// {
	// 	static std::string filename = "Data\\Textures\\PhysicalSky\\noise.dds";
	// 	ImGui::InputText("Path", &filename);
	// 	if (ImGui::Button("Add New Entry"))
	// 		noise_tex_manager.LoadTexture(filename);

	// 	if (ImGui::BeginTable("TexList", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
	// 		ImGui::TableSetupColumn("Path");
	// 		ImGui::TableSetupColumn("Status");
	// 		ImGui::TableSetupColumn("Action");
	// 		ImGui::TableHeadersRow();

	// 		int i = 0;
	// 		std::string mark_delete = {};
	// 		for (auto& [path, srv] : noise_tex_manager.tex_list) {
	// 			ImGui::PushID(i++);

	// 			ImGui::TableNextColumn();
	// 			ImGui::Text(path.c_str());

	// 			ImGui::TableNextColumn();
	// 			if (srv.get())
	// 				ImGui::Text("Loaded");
	// 			else
	// 				ImGui::TextDisabled("Not Loaded");

	// 			ImGui::TableNextColumn();
	// 			if (ImGui::Button("Reload"))
	// 				noise_tex_manager.LoadTexture(path);
	// 			ImGui::SameLine();
	// 			if (ImGui::Button("Delete"))
	// 				mark_delete = path;

	// 			ImGui::PopID();
	// 		}

	// 		if (!mark_delete.empty())
	// 			noise_tex_manager.tex_list.erase(mark_delete);

	// 		ImGui::EndTable();
	// 	}
	// }
}

void PhysicalSky::SettingsDebug()
{
	ImGui::TextWrapped("Beep Boop.");

	if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::Button("Recompile Shaders"))
			ClearShaderCache();
	}

	if (ImGui::CollapsingHeader("Info")) {
		auto accumulator = RE::BSGraphics::BSShaderAccumulator::GetCurrentAccumulator();
		auto calendar = RE::Calendar::GetSingleton();
		auto sky = RE::Sky::GetSingleton();
		auto sun = sky->sun;
		auto climate = sky->currentClimate;
		auto dir_light = skyrim_cast<RE::NiDirectionalLight*>(accumulator->GetRuntimeData().activeShadowSceneNode->GetRuntimeData().sunLight->light.get());

		RE::NiPoint3 cam_pos = { 0, 0, 0 };
		if (auto cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraRoot)
			cam_pos = cam->cameraRoot->world.translate;

		// ImGui::InputFloat("Timer", &phys_sky_sb_data.timer, 0, 0, "%.6f", ImGuiInputTextFlags_ReadOnly);

		if (calendar) {
			ImGui::SeparatorText("Calendar");

			auto game_time = calendar->GetCurrentGameTime();
			auto game_hour = calendar->GetHour();
			auto game_day = calendar->GetDay();
			auto game_month = calendar->GetMonth();
			auto day_in_year = getDayInYear();
			ImGui::Text("Game Time: %.3f", game_time);
			ImGui::SameLine();
			ImGui::Text("Hour: %.3f", game_hour);
			ImGui::SameLine();
			ImGui::Text("Day: %.3f", game_day);
			ImGui::SameLine();
			ImGui::Text("Month: %u", game_month);
			ImGui::SameLine();
			ImGui::Text("Day in Year: %.3f", day_in_year);

			if (climate) {
				auto sunrise = climate->timing.sunrise;
				auto sunset = climate->timing.sunset;
				ImGui::Text("Sunrise: %u-%u", sunrise.GetBeginTime().tm_hour, sunrise.GetEndTime().tm_hour);
				ImGui::SameLine();
				ImGui::Text("Sunset: %u-%u", sunset.GetBeginTime().tm_hour, sunset.GetEndTime().tm_hour);
				ImGui::SameLine();
			}

			auto vanilla_sun_lerp = getVanillaSunLerpFactor();
			ImGui::SameLine();
			ImGui::Text("Vanilla sun lerp: %.3f", vanilla_sun_lerp);
		}

		ImGui::SeparatorText("Celestials");
		{
			ImGui::InputFloat3("Mod Sun Direction", &phys_sky_sb_data.sun_dir.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
			if (sun) {
				auto sun_dir = sun->sunBase->world.translate - cam_pos;
				sun_dir.Unitize();
				ImGui::InputFloat3("Vanilla Sun Mesh Direction", &sun_dir.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
			}
			if (dir_light) {
				auto dirlight_dir = -dir_light->GetWorldDirection();
				ImGui::InputFloat3("Vanilla Light Direction", &dirlight_dir.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
			}

			ImGui::Text("Masser Phase: %s", magic_enum::enum_name((RE::Moon::Phase)current_moon_phases[0]).data());
			ImGui::InputFloat3("Mod Masser Direction", &phys_sky_sb_data.masser_dir.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
			ImGui::InputFloat3("Mod Masser Up", &phys_sky_sb_data.masser_upvec.x, "%.3f", ImGuiInputTextFlags_ReadOnly);

			ImGui::Text("Secunda Phase: %s", magic_enum::enum_name((RE::Moon::Phase)current_moon_phases[1]).data());
			ImGui::InputFloat3("Mod Secunda Direction", &phys_sky_sb_data.secunda_dir.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
			ImGui::InputFloat3("Mod Secunda Up", &phys_sky_sb_data.secunda_upvec.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
		}

		ImGui::SeparatorText("Textures");
		{
			ImGui::BulletText("Transmittance LUT");
			ImGui::Image((void*)(transmittance_lut->srv.get()), { s_transmittance_width, s_transmittance_height });

			ImGui::BulletText("Multiscatter LUT");
			ImGui::Image((void*)(multiscatter_lut->srv.get()), { s_multiscatter_width, s_multiscatter_height });

			ImGui::BulletText("Sky-View LUT");
			ImGui::Image((void*)(sky_view_lut->srv.get()), { s_sky_view_width, s_sky_view_height });

			ImGui::BulletText("Main View Transmittance");
			ImGui::Image((void*)(main_view_tr_tex->srv.get()), { main_view_tr_tex->desc.Width * 0.2f, main_view_tr_tex->desc.Height * 0.2f });

			ImGui::BulletText("Main View Luminance");
			ImGui::Image((void*)(main_view_lum_tex->srv.get()), { main_view_lum_tex->desc.Width * 0.2f, main_view_lum_tex->desc.Height * 0.2f });
		}
	}
}