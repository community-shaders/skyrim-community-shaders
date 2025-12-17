#include "Raytracing/Includes/Types.hlsli"
#include "Raytracing/Includes/RT/Geometry.hlsli"

[shader("closesthit")]
void main(inout Payload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    payload.instanceIndex = InstanceIndex();
    payload.primitiveIndex = PrimitiveIndex();
    payload.shapeIndex = GetShapeIdx();
    payload.barycentrics = attribs.barycentrics;
    payload.hitDistance = RayTCurrent();
}