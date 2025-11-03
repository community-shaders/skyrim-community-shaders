#ifndef PAYLOAD_HLSI
#define PAYLOAD_HLSI

struct PayloadData
{
    uint data;  // bit 0: missed, bits 1-4: depth (0-15), bits 5-31: seed (27 bits)
    
    // Create from components
    static PayloadData Create(bool missedFlag, uint depth, uint seed)
    {
        PayloadData p;
        p.data = (uint(missedFlag) & 0x1) | 
                 ((depth & 0xF) << 1) | 
                 ((seed & 0x7FFFFFF) << 5);
        return p;
    }
    
    // Get missed flag
    bool GetMissed()
    {
        return (data & 0x1) != 0;
    }
    
    // Get depth value (0-15)
    uint GetDepth()
    {
        return (data >> 1) & 0xF;
    }
    
    // Get seed value (27 bits)
    uint GetSeed()
    {
        return (data >> 5) & 0x7FFFFFF;
    }
    
    // Set missed flag
    void SetMissed(bool missedFlag)
    {
        data = (data & ~0x1) | (uint(missedFlag) & 0x1);
    }
    
    // Set depth value
    void SetDepth(uint depth)
    {
        data = (data & ~0x1E) | ((depth & 0xF) << 1);
    }
    
    // Set seed value
    void SetSeed(uint seed)
    {
        data = (data & 0x1F) | ((seed & 0x7FFFFFF) << 5);
    }
};

struct Payload
{
    float3 color;
    PayloadData data;
};

#endif