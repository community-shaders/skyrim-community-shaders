// Unit tests for Util::Subrect::Controller.
//
// Focus: API contract for the stereo extension (PR-1 of the DLSS-PR-PLAN decomposition).
// Cannot exercise DrawEditor (needs an ImGui context); covers everything else:
// JSON load/save round-trips, mirror math, preset apply, mono/stereo back-compat.

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <imgui.h>  // ImDrawCallback declared in Subrect.h signature

#include "Utils/Subrect.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
using Util::Subrect::Controller;
using Util::Subrect::Preset;
using Util::Subrect::UVRegion;

namespace
{
	bool UVApprox(const UVRegion& a, const UVRegion& b, float eps = 1e-5f)
	{
		return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps &&
		       std::abs(a.w - b.w) < eps && std::abs(a.h - b.h) < eps;
	}
}

TEST_CASE("Controller defaults to mono mode", "[subrect]")
{
	Controller c;
	REQUIRE_FALSE(c.IsStereoEnabled());
	// Right-eye accessor folds onto primary UV in mono mode.
	REQUIRE(UVApprox(c.GetUV(), c.GetRightEyeUV()));
}

TEST_CASE("SaveSettings in mono mode emits no right-eye keys", "[subrect][backcompat]")
{
	// Pre-stereo screenshot JSON shape must round-trip bit-identically: this is
	// the core back-compat contract for the existing ScreenshotFeature consumer.
	// EnsureDefaultPreset is lazy (only runs in LoadSettings/ApplyPreset/DrawEditor),
	// so prime it with an empty load to realize the placeholder preset.
	Controller c;
	c.LoadSettings(json::object());
	json out;
	c.SaveSettings(out);

	REQUIRE(out.contains("CropX"));
	REQUIRE(out.contains("CropY"));
	REQUIRE(out.contains("CropW"));
	REQUIRE(out.contains("CropH"));
	REQUIRE_FALSE(out.contains("CropRightX"));
	REQUIRE_FALSE(out.contains("CropRightY"));
	REQUIRE_FALSE(out.contains("CropRightW"));
	REQUIRE_FALSE(out.contains("CropRightH"));

	REQUIRE(out["CropPresets"].is_array());
	REQUIRE(out["CropPresets"].size() >= 1);
	for (const auto& entry : out["CropPresets"]) {
		REQUIRE(entry.contains("uv"));
		REQUIRE_FALSE(entry.contains("right_uv"));
	}
}

TEST_CASE("LoadSettings reads legacy mono JSON unchanged", "[subrect][backcompat]")
{
	// Replicates the screenshot feature's existing on-disk schema.
	json in = {
		{ "CropX", 0.25f },
		{ "CropY", 0.10f },
		{ "CropW", 0.5f },
		{ "CropH", 0.8f },
		{ "CropPresets", json::array({
							 {
								 { "name", "Full Frame" },
								 { "uv", { 0.0f, 0.0f, 1.0f, 1.0f } },
							 },
						 }) },
		{ "SelectedPresetIndex", -1 },
	};

	Controller c;
	c.LoadSettings(in);

	REQUIRE(UVApprox(c.GetUV(), { 0.25f, 0.10f, 0.5f, 0.8f }));
	REQUIRE_FALSE(c.IsStereoEnabled());
}

TEST_CASE("SetStereoEnabled toggles mirror sync", "[subrect][stereo]")
{
	// Stereo enable on a fresh controller mirrors the default UV (which is
	// {0,0,1,1}); the mirror of a full-frame is still full-frame.
	Controller c;
	c.SetStereoEnabled(true);
	REQUIRE(c.IsStereoEnabled());

	// Re-enable is a no-op (no double-sync, no state corruption).
	c.SetStereoEnabled(true);
	REQUIRE(c.IsStereoEnabled());

	c.SetStereoEnabled(false);
	REQUIRE_FALSE(c.IsStereoEnabled());
}

