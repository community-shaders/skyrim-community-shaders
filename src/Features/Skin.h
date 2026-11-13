#pragma once

#include "I18n/I18n.h"
#include "Skin/Editor/Files.h"
#include "Skin/Editor/ResourceBrowser.h"
#include "Skin/Editor/TextureTools.h"
#include "Skin/Material/Library.h"
#include "Skin/Persistence/Characters.h"
#include "Skin/Runtime/Surfaces.h"
#include "Skin/Runtime/Textures.h"

#include <mutex>

struct Skin : Feature
{
	static Skin* GetSingleton()
	{
		static Skin singleton;
		return &singleton;
	}
	std::string GetName() override { return "Advanced Skin"; }
	std::string GetDisplayName() override { return T("feature.skin.name", "Advanced Skin"); }
	std::string GetShortName() override { return "Skin"; }
	std::string_view GetShaderDefineName() override { return "CS_SKIN"; }
	std::string_view GetCategory() const override { return FeatureCategories::kCharacters; }
	std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.skin.summary_texture_materials", "Physically based skin whose materials follow the skin texture, with file-based character adjustments shared across saves."),
			{ T("feature.skin.roughness_fuzz_and_reflectance_controls", "Roughness, fuzz and reflectance controls"), T("feature.skin.detail_normals_with_a_main_uv_region_mask", "Detail normals with a main-UV region mask"),
				T("feature.skin.independent_transmission_and_post_process_sss_strength", "Independent transmission and post-process SSS strength"), T("feature.skin.summary_editor", "In-game material editor with live preview") } };
	}
	bool HasShaderDefine(RE::BSShader::Type a_type) override { return a_type == RE::BSShader::Type::Lighting; }
	void RestoreDefaultSettings() override;
	void DrawSettings() override;
	void LoadSettings(json& a_json) override;
	void SaveSettings(json& a_json) override;
	void Prepass() override;
	void PostPostLoad() override;
	void SetupResources() override;
	void DataLoaded() override;
	void GameReset() override;
	void DrawMaterialEditor();
	/// Page header subtitle and right-aligned page actions, drawn by the CS Editor beside the page title.
	std::string GetEditorSubtitle();
	void DrawEditorPageActions();
	void SetShaderResources(ID3D11DeviceContext* a_context);
	void BSLightingShader_SetupGeometry(RE::BSRenderPass* a_pass);
	void RestoreSampler();

	struct Settings
	{
		bool EnableSkin = true;
		SkinMaterials::Parameters DefaultProfile;
		std::string DetailTexture = "Data/Shaders/Skin/skin_detail_n.dds";
		float ExtraSkinWetness = 0.0f;
		float WetFadeTime = 10.0f;
		float StartSweat = 0.75f;
		float FullSweat = 0.15f;
		float4 WetParams{ 512.0f, 0.7f, 10.0f, 4.0f };
		/// Mod folder that receives material files saved by the editor; empty means the game's Data folder.
		std::string MaterialOutputFolder;
	} settings;

	struct alignas(16) SkinData
	{
		float4 skinParams;
		float4 skinParams2;
		float4 skinDetailParams;
		float4 sssParams;
		float4 fuzzParams;
		float4 physicalParams;
		float4 wetParams;
	};
	struct alignas(16) PerGeometryData
	{
		float4 skinPerGeometry;
		float4 materialFlags;
		SkinData profile;
	};
	static_assert(sizeof(SkinData) == 112);
	static_assert(sizeof(PerGeometryData) == 144);
	SkinData GetCommonBufferData();

	enum class Layer
	{
		Material,          ///< The texture material file shared by everyone showing the texture.
		CharacterAll,      ///< Scalars applied to every skin surface of one character.
		CharacterMaterial  ///< Changes applied to one character while it shows one texture.
	};
	/// Where the value a control shows comes from, relative to the layer being edited.
	struct Provenance
	{
		bool own = false;                    ///< This layer sets it.
		std::optional<Layer> overriddenBy;   ///< A higher layer of the selected character sets it too.
		std::optional<Layer> inheritedFrom;  ///< Lower layer supplying it; empty for the common settings.
	};

