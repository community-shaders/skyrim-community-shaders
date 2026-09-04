// Adapted from NativeSystemMenuFramework (https://github.com/RoseEden30/NativeSystemMenuFramework)
// License: GPL-3.0-or-later

#include "NativeMenu/Vendor/VanillaSettingsEngine.h"

#include "NativeMenu/Vendor/ListRows.h"
#include "NativeMenu/Vendor/Text.h"

#include <chrono>
#include <cmath>
#include <deque>
#include <mutex>

namespace NativeMenu::Vendor::VanillaSettingsEngine
{
	namespace
	{
		// Vanilla hands out small sequential IDs, so this range never collides.
		constexpr std::uint32_t kIdBase = 900000000;

		struct Setting
		{
			std::string                            tab;
			Type                                    type;
			std::string                             label;
			std::function<float()>                  getValue;
			std::function<void(float)>              onChange;
			std::function<void(float)>              onCommit;
			float                                   defaultValue = 0.0f;
			std::vector<std::string>                options;
			std::function<bool()>                   isEnabled;
			std::function<void(float, char*, int)>  formatValue;
			std::function<void(char*, int)>         getText;
			std::function<void()>                   onPress;
			std::string                             description;
			// kLabel only.
			Align align = Align::kLeft;
			// kButton only: ticks left to keep the box checked after a press,
			// so the click is actually seen before it springs back.
			int flashTicks = 0;
			// kSlider only.
			bool                                   commitPending = false;
			bool                                   dragging = false;
			bool                                   wasDragging = false;
			float                                  commitValue = 0.0f;
			std::chrono::steady_clock::time_point  commitDeadline{};
		};
		constexpr int kButtonFlashTicks = 10;

		// A click on the widget itself reaches both itemPress and OptionChange.
		// Whichever runs first takes the toggle, the other drops it.
		int                                   g_lastToggle = -1;
		std::chrono::steady_clock::time_point g_lastToggleTime{};

		bool ClaimToggle(int a_index)
		{
			const auto now = std::chrono::steady_clock::now();
			if (a_index == g_lastToggle && now - g_lastToggleTime < std::chrono::milliseconds(150))
				return false;
			g_lastToggle = a_index;
			g_lastToggleTime = now;
			return true;
		}

		// Backstop for the arrows and the gamepad, which never touch the thumb.
		constexpr auto kCommitSettleTime = std::chrono::milliseconds(250);

		// A deque, not a vector: the passes below call into mod code while
		// holding a reference to a row, and that code is allowed to register
		// one in turn.
		std::deque<Setting> g_settings;

		int g_pendingCommits = 0;

		void Commit(Setting& a_setting, const char* a_why)
		{
			if (!a_setting.commitPending)
				return;
			a_setting.commitPending = false;
			a_setting.wasDragging = false;
			--g_pendingCommits;
			logger::debug("VanillaSettingsEngine: commit '{}' = {} ({})", a_setting.label, a_setting.commitValue, a_why);
			a_setting.onCommit(a_setting.commitValue);
		}

		void CommitAll(const char* a_why)
		{
			for (std::size_t i = 0; i < g_settings.size() && g_pendingCommits > 0; ++i)
				Commit(g_settings[i], a_why);
		}

		void QueueCommit(Setting& a_setting, float a_value)
		{
			if (!a_setting.onCommit)
				return;

			if (a_setting.type != Type::kSlider) {
				logger::debug("VanillaSettingsEngine: commit '{}' = {} (no drag)", a_setting.label, a_value);
				a_setting.onCommit(a_value);
				return;
			}

			if (!a_setting.commitPending) {
				a_setting.commitPending = true;
				++g_pendingCommits;
			}
			a_setting.commitValue = a_value;
			a_setting.commitDeadline = std::chrono::steady_clock::now() + kCommitSettleTime;
		}

		// Registration normally happens at load, but nothing stops a caller
		// calling Add* later or off the menu thread. Recursive because the
		// tick calls into caller code while holding it, and that code may
		// register something in turn.
		std::recursive_mutex g_settingsMutex;

		constexpr const char* kNativeTabs[] = { "Gameplay", "Display", "Audio" };
		constexpr std::size_t kNativeTabCount = std::size(kNativeTabs);

		int NativeTabIndex(const std::string& a_tab)
		{
			for (std::size_t i = 0; i < kNativeTabCount; ++i) {
				if (a_tab == kNativeTabs[i])
					return static_cast<int>(i);
			}
			return -1;
		}

		bool IsNativeTab(const std::string& a_tab) { return NativeTabIndex(a_tab) >= 0; }

		std::string g_customTab;
		bool        g_haveCustomTab = false;

		void RegisterCustomTab(const std::string& a_tab)
		{
			if (g_haveCustomTab) {
				if (a_tab != g_customTab)
					logger::warn(
						"VanillaSettingsEngine: '{}' ignored - only one custom tab ('{}') is supported",
						a_tab, g_customTab);
				return;
			}
			g_customTab = a_tab;
			g_haveCustomTab = true;
		}

		// Category entries carry a translated label, not the tab name, so
		// position is what identifies them going back the other way.
		std::string TabAt(int a_index)
		{
			if (a_index < 0)
				return {};
			const auto index = static_cast<std::size_t>(a_index);

			if (index < kNativeTabCount)
				return kNativeTabs[index];
			return index == kNativeTabCount && g_haveCustomTab ? g_customTab : std::string{};
		}

		bool                          g_hooked = false;
		RE::FxDelegate::CallbackDefn  g_originalOptionChange{};
		RE::FxDelegate::CallbackDefn  g_originalRequestGameplay{};
		RE::FxDelegate::CallbackDefn  g_originalRequestDisplay{};
		RE::FxDelegate::CallbackDefn  g_originalRequestAudio{};
		std::string                   g_currentTab;
		bool                          g_haveCurrentTab = false;
		bool                          g_settingsListInjected = false;
		// g_currentTab only updates on native-tab selection, so this stops
		// Tick() re-injecting its rows over the custom tab.
		bool                          g_showingCustomTab = false;

		// Captured once per menu open, before anything is widened. Not
		// re-read live: a clip's _width follows its children, so the two
		// would feed each other and grow without bound.
		double g_rowWidth = 0.0;
		double g_textFieldWidth = 0.0;

