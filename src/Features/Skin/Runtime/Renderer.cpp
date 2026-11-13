#include "Features/Skin.h"

#include "CSEditor/EditorWindow.h"
#include "Globals.h"
#include "State.h"

#include <cmath>

using namespace SkinMaterials;

void Skin::Invalidate(bool a_resources)
{
	++revision;
	if (a_resources) {
		textures.clear();
		library.clear();
		for (auto& [geometry, entry] : geometries)
			entry.textures = {};
	}
}

void Skin::SetupResources()
{
	std::lock_guard lock(mutex);
	RestoreSampler();
	detailSampler = nullptr;
	PerGeometryCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<PerGeometryData>(), "Skin::PerGeometryCB");
	D3D11_SAMPLER_DESC sampler{};
	sampler.Filter = D3D11_FILTER_ANISOTROPIC;
	sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler.MaxAnisotropy = 16;
	sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sampler.MaxLOD = D3D11_FLOAT32_MAX;
	DX::ThrowIfFailed(globals::d3d::device->CreateSamplerState(&sampler, detailSampler.put()));
	Invalidate(true);
}

void Skin::DataLoaded()
{
	std::lock_guard lock(mutex);
	SkinCharacters::Reload();
	Invalidate(true);
}

void Skin::ReloadResources()
{
	std::lock_guard lock(mutex);
	SkinCharacters::Reload();
	Invalidate(true);
	editor.thumbnailKey.clear();
	// Files may have been edited outside the game. Drafts without unsaved changes follow the files.
	if (!editor.draftKey.empty() && !MaterialDirty())
		LoadMaterialDraft(editor.draftKey, false);
	if (editor.draftTarget && !CharacterDirty()) {
		const auto* saved = SkinCharacters::Find(*SkinCharacters::Get(), editor.draftTarget.key);
		editor.character = editor.characterBaseline = saved ? saved->customization : SkinCharacters::Customization{};
		for (auto& draft : editor.undo)
			draft.character = editor.character;
		for (auto& draft : editor.redo)
			draft.character = editor.character;
	}
}

void Skin::GameReset()
{
	std::lock_guard lock(mutex);
	// References and their 3D are about to be replaced. Materials, character files and drafts do not depend on the save.
	geometries.clear();
	actorWetnessMap.clear();
	pendingViews = {};
	editor.surfaces.clear();
	editor.actor = {};
	Invalidate();
}

void Skin::PostPostLoad()
{
	stl::write_vfunc<0x6, Hooks::SetupGeometry>(RE::VTABLE_BSLightingShader[0]);
	stl::write_vfunc<0x7, Hooks::RestoreGeometry>(RE::VTABLE_BSLightingShader[0]);
}

std::shared_ptr<Skin::TextureEntry> Skin::RequestTexture(const std::string& a_path)
{
	if (a_path.empty())
		return {};
	auto& entry = textures[a_path];
	if (!entry)
		entry = std::make_shared<TextureEntry>();
	return entry;
}

SkinLibrary::Pointer Skin::FindMaterial(const std::string& a_key)
{
	if (a_key.empty())
		return std::make_shared<const SkinLibrary::Entry>();
	auto& entry = library[a_key];
	if (!entry)
		entry = std::make_shared<const SkinLibrary::Entry>(SkinLibrary::Read(a_key));
	return entry;
}

SkinMaterials::Material Skin::BaseMaterial(const std::string& a_key)
{
	if (editor.open && !a_key.empty() && a_key == editor.draftKey)
		return Apply({}, editor.material);
	const auto entry = FindMaterial(a_key);
	return entry->definition.status == Status::Valid ? entry->definition.material : SkinMaterials::Material{};
}

const SkinCharacters::Customization* Skin::FindCustomization(const std::string& a_target, const SkinCharacters::State& a_state) const
{
	if (a_target.empty())
		return nullptr;
	if (editor.open && a_target == editor.draftTarget.key)
		return &editor.character;
	const auto* character = SkinCharacters::Find(a_state, a_target);
	return character ? &character->customization : nullptr;
}

SkinMaterials::Material Skin::Compose(const std::string& a_key, const SkinCharacters::Customization* a_customization, Layer a_upTo)
{
	auto material = BaseMaterial(a_key);
	if (!a_customization || a_upTo == Layer::Material)
		return material;
	material = Apply(material, ScalarsOnly(a_customization->all));
	if (a_upTo == Layer::CharacterMaterial)
		if (const auto found = a_customization->materials.find(a_key); found != a_customization->materials.end())
			material = Apply(material, found->second);
	return material;
}

