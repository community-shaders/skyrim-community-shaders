#pragma once

#include <Effects11/d3dx11effect.h>
#include <filesystem>
#include <winrt/base.h>

#include "../TextureManager.h"

class Effect
{
public:
	Effect() = default;
	virtual ~Effect() = default;

	// UI technique structure (defined early for use in method declarations)
	struct UITechnique
	{
		std::string techniqueName;  // Actual technique name
		std::string displayName;    // UIName annotation
	};

	// Settings methods
	bool Load();
	void Save();

	// Effect lifecycle
	virtual bool Apply();   // Clear resources, load settings, recompile, create resources
	virtual void Unload();  // Clear all resources

	bool IsCompiled() const { return errors.empty(); }
	const std::vector<std::string>& GetErrors() const { return errors; }

	virtual void Execute() = 0;
	/**
 * Update effect variables from CPU-side state prior to execution.
 *
 * Default implementation does nothing; override in derived classes to write current values into effect variables before the effect is executed.
 */
virtual void UpdateEffectVariables() {}

	/**
 * Hook for derived classes to create or populate effect-owned textures used by the effect.
 *
 * Derived effects should override this to allocate, initialize or update any TextureManager::Texture
 * instances the effect requires for rendering passes or shader variables.
 *
 * The default implementation is a no-op.
 */
	virtual void CreateEffectTextures() {}

	// UI System
	void RenderImGui();
	void LoadUIVariables();
	void UpdateUIVariables();

	// Technique selection
	std::string GetSelectedTechnique() const;

	// Pure virtual methods for derived classes to implement
	virtual std::string GetName() const = 0;

	struct TechniqueInfo
	{
		winrt::com_ptr<ID3DX11EffectTechnique> technique;
		std::string renderTargetName;
	};

	winrt::com_ptr<ID3DX11Effect> effect;
	std::unordered_map<std::string, std::vector<TechniqueInfo>> techniques;
	std::unordered_map<std::string, winrt::com_ptr<ID3DX11EffectVariable>> variables;

	std::unordered_map<std::string, TextureManager::Texture> effectTextureCache;
	std::unordered_map<std::string, winrt::com_ptr<ID3D11ShaderResourceView>> customTextureCache;

	// UI Variable System
	enum class UIVariableType
	{
		Float,
		Int,
		Bool,
		Color3,
		Color4
	};

