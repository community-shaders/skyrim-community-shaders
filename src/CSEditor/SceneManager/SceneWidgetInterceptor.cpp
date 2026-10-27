#include "SceneWidgetInterceptor.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

#include <detours/detours.h>
#include <imgui.h>

#include "SceneSettingsCatalog.generated.h"
#include "SceneWidgetBinding.h"
#include "Utils/UIAnim.h"

namespace
{
	using namespace SceneWidgetInterceptor;

	bool installed = false;
	std::string installError;

	bool armed = false;
	Context armedContext;
	Proxy armedProxy;

	// DetourAttach repoints each at a trampoline into the real ImGui function.
	auto* RealSliderFloat = &ImGui::SliderFloat;
	auto* RealSliderFloat2 = &ImGui::SliderFloat2;
	auto* RealSliderFloat3 = &ImGui::SliderFloat3;
	auto* RealSliderFloat4 = &ImGui::SliderFloat4;
	auto* RealDragFloat = &ImGui::DragFloat;
	auto* RealDragFloat2 = &ImGui::DragFloat2;
	auto* RealDragFloat3 = &ImGui::DragFloat3;
	auto* RealDragFloat4 = &ImGui::DragFloat4;
	auto* RealInputFloat = &ImGui::InputFloat;
	auto* RealInputFloat2 = &ImGui::InputFloat2;
	auto* RealInputFloat3 = &ImGui::InputFloat3;
	auto* RealInputInt = &ImGui::InputInt;
	auto* RealSliderInt = &ImGui::SliderInt;
	auto* RealSliderScalar = &ImGui::SliderScalar;
	auto* RealSliderAngle = &ImGui::SliderAngle;
	auto* RealCheckbox = &ImGui::Checkbox;
	auto* RealColorEdit3 = &ImGui::ColorEdit3;
	auto* RealColorEdit4 = &ImGui::ColorEdit4;
	// Both Combo overloads are used in src/Features, and RadioButton is overloaded too, so the
	// address of each has to be disambiguated by its exact signature.
	auto* RealComboArray = static_cast<bool (*)(const char*, int*, const char* const[], int, int)>(
		&ImGui::Combo);
	auto* RealComboZeroSeparated = static_cast<bool (*)(const char*, int*, const char*, int)>(
		&ImGui::Combo);
	auto* RealRadioButton = static_cast<bool (*)(const char*, int*, int)>(&ImGui::RadioButton);

	// ImGui delegates between entry points we also detour (SliderFloat -> SliderScalar,
	// ColorEdit3 -> ColorEdit4), and the gutter toggle is itself a Checkbox: only the outermost binds.
	bool insideInterceptedCall = false;

	/** @brief RAII scope that sets insideInterceptedCall. */
	struct InterceptedCall
	{
		InterceptedCall() { insideInterceptedCall = true; }
		~InterceptedCall() { insideInterceptedCall = false; }

		InterceptedCall(const InterceptedCall&) = delete;
		InterceptedCall& operator=(const InterceptedCall&) = delete;
	};

	/** @brief True when armed and not nested inside another intercepted call. */
	bool ShouldIntercept()
	{
		return armed && !insideInterceptedCall;
	}
}

namespace
{
	/** @brief Whether a palette drop may land; BeginDragDropTarget ignores BeginDisabled, so greyed
	 *  controls must refuse it here or a drop would rewrite the base value. */
	bool CanAcceptPaletteDrop(const SceneWidgetBinding::Guard& a_guard)
	{
		const auto state = a_guard.GetState();
		return state != SceneWidgetBinding::State::Unbound &&
		       state != SceneWidgetBinding::State::Unavailable;
	}

	bool DetouredSliderFloat(const char* label, float* v, float vMin, float vMax,
		const char* format, ImGuiSliderFlags flags)
	{
		if (!ShouldIntercept())
			return RealSliderFloat(label, v, vMin, vMax, format, flags);
		InterceptedCall interceptedCall;
		SceneWidgetBinding::Guard guard(label, SceneWidgetBinding::Value::Float(v));
		return guard.Finish(RealSliderFloat(label, guard.Float(), vMin, vMax, format, flags));
	}

	SceneWidgetBinding::Value MakeScalarValue(float* a_v) { return SceneWidgetBinding::Value::Float(a_v); }
	SceneWidgetBinding::Value MakeScalarValue(int* a_v) { return SceneWidgetBinding::Value::Int(a_v); }