		void OnOptionChange(const RE::FxDelegateArgs& a_params)
		{
			const std::lock_guard lock(g_settingsMutex);

			if (a_params.GetArgCount() >= 2 && a_params[0].IsNumber()) {
				const auto id = static_cast<std::uint32_t>(a_params[0].GetNumber());
				if (id >= kIdBase) {
					const auto idx = id - kIdBase;
					const auto value = a_params[1].IsNumber() ? a_params[1].GetNumber() : -1.0;
					if (idx < g_settings.size()) {
						logger::debug("VanillaSettingsEngine: OptionChange '{}' in '{}' = {} (id={})",
							g_settings[idx].type == Type::kLabel ? "(label)" : g_settings[idx].label.c_str(),
							g_settings[idx].tab, value, id);
					} else {
						logger::warn("VanillaSettingsEngine: OptionChange for unknown id {} (idx {})", id, idx);
					}
					// Vanilla doesn't block input on a greyed row, so drop the change here instead.
					if (idx < g_settings.size() && a_params[1].IsNumber() &&
						(!g_settings[idx].isEnabled || g_settings[idx].isEnabled())) {
						// A press arrives as 1; vanilla's reset-to-defaults
						// dispatches the default instead, 0 for a button, which
						// must not fire the action.
						const auto toggles = g_settings[idx].type == Type::kButton ||
							g_settings[idx].type == Type::kCheckbox;
						if (toggles && !ClaimToggle(static_cast<int>(idx)))
							return;

						if (g_settings[idx].type == Type::kButton) {
							if (g_settings[idx].onPress && value != 0.0) {
								g_settings[idx].flashTicks = kButtonFlashTicks;
								g_settings[idx].onPress();
							}
						} else if (g_settings[idx].onChange) {
							g_settings[idx].onChange(static_cast<float>(value));
							QueueCommit(g_settings[idx], static_cast<float>(value));
						}
					}
					return;
				}
			}

			if (g_originalOptionChange.callback)
				g_originalOptionChange.callback(a_params);
		}

		// Fires when the player picks a native tab - forwarded to the real
		// handler, and tells us which tab is on screen.
		void OnRequestGameplayOptions(const RE::FxDelegateArgs& a_params)
		{
			g_currentTab = "Gameplay";
			g_haveCurrentTab = true;
			g_showingCustomTab = false;
			if (g_originalRequestGameplay.callback)
				g_originalRequestGameplay.callback(a_params);
		}

		void OnRequestDisplayOptions(const RE::FxDelegateArgs& a_params)
		{
			g_currentTab = "Display";
			g_haveCurrentTab = true;
			g_showingCustomTab = false;
			if (g_originalRequestDisplay.callback)
				g_originalRequestDisplay.callback(a_params);
		}

		void OnRequestAudioOptions(const RE::FxDelegateArgs& a_params)
		{
			g_currentTab = "Audio";
			g_haveCurrentTab = true;
			g_showingCustomTab = false;
			if (g_originalRequestAudio.callback)
				g_originalRequestAudio.callback(a_params);
		}

		void InstallHooks(RE::FxDelegate* a_fxDelegate)
		{
			if (!a_fxDelegate)
				return;

			const auto hook = [&](const char* a_name, RE::FxDelegate::CallbackDefn& a_original,
								   RE::FxDelegateHandler::CallbackFn* a_replacement) {
				RE::GString name(a_name);
				a_fxDelegate->callbacks.Get(name, &a_original);
				a_fxDelegate->callbacks.Set(name, RE::FxDelegate::CallbackDefn{ a_original.handler, a_replacement });
			};

			hook("OptionChange", g_originalOptionChange, &OnOptionChange);
			hook("RequestGameplayOptions", g_originalRequestGameplay, &OnRequestGameplayOptions);
			hook("RequestDisplayOptions", g_originalRequestDisplay, &OnRequestDisplayOptions);
			hook("RequestAudioOptions", g_originalRequestAudio, &OnRequestAudioOptions);

			g_hooked = true;
			logger::debug("VanillaSettingsEngine: OptionChange/Request*Options hooked");
		}

		// A Scaleform write costs far more than a read, and these run on
		// every row of every tick.
		void SetIfChanged(RE::GFxValue& a_object, const char* a_member, double a_value, double a_epsilon = 0.0001)
		{
			RE::GFxValue current;
			if (a_object.GetMember(a_member, &current) && current.IsNumber() &&
				std::abs(current.GetNumber() - a_value) <= a_epsilon)
				return;
			a_object.SetMember(a_member, RE::GFxValue(a_value));
		}

		void SetIfChanged(RE::GFxValue& a_object, const char* a_member, bool a_value)
		{
			RE::GFxValue current;
			if (a_object.GetMember(a_member, &current) && current.IsBool() && current.GetBool() == a_value)
				return;
			a_object.SetMember(a_member, RE::GFxValue(a_value));
		}

		// BSScrollingList takes its scrollbar from a timeline child named
		// "scrollbar", which the Settings lists were never given. Attaching
		// one and filling in ListScrollbar hands the job back to vanilla: it
		// sizes, moves and hides the bar as it does for its own lists.
		void EnsureScrollbar(RE::GFxValue& a_list)
		{
			RE::GFxValue shownV, maxScrollV, row, rowW, rowH, rowY, rowX;
			if (!a_list.GetMember("iListItemsShown", &shownV) || !shownV.IsNumber() || shownV.GetNumber() < 1.0 ||
				!a_list.GetMember("iMaxScrollPosition", &maxScrollV) || !maxScrollV.IsNumber() ||
				!a_list.GetMember("Entry0", &row) || !row.IsObject() ||
				!row.GetMember("_x", &rowX) || !rowX.IsNumber() || !row.GetMember("_y", &rowY) || !rowY.IsNumber() ||
				!row.GetMember("_width", &rowW) || !rowW.IsNumber() ||
				!row.GetMember("_height", &rowH) || !rowH.IsNumber())
				return;

			RE::GFxValue bar;
			if (!a_list.GetMember("ListScrollbar", &bar) || !bar.IsObject()) {
				// JournalScrollBar, not SettingsScrollbar: despite the name
				// the latter is the slider inside a settings row, and its
				// thumb has no scaling grid, so stretching it draws a
				// spindle. This is what the other lists on this page attach.
				const RE::GFxValue attach[3] = { RE::GFxValue("JournalScrollBar"),
					RE::GFxValue("__cs_scrollbar"), RE::GFxValue(22000.0) };
				if (!a_list.Invoke("attachMovie", &bar, attach, 3) || !bar.IsObject()) {
					// A replacer interface may not export it. Leave the list
					// as vanilla built it, arrows included.
					static bool loggedOnce = false;
					if (!loggedOnce) {
						loggedOnce = true;
						logger::warn(
							"VanillaSettingsEngine: no JournalScrollBar in this interface - lists keep their arrows");
					}
					return;
				}

				a_list.SetMember("ListScrollbar", bar);

				// What BSScrollingList::onLoad does for the lists that come
				// with a scrollbar already attached.
				bar.SetMember("position", RE::GFxValue(0.0));
				const RE::GFxValue listen[3] = { RE::GFxValue("scroll"), a_list, RE::GFxValue("onScroll") };
				bar.Invoke("addEventListener", nullptr, listen, 3);
				logger::debug("VanillaSettingsEngine: scrollbar attached");
			}

			// Only once the bar exists, or a list would lose both.
			RE::GFxValue up, down;
			if (a_list.GetMember("ScrollUp", &up) && up.IsObject())
				SetIfChanged(up, "_visible", false);
			if (a_list.GetMember("ScrollDown", &down) && down.IsObject())
				SetIfChanged(down, "_visible", false);

			// Only InvalidateData calls SetScrollbarVisibility, and it ran
			// before this bar existed - so the list would never hide it.
			SetIfChanged(bar, "_visible", maxScrollV.GetNumber() > 0.0);
			if (maxScrollV.GetNumber() > 0.0) {
				// Re-applied every tick: a UIComponent reports no usable width
				// until the frame after attachMovie.
				RE::GFxValue barW;
				if (bar.GetMember("_width", &barW) && barW.IsNumber() && barW.GetNumber() > 0.0) {
					// The border frames the list at a fixed width; a row's own
					// is its content's, so it shifts with the label.
					double       edge = rowW.GetNumber();
					RE::GFxValue border, borderW;
					if (a_list.GetMember("border", &border) && border.IsObject() &&
						border.GetMember("_width", &borderW) && borderW.IsNumber() && borderW.GetNumber() > 0.0)
						edge = borderW.GetNumber();

					SetIfChanged(bar, "_x", rowX.GetNumber() + edge, 0.5);
					SetIfChanged(bar, "_y", rowY.GetNumber(), 0.5);

					// setSize, not _height, which would stretch the arrows too.
					RE::GFxValue sizedH;
					const auto   height = rowH.GetNumber() * shownV.GetNumber();
					if (!bar.GetMember("__height", &sizedH) || !sizedH.IsNumber() ||
						std::abs(sizedH.GetNumber() - height) > 0.5) {
						const RE::GFxValue size[2] = { barW, RE::GFxValue(height) };
						bar.Invoke("setSize", nullptr, size, 2);
					}
				}

				// pageSize is rows on screen, not iMaxItemsShown, which counts
				// clips - more are created than fit, and the thumb would come out
				// sized as if nothing scrolled. Read before writing:
				// setScrollProperties redraws the thumb on every call.
				RE::GFxValue pageSize, maxPosition;
				if (!bar.GetMember("pageSize", &pageSize) || !pageSize.IsNumber() ||
					!bar.GetMember("maxPosition", &maxPosition) || !maxPosition.IsNumber() ||
					pageSize.GetNumber() != shownV.GetNumber() || maxPosition.GetNumber() != maxScrollV.GetNumber()) {
					const RE::GFxValue props[3] = { shownV, RE::GFxValue(0.0), maxScrollV };
					bar.Invoke("setScrollProperties", nullptr, props, 3);
				}
			}
		}

