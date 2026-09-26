#pragma once

// Cinematic Camera — Community Shaders / Post Processing
//
// A CPU-side controller that unifies the physical camera parameters (filmback,
// lens, focus, exposure) consumed by the post processing effects. It owns no
// shaders and adds no passes: each effect reads the per-frame immutable
// PhysicalCameraState and adapts it to its own existing settings (per-effect
// adapters in Draw()). The controller has no knowledge of individual features,
// and shaders have no knowledge of the controller.
//
// The controller is also the only writer of PlayerCamera::worldFOV. It applies
// the FOV implied by focal length + filmback when its projection parameters
// change (never per frame), detects external FOV writers, and restores the
// pre-existing value conditionally on disable/suspend.

#define TDM_API_COMMONLIB
#include "TDM/TrueDirectionalMovementAPI.h"

namespace CinematicCamera
{
	// Reference velocity scale of the Motion Blur Medium preset, calibrated to
	// a 180 deg shutter (see the Motion Blur adapter).
	constexpr float kMotionBlurReferenceScale = 300.0f;

	enum class FilmbackPreset : int
	{
		FullFrame = 0,  // 36 x 24 mm
		Super35 = 1,    // 24.89 x 18.66 mm
		APSC = 2,       // 23.6 x 15.7 mm
		Custom = 3,
	};

	enum class GateFit : int
	{
		Horizontal = 0,  // sensor width maps to the horizontal FOV
		Vertical = 1,    // sensor height maps to the vertical FOV
	};

	enum class FocusMode : int
	{
		Manual = 0,
		ScreenPoint = 1,
		Target = 2,
	};

	enum class ExposureMode : int
	{
		AutoISO = 0,
		Manual = 1,
	};

	struct FilmbackSettings
	{
		int Preset = (int)FilmbackPreset::FullFrame;
		float SensorWidthMM = 36.0f;
		float SensorHeightMM = 24.0f;
		int GateFit = (int)GateFit::Horizontal;
	};

	struct LensSettings
	{
		float FocalLengthMM = 50.0f;
		float FNumber = 2.8f;
		int ApertureBladeCount = 6;
		float ApertureBladeRotationDeg = 0.0f;
		float ApertureRoundness = 0.5f;
	};

	struct FocusSettings
	{
		int Mode = (int)FocusMode::ScreenPoint;
		float ManualDistanceM = 10.0f;
		float2 ScreenPointUV = float2(0.5f, 0.5f);
		float TransitionSpeed = 0.5f;
	};

	struct ExposureSettings
	{
		int Mode = (int)ExposureMode::AutoISO;
		float ISO = 100.0f;
		float MinISO = 25.0f;
		float MaxISO = 12800.0f;
		float FrameRate = 24.0f;
		float ShutterAngleDeg = 180.0f;
		float ExposureCompensationEV = 0.0f;
	};

	struct Settings
	{
		bool Enabled = false;
		FilmbackSettings Filmback;
		LensSettings Lens;
		FocusSettings Focus;
		ExposureSettings Exposure;
	};

	// Immutable per-frame physical camera state. The only contract between the
	// controller and the effect adapters.
	struct PhysicalCameraState
	{
		bool Valid = false;

		// Filmback (mm).
		float SensorWidthMM = 36.0f;
		float SensorHeightMM = 24.0f;
		// Horizontal sensor extent matching the game FOV under the gate fit:
		// Horizontal -> SensorWidthMM, Vertical -> SensorHeightMM * aspect.
		float EffectiveSensorWidthMM = 36.0f;

		// Lens.
		float FocalLengthMM = 50.0f;
		float FNumber = 2.8f;
		int ApertureBladeCount = 6;
		float ApertureBladeRotationDeg = 0.0f;  // degrees
		float ApertureRoundness = 0.5f;
		float EntrancePupilMM = 17.857f;

		// Focus.
		FocusMode Mode = FocusMode::ScreenPoint;
		float ManualDistanceM = 10.0f;
		float2 ScreenPointUV = float2(0.5f, 0.5f);
		float TransitionSpeed = 0.5f;