	/** @brief Binds a one-value float or int control; the scalar widgets differ only in trailing args. */
	template <typename T, typename Real, typename... Args>
	bool InterceptScalar(Real a_real, const char* a_label, T* a_v, Args... a_args)
	{
		InterceptedCall interceptedCall;
		SceneWidgetBinding::Guard guard(a_label, MakeScalarValue(a_v));
		return guard.Finish(a_real(a_label, static_cast<T*>(guard.Raw()), a_args...));
	}

	bool DetouredDragFloat(const char* label, float* v, float speed, float vMin, float vMax,
		const char* format, ImGuiSliderFlags flags)
	{
		if (!ShouldIntercept())
			return RealDragFloat(label, v, speed, vMin, vMax, format, flags);
		return InterceptScalar(RealDragFloat, label, v, speed, vMin, vMax, format, flags);
	}

	bool DetouredInputFloat(const char* label, float* v, float step, float stepFast, const char* format,
		ImGuiInputTextFlags flags)
	{
		if (!ShouldIntercept())
			return RealInputFloat(label, v, step, stepFast, format, flags);
		return InterceptScalar(RealInputFloat, label, v, step, stepFast, format, flags);
	}

	bool DetouredInputInt(const char* label, int* v, int step, int stepFast, ImGuiInputTextFlags flags)
	{
		if (!ShouldIntercept())
			return RealInputInt(label, v, step, stepFast, flags);
		return InterceptScalar(RealInputInt, label, v, step, stepFast, flags);
	}

	/** @brief Binds an N-component float control; the vector widgets differ only in N and trailing args. */
	template <std::uint8_t ComponentCount, typename Real, typename... Args>
	bool InterceptFloatVector(Real a_real, const char* a_label, float* a_v, Args... a_args)
	{
		static_assert(ComponentCount <= SceneWidgetBinding::Value::kMaxComponents);
		InterceptedCall interceptedCall;
		SceneWidgetBinding::Guard guard(a_label, SceneWidgetBinding::Value::FloatVector(a_v, ComponentCount));
		return guard.Finish(a_real(a_label, guard.Float(), a_args...));
	}

	bool DetouredSliderFloat2(const char* label, float v[2], float vMin, float vMax,
		const char* format, ImGuiSliderFlags flags)
	{
		if (!ShouldIntercept())
			return RealSliderFloat2(label, v, vMin, vMax, format, flags);
		return InterceptFloatVector<2>(RealSliderFloat2, label, v, vMin, vMax, format, flags);
	}

	bool DetouredSliderFloat3(const char* label, float v[3], float vMin, float vMax,
		const char* format, ImGuiSliderFlags flags)
	{
		if (!ShouldIntercept())
			return RealSliderFloat3(label, v, vMin, vMax, format, flags);
		return InterceptFloatVector<3>(RealSliderFloat3, label, v, vMin, vMax, format, flags);
	}

	bool DetouredSliderFloat4(const char* label, float v[4], float vMin, float vMax,
		const char* format, ImGuiSliderFlags flags)
	{
		if (!ShouldIntercept())
			return RealSliderFloat4(label, v, vMin, vMax, format, flags);
		return InterceptFloatVector<4>(RealSliderFloat4, label, v, vMin, vMax, format, flags);
	}

	bool DetouredDragFloat2(const char* label, float v[2], float speed, float vMin, float vMax,
		const char* format, ImGuiSliderFlags flags)
	{
		if (!ShouldIntercept())
			return RealDragFloat2(label, v, speed, vMin, vMax, format, flags);
		return InterceptFloatVector<2>(RealDragFloat2, label, v, speed, vMin, vMax, format, flags);
	}

	bool DetouredDragFloat3(const char* label, float v[3], float speed, float vMin, float vMax,
		const char* format, ImGuiSliderFlags flags)
	{
		if (!ShouldIntercept())
			return RealDragFloat3(label, v, speed, vMin, vMax, format, flags);
		return InterceptFloatVector<3>(RealDragFloat3, label, v, speed, vMin, vMax, format, flags);
	}

	bool DetouredDragFloat4(const char* label, float v[4], float speed, float vMin, float vMax,
		const char* format, ImGuiSliderFlags flags)
	{
		if (!ShouldIntercept())
			return RealDragFloat4(label, v, speed, vMin, vMax, format, flags);
		return InterceptFloatVector<4>(RealDragFloat4, label, v, speed, vMin, vMax, format, flags);
	}