		std::string FormatLabel(const Setting& a_setting, float a_value)
		{
			if (!a_setting.formatValue)
				return a_setting.label;
			char buffer[64] = {};
			a_setting.formatValue(a_value, buffer, static_cast<int>(std::size(buffer)));
			return a_setting.label + ": " + buffer;
		}

		// Fractions of a row, not pixels: both follow the interface's own
		// metrics instead of assuming Bethesda's.
		constexpr double kDescriptionGap = 0.4;   // below the last row
		constexpr double kDescriptionRows = 2.0;  // tall enough for a wrapped sentence

		std::string GetLabelText(const Setting& a_setting)
		{
			if (!a_setting.getText)
				return {};
			char buffer[128] = {};
			a_setting.getText(buffer, static_cast<int>(std::size(buffer)));
			return buffer;
		}

		RE::GFxValue BuildEntry(RE::GFxMovie* a_view, const Setting& a_setting, std::size_t a_settingIndex)
		{
			const float value = a_setting.getValue ? a_setting.getValue() : 0.0f;
			const auto  text = a_setting.type == Type::kLabel ? GetLabelText(a_setting) : FormatLabel(a_setting, value);

			// Vanilla has no button widget - kButton is a CheckBox that
			// RefreshRowAppearance keeps snapping back to unchecked.
			const auto movieType = a_setting.type == Type::kButton ? static_cast<double>(Type::kCheckbox) :
			                                                          static_cast<double>(a_setting.type);

			RE::GFxValue entry;
			a_view->CreateObject(&entry);
			entry.SetMember("ID", RE::GFxValue(static_cast<double>(kIdBase + a_settingIndex)));
			entry.SetMember("movieType", RE::GFxValue(movieType));
			entry.SetMember("text", Text::MakeGFxString(text));
			entry.SetMember("value", RE::GFxValue(static_cast<double>(value)));
			entry.SetMember("defaultVal", RE::GFxValue(static_cast<double>(a_setting.defaultValue)));

			if (a_setting.type == Type::kDropdown && !a_setting.options.empty()) {
				RE::GFxValue options;
				a_view->CreateArray(&options);
				for (std::size_t o = 0; o < a_setting.options.size(); ++o)
					options.SetElement(static_cast<std::uint32_t>(o), Text::MakeGFxString(a_setting.options[o]));
				entry.SetMember("options", options);
			}
			return entry;
		}

		bool GetOptionsList(RE::GFxValue& a_systemPage, RE::GFxValue& a_out)
		{
			RE::GFxValue panel, lists;
			return a_systemPage.GetMember("OptionsListsPanel", &panel) && panel.IsObject() &&
				panel.GetMember("OptionsLists", &lists) && lists.IsObject() &&
				lists.GetMember("List_mc", &a_out) && a_out.IsObject();
		}

		bool HasOurEntries(RE::GFxValue& a_entryList)
		{
			const auto count = a_entryList.GetArraySize();
			for (std::uint32_t i = 0; i < count; ++i) {
				RE::GFxValue entry, id;
				if (a_entryList.GetElement(i, &entry) && entry.GetMember("ID", &id) && id.IsNumber() &&
					static_cast<std::uint32_t>(id.GetNumber()) >= kIdBase)
					return true;
			}
			return false;
		}

