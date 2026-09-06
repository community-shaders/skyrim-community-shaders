// Adapted from NativeSystemMenuFramework (https://github.com/RoseEden30/NativeSystemMenuFramework)
// License: GPL-3.0-or-later

#include "NativeMenu/Vendor/SystemMenuHook.h"

#include "NativeMenu/Vendor/VanillaSettingsEngine.h"

#include "Globals.h"

#include <atomic>
#include <vector>

namespace NativeMenu::Vendor::SystemMenuHook
{
	namespace
	{
		constexpr const char* kMenuRootPath = "_root.QuestJournalFader.Menu_mc";
		constexpr const char* kSystemPageMember = "__cs_systemPage";
		constexpr int kInjectionRetryTicks = 150;

		std::atomic<bool> g_injected{ false };
		std::atomic<int>  g_injectTicks{ 0 };

		bool IsSystemPage(const RE::GFxValue& a_value)
		{
			return a_value.IsObject() && a_value.HasMember("CategoryList") && a_value.HasMember("SettingsList") &&
				a_value.HasMember("MappingList");
		}

		// BFS for SystemPage by structural signature.
		bool FindSystemPage(const RE::GFxValue& a_root, RE::GFxValue& a_out, int a_maxDepth)
		{
			std::vector<RE::GFxValue> current{ a_root };
			int                       budget = 2500;
			for (int depth = 0; depth <= a_maxDepth && !current.empty(); ++depth) {
				std::vector<RE::GFxValue> next;
				for (auto& node : current) {
					if (--budget <= 0)
						return false;
					if (IsSystemPage(node)) {
						a_out = node;
						return true;
					}
					if (depth == a_maxDepth)
						continue;

					node.VisitMembers([&next](const char* a_name, const RE::GFxValue& a_val) {
						static constexpr std::string_view skip[] = { "_parent", "_root", "_global", "_level0",
							"__proto__", "prototype", "constructor", "_listeners", "stage" };
						for (auto s : skip)
							if (s == a_name)
								return;
						if (a_val.IsObject() || a_val.IsArray() || a_val.IsDisplayObject())
							next.push_back(a_val);
					});
				}
				current.swap(next);
			}
			return false;
		}

		void Tick(RE::JournalMenu* a_this)
		{
			if (!a_this || !a_this->uiMovie)
				return;
			auto* view = a_this->uiMovie.get();

			if (!g_injected.load()) {
				if (g_injectTicks.load() <= 0)
					return;
				g_injectTicks.fetch_sub(1);

				RE::GFxValue root, page;
				if (view->GetVariable(&root, kMenuRootPath) && FindSystemPage(root, page, 10)) {
					root.SetMember(kSystemPageMember, page);
					g_injected.store(true);
					logger::info("NativeMenu: found the System menu's SettingsPage");
				}
				return;
			}

			RE::GFxValue root, systemPage;
			if (!view->GetVariable(&root, kMenuRootPath) ||
				!root.GetMember(kSystemPageMember, &systemPage) || !systemPage.IsObject())
				return;

			VanillaSettingsEngine::Tick(a_this, view, systemPage);
		}

		struct JournalMenu_AdvanceMovie
		{
			static void thunk(RE::JournalMenu* a_this, float a_interval, std::uint32_t a_currentTime)
			{
				func(a_this, a_interval, a_currentTime);
				Tick(a_this);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// Reset injection state each time the System menu opens.
		class JournalSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			static JournalSink* GetSingleton()
			{
				static JournalSink singleton;
				return &singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(
				const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (a_event && a_event->menuName == RE::JournalMenu::MENU_NAME) {
					g_injected.store(false);
					g_injectTicks.store(a_event->opening ? kInjectionRetryTicks : 0);
					VanillaSettingsEngine::Reset();
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Install()
	{
		static bool installed = false;
		if (installed)
			return;
		installed = true;

		stl::write_vfunc<0x5, JournalMenu_AdvanceMovie>(RE::JournalMenu::VTABLE[0]);
		if (auto* ui = globals::game::ui)
			ui->GetEventSource<RE::MenuOpenCloseEvent>()->AddEventSink(JournalSink::GetSingleton());
		logger::info("NativeMenu: System menu hooks installed");
	}
}