	bool DetouredInputFloat2(const char* label, float v[2], const char* format, ImGuiInputTextFlags flags)
	{
		if (!ShouldIntercept())
			return RealInputFloat2(label, v, format, flags);
		return InterceptFloatVector<2>(RealInputFloat2, label, v, format, flags);
	}

	bool DetouredInputFloat3(const char* label, float v[3], const char* format, ImGuiInputTextFlags flags)
	{
		if (!ShouldIntercept())
			return RealInputFloat3(label, v, format, flags);
		return InterceptFloatVector<3>(RealInputFloat3, label, v, format, flags);
	}

	bool DetouredSliderInt(const char* label, int* v, int vMin, int vMax,
		const char* format, ImGuiSliderFlags flags)
	{
		if (!ShouldIntercept())
			return RealSliderInt(label, v, vMin, vMax, format, flags);
		InterceptedCall interceptedCall;
		SceneWidgetBinding::Guard guard(label, SceneWidgetBinding::Value::Int(v));
		return guard.Finish(RealSliderInt(label, guard.Int(), vMin, vMax, format, flags));
	}

	bool DetouredSliderScalar(const char* label, ImGuiDataType dataType, void* data,
		const void* min, const void* max, const char* format, ImGuiSliderFlags flags)
	{
		// Every slider ends here, so the easing for externally changed values applies to all of them, bound or not
		if (!ShouldIntercept())
			return Util::Anim::EasedSliderScalar(RealSliderScalar, label, dataType, data, min, max, format, flags);
		InterceptedCall interceptedCall;
		SceneWidgetBinding::Guard guard(label, SceneWidgetBinding::Value::Scalar(data, dataType));
		return guard.Finish(Util::Anim::EasedSliderScalar(RealSliderScalar, label, dataType, guard.Raw(), min, max, format, flags));
	}

	bool DetouredSliderAngle(const char* label, float* radians, float degreesMin, float degreesMax,
		const char* format, ImGuiSliderFlags flags)
	{
		if (!ShouldIntercept())
			return RealSliderAngle(label, radians, degreesMin, degreesMax, format, flags);
		InterceptedCall interceptedCall;
		SceneWidgetBinding::Guard guard(label, SceneWidgetBinding::Value::Float(radians));
		return guard.Finish(RealSliderAngle(label, guard.Float(), degreesMin, degreesMax, format, flags));
	}

	bool DetouredCheckbox(const char* label, bool* v)
	{
		// Every checkbox ends here, so the check mark's fade applies to all of them, bound or not
		if (!ShouldIntercept())
			return Util::Anim::EasedCheckbox(RealCheckbox, label, v);
		InterceptedCall interceptedCall;
		SceneWidgetBinding::Guard guard(label, SceneWidgetBinding::Value::Bool(v));
		return guard.Finish(Util::Anim::EasedCheckbox(RealCheckbox, label, guard.Bool()));
	}

	/** @brief Accepts the palette's custom "COLOR_DND" payload, which the real color widgets ignore,
	 *  so every intercepted color control must call this. */
	bool AcceptPaletteColorDrop(float* col, int componentCount)
	{
		bool accepted = false;
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("COLOR_DND")) {
				if (payload->DataSize == sizeof(float3)) {
					std::memcpy(col, payload->Data, sizeof(float) * std::min(componentCount, 3));
					accepted = true;
				}
			}
			ImGui::EndDragDropTarget();
		}
		return accepted;
	}

	/// The two color controls differ only in how many components they bind and accept a drop into.
	template <typename Real>
	bool InterceptColorEdit(Real a_real, const char* a_label, float* a_col, int a_componentCount,
		ImGuiColorEditFlags a_flags)
	{
		InterceptedCall interceptedCall;
		SceneWidgetBinding::Guard guard(a_label,
			SceneWidgetBinding::Value::FloatVector(a_col, static_cast<std::uint8_t>(a_componentCount)));
		const bool changed = a_real(a_label, guard.Float(), a_flags);
		const bool dropped =
			CanAcceptPaletteDrop(guard) && AcceptPaletteColorDrop(guard.Float(), a_componentCount);
		return guard.Finish(dropped || changed);
	}

	bool DetouredColorEdit3(const char* label, float col[3], ImGuiColorEditFlags flags)
	{
		if (!ShouldIntercept())
			return RealColorEdit3(label, col, flags);
		return InterceptColorEdit(RealColorEdit3, label, col, 3, flags);
	}

	bool DetouredColorEdit4(const char* label, float col[4], ImGuiColorEditFlags flags)
	{
		if (!ShouldIntercept())
			return RealColorEdit4(label, col, flags);
		return InterceptColorEdit(RealColorEdit4, label, col, 4, flags);
	}

	bool DetouredComboArray(const char* label, int* current, const char* const items[],
		int itemCount, int popupMaxHeight)
	{
		if (!ShouldIntercept())
			return RealComboArray(label, current, items, itemCount, popupMaxHeight);
		InterceptedCall interceptedCall;
		SceneWidgetBinding::Guard guard(label, SceneWidgetBinding::Value::Int(current));
		return guard.Finish(
			RealComboArray(label, guard.Int(), items, itemCount, popupMaxHeight));
	}

	bool DetouredComboZeroSeparated(const char* label, int* current, const char* items,
		int popupMaxHeight)
	{
		if (!ShouldIntercept())
			return RealComboZeroSeparated(label, current, items, popupMaxHeight);
		InterceptedCall interceptedCall;
		SceneWidgetBinding::Guard guard(label, SceneWidgetBinding::Value::Int(current));
		return guard.Finish(RealComboZeroSeparated(label, guard.Int(), items, popupMaxHeight));
	}

	bool DetouredRadioButton(const char* label, int* v, int buttonValue)
	{
		if (!ShouldIntercept())
			return RealRadioButton(label, v, buttonValue);
		// A radio group is several calls against one address; only the first owns the gutter.
		InterceptedCall interceptedCall;
		SceneWidgetBinding::Guard guard(label, SceneWidgetBinding::Value::Int(v),
			SceneWidgetBinding::GutterPolicy::GroupMember);
		return guard.Finish(RealRadioButton(label, guard.Int(), buttonValue));
	}
}