Skin::GeometryEntry& Skin::Observe(RE::BSGeometry* a_geometry, RE::BSShaderProperty* a_property)
{
	auto& entry = geometries[a_geometry];
	entry.frame = globals::state->frameCount;
	auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(a_property->material);
	auto* actor = SkinSurfaces::Owner(a_geometry);
	const auto actorHandle = actor ? actor->GetHandle() : RE::ActorHandle{};
	// The material key follows the bound diffuse, so any texture replacement re-identifies the surface.
	if (!entry.geometry || entry.property.get() != a_property || entry.materialPointer != material ||
		entry.textureSet != material->textureSet.get() || entry.diffuse != material->diffuseTexture.get() ||
		entry.actor != actorHandle || entry.parent != a_geometry->parent) {
		entry.geometry = RE::NiPointer(a_geometry);
		entry.property = RE::NiPointer(a_property);
		entry.materialPointer = material;
		entry.textureSet = material->textureSet.get();
		entry.diffuse = material->diffuseTexture.get();
		entry.parent = a_geometry->parent;
		entry.actor = actorHandle;
		entry.target = SkinCharacters::TargetOf(actor).key;
		entry.key = SkinSurfaces::Key(a_property);
		entry.head = SkinSurfaces::IsHead(a_geometry);
		entry.thickness = SkinSurfaces::HasThickness(a_property);
		entry.ready = false;
	}
	return entry;
}

void Skin::Prepare(GeometryEntry& a_entry, const SkinCharacters::State& a_state)
{
	a_entry.diagnostic.clear();
	a_entry.textures = {};
	try {
		const auto source = FindMaterial(a_entry.key);
		if (source->definition.status == Status::Invalid)
			a_entry.diagnostic = source->path + ": " + source->definition.diagnostic + " ";
		a_entry.effective = Resolve(Compose(a_entry.key, FindCustomization(a_entry.target, a_state), Layer::CharacterMaterial), settings.DefaultProfile);
		for (size_t i = 0; i < a_entry.textures.size(); ++i) {
			try {
				a_entry.textures[i] = RequestTexture(NormalizeTexturePath(a_entry.effective.textures[i]));
			} catch (const std::exception& error) {
				a_entry.diagnostic += std::string(TextureLabels()[i]) + ": " + error.what() + " ";
			}
		}
		for (size_t i = 0; i < 2; ++i)
			if (a_entry.textures[i] && a_entry.textures[i]->prepared && a_entry.textures[i]->resource.reconstructNormal)
				a_entry.diagnostic += T("feature.skin.bc5_unorm_is_only_supported_for_detail_normals_without_a", "BC5 UNORM is only supported for detail normals without a mask.");
		if (a_entry.effective.textures[2].empty())
			a_entry.textures[2] = RequestTexture(settings.DetailTexture);
	} catch (const std::exception& e) {
		a_entry.effective = Resolve({}, settings.DefaultProfile);
		a_entry.diagnostic = e.what();
	}
	a_entry.revision = revision;
	a_entry.characterRevision = a_state.revision;
	a_entry.ready = true;
}

void Skin::Prepass()
{
	std::lock_guard lock(mutex);
	const auto characters = SkinCharacters::Get();
	RestoreSampler();
	const bool open = EditorWindow::GetSingleton()->open;
	if (editor.open != open) {
		// Drafts preview only while the editor is open.
		editor.open = open;
		if (!open)
			editor.previewWetness = -1;
		Invalidate();
	}
	characterRevision = characters->revision;
	for (auto& [geometry, entry] : geometries) {
		if (entry.actor && !SkinSurfaces::Belongs(geometry, entry.actor.get().get())) {
			entry.ready = false;
			continue;
		}
		if (!SkinMaterials::IsCompatible(entry.property.get())) {
			entry.ready = false;
			continue;
		}
		if (!entry.ready || entry.revision != revision || entry.characterRevision != characters->revision)
			Prepare(entry, *characters);
	}
	for (auto& [path, entry] : textures) {
		if (!entry->prepared) {
			entry->resource = SkinTextures::Load(path);
			entry->prepared = true;
		}
	}
	const auto frame = globals::state->frameCount;
	if (frame - lastPrune >= 120) {
		lastPrune = frame;
		std::erase_if(geometries, [frame, this](const auto& item) {
			return frame - item.second.frame > 600 &&
			       std::none_of(editor.surfaces.begin(), editor.surfaces.end(), [&](const auto& surface) { return surface.geometry.get() == item.first; });
		});
		std::erase_if(textures, [](const auto& item) { return item.second.use_count() == 1; });
	}
}

