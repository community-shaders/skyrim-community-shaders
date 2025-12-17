#include "Raytracing/Includes/Types.hlsli"
#include "Raytracing/Includes/RT/Geometry.hlsli"

[shader("closesthit")]
void main(inout Payload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    payload.instanceIndex = (uint16_t)InstanceIndex();
    payload.primitiveIndex = PrimitiveIndex();
    payload.shapeIndex = (uint16_t)GetShapeIdx();
    payload.barycentrics = (half2)attribs.barycentrics;
    payload.hitDistance = RayTCurrent();
}