namespace
{
	/// One detourable entry point, named so the catalog's coverage list can be checked against it.
	struct DetourEntry
	{
		std::string_view name;
		PVOID* original;
		PVOID replacement;
	};

	std::span<const DetourEntry> GetDetourTable()
	{
		// Function-local so the entries are built after the Real* pointers are initialised.
		static const DetourEntry table[] = {
			{ "SliderFloat", reinterpret_cast<PVOID*>(&RealSliderFloat), reinterpret_cast<PVOID>(&DetouredSliderFloat) },
			{ "SliderFloat2", reinterpret_cast<PVOID*>(&RealSliderFloat2), reinterpret_cast<PVOID>(&DetouredSliderFloat2) },
			{ "SliderFloat3", reinterpret_cast<PVOID*>(&RealSliderFloat3), reinterpret_cast<PVOID>(&DetouredSliderFloat3) },
			{ "SliderFloat4", reinterpret_cast<PVOID*>(&RealSliderFloat4), reinterpret_cast<PVOID>(&DetouredSliderFloat4) },
			{ "DragFloat", reinterpret_cast<PVOID*>(&RealDragFloat), reinterpret_cast<PVOID>(&DetouredDragFloat) },
			{ "DragFloat2", reinterpret_cast<PVOID*>(&RealDragFloat2), reinterpret_cast<PVOID>(&DetouredDragFloat2) },
			{ "DragFloat3", reinterpret_cast<PVOID*>(&RealDragFloat3), reinterpret_cast<PVOID>(&DetouredDragFloat3) },
			{ "DragFloat4", reinterpret_cast<PVOID*>(&RealDragFloat4), reinterpret_cast<PVOID>(&DetouredDragFloat4) },
			{ "InputFloat", reinterpret_cast<PVOID*>(&RealInputFloat), reinterpret_cast<PVOID>(&DetouredInputFloat) },
			{ "InputFloat2", reinterpret_cast<PVOID*>(&RealInputFloat2), reinterpret_cast<PVOID>(&DetouredInputFloat2) },
			{ "InputFloat3", reinterpret_cast<PVOID*>(&RealInputFloat3), reinterpret_cast<PVOID>(&DetouredInputFloat3) },
			{ "InputInt", reinterpret_cast<PVOID*>(&RealInputInt), reinterpret_cast<PVOID>(&DetouredInputInt) },
			{ "SliderInt", reinterpret_cast<PVOID*>(&RealSliderInt), reinterpret_cast<PVOID>(&DetouredSliderInt) },
			{ "SliderScalar", reinterpret_cast<PVOID*>(&RealSliderScalar), reinterpret_cast<PVOID>(&DetouredSliderScalar) },
			{ "SliderAngle", reinterpret_cast<PVOID*>(&RealSliderAngle), reinterpret_cast<PVOID>(&DetouredSliderAngle) },
			{ "Checkbox", reinterpret_cast<PVOID*>(&RealCheckbox), reinterpret_cast<PVOID>(&DetouredCheckbox) },
			{ "ColorEdit3", reinterpret_cast<PVOID*>(&RealColorEdit3), reinterpret_cast<PVOID>(&DetouredColorEdit3) },
			{ "ColorEdit4", reinterpret_cast<PVOID*>(&RealColorEdit4), reinterpret_cast<PVOID>(&DetouredColorEdit4) },
			{ "Combo", reinterpret_cast<PVOID*>(&RealComboArray), reinterpret_cast<PVOID>(&DetouredComboArray) },
			{ "Combo", reinterpret_cast<PVOID*>(&RealComboZeroSeparated), reinterpret_cast<PVOID>(&DetouredComboZeroSeparated) },
			{ "RadioButton", reinterpret_cast<PVOID*>(&RealRadioButton), reinterpret_cast<PVOID>(&DetouredRadioButton) },
		};
		return table;
	}