		// Vanilla shows no description for a setting, so this adds one: a text
		// field under the rows, following whatever is selected. Built once per
		// menu open; its position is refreshed every tick because each tab
		// shows a different number of rows.
		void RefreshDescription(RE::GFxValue& a_list)
		{
			RE::GFxValue entries, selectedIdx;
			if (!a_list.GetMember("EntriesA", &entries) || !entries.IsArray() ||
				!a_list.GetMember("iSelectedIndex", &selectedIdx) || !selectedIdx.IsNumber())
				return;

			// Ours by ID; anything else (vanilla's own rows) gets no
			// description - upstream's per-vanilla-row description table isn't
			// carried over here.
			std::string description;
			const auto  index = selectedIdx.GetNumber();
			if (index >= 0.0 && index < static_cast<double>(entries.GetArraySize())) {
				RE::GFxValue entry, idVal;
				if (entries.GetElement(static_cast<std::uint32_t>(index), &entry) && entry.IsObject() &&
					entry.GetMember("ID", &idVal) && idVal.IsNumber() &&
					static_cast<std::uint32_t>(idVal.GetNumber()) >= kIdBase) {
					const auto idx = static_cast<std::uint32_t>(idVal.GetNumber()) - kIdBase;
					if (idx < g_settings.size())
						description = g_settings[idx].description;
				}
			}

			// Sits in the band between the last row and the panel's border,
			// clear of the scroll chevron above it. Expressed as a fraction of
			// a row rather than in pixels, so it holds whatever row height the
			// interface uses.
			//
			// Recomputed rather than stored: every tab shows a different
			// number of rows, so a position fixed at creation would sit wrong
			// on all the others.
			RE::GFxValue entry0, rowX, rowY, rowH, rowW, shownV;
			if (!a_list.GetMember("Entry0", &entry0) || !entry0.IsObject() ||
				!entry0.GetMember("_x", &rowX) || !rowX.IsNumber() ||
				!entry0.GetMember("_y", &rowY) || !rowY.IsNumber() ||
				!entry0.GetMember("_height", &rowH) || !rowH.IsNumber() ||
				!entry0.GetMember("_width", &rowW) || !rowW.IsNumber() ||
				!a_list.GetMember("iListItemsShown", &shownV) || !shownV.IsNumber())
				return;

			const auto y = rowY.GetNumber() + rowH.GetNumber() * (shownV.GetNumber() + kDescriptionGap);
			const auto height = rowH.GetNumber() * kDescriptionRows;

			// Rows indent their own label, so the row's origin is not where the
			// text starts.
			auto         x = rowX.GetNumber();
			RE::GFxValue label, labelX;
			if (entry0.GetMember("textField", &label) && label.IsObject() &&
				label.GetMember("_x", &labelX) && labelX.IsNumber())
				x += labelX.GetNumber();

			// The border frames the list at a fixed width; a label's own is its
			// text, so it would wrap differently on every tab.
			auto         width = rowW.GetNumber();
			RE::GFxValue border, borderW;
			if (a_list.GetMember("border", &border) && border.IsObject() &&
				border.GetMember("_width", &borderW) && borderW.IsNumber() && borderW.GetNumber() > 0.0)
				width = borderW.GetNumber();
			width -= x - rowX.GetNumber();

			RE::GFxValue field;
			if (!a_list.GetMember("__cs_description", &field) || !field.IsObject()) {
				// Nothing to build until there is something to say.
				if (description.empty())
					return;

				// Two rows tall, so a sentence can wrap.
				const RE::GFxValue args[6] = { RE::GFxValue("__cs_description"), RE::GFxValue(23000.0),
					RE::GFxValue(x), RE::GFxValue(y), RE::GFxValue(width), RE::GFxValue(height) };
				a_list.Invoke("createTextField", nullptr, args, 6);

				if (!a_list.GetMember("__cs_description", &field) || !field.IsObject()) {
					logger::warn("VanillaSettingsEngine: couldn't create the description field");
					return;
				}

				field.SetMember("selectable", RE::GFxValue(false));
				field.SetMember("multiline", RE::GFxValue(true));
				field.SetMember("wordWrap", RE::GFxValue(true));

				// Shrinks to fit rather than losing its last line, as vanilla
				// does on its own labels: a translation runs well past the
				// English an author writes against.
				field.SetMember("textAutoSize", RE::GFxValue("shrink"));
				field.SetMember("_alpha", RE::GFxValue(100.0));

				// Same font as the rows - the engine default renders as boxes.
				// Forced left: a row label's own alignment would leave wrapped
				// lines hanging off the right edge.
				RE::GFxValue sourceText, format;
				if (entry0.GetMember("textField", &sourceText) && sourceText.IsObject() &&
					sourceText.Invoke("getTextFormat", &format) && format.IsObject()) {
					format.SetMember("align", RE::GFxValue("left"));

					// A notch under the rows it explains, and it buys room in
					// the same box. Relative to the row so it follows whatever
					// font the interface uses.
					RE::GFxValue size;
					if (format.GetMember("size", &size) && size.IsNumber())
						format.SetMember("size", RE::GFxValue(std::floor(size.GetNumber() * 0.85)));

					field.Invoke("setNewTextFormat", nullptr, &format, 1);
				}
			}

			field.SetMember("_x", RE::GFxValue(x));
			field.SetMember("_y", RE::GFxValue(y));
			field.SetMember("_width", RE::GFxValue(width));
			field.SetMember("_height", RE::GFxValue(height));
			field.SetMember("text", Text::MakeGFxString(description));
		}

		// Which of our settings a clip is showing, if any - vanilla's own
		// rows keep their small IDs and stay out of everything below.
		constexpr std::size_t kNoSetting = static_cast<std::size_t>(-1);

		std::size_t SettingForClip(RE::GFxValue& a_clip)
		{
			RE::GFxValue idVal;
			if (!a_clip.GetMember("ID", &idVal) || !idVal.IsNumber())
				return kNoSetting;

			const auto id = static_cast<std::uint32_t>(idVal.GetNumber());
			if (id < kIdBase || (id - kIdBase) >= g_settings.size())
				return kNoSetting;
			return id - kIdBase;
		}

		// The owning code is the source of truth, so its value is pulled back
		// whenever the clip disagrees - a widget replaying frames can come
		// back stale, and a click on a blocked row still moves it before
		// OptionChange is dispatched. Player edits reach the backend
		// synchronously, so this never fights input.
		//
		// Runs before anything else reads the clip, or the arrow pass below
		// would be drawn from a stale position.
		void SyncRowValue(RE::GFxValue& a_clip, const Setting& a_setting)
		{
			if (a_setting.getValue && a_setting.type != Type::kButton)
				SetIfChanged(a_clip, "value", static_cast<double>(a_setting.getValue()));
		}

		// Bounds come from the widget's own dataProvider, so at-limit arrows
		// are hidden on vanilla's dropdowns too, not just ours.
		void RefreshDropdownArrows(RE::GFxValue& a_clip)
		{
			RE::GFxValue movieType;
			if (!a_clip.GetMember("movieType", &movieType) || !movieType.IsNumber() ||
				static_cast<int>(movieType.GetNumber()) != static_cast<int>(Type::kDropdown))
				return;

			RE::GFxValue stepper, dataProvider, value;
			if (!a_clip.GetMember("OptionStepper_mc", &stepper) || !stepper.IsObject() ||
				!stepper.GetMember("dataProvider", &dataProvider) || !dataProvider.IsArray() ||
				!a_clip.GetMember("value", &value) || !value.IsNumber())
				return;

			const auto   current = static_cast<std::int64_t>(value.GetNumber());
			const auto   count = static_cast<std::int64_t>(dataProvider.GetArraySize());
			RE::GFxValue prevBtn, nextBtn;
			if (stepper.GetMember("prevBtn", &prevBtn) && prevBtn.IsObject())
				SetIfChanged(prevBtn, "_visible", current > 0);
			if (stepper.GetMember("nextBtn", &nextBtn) && nextBtn.IsObject())
				SetIfChanged(nextBtn, "_visible", current < count - 1);
		}

		// Label rows bind no widget, so the whole row is theirs; every other
		// row keeps the narrow field vanilla gave it. Both directions are
		// needed because clips are reused as the player changes tab - a field
		// widened for a label would otherwise stay wide under a slider.
		//
		// Alignment goes with the width: the field aligns right by default,
		// which only shows once it is wider than its text - so a label has to
		// say where it wants its text, every time the width changes.
		constexpr const char* kAlignNames[] = { "left", "center", "right" };

