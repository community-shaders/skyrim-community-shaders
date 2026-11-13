#include "Features/Skin.h"

#include "CSEditor/EditorWindow.h"
#include "Globals.h"
#include "State.h"
#include "Utils/UI.h"

#include <set>

// Editor state and actions: selection, drafts, saving and history. Drawing lives in Layout.cpp and Editor.cpp.

using namespace SkinMaterials;
using namespace SkinEditor;

namespace
{
	Changes ToChanges(const SkinMaterials::Material& a_material)
	{
		Changes result;
		if (!a_material.enabled)
			return result;
		for (size_t i = 0; i < ParameterCount; ++i)
			if (a_material.specified[i])
				result.parameters[i] = a_material.parameters.values[i];
		for (size_t i = 0; i < TextureCount; ++i)
			if (!a_material.textures[i].empty())
				result.textures[i] = { TextureMode::Resource, a_material.textures[i] };
		return result;
	}
}

bool Skin::MaterialDirty() const
{
	return !editor.draftKey.empty() && editor.material != editor.materialBaseline;
}

bool Skin::CharacterDirty() const
{
	return editor.draftTarget && editor.character != editor.characterBaseline;
}

bool Skin::EditorDirty() const
{
	return MaterialDirty() || CharacterDirty();
}

bool Skin::LayerDirty(Layer a_layer) const
{
	switch (a_layer) {
	case Layer::Material:
		return MaterialDirty();
	case Layer::CharacterAll:
		return CharacterDirty() && editor.character.all != editor.characterBaseline.all;
	default:
		{
			if (!CharacterDirty())
				return false;
			const auto current = editor.character.materials.find(editor.key);
			const auto saved = editor.characterBaseline.materials.find(editor.key);
			const auto empty = Changes{};
			return (current == editor.character.materials.end() ? empty : current->second) !=
			       (saved == editor.characterBaseline.materials.end() ? empty : saved->second);
		}
	}
}

bool Skin::LayerAvailable(Layer a_layer) const
{
	return a_layer == Layer::Material ? !editor.key.empty() : bool(editor.draftTarget) && (a_layer == Layer::CharacterAll || !editor.key.empty());
}

const char* Skin::LayerLabel(Layer a_layer) const
{
	switch (a_layer) {
	case Layer::Material:
		return T("feature.skin.layer_material", "Texture material");
	case Layer::CharacterAll:
		return T("feature.skin.layer_character_all", "Whole character");
	default:
		return T("feature.skin.layer_character_material", "Character on this texture");
	}
}

void Skin::Notify(const std::string& a_message, bool a_error) const
{
	if (auto* window = EditorWindow::GetSingleton())
		window->ShowNotification(a_message, a_error ? Util::Colors::GetError() : Util::Colors::GetSuccess(),
			a_error ? EditorWindow::kLongNotificationDuration : Util::Notifications::kDefaultDuration);
	if (a_error)
		logger::warn("[Advanced Skin Editor] {}", a_message);
}

void Skin::ChangeEditor(bool a_discards, std::function<void()> a_action)
{
	if (a_discards && !editor.discardConfirmed)
		editor.pendingAction = std::move(a_action);
	else
		a_action();
}

void Skin::SelectActor(RE::Actor* a_actor, Editor::Source a_source)
{
	if (!a_actor || !a_actor->Get3D(false))
		throw std::runtime_error(T("feature.skin.select_loaded_character", "Select a character with loaded 3D."));
	const auto handle = a_actor->GetHandle();
	ChangeEditor(CharacterDirty() && !(SkinCharacters::TargetOf(a_actor) == editor.draftTarget), [this, handle, a_source] {
		const auto actor = handle.get();
		if (!actor)
			throw std::runtime_error(T("feature.skin.select_loaded_character", "Select a character with loaded 3D."));
		editor.source = a_source;
		editor.actor = handle;
		editor.actorName = actor->GetName() ? actor->GetName() : "";
		const auto target = SkinCharacters::TargetOf(actor.get());
		if (!(target == editor.draftTarget) || !editor.draftTarget) {
			editor.draftTarget = target;
			const auto* saved = target ? SkinCharacters::Find(*SkinCharacters::Get(), target.key) : nullptr;
			editor.character = editor.characterBaseline = saved ? saved->customization : SkinCharacters::Customization{};
			if (!saved)
				editor.character.name = editor.characterBaseline.name = editor.actorName;
			editor.undo.clear();
			editor.redo.clear();
		}
		if (!LayerAvailable(editor.layer))
			editor.layer = Layer::Material;
		RefreshSurfaces();
		const bool shown = std::any_of(editor.surfaces.begin(), editor.surfaces.end(), [this](const auto& surface) { return surface.key == editor.key; });
		if (!shown && !MaterialDirty()) {
			// Prefer the body: it is usually what an author is working on and what armor shares.
			const auto body = std::find_if(editor.surfaces.begin(), editor.surfaces.end(), [](const auto& surface) { return !surface.key.empty() && surface.description.part == SkinSurfaces::Part::Body; });
			const auto any = std::find_if(editor.surfaces.begin(), editor.surfaces.end(), [](const auto& surface) { return !surface.key.empty(); });
			const auto chosen = body != editor.surfaces.end() ? body : any;
			if (chosen != editor.surfaces.end()) {
				editor.key = chosen->key;
				LoadMaterialDraft(chosen->key);
			}
		}
		Invalidate();
	});
}

