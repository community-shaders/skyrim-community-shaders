#pragma once

#include <functional>
#include <map>
#include <span>
#include <string>

#include <imgui.h>

struct Feature;

/// The feature column shared by Base Settings and the scene pages: an optional search bar, optional
/// category groups, and a dot beside each feature holding settings on the layer being edited.
namespace FeatureListPicker
{
	/// A feature's dot: none, hollow (settings that do not apply here) or filled (settings that do).
	enum class Marker
	{
		None,
		Hollow,
		Filled,
	};

	/// Starting width of the feature column at the baseline font size; the user can drag it.
	inline constexpr float kColumnWidth = 180.0f;

	/// The divider doubles as the resize grip, so the feature column and the panel share one border.
	inline constexpr ImGuiTableFlags kLayoutFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV;

	struct Options
	{
		/// Search text; null hides the search bar.
		std::string* search = nullptr;
		/// Expansion state per category; null lists the features flat, in the order given.
		std::map<std::string, bool>* categories = nullptr;
		/// Listed first and outside any category, such as Post Processing in Base Settings.
		std::span<Feature* const> pinned;
		/// The dot beside a feature's name; unset draws none.
		std::function<Marker(const Feature&)> marker;
		/// Tooltip for a feature's dot, shown while it is hovered.
		std::function<const char*(const Feature&, Marker)> markerTooltip;
	};

	/**
	 * @brief Draws the list and selects a feature on click.
	 * @param features Features to offer, besides the pinned ones.
	 * @param selected Short name of the selected feature; updated on click.
	 */
	void Draw(std::span<Feature* const> features, std::string& selected, const Options& options);
}