		void ApplyTextFieldWidth(RE::GFxValue& a_clip, const Setting* a_setting)
		{
			const bool isLabel = a_setting && a_setting->type == Type::kLabel;
			const auto width = isLabel ? g_rowWidth : g_textFieldWidth;
			if (width <= 0.0)
				return;

			RE::GFxValue textField;
			if (!a_clip.GetMember("textField", &textField) || !textField.IsObject())
				return;

			RE::GFxValue current;
			if (!textField.GetMember("_width", &current) || !current.IsNumber() ||
				std::abs(current.GetNumber() - width) > 0.5)
				textField.SetMember("_width", RE::GFxValue(width));

			// Set on every row, not just the labels: clips are reused, so a
			// row that states no alignment inherits whatever the last label
			// left. Checked apart from the width for the same reason - a clip
			// reused at the same width would keep the old alignment for good.
			const auto*  wanted = isLabel ? kAlignNames[static_cast<int>(a_setting->align)] : kAlignNames[0];
			RE::GFxValue format, align;
			if (!textField.Invoke("getTextFormat", &format) || !format.IsObject())
				return;
			if (format.GetMember("align", &align) && align.IsString() &&
				std::string_view(align.GetString()) == wanted)
				return;

			format.SetMember("align", RE::GFxValue(wanted));
			textField.Invoke("setNewTextFormat", nullptr, &format, 1);
			textField.Invoke("setTextFormat", nullptr, &format, 1);
		}

		// Applied to every row we own, guarded or not: styling only the
		// guarded ones left the rest looking disabled by comparison. A row
		// without an isEnabled is simply always usable.
		void ApplyRowState(RE::GFxValue& a_clip, const Setting& a_setting)
		{
			const bool enabled = !a_setting.isEnabled || a_setting.isEnabled();

			// The two colours BSScrollingList::SetEntryText itself uses.
			RE::GFxValue textField;
			if (a_clip.GetMember("textField", &textField) && textField.IsObject())
				SetIfChanged(textField, "textColor", enabled ? 0xFFFFFF : 0x606060, 0.5);

			// Dimmed with _alpha, not CLIK's "disabled": that one calls
			// gotoAndPlay, and ScrollBar only repositions its thumb when
			// position changes - so the thumb never recovers.
			//
			// SettingsOptionItem already drives _alpha for selection, so this
			// extends that rule: full brightness only when the row is both
			// selected and usable.
			const char* widgetField = a_setting.type == Type::kSlider ? "ScrollBar_mc" :
			                          a_setting.type == Type::kDropdown ? "OptionStepper_mc" :
			                                                               "CheckBox_mc";
			RE::GFxValue widget, selected;
			if (a_clip.GetMember(widgetField, &widget) && widget.IsObject() &&
				a_clip.GetMember("selected", &selected) && selected.IsBool())
				SetIfChanged(widget, "_alpha", enabled && selected.GetBool() ? 100.0 : 30.0, 0.5);
		}

		void RefreshRowText(RE::GFxValue& a_clip, Setting& a_setting)
		{
			switch (a_setting.type) {
			case Type::kSlider:
				// The label keeps vanilla's textAutoSize "shrink": the field
				// has a fixed width, so pinning the font size would only trade
				// a resize for a clipped label.
				if (a_setting.formatValue) {
					RE::GFxValue value;
					if (a_clip.GetMember("value", &value) && value.IsNumber())
						a_clip.SetMember(
							"text", Text::MakeGFxString(FormatLabel(a_setting, static_cast<float>(value.GetNumber()))));
				}
				break;

			case Type::kLabel:
				a_clip.SetMember("text", Text::MakeGFxString(GetLabelText(a_setting)));
				break;

			case Type::kButton: {
				// Held checked for a few ticks so the click is seen; clearing
				// it at once gives no feedback, leaving it reads as a toggle.
				const bool holding = a_setting.flashTicks > 0;
				if (holding)
					--a_setting.flashTicks;
				SetIfChanged(a_clip, "value", holding ? 1.0 : 0.0);
				break;
			}

			default:
				break;
			}
		}

		// -1 for a vanilla row or none at all.
		int SelectedSettingIndex(RE::GFxValue& a_list)
		{
			RE::GFxValue entries, selectedIdx;
			if (!a_list.GetMember("EntriesA", &entries) || !entries.IsArray() ||
				!a_list.GetMember("iSelectedIndex", &selectedIdx) || !selectedIdx.IsNumber())
				return -1;

			const auto index = selectedIdx.GetNumber();
			if (index < 0.0 || index >= static_cast<double>(entries.GetArraySize()))
				return -1;

			RE::GFxValue entry, idVal;
			if (!entries.GetElement(static_cast<std::uint32_t>(index), &entry) || !entry.IsObject() ||
				!entry.GetMember("ID", &idVal) || !idVal.IsNumber())
				return -1;

			const auto id = static_cast<std::uint32_t>(idVal.GetNumber());
			return id >= kIdBase ? static_cast<int>(id - kIdBase) : -1;
		}

		void CommitSettled(RE::GFxValue& a_list, bool a_haveList)
		{
			if (!a_haveList) {
				CommitAll("rows gone");
				return;
			}

			const auto selected = SelectedSettingIndex(a_list);
			const auto now = std::chrono::steady_clock::now();
			// By index: Commit calls owning code that may register a row, and
			// a deque keeps its references across a push_back but not
			// iterators.
			for (std::size_t i = 0; i < g_settings.size() && g_pendingCommits > 0; ++i) {
				auto& setting = g_settings[i];
				if (!setting.commitPending)
					continue;

				// Cleared as it is read, so a row that scrolls out of view
				// stops looking dragged.
				const bool dragging = setting.dragging;
				setting.dragging = false;
				if (dragging) {
					setting.wasDragging = true;
					continue;
				}

				if (setting.wasDragging)
					Commit(setting, "released");
				else if (static_cast<int>(i) != selected)
					Commit(setting, "deselected");
				else if (now >= setting.commitDeadline)
					Commit(setting, "settled");
			}
		}

		// CLIK's own flag, set on thumb press and cleared on release.
		bool IsScrollBarDragging(RE::GFxValue& a_clip)
		{
			RE::GFxValue scrollBar, dragging;
			return a_clip.GetMember("ScrollBar_mc", &scrollBar) && scrollBar.IsObject() &&
				scrollBar.GetMember("isDragging", &dragging) && dragging.IsBool() && dragging.GetBool();
		}

		// None of this is automatic in vanilla, so it reruns every tick.
		// Walks live clips rather than entry data: OptionsList::SetEntry
		// never calls SetEntryText, so textColor is set directly here.
		void RefreshRowAppearance(RE::GFxValue& a_list)
		{
			RE::GFxValue maxShownV;
			if (!a_list.GetMember("iMaxItemsShown", &maxShownV) || !maxShownV.IsNumber())
				return;
			const auto clipCount = static_cast<std::uint32_t>(maxShownV.GetNumber());

			// Captured before anything is widened, so the original width is
			// still there to restore.
			if (g_rowWidth <= 0.0) {
				RE::GFxValue entry0, width, textField, textWidth;
				if (a_list.GetMember("Entry0", &entry0) && entry0.IsObject() &&
					entry0.GetMember("_width", &width) && width.IsNumber()) {
					g_rowWidth = width.GetNumber();
					if (entry0.GetMember("textField", &textField) && textField.IsObject() &&
						textField.GetMember("_width", &textWidth) && textWidth.IsNumber())
						g_textFieldWidth = textWidth.GetNumber();
				}
			}

			for (std::uint32_t i = 0; i < clipCount; ++i) {
				RE::GFxValue clip;
				if (!a_list.GetMember(("Entry" + std::to_string(i)).c_str(), &clip) || !clip.IsObject())
					continue;

				const auto ours = SettingForClip(clip);
				if (ours != kNoSetting)
					SyncRowValue(clip, g_settings[ours]);

				// Vanilla's rows get the arrow pass and the field width too.
				RefreshDropdownArrows(clip);
				ApplyTextFieldWidth(clip, ours == kNoSetting ? nullptr : &g_settings[ours]);

				if (ours == kNoSetting)
					continue;
				ApplyRowState(clip, g_settings[ours]);
				RefreshRowText(clip, g_settings[ours]);
				if (g_settings[ours].commitPending)
					g_settings[ours].dragging = IsScrollBarDragging(clip);
			}
		}