	/**
 * Types of UI widgets available for exposing effect variables.
 */
 
/**
 * Represents a variable exposed to the UI and the metadata required to bind it
 * to an effect variable and render/edit it.
 *
 * Stores the variable's type and widget hint, the effect variable pointer, a
 * union for scalar value storage, color storage, numeric ranges/step, and an
 * optional dropdown item list.
 */

/**
 * Execute a multi-pass technique sequence using ping-pong rendering between
 * a_output and a_temp, starting from the technique base name and using a_input
 * as the initial shader resource.
 *
 * @param a_baseTechniqueName Base name of the technique sequence to execute.
 * @param a_input Shader resource view to use as the first-pass input.
 * @param a_output Texture to receive the final output of the sequence.
 * @param a_temp Temporary texture used for ping-pong intermediate passes.
 * @returns `true` if the sequence completed successfully, `false` otherwise.
 */

/**
 * Execute a single named technique and write its render result into output.
 *
 * @param techniqueName Name of the technique to execute.
 * @param output Destination texture for the technique's output.
 */

/**
 * Return the underlying D3DX11 effect pointer owned by this Effect.
 *
 * @returns Raw pointer to the effect (may be null if not loaded).
 */

/**
 * Set a shader resource variable on this effect instance by name.
 *
 * @param variableName Name of the shader resource variable to set.
 * @param resource Shader resource view to bind (may be null to unbind).
 * @returns `true` if the variable was found and set, `false` otherwise.
 */

/**
 * Create a texture with the specified dimensions, format, and debug name.
 *
 * @param width Width of the texture in pixels.
 * @param height Height of the texture in pixels.
 * @param format DXGI format for the texture.
 * @param debugName Human-readable debug name assigned to the texture.
 * @returns A TextureManager::Texture describing the created texture.
 */

/**
 * Set a shader resource variable on the supplied effect by name.
 *
 * @param effect Effect instance to modify.
 * @param variableName Name of the shader resource variable to set.
 * @param resource Shader resource view to bind (may be null to unbind).
 * @returns `true` if the variable was found and set, `false` otherwise.
 */

/**
 * Set a vector/scalar variable on the supplied effect by name using raw data.
 *
 * @param effect Effect instance to modify.
 * @param variableName Name of the variable to set.
 * @param data Pointer to the value data to copy into the effect variable.
 * @param size Size in bytes of the data pointed to by `data`.
 * @returns `true` if the variable was found and updated, `false` otherwise.
 */

/**
 * Set a vector/scalar variable on this effect by name using raw data.
 *
 * @param variableName Name of the variable to set.
 * @param data Pointer to the value data to copy into the effect variable.
 * @param size Size in bytes of the data pointed to by `data`.
 * @returns `true` if the variable was found and updated, `false` otherwise.
 */

/**
 * Update effect variables that represent output dimensions (width/height)
 * for the provided effect instance.
 *
 * @param effect Effect instance whose size variables will be updated.
 * @param outputWidth Output width value to set.
 * @param outputHeight Output height value to set.
 */

/**
 * Retrieve a cached effect variable pointer by name, using the local cache to
 * avoid repeated lookups.
 *
 * @param name Name of the effect variable to retrieve.
 * @returns Raw pointer to the cached ID3DX11EffectVariable, or nullptr if not found.
 */

/**
 * Retrieve a pointer to a cached common texture by name.
 *
 * @param name Name of the common texture.
 * @returns Pointer to the cached TextureManager::Texture, or nullptr if not found.
 */

/**
 * Clear the internal caches used for variable and common-texture lookups.
 */

/**
 * Load and compile the underlying FX file into the effect member.
 *
 * @returns `true` if the FX file was loaded and compiled successfully, `false` otherwise.
 */

/**
 * Enumerate all effect variables and populate internal registries and UI variable
 * structures as appropriate.
 */

/**
 * Initialize and create any custom textures referenced by effect annotations or variables.
 */

/**
 * Load a texture from disk into a shader resource view.
 *
 * @param filename Path to the texture file to load.
 * @returns Pointer to the created ID3D11ShaderResourceView, or nullptr on failure.
 */

/**
 * Derive a resource name (e.g., texture filename or identifier) from an effect variable's annotations.
 *
 * @param variable Effect variable to inspect.
 * @returns Resource name associated with the variable, or empty string if none.
 */

/**
 * Populate internal technique tables by scanning the effect for techniques and their render target annotations.
 */

/**
 * Return the set of base technique names that form the roots for multi-pass technique sequences.
 *
 * @returns Vector of base technique name strings.
 */

/**
 * Extract the render target name associated with a technique from its annotations.
 *
 * @param technique Technique to inspect.
 * @returns Render target name, or empty string if not specified.
 */

/**
 * Extract the UI display name for a technique from its annotations.
 *
 * @param technique Technique to inspect.
 * @returns UI display name, or empty string if not specified.
 */

/**
 * Populate the UI-specific technique list using annotated techniques found in the effect.
 */

/**
 * Retrieve an effect-owned texture by name.
 *
 * @param name Name of the effect texture to retrieve.
 * @returns Pointer to the TextureManager::Texture if found, or nullptr otherwise.
 */

/**
 * Resolve a render target view by the render target name, returning fallback if resolution fails.
 *
 * @param renderTargetName Name of the render target to resolve.
 * @param fallback Render target view to return if resolution fails.
 * @returns Resolved ID3D11RenderTargetView pointer, or `fallback` if not found.
 */

/**
 * Read a UI-related annotation string from an effect variable.
 *
 * @param variable Effect variable to query.
 * @param annotationName Name of the annotation to read.
 * @returns Annotation value as a string, or empty string if the annotation is absent.
 */

/**
 * Parse a widget type hint string into the corresponding UIWidgetType enum value.
 *
 * @param widget Widget hint string (e.g., "Spinner", "Dropdown").
 * @returns Parsed UIWidgetType; defaults to UIWidgetType::Default for unknown values.
 */

/**
 * Parse a comma-separated dropdown list annotation into individual item strings.
 *
 * @param list Comma-separated list text from an annotation.
 * @returns Vector of dropdown item strings (trimmed).
 */

/**
 * Load the current stored value for a UI variable from settings or the effect's default.
 *
 * @param uiVar UI variable to populate.
 */

/**
 * Parse a textual representation into a UI variable's stored value and update uiVar accordingly.
 *
 * @param uiVar UI variable to update.
 * @param value String containing the value representation to parse.
 */
enum class UIWidgetType
	{
		Default,
		Spinner,
		Dropdown
	};

