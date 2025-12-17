#ifndef INSTANCE_HLSI
#define INSTANCE_HLSI

struct LightData
{
	uint Count; 
	uint Data[4];

    uint GetGroup(uint index) 
    {
        return index >> 2;   
    }        
    
    uint GetOffset(uint index) 
    {
        return (index & 3) << 3;    
    }    
    
    uint GetID(uint index)
    {
        uint group = GetGroup(index);
        uint offset = GetOffset(index);
        
        return (Data[group] >> offset) & 0xFFu;
    }

#ifdef __cplusplus
	LightData() = default;

	LightData(const eastl::vector<size_t>& ids)
	{
		StoreIDs(ids);
	}
	
	void SetID(uint index, uint val)
	{
		uint group = GetGroup(index);
		uint offset = GetOffset(index);
		uint mask = ~(0xFFu << offset);
		Data[group] = (Data[group] & mask) | ((val & 0xFFu) << offset);
	}

	void StoreIDs(const eastl::vector<size_t>& ids)
	{
		size_t count = std::min(ids.size(), static_cast<size_t>(16));
		Count = static_cast<uint32_t>(count);

		for (size_t i = 0; i < count; ++i) {
			uint32_t id = std::min(static_cast<uint32_t>(ids[i]), 255u);
			SetID(static_cast<uint32_t>(i), id);
		}
	}
#endif 
};


#ifdef __cplusplus
struct InstanceData
#else
struct Instance
#endif    
{
#ifdef __cplusplus	
    float3x4 Transform;
#else
	row_major float3x4 Transform;
#endif	
    LightData LightData;
	uint FirstMeshID;
};

#ifdef __cplusplus
static_assert(sizeof(InstanceData) % 4 == 0);
#endif

#endif