		// Native tab (Gameplay/Display/Audio) already showing vanilla's own
		// entries - append ours to what's already there.
		void InjectNativeTab(RE::GFxMovie* a_view, RE::GFxValue& a_list, const std::string& a_tab)
		{
			RE::GFxValue entryList;
			if (!a_list.GetMember("EntriesA", &entryList) || !entryList.IsArray())
				return;
			if (HasOurEntries(entryList))
				return;

			std::uint32_t added = 0;
			for (std::size_t i = 0; i < g_settings.size(); ++i) {
				if (g_settings[i].tab != a_tab)
					continue;
				RE::GFxValue entry = BuildEntry(a_view, g_settings[i], i);
				entryList.Invoke("push", nullptr, &entry, 1);
				++added;
			}
			if (added == 0)
				return;

			ListRows::Ensure(a_list, entryList.GetArraySize(), "OptionsList");
			a_list.Invoke("InvalidateData");
			// Vanilla only recalculates for its own rows - without this,
			// scrolling stops short of the ones appended here.
			a_list.Invoke("CalculateMaxScrollPosition");
			// CalculateMaxScrollPosition measures entries by writing them into
			// Entry0 and never puts it back. Vanilla never sees that because
			// InvalidateData ends in UpdateList - calling it afterwards means
			// repairing the row here.
			a_list.Invoke("UpdateList");
			RefreshRowAppearance(a_list);
			logger::info("VanillaSettingsEngine: injected {} setting(s) into '{}'", added, a_tab);
		}

		// A tab opens at the top; the list otherwise keeps the last one's offset.
		void ScrollToTop(RE::GFxValue& a_list)
		{
			a_list.SetMember("iScrollPosition", RE::GFxValue(0.0));

			RE::GFxValue bar;
			if (a_list.GetMember("ListScrollbar", &bar) && bar.IsObject())
				bar.SetMember("position", RE::GFxValue(0.0));
		}

		// Room on screen, measured from the stage: the list's distance to the
		// bottom minus the margin it has at the top, which is how the page
		// centres it. The panel's own _height measures content, not space, so
		// it grows with every entry. -1 when it can't be worked out.
		double AvailableListHeight(RE::GFxMovieView* a_view, RE::GFxValue& a_list)
		{
			if (!a_view)
				return -1.0;

			RE::GFxValue point;
			a_view->CreateObject(&point);
			point.SetMember("x", RE::GFxValue(0.0));
			point.SetMember("y", RE::GFxValue(0.0));
			if (!a_list.Invoke("localToGlobal", nullptr, &point, 1))
				return -1.0;

			RE::GFxValue top;
			if (!point.GetMember("y", &top) || !top.IsNumber())
				return -1.0;

			const auto rect = a_view->GetVisibleFrameRect();
			return static_cast<double>(rect.bottom) - 2.0 * top.GetNumber();
		}

		// Nothing native populates OptionsList for the custom tab, so build
		// its entryList the same way SystemPage does for its own tabs.
		void ShowCustomTab(RE::GFxMovie* a_view, RE::GFxValue& a_page, const std::string& a_tab)
		{
			RE::GFxValue list;
			if (!GetOptionsList(a_page, list)) {
				logger::debug("VanillaSettingsEngine: ShowCustomTab('{}') - OptionsList not found", a_tab);
				return;
			}

			// onSettingsCategoryPress's switch only handles native tabs 0-2;
			// its default fallthrough clears EntriesA, so run it first and
			// populate after, not before.
			a_page.Invoke("onSettingsCategoryPress");

			// EntriesA, not entryList (that's the category list's field), and
			// mutated in place - the list holds a reference to the array it
			// was first bound to, so replacing it here wouldn't reach it.
			RE::GFxValue entryList;
			if (!list.GetMember("EntriesA", &entryList) || !entryList.IsArray()) {
				a_view->CreateArray(&entryList);
				list.SetMember("EntriesA", entryList);
			} else {
				entryList.SetMember("length", RE::GFxValue(0.0));
			}

			std::uint32_t count = 0;
			for (std::size_t i = 0; i < g_settings.size(); ++i) {
				if (g_settings[i].tab != a_tab)
					continue;
				RE::GFxValue entry = BuildEntry(a_view, g_settings[i], i);
				entryList.Invoke("push", nullptr, &entry, 1);
				++count;
			}

			ListRows::Ensure(list, count, "OptionsList(custom)");
			ScrollToTop(list);
			list.Invoke("InvalidateData");
			list.Invoke("CalculateMaxScrollPosition");
			list.Invoke("UpdateList");
			RefreshRowAppearance(list);

			g_showingCustomTab = true;
			logger::info("VanillaSettingsEngine: showing custom tab '{}' ({} setting(s))", a_tab, count);
		}

		// Pressing a row in the Settings category list toggles which tab's
		// rows OptionsList shows - forwarded to vanilla's own handler for a
		// native tab, or to ShowCustomTab for the custom one.
		class SettingsPressHandler : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				const std::lock_guard lock(g_settingsMutex);

				if (!a_params.thisPtr) {
					logger::debug("VanillaSettingsEngine: SettingsPressHandler - no thisPtr");
					return;
				}
				RE::GFxValue page;
				if (!a_params.thisPtr->GetMember("__cs_page", &page)) {
					logger::debug("VanillaSettingsEngine: SettingsPressHandler - __cs_page not found");
					return;
				}

				RE::GFxValue panel, list, idxVal;
				if (!page.GetMember("SettingsPanel", &panel) || !panel.IsObject() ||
					!panel.GetMember("List_mc", &list) || !list.IsObject() ||
					!list.GetMember("selectedIndex", &idxVal) || !idxVal.IsNumber()) {
					logger::debug("VanillaSettingsEngine: SettingsPressHandler - couldn't read selectedIndex");
					return;
				}

				const int  selected = static_cast<int>(idxVal.GetNumber());
				const auto tab = TabAt(selected);
				logger::debug("VanillaSettingsEngine: tab press [{}] '{}'", selected, tab.empty() ? "?" : tab);
				if (tab.empty())
					return;

				if (IsNativeTab(tab)) {
					if (a_params.argCount >= 1 && a_params.args)
						page.Invoke("onSettingsCategoryPress", nullptr, a_params.args, 1);
					else
						page.Invoke("onSettingsCategoryPress");
					return;
				}

