#ifndef PAYLOAD_HLSI
#define PAYLOAD_HLSI

struct Payload
{   
    float hitDistance;
    uint primitiveIndex;
    float2 barycentrics;
    uint instanceIndex;   
    uint shapeIndex;
 
    bool Hit() { return hitDistance > 0.0f; }
};

#endif // PAYLOAD_HLSI