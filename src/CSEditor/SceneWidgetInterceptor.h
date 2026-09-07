#pragma once

#include <string_view>

#include "SceneSettingsManager.h"

struct Feature;

/// Intercepts ImGui widget calls so a Scene Manager replica of a feature's DrawSettings can bind
/// each control to a scene entry. Detours are installed once and stay inert until a Scope is alive.
namespace SceneWidgetInterceptor
{
	/// What the armed replica is editing. Copied into the interceptor for the scope's lifetime.
	struct Context
	{
		Feature* feature = nullptr;
		SceneSettingsManager::SceneContextId contextId;
		/// False writes every edit to all six periods, which is what "no time of day" means.
		bool perPeriod = true;
	};

	/** @brief Installs the detours. Idempotent; call from the render thread before the first frame. */
	bool Install();

	bool IsInstalled();

	/// True while a Scope is active, i.e. a feature's DrawSettings is being replicated for scene
	/// authoring. Features can use this to hide non-setting UI (debug views, buffer viewers) that
	/// has no scene-context meaning.
	bool IsArmed();

	/// Empty while healthy; otherwise names the entry point whose attach failed.
	std::string_view GetInstallError();

	/// Arms interception for the duration of one replica's DrawSettings call.
	class Scope
	{
	public:
		explicit Scope(const Context& context);
		~Scope();

		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;

	private:
		Context previous;
		bool previousArmed;
	};

	/// A control that binds a stack temporary rather than its settings member, as the Util:: wrappers
	/// that rescale a value before handing it to ImGui do.
	struct Proxy
	{
		/// The settings member the control stands for, which is what the catalog resolves.
		const void* member = nullptr;
		/// Temporary value = member value * displayScale, matching the catalog's displayScale.
		float displayScale = 1.0f;
	};

	/// Declares the proxy for the ImGui control drawn inside it, so a rescaled temporary still binds.
	class ProxyScope
	{
	public:
		ProxyScope(const void* a_member, float a_displayScale);
		~ProxyScope();

		ProxyScope(const ProxyScope&) = delete;
		ProxyScope& operator=(const ProxyScope&) = delete;

	private:
		Proxy previous;
	};

	/// The armed context, or nullptr when unarmed. For SceneWidgetBinding only.
	const Context* GetArmedContext();

	/// The proxy declared around the control being drawn, or nullptr. For SceneWidgetBinding only.
	const Proxy* GetArmedProxy();
}