	struct UIVariable
	{
		UIVariableType type;
		UIWidgetType widgetType;
		std::string name;
		std::string displayName;
		winrt::com_ptr<ID3DX11EffectVariable> effectVariable;

		// Value storage
		union
		{
			float floatValue;
			int intValue;
			bool boolValue;
		};

		// Color value storage
		float colorValue[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		// UI properties
		float floatMin = 0.0f;
		float floatMax = 1.0f;
		float floatStep = 0.01f;
		int intMin = 0;
		int intMax = 100;
		std::vector<std::string> dropdownItems;
	};

	std::vector<UIVariable> uiVariables;

	// Technique selection (legacy)
	std::vector<std::string> availableTechniques;

	// UI technique selection (indexed by uint, only includes annotated techniques)
	std::vector<UITechnique> uiTechniques;
	uint32_t selectedTechniqueIndex = 0;

	// Error tracking
	std::vector<std::string> errors;

	// INI file modification time tracking to skip redundant reloads
	std::filesystem::file_time_type lastIniWriteTime{};

	// Execute a technique sequence with ping-pong rendering
	bool ExecuteTechniqueSequence(const std::string& a_baseTechniqueName, ID3D11ShaderResourceView* a_input, TextureManager::Texture& a_output, TextureManager::Texture& a_temp);

	// Execute a single technique
	void ExecuteTechnique(const std::string& techniqueName, TextureManager::Texture& output);

	// Allow EffectManager to setup common variables
	ID3DX11Effect* GetEffect() const { return effect.get(); }

	// Helper function to set shader resource variables (non-static version for this effect)
	bool SetShaderResourceVariable(const std::string& variableName, ID3D11ShaderResourceView* resource);

	// Texture creation helper
	static TextureManager::Texture CreateTexture(uint32_t width, uint32_t height, DXGI_FORMAT format, const std::string& debugName);

	// Static helper functions for any effect
	static bool SetShaderResourceVariable(ID3DX11Effect* effect, const std::string& variableName, ID3D11ShaderResourceView* resource);
	static bool SetVectorVariable(ID3DX11Effect* effect, const std::string& variableName, const void* data, uint32_t size);

	// Helper function for safe vector variable access
	bool SetVectorVariable(const std::string& variableName, const void* data, uint32_t size);

	static void UpdateSizeVariables(ID3DX11Effect* effect, uint32_t outputWidth, uint32_t outputHeight);

protected:
	ID3DX11EffectVariable* GetCachedVariable(const std::string& name);
	TextureManager::Texture* GetCachedCommonTexture(const std::string& name);
	void ClearVariableCache();

private:
	bool LoadFXFile();

	std::unordered_map<std::string, ID3DX11EffectVariable*> variableCache;
	std::unordered_map<std::string, TextureManager::Texture*> commonTexturePointerCache;

	void EnumerateAllVariables();

	void SetupCustomTextures();
	ID3D11ShaderResourceView* LoadTextureFromFile(const std::string& filename);
	std::string GetResourceNameFromVariable(ID3DX11EffectVariable* variable);

	void LoadTechniques();
	std::vector<std::string> GetBaseTechniqueNames();

	std::string GetRenderTargetFromTechnique(ID3DX11EffectTechnique* technique);
	std::string GetUINameFromTechnique(ID3DX11EffectTechnique* technique);
	void LoadUITechniques();
	TextureManager::Texture* GetEffectTexture(const std::string& name);
	ID3D11RenderTargetView* GetRenderTargetView(const std::string& renderTargetName, ID3D11RenderTargetView* fallback);

	// UI Variable helpers
	std::string GetUIAnnotation(ID3DX11EffectVariable* variable, const std::string& annotationName);
	UIWidgetType ParseWidgetType(const std::string& widget);
	std::vector<std::string> ParseDropdownList(const std::string& list);
	void LoadUIVariableValue(UIVariable& uiVar);
	void LoadVariableFromString(UIVariable& uiVar, const std::string& value);
};