	/// Every widget kind the catalog needs must have a detour, or scene authoring silently loses it.
	bool VerifyCoverage()
	{
		for (const auto required : SceneSettingsCatalog::GetRequiredInterceptorEntryPoints()) {
			const auto matches = [required](const DetourEntry& entry) { return entry.name == required; };
			if (!std::ranges::any_of(GetDetourTable(), matches)) {
				installError = std::string{ required };
				assert(false && "SceneWidgetInterceptor detour table is missing a catalog entry point");
				return false;
			}
		}
		return true;
	}
}

bool SceneWidgetInterceptor::Install()
{
	if (installed)
		return true;

	installError.clear();
	if (!VerifyCoverage()) {
		logger::error(
			"SceneWidgetInterceptor: no detour for required entry point '{}'; "
			"scene authoring is off for this session",
			installError);
		return false;
	}

	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	for (const auto& entry : GetDetourTable()) {
		if (const auto result = DetourAttach(entry.original, entry.replacement); result != NO_ERROR) {
			installError = std::string{ entry.name };
			// Abort rolls back every attach in this transaction, so nothing is left half-hooked.
			DetourTransactionAbort();
			logger::error(
				"SceneWidgetInterceptor: DetourAttach failed for ImGui::{} ({}); "
				"scene authoring is off for this session",
				installError, result);
			return false;
		}
	}
	if (const auto result = DetourTransactionCommit(); result != NO_ERROR) {
		installError = "DetourTransactionCommit";
		logger::error(
			"SceneWidgetInterceptor: DetourTransactionCommit failed ({}); "
			"scene authoring is off for this session",
			result);
		return false;
	}

	installed = true;
	logger::info("SceneWidgetInterceptor: installed {} entry points", GetDetourTable().size());
	return true;
}

bool SceneWidgetInterceptor::IsInstalled()
{
	return installed;
}

std::string_view SceneWidgetInterceptor::GetInstallError()
{
	return installError;
}

SceneWidgetInterceptor::Scope::Scope(const Context& context) :
	previous(armedContext), previousArmed(armed)
{
	armedContext = context;
	armed = installed && context.feature != nullptr;
}

SceneWidgetInterceptor::Scope::~Scope()
{
	armedContext = previous;
	armed = previousArmed;
}

SceneWidgetInterceptor::ProxyScope::ProxyScope(const void* a_member, float a_displayScale) :
	previous(armedProxy)
{
	assert(a_displayScale != 0.0f && "a proxied control cannot collapse its member to one value");
	armedProxy = { a_member, a_displayScale };
}

SceneWidgetInterceptor::ProxyScope::~ProxyScope()
{
	armedProxy = previous;
}

bool SceneWidgetInterceptor::InterceptInt(const char* a_label, int& a_value, const std::function<bool(int&)>& a_draw)
{
	if (!ShouldIntercept())
		return a_draw(a_value);
	return InterceptScalar([&a_draw](const char*, int* a_bound) { return a_draw(*a_bound); }, a_label, &a_value);
}

const SceneWidgetInterceptor::Context* SceneWidgetInterceptor::GetArmedContext()
{
	return armed ? &armedContext : nullptr;
}

const SceneWidgetInterceptor::Proxy* SceneWidgetInterceptor::GetArmedProxy()
{
	return armedProxy.member ? &armedProxy : nullptr;
}

bool SceneWidgetInterceptor::IsArmed()
{
	return armed && !armedContext.baseline;
}
