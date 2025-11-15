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

    void SetID(uint index, uint val)
    {
        uint group = GetGroup(index);
        uint offset = GetOffset(index);      
        uint mask   = ~(0xFFu << offset);
        Data[group] = (Data[group] & mask) | ((val & 0xFFu) << offset);
    }    
};

struct Material
{
	float4 texCoordOffsetScale;
	float4 emissiveColor;
};

struct Instance
{
	uint MeshID;
    LightData LightData;
    Material Material;
};

#endif