Skin::SkinData Skin::MakeData(const SkinMaterials::Material& a_material, bool a_head) const
{
	auto p = a_material.parameters;
	const auto fields = ParameterTable();
	for (size_t i = 0; i < fields.size(); ++i) {
		const auto& field = fields[i];
		auto& value = p.values[i];
		value = std::isfinite(value) ? std::clamp(value, field.minimum, field.maximum) : field.initial;
	}
	SkinData data{};
	data.skinParams = { p[Parameter::Roughness], p[Parameter::SecondaryRoughness], p[Parameter::SpecularTextureMultiplier], float(settings.EnableSkin) };
	data.skinParams2 = { p[Parameter::SecondarySpecularStrength], settings.ExtraSkinWetness, p[Parameter::Reflectance], p[Parameter::BaseColorMultiplier] };
	const float bodyTiling = a_head ? 1.0f : p[Parameter::BodyTilingMultiplier];
	data.skinDetailParams = { p[Parameter::DetailTiling] * bodyTiling, bodyTiling, p[Parameter::DetailStrength], float(p[Parameter::DetailEnabled] != 0) };
	data.sssParams = { p[Parameter::Transmission], p[Parameter::TransmissionDepth], p[Parameter::SSSAmount], p[Parameter::TransmissionEnabled] };
	data.fuzzParams = { p[Parameter::Fuzz], p[Parameter::FuzzRoughness], p[Parameter::FuzzF0], p[Parameter::ExtraEdgeRoughness] };
	data.physicalParams = { p[Parameter::PhysicalMainRoughnessMultiplier], p[Parameter::PhysicalSecondRoughnessMultiplier], p[Parameter::PhysicalSpecularStrength], p[Parameter::WetResponse] };
	data.wetParams = settings.WetParams;
	return data;
}

Skin::SkinData Skin::GetCommonBufferData()
{
	std::lock_guard lock(mutex);
	return MakeData(Resolve({}, settings.DefaultProfile), false);
}

void Skin::RestoreSampler()
{
	std::lock_guard lock(mutex);
	if (samplerBound) {
		auto* sampler = savedSampler.get();
		globals::d3d::context->PSSetSamplers(15, 1, &sampler);
		savedSampler = nullptr;
		samplerBound = false;
	}
}

void Skin::BSLightingShader_SetupGeometry(RE::BSRenderPass* a_pass)
{
	std::lock_guard lock(mutex);
	RestoreSampler();
	pendingViews = {};
	texturesDirty = true;
	if (!PerGeometryCB)
		return;
	PerGeometryData data{};
	data.profile = MakeData(Resolve({}, settings.DefaultProfile), false);
	data.profile.skinParams.w = 0;
	if (a_pass && a_pass->geometry) {
		data.skinPerGeometry = GetWetness(a_pass->geometry);
		if (SkinMaterials::IsCompatible(a_pass->shaderProperty)) {
			auto& entry = Observe(a_pass->geometry, a_pass->shaderProperty);
			data.profile = MakeData(Resolve({}, settings.DefaultProfile), entry.head);
			data.profile.skinDetailParams.w = 0;
			if (entry.ready && entry.revision == revision && entry.characterRevision == characterRevision) {
				data.profile = MakeData(entry.effective, entry.head);
				for (size_t i = 0; i < entry.textures.size(); ++i) {
					if (entry.textures[i] && (i == 2 || !entry.textures[i]->resource.reconstructNormal))
						pendingViews[i] = entry.textures[i]->resource.view;
				}
				data.materialFlags = { float(bool(pendingViews[0])), float(bool(pendingViews[1])),
					float(entry.textures[2] && entry.textures[2]->resource.reconstructNormal), float(entry.thickness) };
				if (!pendingViews[2])
					data.profile.skinDetailParams.w = 0;
				if (editor.open && editor.previewWetness >= 0 && ((!entry.key.empty() && entry.key == editor.draftKey) || (!entry.target.empty() && entry.target == editor.draftTarget.key)))
					data.skinPerGeometry = { editor.previewWetness, 0, 0, 0 };
			}
		}
	}
	PerGeometryCB->Update(data);
	ID3D11Buffer* buffer = PerGeometryCB->CB();
	globals::d3d::context->PSSetConstantBuffers(7, 1, &buffer);
}

void Skin::SetShaderResources(ID3D11DeviceContext* a_context)
{
	std::lock_guard lock(mutex);
	if (!texturesDirty)
		return;
	constexpr std::array<UINT, 3> slots{ 71, 74, 72 };
	for (size_t i = 0; i < slots.size(); ++i) {
		auto* view = pendingViews[i].get();
		a_context->PSSetShaderResources(slots[i], 1, &view);
	}
	if (pendingViews[2] && detailSampler) {
		a_context->PSGetSamplers(15, 1, savedSampler.put());
		auto* sampler = detailSampler.get();
		a_context->PSSetSamplers(15, 1, &sampler);
		samplerBound = true;
	}
	texturesDirty = false;
}

void Skin::Hooks::SetupGeometry::thunk(RE::BSShader* a_shader, RE::BSRenderPass* a_pass, uint32_t a_flags)
{
	globals::features::skin.BSLightingShader_SetupGeometry(a_pass);
	func(a_shader, a_pass, a_flags);
}

void Skin::Hooks::RestoreGeometry::thunk(RE::BSShader* a_shader, RE::BSRenderPass* a_pass, uint32_t a_flags)
{
	globals::features::skin.RestoreSampler();
	func(a_shader, a_pass, a_flags);
}