private:
	struct TextureEntry
	{
		SkinTextures::Resource resource;
		bool prepared = false;
	};
	struct GeometryEntry
	{
		RE::NiPointer<RE::BSGeometry> geometry;
		RE::NiPointer<RE::BSShaderProperty> property;
		const RE::BSShaderMaterial* materialPointer = nullptr;
		const RE::BSTextureSet* textureSet = nullptr;
		const RE::NiSourceTexture* diffuse = nullptr;
		RE::NiAVObject* parent = nullptr;
		RE::ActorHandle actor;
		std::string target;  ///< `SkinCharacters::Target::key` of the owner; empty when it has none.
		std::string key;
		SkinMaterials::Material effective;
		std::array<std::shared_ptr<TextureEntry>, SkinMaterials::TextureCount> textures;
		std::string diagnostic;
		uint32_t frame = 0;
		uint64_t revision = 0;
		uint64_t characterRevision = 0;
		bool ready = false;
		bool head = false;
		bool thickness = false;
	};
	struct ActorWetnessCacheEntry
	{
		float4 wetness{ 0, 0, 0, 0 };
		uint32_t frameCount = 0;
	};
	struct Editor
	{
		struct Surface
		{
			RE::NiPointer<RE::BSGeometry> geometry;
			std::string key;
			SkinSurfaces::Description description;
		};
		struct Draft
		{
			SkinMaterials::Changes material;
			SkinCharacters::Customization character;
			bool operator==(const Draft&) const = default;
		};
		enum class Source
		{
			None,
			Player,
			Console
		};
		Layer layer = Layer::Material;
		int section = 0;
		Source source = Source::None;
		RE::ActorHandle actor;
		std::string actorName;
		std::vector<Surface> surfaces;
		uint32_t lastRefresh = 0;
		std::string filter;
		std::string key;  ///< Selected skin texture; the material draft always belongs to it.
		std::string draftKey;
		SkinMaterials::Changes material, materialBaseline;
		std::string fingerprintWarning;
		bool recordFingerprint = true;
		SkinCharacters::Target draftTarget;
		SkinCharacters::Customization character, characterBaseline;
		std::vector<Draft> undo, redo;
		SkinEditor::TextureTools tools;
		SkinEditor::ResourceBrowser browser;
		std::string toolsOutput;  ///< Resource path the texture tools write to.
		bool toolsWindow = false;
		SkinTextures::Resource thumbnail;
		std::string thumbnailKey;
		std::string generatedDDS;
		bool generatedDetail = false;
		bool open = false;
		bool discardConfirmed = false;
		std::function<void()> pendingAction;
		bool clearRequested = false;
		bool showProblems = false;
		bool editGesture = false;
		float previewWetness = -1.0f;
		int channel = 0;
		int texturePreview = 0;
	};
	std::recursive_mutex mutex;
	std::unordered_map<RE::BSGeometry*, GeometryEntry> geometries;
	std::unordered_map<std::string, std::shared_ptr<TextureEntry>> textures;
	std::unordered_map<std::string, SkinLibrary::Pointer> library;
	std::unordered_map<uint32_t, ActorWetnessCacheEntry> actorWetnessMap;
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, SkinMaterials::TextureCount> pendingViews;
	winrt::com_ptr<ID3D11SamplerState> detailSampler;
	winrt::com_ptr<ID3D11SamplerState> savedSampler;
	bool samplerBound = false;
	eastl::unique_ptr<ConstantBuffer> PerGeometryCB;
	Editor editor;
	uint64_t revision = 1;
	uint64_t characterRevision = 0;
	bool texturesDirty = false;
	uint32_t lastPrune = 0;

	void Invalidate(bool a_resources = false);
	std::shared_ptr<TextureEntry> RequestTexture(const std::string& a_path);
	SkinLibrary::Pointer FindMaterial(const std::string& a_key);
	SkinMaterials::Material BaseMaterial(const std::string& a_key);
	const SkinCharacters::Customization* FindCustomization(const std::string& a_target, const SkinCharacters::State& a_state) const;
	SkinMaterials::Material Compose(const std::string& a_key, const SkinCharacters::Customization* a_customization, Layer a_upTo);
	void Prepare(GeometryEntry& a_entry, const SkinCharacters::State& a_state);
	SkinData MakeData(const SkinMaterials::Material& a_material, bool a_head) const;
	GeometryEntry& Observe(RE::BSGeometry* a_geometry, RE::BSShaderProperty* a_property);

	// Editor
	bool EditorDirty() const;
	bool MaterialDirty() const;
	bool CharacterDirty() const;
	bool LayerDirty(Layer a_layer) const;
	bool LayerAvailable(Layer a_layer) const;
	const char* LayerLabel(Layer a_layer) const;
	void Notify(const std::string& a_message, bool a_error = false) const;
	void ChangeEditor(bool a_discards, std::function<void()> a_action);
	void SelectActor(RE::Actor* a_actor, Editor::Source a_source);
	void RefreshSurfaces();
	void SelectKey(const std::string& a_key);
	void LoadMaterialDraft(const std::string& a_key, bool a_clearHistory = true);
	void SaveMaterial(const std::filesystem::path& a_root);
	std::filesystem::path OutputRoot() const;
	void SaveCharacter();
	void SaveLayer();
	void RevertLayer();
	void ClearLayer();
	void ReloadResources();
	void PushUndo(const Editor::Draft& a_before);
	void Undo();
	void Redo();
	SkinMaterials::Changes* LayerChanges(Layer a_layer, bool a_create = false);
	SkinMaterials::Material InheritedMaterial(Layer a_layer);
	Provenance ParameterProvenance(size_t a_parameter, Layer a_layer) const;
	Provenance TextureProvenance(size_t a_texture, Layer a_layer) const;
	void DrawTargetBar();
	void DrawProblems();
	void DrawTextureList();
	void DrawTexturePanel();
	void DrawTextureHeader();
	void DrawLayerStack();
	void DrawLayerToolbar();
	void DrawLayerNote();
	void DrawMaterialControls();
	void DrawParameters(SkinMaterials::Changes& a_changes, const SkinMaterials::Material& a_inherited);
	void DrawTextureInputs(SkinMaterials::Changes& a_changes, const SkinMaterials::Material& a_inherited);
	void DrawTextureChannels(const SkinMaterials::Material& a_material);
	void DrawTextureTools();
	void DrawEditorDialogs();
	void DrawGlobalSettings();
	float GetWaterHeight(const RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_pos);
	float4 GetWetness(RE::BSGeometry* a_geometry);
	struct Hooks
	{
		struct SetupGeometry
		{
			static void thunk(RE::BSShader* a_shader, RE::BSRenderPass* a_pass, uint32_t a_flags);
			static inline REL::Relocation<decltype(thunk)> func;
		};
		struct RestoreGeometry
		{
			static void thunk(RE::BSShader* a_shader, RE::BSRenderPass* a_pass, uint32_t a_flags);
			static inline REL::Relocation<decltype(thunk)> func;
		};
	};
};
