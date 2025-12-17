#ifndef PAYLOAD_HLSI
#define PAYLOAD_HLSI

struct Payload
{   
    float hitDistance;
    uint primitiveIndex;
    half2 barycentrics;
    uint16_t instanceIndex;   
    uint16_t shapeIndex;
 
    bool Hit() { return hitDistance > 0.0f; }
};

#endif // PAYLOAD_HLSI