		// Exposure.
		ExposureMode Exposure = ExposureMode::AutoISO;
		float ISO = 100.0f;
		float MinISO = 25.0f;
		float MaxISO = 12800.0f;
		float ExposureCompensationEV = 0.0f;
		float ShutterAngleDeg = 180.0f;
		float ShutterTimeS = 1.0f / 48.0f;
		float EV100 = 8.5573427f;
		// Manual exposure relative to the reference camera, before metering compensation.
		float ExposureDeltaEV = 0.0f;

		// Projection (degrees).
		float HorizontalFOVDeg = 39.6f;
		float VerticalFOVDeg = 27.0f;
	};

	// CPU-side focus target resolution shared by the Cinematic Camera focus
	// modes and DoF's own target focus. Target priority is preserved from the
	// original DoF implementation: console selection, TDM lock, dialogue speaker.
	struct FocusResolver
	{
		TDM_API::IVTDM2* g_TDM = nullptr;

		void RequestTDM()
		{
			g_TDM = reinterpret_cast<TDM_API::IVTDM2*>(TDM_API::RequestPluginAPI(TDM_API::InterfaceVersion::V2));
		}

		bool GetTargetLockEnabled();
		bool GetInDialogue();
		RE::NiPoint3 GetCameraPos();
		RE::NiPoint3 GetReferenceFocusPosition(RE::TESObjectREFR* a_ref);
		float GetDistanceToReference(RE::TESObjectREFR* a_ref);
		bool GetReferenceFocusCoord(RE::TESObjectREFR* a_ref, float2& a_focusCoord);

		// Target selection with the existing priority: console pick (when
		// allowed), TDM lock, dialogue speaker. Updates a_currentRef for UI.
		RE::TESObjectREFR* FindTarget(bool a_allowConsoleSelection, uint& a_currentRef);

		struct TargetFocusResult
		{
			bool hasTarget = false;  // a target (or held history) is available
			bool projected = false;  // target visible on screen -> autofocus at coord
			float2 focusCoord = { 0.5f, 0.5f };
			float distanceM = 10.0f;
		};

		// Full resolution for the cinematic Target focus mode. When the target
		// is lost, the last valid input is held; with no history the caller
		// falls back to its manual distance.
		TargetFocusResult ResolveTarget(bool a_allowConsoleSelection, uint& a_currentRef);

	private:
		bool hasValidHistory = false;
		bool historyProjected = false;
		float2 historyCoord = { 0.5f, 0.5f };
		float historyDistanceM = 10.0f;
	};

	struct Controller
	{
		Settings settings;

		FocusResolver focusResolver;

		// Per-frame published state (null to effects while !stateValid).
		PhysicalCameraState activeState{};
		bool stateValid = false;
		uint stateRevision = 0;

		enum class FovState
		{
			Inactive,
			Applied,
			ExternallyModified,
			Suspended,
			Unavailable,
			Invalid
		};
		FovState fovState = FovState::Inactive;

		void LoadSettings(json& o_json);
		void SaveSettings(json& o_json);
		void RestoreDefaultSettings();

		// Per-frame update. `runnable` is false while Post Processing cannot
		// run (bypassed, Effects11 owns the tonemap, main/loading menu open).
		void Update(bool runnable, float viewportAspect);

		[[nodiscard]] const PhysicalCameraState* GetState() const { return stateValid ? &activeState : nullptr; }

		[[nodiscard]] const char* GetFilmbackPresetName() const;
		[[nodiscard]] const char* GetFovStateText() const;

		// Filmback / Lens / Focus / Exposure parameter page with readouts.
		void DrawSettings();

	private:
		[[nodiscard]] PhysicalCameraState BuildState(float viewportAspect) const;
		void ApplyFOV(float viewportAspect);
		void ValidateSettings();

		float restoreFOV = 0.0f;
		float lastAppliedFOV = 0.0f;
		float lastAppliedAspect = 0.0f;
		bool fovWritePending = true;
	};
}
