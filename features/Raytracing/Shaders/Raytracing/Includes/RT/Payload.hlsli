#ifndef PAYLOAD_HLSI
#define PAYLOAD_HLSI

uint PackUnorm2x16(float2 v)
{
    uint2 u = (uint2)round(saturate(v) * 65535.0f);
    return u.x | (u.y << 16);
}

float2 UnpackUnorm2x16(uint p)
{
    uint2 u = uint2(p & 0xFFFF, p >> 16);
    return float2(u) * (1.0f / 65535.0f);
}

struct Payload
{
    float hitDistance;
    uint primitiveIndex;
    uint barycentricsPacked;
    uint instanceShapeIndexPacked;
    
    void PackBarycentrics(float2 barycentrics)
    {
        barycentricsPacked = PackUnorm2x16(barycentrics);
    }    
   
    float2 Barycentrics()
    {
        return UnpackUnorm2x16(barycentricsPacked);
    }        
    
    void PackInstanceShapeIndex(uint instanceIndex, uint shapeIndex)
    {
        instanceShapeIndexPacked = (instanceIndex & 0xFFFF) | ((shapeIndex & 0xFFFF) << 16);
    }    
    
    uint InstanceIndex()
    {
        return instanceShapeIndexPacked & 0xFFFF;
    }

    uint ShapeIndex()
    {
        return instanceShapeIndexPacked >> 16;
    }    
    
    bool Hit() { return hitDistance > 0.0f; }
};

#endif // PAYLOAD_HLSI