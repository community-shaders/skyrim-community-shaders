#include "CinematicCamera.h"

#include "I18n/I18n.h"
#include "Util.h"

#include <cmath>
#include <numbers>

namespace CinematicCamera
{
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
		FilmbackSettings,
		Preset,
		SensorWidthMM,
		SensorHeightMM,
		GateFit)

	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
		LensSettings,
		FocalLengthMM,
		FNumber,
		ApertureBladeCount,
		ApertureBladeRotationDeg,
		ApertureRoundness)

	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
		FocusSettings,
		Mode,
		ManualDistanceM,
		ScreenPointUV,
		TransitionSpeed)

	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
		ExposureSettings,
		Mode,
		ISO,
		MinISO,
		MaxISO,
		FrameRate,
		ShutterAngleDeg,
		ExposureCompensationEV)

	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
		Settings,
		Enabled,
		Filmback,
		Lens,
		Focus,
		Exposure)

	// FOV write bounds; matches what the game UI accepts.
	constexpr float kMinFOVDeg = 5.0f;
	constexpr float kMaxFOVDeg = 150.0f;
	// FOV comparisons use a small epsilon so float round-trips through the
	// game never look like an external takeover.
	constexpr float kFovEpsilon = 1e-3f;

	// Reference camera the exposure delta is measured against:
	// f/2.8, ISO 100, 24 fps, 180 deg shutter. EV100 = log2(2.8^2 * 48) = 8.5573427,
	// so the default settings produce a 0 EV offset and keep the current look.
	constexpr float kReferenceEV100 = 8.5573427f;

	/////////////////////////////////////////////////////////////////////////////////
	// FocusResolver
	/////////////////////////////////////////////////////////////////////////////////

	bool FocusResolver::GetTargetLockEnabled()
	{
		return g_TDM && g_TDM->GetCurrentTarget();
	}

	bool FocusResolver::GetInDialogue()
	{
		return RE::MenuTopicManager::GetSingleton()->speaker || RE::MenuTopicManager::GetSingleton()->lastSpeaker;
	}

	// Thanks Ershin!
	RE::NiPoint3 FocusResolver::GetCameraPos()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		auto playerCamera = RE::PlayerCamera::GetSingleton();
		RE::NiPoint3 ret;

		if (playerCamera->currentState == playerCamera->GetRuntimeData().cameraStates[RE::CameraStates::kFirstPerson] ||
			playerCamera->currentState == playerCamera->GetRuntimeData().cameraStates[RE::CameraStates::kThirdPerson] ||
			playerCamera->currentState == playerCamera->GetRuntimeData().cameraStates[RE::CameraStates::kMount]) {
			RE::NiNode* root = playerCamera->cameraRoot.get();
			if (root) {
				ret.x = root->world.translate.x;
				ret.y = root->world.translate.y;
				ret.z = root->world.translate.z;
			}
		} else if (playerCamera->IsInFreeCameraMode()) {
			auto freeCameraState = static_cast<RE::FreeCameraState*>(playerCamera->currentState.get());
			ret = freeCameraState->translation;
		} else {
			RE::NiPoint3 playerPos = player->GetLookingAtLocation();

			ret.z = playerPos.z;
			ret.x = player->GetPositionX();
			ret.y = player->GetPositionY();
		}

		return ret;
	}

	RE::NiPoint3 FocusResolver::GetReferenceFocusPosition(RE::TESObjectREFR* a_ref)
	{
		RE::NiPoint3 targetPosition = a_ref->GetPosition();
		if (a_ref->GetFormType() == RE::FormType::ActorCharacter) {
			auto head = a_ref->GetNodeByName("NPC Head [Head]");
			if (head)
				targetPosition = head->world.translate;
		}
		return targetPosition;
	}

	float FocusResolver::GetDistanceToReference(RE::TESObjectREFR* a_ref)
	{
		auto* camera = RE::Main::WorldRootCamera();
		RE::NiPoint3 cameraPosition = camera ? camera->world.translate : GetCameraPos();
		return cameraPosition.GetDistance(GetReferenceFocusPosition(a_ref));
	}

	bool FocusResolver::GetReferenceFocusCoord(RE::TESObjectREFR* a_ref, float2& a_focusCoord)
	{
		auto* camera = RE::Main::WorldRootCamera();
		if (!camera)
			return false;

		float screenX = 0.0f;
		float screenY = 0.0f;
		float screenZ = 0.0f;
		if (!camera->WorldPtToScreenPt3(GetReferenceFocusPosition(a_ref), screenX, screenY, screenZ, 1e-5f) ||
			!std::isfinite(screenX) || !std::isfinite(screenY) || screenZ <= 0.0f ||
			screenX < 0.0f || screenX > 1.0f || screenY < 0.0f || screenY > 1.0f) {
			return false;
		}

		// WorldPtToScreenPt3 uses a bottom-left origin; texture UVs use a top-left origin.
		a_focusCoord = float2(screenX, 1.0f - screenY);
		return true;
	}

	RE::TESObjectREFR* FocusResolver::FindTarget(bool a_allowConsoleSelection, uint& a_currentRef)
	{
		RE::TESObjectREFR* target = nullptr;
		const auto consoleRef = RE::Console::GetSelectedRef();
		if (a_allowConsoleSelection) {
			if (consoleRef && !consoleRef->IsDisabled() && !consoleRef->IsDeleted() && consoleRef->Is3DLoaded()) {
				a_currentRef = consoleRef->formID;
				target = consoleRef.get();
			} else {
				a_currentRef = 0;
			}
		}

		if (GetTargetLockEnabled()) {
			target = g_TDM->GetCurrentTarget().get().get();
		}

		if (GetInDialogue()) {
			if (RE::MenuTopicManager::GetSingleton()->speaker) {
				target = RE::MenuTopicManager::GetSingleton()->speaker.get().get();
			} else {
				target = RE::MenuTopicManager::GetSingleton()->lastSpeaker.get().get();
			}
		}

		return target;
	}

	FocusResolver::TargetFocusResult FocusResolver::ResolveTarget(bool a_allowConsoleSelection, uint& a_currentRef)
	{
		TargetFocusResult result;

		RE::TESObjectREFR* target = FindTarget(a_allowConsoleSelection, a_currentRef);
		if (!target) {
			// Target lost: hold the last valid input.
			if (hasValidHistory) {
				result.hasTarget = true;
				result.projected = historyProjected;
				result.focusCoord = historyCoord;
				result.distanceM = historyDistanceM;
			}
			return result;
		}

		const float distanceGameUnits = GetDistanceToReference(target);
		result.hasTarget = true;
		result.distanceM = Util::Units::GameUnitsToMeters(distanceGameUnits);
		result.projected = GetReferenceFocusCoord(target, result.focusCoord);

		hasValidHistory = true;
		historyProjected = result.projected;
		historyCoord = result.focusCoord;
		historyDistanceM = result.distanceM;
		return result;
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Controller: serialization
	/////////////////////////////////////////////////////////////////////////////////

	void Controller::LoadSettings(json& o_json)
	{
		settings = o_json;
		ValidateSettings();
	}

	void Controller::SaveSettings(json& o_json)
	{
		o_json = settings;
	}

	void Controller::RestoreDefaultSettings()
	{
		settings = Settings{};
		ValidateSettings();
	}

	void Controller::ValidateSettings()
	{
		auto& fb = settings.Filmback;
		fb.Preset = std::clamp(fb.Preset, (int)FilmbackPreset::FullFrame, (int)FilmbackPreset::Custom);
		fb.GateFit = std::clamp(fb.GateFit, (int)GateFit::Horizontal, (int)GateFit::Vertical);
		// Non-custom presets own the sensor dimensions.
		switch ((FilmbackPreset)fb.Preset) {
		case FilmbackPreset::Super35:
			fb.SensorWidthMM = 24.89f;
			fb.SensorHeightMM = 18.66f;
			break;
		case FilmbackPreset::APSC:
			fb.SensorWidthMM = 23.6f;
			fb.SensorHeightMM = 15.7f;
			break;
		case FilmbackPreset::Custom:
			fb.SensorWidthMM = std::clamp(fb.SensorWidthMM, 1.0f, 100.0f);
			fb.SensorHeightMM = std::clamp(fb.SensorHeightMM, 1.0f, 100.0f);
			break;
		case FilmbackPreset::FullFrame:
		default:
			fb.SensorWidthMM = 36.0f;
			fb.SensorHeightMM = 24.0f;
			break;
		}

		auto& lens = settings.Lens;
		lens.FocalLengthMM = std::clamp(lens.FocalLengthMM, 1.0f, 300.0f);
		lens.FNumber = std::clamp(lens.FNumber, 0.7f, 32.0f);
		lens.ApertureBladeCount = std::clamp(lens.ApertureBladeCount, 4, 10);
		lens.ApertureBladeRotationDeg = std::clamp(lens.ApertureBladeRotationDeg, 0.0f, 360.0f);
		lens.ApertureRoundness = std::clamp(lens.ApertureRoundness, 0.0f, 1.0f);

		auto& focus = settings.Focus;
		focus.Mode = std::clamp(focus.Mode, (int)FocusMode::Manual, (int)FocusMode::Target);
		focus.ManualDistanceM = std::clamp(focus.ManualDistanceM, 0.01f, 10000.0f);
		focus.ScreenPointUV.x = std::clamp(focus.ScreenPointUV.x, 0.0f, 1.0f);
		focus.ScreenPointUV.y = std::clamp(focus.ScreenPointUV.y, 0.0f, 1.0f);
		focus.TransitionSpeed = std::clamp(focus.TransitionSpeed, 0.1f, 1.0f);

		auto& exp = settings.Exposure;
		exp.Mode = std::clamp(exp.Mode, (int)ExposureMode::AutoISO, (int)ExposureMode::Manual);
		exp.ISO = std::clamp(exp.ISO, 25.0f, 12800.0f);
		exp.MinISO = std::clamp(exp.MinISO, 25.0f, 12800.0f);
		exp.MaxISO = std::clamp(exp.MaxISO, exp.MinISO, 12800.0f);
		exp.FrameRate = std::clamp(exp.FrameRate, 1.0f, 240.0f);
		exp.ShutterAngleDeg = std::clamp(exp.ShutterAngleDeg, 1.0f, 360.0f);
		exp.ExposureCompensationEV = std::clamp(exp.ExposureCompensationEV, -5.0f, 5.0f);
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Controller: per-frame state
	/////////////////////////////////////////////////////////////////////////////////

	PhysicalCameraState Controller::BuildState(float viewportAspect) const
	{
		PhysicalCameraState s{};

		const auto& fb = settings.Filmback;
		const auto& lens = settings.Lens;
		const auto& focus = settings.Focus;
		const auto& exp = settings.Exposure;

		s.SensorWidthMM = fb.SensorWidthMM;
		s.SensorHeightMM = fb.SensorHeightMM;

		// The horizontal sensor extent that matches the game FOV under the gate fit.
		if ((GateFit)fb.GateFit == GateFit::Vertical) {
			s.EffectiveSensorWidthMM = fb.SensorHeightMM * viewportAspect;
			s.VerticalFOVDeg = DirectX::XMConvertToDegrees(2.0f * std::atan(fb.SensorHeightMM / (2.0f * lens.FocalLengthMM)));
			s.HorizontalFOVDeg = DirectX::XMConvertToDegrees(2.0f * std::atan(
																		std::tan(DirectX::XMConvertToRadians(s.VerticalFOVDeg) / 2.0f) * viewportAspect));
		} else {
			s.EffectiveSensorWidthMM = fb.SensorWidthMM;
			s.HorizontalFOVDeg = DirectX::XMConvertToDegrees(2.0f * std::atan(fb.SensorWidthMM / (2.0f * lens.FocalLengthMM)));
			s.VerticalFOVDeg = DirectX::XMConvertToDegrees(2.0f * std::atan(
																	  fb.SensorHeightMM / (2.0f * lens.FocalLengthMM)));
		}

		s.FocalLengthMM = lens.FocalLengthMM;
		s.FNumber = lens.FNumber;
		s.ApertureBladeCount = lens.ApertureBladeCount;
		s.ApertureBladeRotationDeg = lens.ApertureBladeRotationDeg;
		s.ApertureRoundness = lens.ApertureRoundness;
		s.EntrancePupilMM = lens.FocalLengthMM / std::max(lens.FNumber, 0.1f);

		s.Mode = (FocusMode)focus.Mode;
		s.ManualDistanceM = focus.ManualDistanceM;
		s.ScreenPointUV = focus.ScreenPointUV;
		s.TransitionSpeed = focus.TransitionSpeed;

		s.Exposure = (ExposureMode)exp.Mode;
		s.ISO = exp.ISO;
		s.MinISO = exp.MinISO;
		s.MaxISO = exp.MaxISO;
		s.ExposureCompensationEV = exp.ExposureCompensationEV;
		s.ShutterAngleDeg = exp.ShutterAngleDeg;
		s.ShutterTimeS = exp.ShutterAngleDeg / (360.0f * exp.FrameRate);
		s.EV100 = std::log2(lens.FNumber * lens.FNumber / s.ShutterTimeS) - std::log2(exp.ISO / 100.0f);
		s.ExposureDeltaEV = kReferenceEV100 - s.EV100;

		s.Valid = std::isfinite(s.HorizontalFOVDeg) && std::isfinite(s.EV100) &&
		          std::isfinite(s.EffectiveSensorWidthMM) && s.EffectiveSensorWidthMM > 0.0f &&
		          s.HorizontalFOVDeg > 0.0f;
		return s;
	}

	void Controller::Update(bool runnable, float viewportAspect)
	{
		if (!settings.Enabled || !runnable) {
			// Conditional restore: only when nothing else has taken the value
			// since our last write.
			if (fovState == FovState::Applied || fovState == FovState::ExternallyModified) {
				if (auto playerCamera = RE::PlayerCamera::GetSingleton()) {
					float& worldFOV = playerCamera->GetRuntimeData2().worldFOV;
					if (std::abs(worldFOV - lastAppliedFOV) < kFovEpsilon)
						worldFOV = restoreFOV;
				}
			}
			fovState = settings.Enabled ? FovState::Suspended : FovState::Inactive;
			stateValid = false;
			return;
		}

		const bool resumed = fovState != FovState::Applied && fovState != FovState::ExternallyModified;

		PhysicalCameraState s = BuildState(viewportAspect);
		if (s.Valid) {
			activeState = s;
			stateValid = true;
			++stateRevision;
		} else if (!stateValid) {
			// Invalid parameters with no valid history: suspend.
			fovState = FovState::Invalid;
			return;
		}

		if (resumed)
			fovWritePending = true;

		ApplyFOV(viewportAspect);
	}

	void Controller::ApplyFOV(float viewportAspect)
	{
		auto playerCamera = RE::PlayerCamera::GetSingleton();
		if (!playerCamera) {
			fovState = FovState::Unavailable;
			fovWritePending = true;
			return;
		}
		float& worldFOV = playerCamera->GetRuntimeData2().worldFOV;

		const float target = std::clamp(activeState.HorizontalFOVDeg, kMinFOVDeg, kMaxFOVDeg);

		// Detect an external FOV writer since our last write.
		if (fovState == FovState::Applied && std::abs(worldFOV - lastAppliedFOV) >= kFovEpsilon)
			fovState = FovState::ExternallyModified;

		// Write only when the desired value (or aspect under Vertical fit)
		// changed, or a write is pending — never per frame.
		const bool targetChanged = std::abs(target - lastAppliedFOV) >= kFovEpsilon;
		const bool aspectChanged = std::abs(viewportAspect - lastAppliedAspect) >= 1e-4f;
		if (!fovWritePending && !targetChanged && !aspectChanged) {
			if (fovState == FovState::ExternallyModified)
				return;  // unchanged target: never fight an external writer
			if (fovState == FovState::Applied)
				return;  // still correct
		}

		// New transaction: the restore baseline is the value being replaced.
		if (fovState != FovState::Applied || std::abs(worldFOV - lastAppliedFOV) >= kFovEpsilon)
			restoreFOV = worldFOV;

		worldFOV = target;
		lastAppliedFOV = target;
		lastAppliedAspect = viewportAspect;
		fovWritePending = false;
		fovState = FovState::Applied;
	}

	/////////////////////////////////////////////////////////////////////////////////
	// Controller: UI
	/////////////////////////////////////////////////////////////////////////////////

	const char* Controller::GetFilmbackPresetName() const
	{
		switch ((FilmbackPreset)settings.Filmback.Preset) {
		case FilmbackPreset::Super35:
			return "Super 35";
		case FilmbackPreset::APSC:
			return "APS-C";
		case FilmbackPreset::Custom:
			return T("feature.post_processing.cinematic_camera.custom", "Custom");
		case FilmbackPreset::FullFrame:
		default:
			return "Full Frame";
		}
	}

	const char* Controller::GetFovStateText() const
	{
		switch (fovState) {
		case FovState::Applied:
			return T("feature.post_processing.cinematic_camera.status_applied", "Applied");
		case FovState::ExternallyModified:
			return T("feature.post_processing.cinematic_camera.status_externally_modified", "Externally Modified");
		case FovState::Suspended:
			return T("feature.post_processing.cinematic_camera.status_suspended", "Suspended");
		case FovState::Unavailable:
			return T("feature.post_processing.cinematic_camera.status_unavailable", "Unavailable");
		case FovState::Invalid:
			return T("feature.post_processing.cinematic_camera.status_invalid", "Invalid");
		case FovState::Inactive:
		default:
			return T("feature.post_processing.cinematic_camera.status_inactive", "Inactive");
		}
	}

	void Controller::DrawSettings()
	{
		auto& fb = settings.Filmback;
		auto& lens = settings.Lens;
		auto& focus = settings.Focus;
		auto& exp = settings.Exposure;

		constexpr auto logSliderFlags = ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic;
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextDisabled("%s", T("feature.post_processing.cinematic_camera.slider_input_hint", "Ctrl+click a slider to enter an exact value."));
		ImGui::PopTextWrapPos();

		if (ImGui::CollapsingHeader(T("feature.post_processing.cinematic_camera.filmback", "Filmback"))) {
			const char* presetNames[] = {
				T("feature.post_processing.cinematic_camera.preset_full_frame", "Full Frame"),
				T("feature.post_processing.cinematic_camera.preset_super_35", "Super 35"),
				T("feature.post_processing.cinematic_camera.preset_aps_c", "APS-C"),
				T("feature.post_processing.cinematic_camera.custom", "Custom"),
			};
			if (ImGui::Combo(T("feature.post_processing.cinematic_camera.preset", "Filmback Preset"), &fb.Preset, presetNames, (int)std::size(presetNames))) {
				ValidateSettings();
			}

			if ((FilmbackPreset)fb.Preset == FilmbackPreset::Custom) {
				ImGui::SliderFloat(T("feature.post_processing.cinematic_camera.sensor_width", "Sensor Width"), &fb.SensorWidthMM, 1.0f, 100.0f, "%.2f mm", ImGuiSliderFlags_AlwaysClamp);
				ImGui::SliderFloat(T("feature.post_processing.cinematic_camera.sensor_height", "Sensor Height"), &fb.SensorHeightMM, 1.0f, 100.0f, "%.2f mm", ImGuiSliderFlags_AlwaysClamp);
			} else {
				ImGui::Text(T("feature.post_processing.cinematic_camera.sensor_size_readout", "Sensor: %.2f x %.2f mm"), fb.SensorWidthMM, fb.SensorHeightMM);
			}

			const char* gateFitNames[] = {
				T("feature.post_processing.cinematic_camera.gate_fit_horizontal", "Horizontal"),
				T("feature.post_processing.cinematic_camera.gate_fit_vertical", "Vertical"),
			};
			ImGui::Combo(T("feature.post_processing.cinematic_camera.gate_fit", "Gate Fit"), &fb.GateFit, gateFitNames, (int)std::size(gateFitNames));
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text(T("feature.post_processing.cinematic_camera.gate_fit_desc",
					"Which sensor dimension maps to the game FOV. Horizontal keeps the horizontal angle of view; Vertical keeps the vertical angle and derives the horizontal FOV from the viewport aspect."));
		}

		if (ImGui::CollapsingHeader(T("feature.post_processing.cinematic_camera.lens", "Lens"), ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SliderFloat(T("feature.post_processing.cinematic_camera.focal_length", "Focal Length"), &lens.FocalLengthMM, 1.0f, 300.0f, "%.1f mm", logSliderFlags);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextUnformatted(T("feature.post_processing.cinematic_camera.focal_length_desc", "Shorter focal lengths give a wider view; longer focal lengths zoom in. Uses a logarithmic scale for finer control at short focal lengths."));
			ImGui::SliderFloat(T("feature.post_processing.cinematic_camera.f_number", "F-Number"), &lens.FNumber, 0.7f, 32.0f, "f/%.2f", logSliderFlags);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextUnformatted(T("feature.post_processing.cinematic_camera.f_number_desc", "Lower f-numbers give a shallower depth of field and let in more light. Auto ISO compensates for aperture changes within its ISO limits."));
			if (ImGui::TreeNode(T("feature.post_processing.cinematic_camera.aperture_shape", "Aperture Shape"))) {
				ImGui::SliderInt(T("feature.post_processing.cinematic_camera.aperture_blades", "Aperture Blades"), &lens.ApertureBladeCount, 4, 10, "%d", ImGuiSliderFlags_AlwaysClamp);
				ImGui::SliderFloat(T("feature.post_processing.cinematic_camera.aperture_rotation", "Aperture Rotation"), &lens.ApertureBladeRotationDeg, 0.0f, 360.0f, "%.1f°", ImGuiSliderFlags_AlwaysClamp);
				ImGui::SliderFloat(T("feature.post_processing.cinematic_camera.aperture_roundness", "Aperture Roundness"), &lens.ApertureRoundness, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				ImGui::TreePop();
			}

			if (const auto* state = GetState()) {
				ImGui::Text(T("feature.post_processing.cinematic_camera.fov_readout", "FOV: %.2f° H / %.2f° V"),
					state->HorizontalFOVDeg, state->VerticalFOVDeg);
				ImGui::Text(T("feature.post_processing.cinematic_camera.entrance_pupil_readout", "Entrance Pupil: %.2f mm"),
					state->EntrancePupilMM);
			}
		}

		if (ImGui::CollapsingHeader(T("feature.post_processing.cinematic_camera.focus", "Focus"), ImGuiTreeNodeFlags_DefaultOpen)) {
			const char* modeNames[] = {
				T("feature.post_processing.cinematic_camera.focus_manual", "Manual"),
				T("feature.post_processing.cinematic_camera.focus_screen_point", "Screen Point"),
				T("feature.post_processing.cinematic_camera.focus_target", "Target"),
			};
			ImGui::Combo(T("feature.post_processing.cinematic_camera.focus_mode", "Focus Mode"), &focus.Mode, modeNames, (int)std::size(modeNames));

			if ((FocusMode)focus.Mode != FocusMode::ScreenPoint) {
				const char* distanceLabel = (FocusMode)focus.Mode == FocusMode::Target ?
				                                T("feature.post_processing.cinematic_camera.fallback_distance", "Fallback Distance") :
				                                T("feature.post_processing.cinematic_camera.focus_distance", "Focus Distance");
				ImGui::SliderFloat(distanceLabel, &focus.ManualDistanceM, 0.01f, 10000.0f, "%.3f m", logSliderFlags | ImGuiSliderFlags_NoRoundToFormat);
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::TextUnformatted(T("feature.post_processing.cinematic_camera.focus_distance_desc", "Logarithmic scale for precise close-range focus while retaining the full distance range. In Target mode, this is used until a target is found; losing the target holds the last focus input."));
			}
			if ((FocusMode)focus.Mode == FocusMode::ScreenPoint) {
				float focusX = focus.ScreenPointUV.x * 100.0f;
				float focusY = focus.ScreenPointUV.y * 100.0f;
				if (ImGui::SliderFloat(T("feature.post_processing.cinematic_camera.focus_point_x", "Horizontal Focus Position"), &focusX, 0.0f, 100.0f, "%.1f%%", ImGuiSliderFlags_AlwaysClamp))
					focus.ScreenPointUV.x = focusX / 100.0f;
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::TextUnformatted(T("feature.post_processing.cinematic_camera.focus_point_x_desc", "0% is the left edge; 100% is the right edge."));
				if (ImGui::SliderFloat(T("feature.post_processing.cinematic_camera.focus_point_y", "Vertical Focus Position"), &focusY, 0.0f, 100.0f, "%.1f%%", ImGuiSliderFlags_AlwaysClamp))
					focus.ScreenPointUV.y = focusY / 100.0f;
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::TextUnformatted(T("feature.post_processing.cinematic_camera.focus_point_y_desc", "0% is the top edge; 100% is the bottom edge."));
				if (ImGui::Button(T("feature.post_processing.cinematic_camera.center_focus_point", "Center Focus Point")))
					focus.ScreenPointUV = float2(0.5f, 0.5f);
			}
			ImGui::SliderFloat(T("feature.post_processing.cinematic_camera.transition_speed", "Transition Speed"), &focus.TransitionSpeed, 0.1f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextUnformatted(T("feature.post_processing.cinematic_camera.transition_speed_desc", "Lower values make focus changes smoother; 1 snaps to the new focus distance immediately. Applies to all focus modes."));

			if ((FocusMode)focus.Mode == FocusMode::Target) {
				ImGui::PushTextWrapPos(0.0f);
				ImGui::TextDisabled("%s", T("feature.post_processing.cinematic_camera.target_source_note",
											  "Target priority (highest first): dialogue speaker, True Directional Movement lock, console selection (if enabled in Depth of Field)."));
				ImGui::PopTextWrapPos();
			}
		}

		if (ImGui::CollapsingHeader(T("feature.post_processing.cinematic_camera.exposure", "Exposure"), ImGuiTreeNodeFlags_DefaultOpen)) {
			const char* exposureModes[] = {
				T("feature.post_processing.cinematic_camera.exposure_auto_iso", "Auto ISO"),
				T("feature.post_processing.cinematic_camera.exposure_manual", "Manual"),
			};
			ImGui::Combo(T("feature.post_processing.cinematic_camera.exposure_mode", "Exposure Mode"), &exp.Mode, exposureModes, (int)std::size(exposureModes));
			const bool autoISO = (ExposureMode)exp.Mode == ExposureMode::AutoISO;
			if (autoISO) {
				ImGui::SliderFloat(T("feature.post_processing.cinematic_camera.iso_min", "Minimum ISO"), &exp.MinISO, 25.0f, exp.MaxISO, "%.0f", logSliderFlags);
				ImGui::SliderFloat(T("feature.post_processing.cinematic_camera.iso_max", "Maximum ISO"), &exp.MaxISO, exp.MinISO, 12800.0f, "%.0f", logSliderFlags);
			} else {
				ImGui::SliderFloat(T("feature.post_processing.cinematic_camera.iso", "ISO"), &exp.ISO, 25.0f, 12800.0f, "%.0f", logSliderFlags);
			}
			ImGui::SliderFloat(T("feature.post_processing.cinematic_camera.frame_rate", "Frame Rate"), &exp.FrameRate, 1.0f, 240.0f, "%.2f fps", ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextUnformatted(T("feature.post_processing.cinematic_camera.frame_rate_desc", "Virtual camera frame rate used with the shutter angle to calculate exposure time. Does not change or limit the game's frame rate."));
			ImGui::SliderFloat(T("feature.post_processing.cinematic_camera.shutter_angle", "Shutter Angle"), &exp.ShutterAngleDeg, 1.0f, 360.0f, "%.0f°", ImGuiSliderFlags_AlwaysClamp);
			if (autoISO)
				ImGui::SliderFloat(T("feature.post_processing.cinematic_camera.exposure_compensation", "Exposure Compensation"), &exp.ExposureCompensationEV, -5.0f, 5.0f, "%+.2f EV", ImGuiSliderFlags_AlwaysClamp);

			if (const auto* state = GetState()) {
				ImGui::Text(T("feature.post_processing.cinematic_camera.shutter_readout", "Shutter: 1/%.2f s"), 1.0f / std::max(state->ShutterTimeS, 1e-6f));
			}
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextDisabled("%s", autoISO ?
										  T("feature.post_processing.cinematic_camera.auto_iso_desc", "Aperture and shutter stay fixed. Metering adjusts ISO within the selected limits; exposure compensation shifts the metering target. At the limits, the image may remain too dark or too bright.") :
										  T("feature.post_processing.cinematic_camera.manual_exposure_desc", "Aperture, shutter and ISO determine exposure. Scene brightness does not change it automatically; exposure compensation is only used in Auto ISO mode."));
			ImGui::TextDisabled("%s", T("feature.post_processing.cinematic_camera.exposure_note",
										  "Exposure processing is active while Cinematic Camera is active. Metering area and adaptation speed are configured in Histogram Auto Exposure. Motion Blur follows the shutter angle."));
			ImGui::PopTextWrapPos();
		}

		// FOV ownership status.
		ImGui::Separator();
		ImGui::Text(T("feature.post_processing.cinematic_camera.fov_status", "Camera FOV: %s"), GetFovStateText());
		if (fovState == FovState::ExternallyModified) {
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextDisabled("%s", T("feature.post_processing.cinematic_camera.fov_external_note",
										  "Another mod changed the FOV after the last write. Cinematic Camera keeps the external value; changing focal length or filmback starts a new write."));
			ImGui::PopTextWrapPos();
		}
	}
}