TEST_CASE("Stereo save then load round-trips right-eye keys", "[subrect][stereo]")
{
	// Seed presets with explicit asymmetric right-eye to confirm the on-disk
	// schema preserves it (rather than always mirroring on load).
	Controller src;
	src.SetStereoEnabled(true);
	src.SeedDefaultPresets({
		Preset{ .name = "Asym", .uv = { 0.1f, 0.0f, 0.5f, 1.0f }, .rightUV = { 0.3f, 0.0f, 0.4f, 0.9f } },
	});
	// Realize the seed and copy it into currentUV/currentRightUV.
	src.LoadSettings(json::object());

	json saved;
	src.SaveSettings(saved);

	REQUIRE(saved.contains("CropRightX"));
	REQUIRE(saved["CropPresets"][0].contains("right_uv"));

	Controller dst;
	dst.SetStereoEnabled(true);
	dst.LoadSettings(saved);

	// After load, applying the asymmetric preset must restore both eyes
	// exactly (not mirror-overwrite the right eye).
	REQUIRE(UVApprox(dst.GetUV(), { 0.1f, 0.0f, 0.5f, 1.0f }));
	REQUIRE(UVApprox(dst.GetRightEyeUV(), { 0.3f, 0.0f, 0.4f, 0.9f }));
}

TEST_CASE("Stereo load mirrors right-eye when JSON lacks CropRight keys", "[subrect][stereo][backcompat]")
{
	// A user upgrading from a mono build has CropX/Y/W/H but no CropRight*.
	// In stereo mode, the controller mirrors the primary UV around x=0.5.
	json legacy = {
		{ "CropX", 0.2f },
		{ "CropY", 0.0f },
		{ "CropW", 0.5f },
		{ "CropH", 1.0f },
	};

	Controller c;
	c.SetStereoEnabled(true);
	c.LoadSettings(legacy);

	REQUIRE(UVApprox(c.GetUV(), { 0.2f, 0.0f, 0.5f, 1.0f }));
	// Mirror: x = 1 - 0.2 - 0.5 = 0.3
	REQUIRE(UVApprox(c.GetRightEyeUV(), { 0.3f, 0.0f, 0.5f, 1.0f }));
}

TEST_CASE("GetStereoPixelRegions splits SBS width per eye", "[subrect][stereo]")
{
	// Stereo regions resolve against half-width per eye (the texture is SBS).
	// Caller passes the full stereo texture size; controller divides W by 2.
	Controller c;
	c.SetStereoEnabled(true);

	// Make eyes asymmetric so we can tell them apart.
	c.SeedDefaultPresets({
		Preset{
			.name = "Asym",
			.uv = { 0.0f, 0.0f, 1.0f, 1.0f },       // full left eye
			.rightUV = { 0.0f, 0.0f, 0.5f, 1.0f },  // left half of right eye
		},
	});
	// Realize the seed so currentUV/currentRightUV pick up the preset values.
	c.LoadSettings(json::object());

	const auto regions = c.GetStereoPixelRegions(2000, 1000);
	// Left eye spans the full 1000-wide left half.
	REQUIRE(regions.leftEye.x == 0);
	REQUIRE(regions.leftEye.w == 1000);
	REQUIRE(regions.leftEye.h == 1000);
	// Right eye spans only half the 1000-wide right half = 500 px.
	REQUIRE(regions.rightEye.w == 500);
}

TEST_CASE("GetStereoPixelRegions in mono mode returns identical eyes", "[subrect][stereo]")
{
	// Mono callers can use the stereo accessor and both eyes will resolve from
	// the primary UV — lets DLSS consumers stay agnostic of stereo state.
	Controller c;
	json in = {
		{ "CropX", 0.0f },
		{ "CropY", 0.0f },
		{ "CropW", 1.0f },
		{ "CropH", 1.0f },
	};
	c.LoadSettings(in);

	const auto regions = c.GetStereoPixelRegions(2000, 1000);
	REQUIRE(regions.leftEye.x == regions.rightEye.x);
	REQUIRE(regions.leftEye.w == regions.rightEye.w);
}

TEST_CASE("MirrorUVHorizontal symmetry via SetStereoEnabled", "[subrect][stereo][math]")
{
	// {0.4, *, 0.6, *} mirrors to {0, *, 0.6, *} — the nose-side overlap case.
	// Exercised through SetStereoEnabled since MirrorUVHorizontal is private.
	Controller c;
	json in = {
		{ "CropX", 0.4f },
		{ "CropY", 0.0f },
		{ "CropW", 0.6f },
		{ "CropH", 1.0f },
	};
	c.LoadSettings(in);
	REQUIRE_FALSE(c.IsStereoEnabled());

	c.SetStereoEnabled(true);
	// x = 1 - 0.4 - 0.6 = 0.0
	REQUIRE(UVApprox(c.GetRightEyeUV(), { 0.0f, 0.0f, 0.6f, 1.0f }));
}