void Skin::RefreshSurfaces()
{
	editor.lastRefresh = globals::state->frameCount;
	const auto actor = editor.actor.get();
	if (!actor) {
		editor.surfaces.clear();
		return;
	}
	std::vector<Editor::Surface> surfaces;
	std::set<RE::NiAVObject*> roots;
	for (bool first : { false, true }) {
		if (first && actor.get() != RE::PlayerCharacter::GetSingleton())
			continue;
		auto* root = actor->Get3D(first);
		if (!root || !roots.insert(root).second)
			continue;
		RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* geometry) {
			auto* property = geometry->GetGeometryRuntimeData().shaderProperty.get();
			if (IsCompatible(property)) {
				const auto& entry = Observe(geometry, property);
				surfaces.push_back({ RE::NiPointer(geometry), entry.key, SkinSurfaces::Describe(geometry, actor.get()) });
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
	}
	std::stable_sort(surfaces.begin(), surfaces.end(), [](const auto& a, const auto& b) {
		return std::tie(a.description.firstPerson, a.description.part) < std::tie(b.description.firstPerson, b.description.part);
	});
	editor.surfaces = std::move(surfaces);
}

void Skin::SelectKey(const std::string& a_key)
{
	if (a_key.empty() || a_key == editor.key)
		return;
	ChangeEditor(MaterialDirty(), [this, a_key] {
		editor.key = a_key;
		LoadMaterialDraft(a_key);
		if (!LayerAvailable(editor.layer))
			editor.layer = Layer::Material;
	});
}

void Skin::LoadMaterialDraft(const std::string& a_key)
{
	const auto entry = FindMaterial(a_key);
	editor.draftKey = a_key;
	editor.material = editor.materialBaseline = entry->definition.status == Status::Valid ? ToChanges(entry->definition.material) : Changes{};
	editor.recordFingerprint = entry->definition.status != Status::Valid || entry->definition.authoredFor.has_value();
	editor.fingerprintWarning.clear();
	if (entry->definition.authoredFor) {
		try {
			if (ResourceFingerprint(a_key) != *entry->definition.authoredFor)
				editor.fingerprintWarning = T("feature.skin.material_authored_for_other_texture", "This material was made for a different version of this texture. Its painted masks may not line up; check the result or save again to accept it.");
		} catch (const std::exception& e) {
			editor.fingerprintWarning = e.what();
		}
	}
	editor.undo.clear();
	editor.redo.clear();
	editor.thumbnailKey.clear();
	Invalidate();
}

std::filesystem::path Skin::OutputRoot() const
{
	return settings.MaterialOutputFolder.empty() ? std::filesystem::path("Data") : std::filesystem::u8path(settings.MaterialOutputFolder);
}

void Skin::SaveMaterial(const std::filesystem::path& a_root)
{
	if (editor.draftKey.empty())
		throw std::runtime_error(T("feature.skin.select_skin_texture", "Select a skin texture first."));
	Validate(editor.material);
	const auto material = Apply({}, editor.material);
	CheckTextures(material);
	std::optional<SkinMaterials::Fingerprint> fingerprint;
	if (editor.recordFingerprint) {
		// Advisory data only: a diffuse that cannot be read back must not block saving the material.
		try {
			fingerprint = ResourceFingerprint(editor.draftKey);
		} catch (const std::exception& e) {
			logger::warn("[Advanced Skin Editor] {} was not fingerprinted: {}", editor.draftKey, e.what());
		}
	}
	const auto text = Encode(material, fingerprint).dump(2) + "\n";
	const Bytes bytes(text.begin(), text.end());
	const auto relative = SidecarPath(editor.draftKey);
	const auto destination = a_root / std::filesystem::u8path(relative);
	PublishResource(a_root, relative, bytes);
	SkinLibrary::Entry entry;
	entry.key = editor.draftKey;
	entry.path = relative;
	entry.definition = Parse(bytes);
	entry.origin = SkinLibrary::Origin::Session;
	entry.savedTo = UTF8(destination);
	library[editor.draftKey] = std::make_shared<const SkinLibrary::Entry>(std::move(entry));
	editor.materialBaseline = editor.material;
	editor.fingerprintWarning.clear();
	Notify(I18n::GetSingleton()->Format("feature.skin.material_saved_short", { { "path", UTF8(destination) } }, "Saved {path}"));
	Invalidate();
}

void Skin::SaveCharacter()
{
	if (!editor.draftTarget)
		throw std::runtime_error(T("feature.skin.character_no_identity", "This character was generated at runtime and has no record to save adjustments for."));
	if (editor.character.name.empty())
		editor.character.name = editor.actorName;
	std::erase_if(editor.character.materials, [](const auto& item) { return item.second.Empty(); });
	const bool removed = editor.character.Empty();
	const auto file = SkinCharacters::Save(editor.draftTarget, editor.character);
	editor.characterBaseline = editor.character;
	Notify(removed ? I18n::GetSingleton()->Format("feature.skin.character_file_removed", { { "path", UTF8(file) } }, "No adjustments left; removed {path}.") :
					 I18n::GetSingleton()->Format("feature.skin.character_saved_short", { { "path", UTF8(file) } }, "Saved {path}"));
	Invalidate();
}

void Skin::SaveLayer()
{
	if (editor.layer == Layer::Material)
		SaveMaterial(OutputRoot());
	else
		SaveCharacter();
}

void Skin::RevertLayer()
{
	PushUndo({ editor.material, editor.character });
	if (editor.layer == Layer::Material)
		editor.material = editor.materialBaseline;
	else
		editor.character = editor.characterBaseline;
	Invalidate();
}

void Skin::ClearLayer()
{
	PushUndo({ editor.material, editor.character });
	switch (editor.layer) {
	case Layer::Material:
		editor.material = {};
		break;
	case Layer::CharacterAll:
		editor.character.all = {};
		break;
	default:
		editor.character.materials.erase(editor.key);
		break;
	}
	Invalidate();
}

void Skin::PushUndo(const Editor::Draft& a_before)
{
	editor.undo.push_back(a_before);
	if (editor.undo.size() > 64)
		editor.undo.erase(editor.undo.begin());
	editor.redo.clear();
}

void Skin::Undo()
{
	if (editor.undo.empty())
		return;
	editor.redo.push_back({ editor.material, editor.character });
	editor.material = editor.undo.back().material;
	editor.character = editor.undo.back().character;
	editor.undo.pop_back();
	Invalidate();
}

void Skin::Redo()
{
	if (editor.redo.empty())
		return;
	editor.undo.push_back({ editor.material, editor.character });
	editor.material = editor.redo.back().material;
	editor.character = editor.redo.back().character;
	editor.redo.pop_back();
	Invalidate();
}

SkinMaterials::Changes* Skin::LayerChanges(Layer a_layer)
{
	switch (a_layer) {
	case Layer::Material:
		return &editor.material;
	case Layer::CharacterAll:
		return &editor.character.all;
	default:
		return editor.key.empty() ? nullptr : &editor.character.materials[editor.key];
	}
}

SkinMaterials::Material Skin::InheritedMaterial(Layer a_layer)
{
	switch (a_layer) {
	case Layer::Material:
		return Resolve({}, settings.DefaultProfile);
	case Layer::CharacterAll:
		return Resolve(BaseMaterial(editor.key), settings.DefaultProfile);
	default:
		return Resolve(Apply(BaseMaterial(editor.key), ScalarsOnly(editor.character.all)), settings.DefaultProfile);
	}
}

Skin::Provenance Skin::ParameterProvenance(size_t a_parameter, Layer a_layer) const
{
	std::array<bool, 3> set{};
	set[0] = editor.material.parameters[a_parameter].has_value();
	if (editor.draftTarget) {
		set[1] = editor.character.all.parameters[a_parameter].has_value();
		const auto found = editor.character.materials.find(editor.key);
		set[2] = found != editor.character.materials.end() && found->second.parameters[a_parameter].has_value();
	}
	Provenance result;
	const auto current = static_cast<size_t>(a_layer);
	result.own = set[current];
	for (size_t layer = 2; layer > current; --layer)
		if (set[layer]) {
			result.overriddenBy = static_cast<Layer>(layer);
			break;
		}
	for (size_t layer = current; layer-- > 0;)
		if (set[layer]) {
			result.inheritedFrom = static_cast<Layer>(layer);
			break;
		}
	return result;
}

Skin::Provenance Skin::TextureProvenance(size_t a_texture, Layer a_layer) const
{
	// Whole-character adjustments cannot hold textures, so only the texture material and the per-texture layer compete.
	std::array<bool, 3> set{};
	set[0] = editor.material.textures[a_texture].mode != TextureMode::Inherit;
	if (editor.draftTarget) {
		const auto found = editor.character.materials.find(editor.key);
		set[2] = found != editor.character.materials.end() && found->second.textures[a_texture].mode != TextureMode::Inherit;
	}
	Provenance result;
	const auto current = static_cast<size_t>(a_layer);
	result.own = set[current];
	if (current < 2 && set[2])
		result.overriddenBy = Layer::CharacterMaterial;
	if (current > 0 && set[0])
		result.inheritedFrom = Layer::Material;
	return result;
}
