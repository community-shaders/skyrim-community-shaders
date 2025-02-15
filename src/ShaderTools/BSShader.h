#pragma once

class BSRenderPass;
class VertexShader;

namespace REX
{
	// This code is taken from Nukem's work here - https://github.com/Nukem9/SkyrimSETest/blob/master/skyrim64_test/src/patches/TES/

	using TechniqueID = std::uint32_t;

	struct Buffer
	{
		ID3D11Buffer* m_Buffer;  // Selected from pool in Load*ShaderFromFile()
		void* m_Data;            // m_Data = DeviceContext->Map(m_Buffer)

		// Based on shader load flags, these **can be null**. At least one of the
		// pointers is guaranteed to be non-null.
	};

	enum ConstantGroupLevel
	{
		CONSTANT_GROUP_LEVEL_TECHNIQUE = 0x0,  // Varies between PS/VS shaders
		CONSTANT_GROUP_LEVEL_MATERIAL = 0x1,   // Varies between PS/VS shaders
		CONSTANT_GROUP_LEVEL_GEOMETRY = 0x2,   // Varies between PS/VS shaders
		CONSTANT_GROUP_LEVEL_COUNT = 0x3,

		// Slot 7 is used for grass but unknown
		CONSTANT_GROUP_LEVEL_INSTANCE = 0x8,  // Instanced geometry such as grass and trees
		CONSTANT_GROUP_LEVEL_PREVIOUS_BONES = 0x9,
		CONSTANT_GROUP_LEVEL_BONES = 0xA,
		CONSTANT_GROUP_LEVEL_ALPHA_TEST_REF = 0xB,  // PS/VS. Used as a single float value for alpha testing (16 bytes allocated)
		CONSTANT_GROUP_LEVEL_PERFRAME = 0xC,        // PS/VS. Per-frame constants. Contains view projections and some other variables.
	};

#define MAX_PS_CONSTANTS 64

	struct PixelShader
	{
		std::uint32_t m_TechniqueID;  // Bit flags
		ID3D11PixelShader* m_Shader;  // DirectX handle

		union
		{
			struct BufferUnion
			{
				Buffer m_PerTechnique;  // CONSTANT_GROUP_LEVEL_TECHNIQUE
				Buffer m_PerMaterial;   // CONSTANT_GROUP_LEVEL_MATERIAL
				Buffer m_PerGeometry;   // CONSTANT_GROUP_LEVEL_GEOMETRY
			};

			BufferUnion m_ConstantUnion;
			Buffer m_ConstantGroups[CONSTANT_GROUP_LEVEL_COUNT];
		};

		std::uint8_t m_ConstantOffsets[MAX_PS_CONSTANTS];  // Actual offset is multiplied by 4
	};

	static_assert(sizeof(PixelShader) == 0x80);
	static_assert(offsetof(PixelShader, m_ConstantUnion.m_PerMaterial) == 0x20);
	static_assert(offsetof(PixelShader, m_ConstantOffsets) == 0x40);

	class BSShader :
		public RE::NiRefObject,          // 00
		public RE::NiBoneMatrixSetterI,  // 10
		public RE::BSReloadShaderI       // 18
	{
	public:
		template <class Key>
		struct TechniqueIDHasher
		{
		public:
			std::uint32_t operator()(const Key& a_key) const
			{
				return a_key->m_TechniqueID;
			}
		};

		template <class T>
		struct TechniqueIDCompare
		{
		public:
			bool operator()(const T& a_lhs, const T& a_rhs) const
			{
				return a_lhs->m_TechniqueID == a_rhs->m_TechniqueID;
			}
		};

		template <class Key>
		using TechniqueIDMap = RE::BSTSet<Key, TechniqueIDHasher<Key>, TechniqueIDCompare<Key>>;

		inline static constexpr auto RTTI = RE::RTTI_BSShader;

		~BSShader() override;  // 00

		// add
		virtual bool SetupTechnique(TechniqueID Technique) = 0;                                   // 02
		virtual void RestoreTechnique(TechniqueID Technique) = 0;                                 // 03
		virtual void SetupMaterial(RE::BSShaderMaterial const* Material);                         // 04
		virtual void RestoreMaterial(RE::BSShaderMaterial const* Material);                       // 05
		virtual void SetupGeometry(BSRenderPass* Pass, uint32_t Flags) = 0;                       // 06
		virtual void RestoreGeometry(BSRenderPass* Pass, uint32_t RenderFlags) = 0;               // 07
		virtual void GetTechniqueName(TechniqueID Technique, char* Buffer, uint32_t BufferSize);  // 08
		virtual void ReloadShaders(bool Unknown);                                                 // 09

		std::uint32_t m_Type;
		TechniqueIDMap<VertexShader*> m_VertexShaderTable;
		TechniqueIDMap<PixelShader*> m_PixelShaderTable;
		const char* m_LoaderType;
	};

	static_assert(sizeof(BSShader) == 0x90);
	static_assert(offsetof(BSShader, m_Type) == 0x20);
	static_assert(offsetof(BSShader, m_VertexShaderTable) == 0x28);
	static_assert(offsetof(BSShader, m_PixelShaderTable) == 0x58);
	static_assert(offsetof(BSShader, m_LoaderType) == 0x88);
}