				ShowCustomTab(a_params.movie, page, tab);
			}
		};
		SettingsPressHandler g_settingsPressHandler;

		// Appends the custom tab to the Settings category list, once it has
		// been populated with vanilla's own Gameplay/Display/Audio entries -
		// mirrors InjectNativeTab, but for the list of tabs itself rather
		// than a tab's rows.
		void InjectSettingsList(RE::GFxMovieView* a_view, RE::GFxValue& a_page)
		{
			if (g_settingsListInjected || !g_haveCustomTab)
				return;

			RE::GFxValue panel, list;
			const bool   havePanel = a_page.GetMember("SettingsPanel", &panel) && panel.IsObject();
			const bool   haveList = havePanel && panel.GetMember("List_mc", &list) && list.IsObject();
			if (!haveList) {
				logger::debug("VanillaSettingsEngine: SettingsPanel found={} List_mc found={}", havePanel, haveList);
				return;
			}

			RE::GFxValue   entryList;
			const bool     haveEntryList = list.GetMember("entryList", &entryList) && entryList.IsArray();
			const uint32_t entryCount = haveEntryList ? entryList.GetArraySize() : 0;
			if (!haveEntryList || entryCount < kNativeTabCount) {
				logger::debug("VanillaSettingsEngine: SettingsList.entryList found={} size={} - not populated yet",
					haveEntryList, entryCount);
				return;
			}

			RE::GFxValue entry;
			a_view->CreateObject(&entry);
			entry.SetMember("text", Text::MakeGFxString(g_customTab));
			entryList.Invoke("push", nullptr, &entry, 1);

			ListRows::Ensure(list, entryList.GetArraySize(), "SettingsList");

			// border._height, not List_mc's own, caps how many rows render
			// without scrolling, and it is sized off Entry0._height - the
			// figure UpdateList accumulates against.
			//
			// Capped at what the screen can show: past that, rows are laid out
			// off screen and CalculateMaxScrollPosition, measuring against the
			// same height, reports nothing to scroll - leaving the tab both
			// invisible and unreachable.
			RE::GFxValue entry0, border;
			if (list.GetMember("Entry0", &entry0) && list.GetMember("border", &border) && border.IsObject()) {
				RE::GFxValue rowHeight, curHeight, panelHeight;
				if (entry0.GetMember("_height", &rowHeight) && rowHeight.IsNumber() && rowHeight.GetNumber() > 0.0 &&
					border.GetMember("_height", &curHeight) && curHeight.IsNumber() &&
					panel.GetMember("_height", &panelHeight) && panelHeight.IsNumber() &&
					panelHeight.GetNumber() > 0.0) {
					// The slack absorbs Flash rounding the height to twips,
					// which otherwise puts an exact fit just over UpdateList's
					// "iItemHeightSum <= fListHeight" and drops the last row.
					auto       grown = rowHeight.GetNumber() * static_cast<double>(entryList.GetArraySize()) + 0.5;
					const auto available = AvailableListHeight(a_view, list);
					if (available > rowHeight.GetNumber() && grown > available)
						grown = available;

					if (grown > curHeight.GetNumber())
						border.SetMember("_height", RE::GFxValue(grown));
				}
			}

			list.Invoke("InvalidateData");
			// Without this the appended row exists in the data but stays
			// clipped out of the viewport entirely.
			list.Invoke("CalculateMaxScrollPosition");
			list.Invoke("UpdateList");

			EnsureScrollbar(list);

			RE::GFxValue scope, fn;
			a_view->CreateObject(&scope);
			a_view->CreateFunction(&fn, &g_settingsPressHandler);
			scope.SetMember("onCSSettingsPress", fn);
			scope.SetMember("__cs_page", a_page);

			const RE::GFxValue rm[3] = { RE::GFxValue("itemPress"), a_page, RE::GFxValue("onSettingsCategoryPress") };
			list.Invoke("removeEventListener", nullptr, rm, 3);
			const RE::GFxValue add[3] = { RE::GFxValue("itemPress"), scope, RE::GFxValue("onCSSettingsPress") };
			list.Invoke("addEventListener", nullptr, add, 3);

			g_settingsListInjected = true;
			logger::info("VanillaSettingsEngine: added the '{}' tab to the Settings list", g_customTab);
		}

		// Clips are recycled as the list scrolls, so the entry says which
		// one is currently drawing it.
		bool ClipForEntry(RE::GFxValue& a_list, RE::GFxValue& a_entry, RE::GFxValue& a_clip)
		{
			RE::GFxValue clipIndex;
			if (!a_entry.GetMember("clipIndex", &clipIndex) || !clipIndex.IsNumber())
				return false;
			const auto name = "Entry" + std::to_string(static_cast<int>(clipIndex.GetNumber()));
			return a_list.GetMember(name.c_str(), &a_clip) && a_clip.IsObject();
		}

		// SettingsOptionItem toggles the box itself on release, so a press
		// that landed there must be left alone or the row flips twice.
		// Unreadable bounds count as a hit: a missed toggle beats a double.
		bool PressedOnCheckbox(RE::GFxValue& a_clip, RE::GFxValue& a_checkBox)
		{
			RE::GFxValue mouseX, mouseY, bounds, xMin, xMax, yMin, yMax;
			if (!a_clip.GetMember("_xmouse", &mouseX) || !mouseX.IsNumber() ||
				!a_clip.GetMember("_ymouse", &mouseY) || !mouseY.IsNumber() ||
				!a_checkBox.Invoke("getBounds", &bounds, &a_clip, 1) || !bounds.IsObject() ||
				!bounds.GetMember("xMin", &xMin) || !xMin.IsNumber() ||
				!bounds.GetMember("xMax", &xMax) || !xMax.IsNumber() ||
				!bounds.GetMember("yMin", &yMin) || !yMin.IsNumber() ||
				!bounds.GetMember("yMax", &yMax) || !yMax.IsNumber()) {
				logger::debug("VanillaSettingsEngine: PressedOnCheckbox - couldn't measure the box");
				return true;
			}

			return mouseX.GetNumber() >= xMin.GetNumber() && mouseX.GetNumber() <= xMax.GetNumber() &&
				mouseY.GetNumber() >= yMin.GetNumber() && mouseY.GetNumber() <= yMax.GetNumber();
		}

		// Handed to the widget's own ToggleCheckbox, which moves the frame,
		// dispatches OptionChange and writes the entry back. Setting the
		// entry directly wouldn't show: OptionsList::SetEntry only pushes a
		// value onto a clip whose ID changed. movieType 2 is the checkbox.
		void ToggleVanillaCheckbox(RE::GFxValue& a_list)
		{
			RE::GFxValue entries, selectedIdx, entry, typeVal;
			if (!a_list.GetMember("EntriesA", &entries) || !entries.IsArray() ||
				!a_list.GetMember("iSelectedIndex", &selectedIdx) || !selectedIdx.IsNumber() ||
				selectedIdx.GetNumber() < 0.0 ||
				!entries.GetElement(static_cast<std::uint32_t>(selectedIdx.GetNumber()), &entry) ||
				!entry.IsObject() || !entry.GetMember("movieType", &typeVal) || !typeVal.IsNumber() ||
				static_cast<int>(typeVal.GetNumber()) != 2) {
				return;
			}

			RE::GFxValue clip, checkBox;
			if (!ClipForEntry(a_list, entry, clip) || !clip.GetMember("CheckBox_mc", &checkBox) ||
				!checkBox.IsObject())
				return;

			if (PressedOnCheckbox(clip, checkBox))
				return;

			clip.Invoke("ToggleCheckbox");
		}

		// Pressing a row toggles it, rather than only the widget on its right.
		class OptionsPressHandler : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				const std::lock_guard lock(g_settingsMutex);

				RE::GFxValue page, list;
				if (!a_params.thisPtr || !a_params.thisPtr->GetMember("__cs_page", &page) ||
					!GetOptionsList(page, list))
					return;

				const auto idx = SelectedSettingIndex(list);
				if (idx < 0) {
					ToggleVanillaCheckbox(list);
					return;
				}
				if (static_cast<std::size_t>(idx) >= g_settings.size())
					return;

				auto& setting = g_settings[idx];
				if (setting.type != Type::kCheckbox && setting.type != Type::kButton)
					return;
				if (setting.isEnabled && !setting.isEnabled())
					return;
				if (!ClaimToggle(idx))
					return;

				const auto value = setting.type == Type::kButton ? 1.0f :
					setting.getValue && setting.getValue() != 0.0f ? 0.0f :
					                                                  1.0f;

				if (setting.type == Type::kButton) {
					if (!setting.onPress)
						return;
					setting.flashTicks = kButtonFlashTicks;
					setting.onPress();
				} else {
					if (!setting.onChange)
						return;
					setting.onChange(value);
					QueueCommit(setting, value);
				}

				RE::GFxValue entries, selectedIdx, entry;
				if (list.GetMember("EntriesA", &entries) && entries.IsArray() &&
					list.GetMember("iSelectedIndex", &selectedIdx) && selectedIdx.IsNumber() &&
					entries.GetElement(static_cast<std::uint32_t>(selectedIdx.GetNumber()), &entry) &&
					entry.IsObject()) {
					entry.SetMember("value", RE::GFxValue(static_cast<double>(value)));
					list.Invoke("InvalidateData");
					list.Invoke("UpdateList");
				}
			}
		};
		OptionsPressHandler g_optionsPressHandler;
		bool                g_optionsPressHooked = false;
	}

	void Tick(RE::JournalMenu* a_this, RE::GFxMovieView* a_view, RE::GFxValue& a_systemPage)
	{
		const std::lock_guard lock(g_settingsMutex);

		if (g_settings.empty())
			return;

		if (!g_hooked && a_this->fxDelegate)
			InstallHooks(a_this->fxDelegate.get());

		InjectSettingsList(a_view, a_systemPage);

		// Kept in step with scrolling, which vanilla drives on its own.
		if (g_settingsListInjected) {
			RE::GFxValue panel, tabList;
			if (a_systemPage.GetMember("SettingsPanel", &panel) && panel.IsObject() &&
				panel.GetMember("List_mc", &tabList) && tabList.IsObject())
				EnsureScrollbar(tabList);
		}

		RE::GFxValue list;
		const bool   haveList = GetOptionsList(a_systemPage, list);
		if (!g_showingCustomTab && g_haveCurrentTab && IsNativeTab(g_currentTab) && haveList) {
			InjectNativeTab(a_view, list, g_currentTab);
		}

		if (haveList && !g_optionsPressHooked) {
			g_optionsPressHooked = true;

			RE::GFxValue scope, fn;
			a_view->CreateObject(&scope);
			a_view->CreateFunction(&fn, &g_optionsPressHandler);
			scope.SetMember("onCSOptionPress", fn);
			scope.SetMember("__cs_page", a_systemPage);

			// Added, not swapped: vanilla's own handler still runs.
			const RE::GFxValue add[3] = { RE::GFxValue("itemPress"), scope, RE::GFxValue("onCSOptionPress") };
			list.Invoke("addEventListener", nullptr, add, 3);
			logger::debug("VanillaSettingsEngine: OptionsList itemPress hooked");
		}

		// Every tick rather than on change: a freshly duplicated clip can take
		// more than one frame to finish initializing.
		if (haveList) {
			RefreshRowAppearance(list);
			RefreshDescription(list);
			EnsureScrollbar(list);
		}

		if (g_pendingCommits > 0)
			CommitSettled(list, haveList);
	}

	void Reset()
	{
		const std::lock_guard lock(g_settingsMutex);

		CommitAll("menu closed");

		for (auto& setting : g_settings)
			setting.flashTicks = 0;

		g_hooked = false;
		g_haveCurrentTab = false;
		g_settingsListInjected = false;
		g_optionsPressHooked = false;
		g_showingCustomTab = false;
		g_rowWidth = 0.0;
		g_textFieldWidth = 0.0;
		g_originalOptionChange = {};
		g_originalRequestGameplay = {};
		g_originalRequestDisplay = {};
		g_originalRequestAudio = {};
	}

	bool AddSetting(std::string a_tab, Type a_type, std::string a_label, std::function<float()> a_getValue,
		std::function<void(float)> a_onChange, float a_defaultValue, std::vector<std::string> a_options,
		std::function<bool()> a_isEnabled, std::function<void(float, char*, int)> a_formatValue,
		std::string a_description, std::function<void(float)> a_onCommit)
	{
		const std::lock_guard lock(g_settingsMutex);

		if (a_tab.empty() || a_label.empty() || !a_onChange)
			return false;
		// kLabel and kButton have their own calls; anything else binds no widget.
		if (a_type != Type::kSlider && a_type != Type::kDropdown && a_type != Type::kCheckbox) {
			logger::warn("VanillaSettingsEngine: '{}' asked for type {} - use AddLabel or AddButton", a_label,
				static_cast<int>(a_type));
			return false;
		}
		if (!IsNativeTab(a_tab))
			RegisterCustomTab(a_tab);
		g_settings.push_back({ std::move(a_tab), a_type, std::move(a_label), std::move(a_getValue),
			std::move(a_onChange), std::move(a_onCommit), a_defaultValue, std::move(a_options),
			std::move(a_isEnabled), std::move(a_formatValue), nullptr, nullptr, std::move(a_description),
			Align::kLeft });
		return true;
	}

	bool AddLabel(std::string a_tab, std::function<void(char*, int)> a_getText, Align a_align)
	{
		const std::lock_guard lock(g_settingsMutex);

		if (a_tab.empty() || !a_getText)
			return false;
		if (!IsNativeTab(a_tab))
			RegisterCustomTab(a_tab);
		g_settings.push_back({ std::move(a_tab), Type::kLabel, {}, nullptr, nullptr, nullptr, 0.0f, {}, nullptr,
			nullptr, std::move(a_getText), nullptr, {}, a_align });
		return true;
	}

	bool AddButton(std::string a_tab, std::string a_label, std::function<void()> a_onPress,
		std::string a_description, std::function<bool()> a_isEnabled)
	{
		const std::lock_guard lock(g_settingsMutex);

		if (a_tab.empty() || a_label.empty() || !a_onPress)
			return false;
		if (!IsNativeTab(a_tab))
			RegisterCustomTab(a_tab);
		g_settings.push_back({ std::move(a_tab), Type::kButton, std::move(a_label), nullptr, nullptr, nullptr, 0.0f,
			{}, std::move(a_isEnabled), nullptr, nullptr, std::move(a_onPress), std::move(a_description),
			Align::kLeft });
		return true;
	}
}
