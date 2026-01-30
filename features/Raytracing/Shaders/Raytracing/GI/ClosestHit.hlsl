#include "Raytracing/Includes/Types.hlsli"

#include "Raytracing/Includes/Materials/TexLODHelpers.hlsli"

[shader("closesthit")]
void main(inout Payload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    payload.hitDistance = RayTCurrent();
    payload.primitiveIndex = PrimitiveIndex();
    payload.PackBarycentrics(attribs.barycentrics);
    payload.PackInstanceGeometryIndex(InstanceIndex(), GeometryIndex());
}