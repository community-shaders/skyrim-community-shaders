#ifndef __COORDMATH_DEPENDENCY_HLSL__
#define __COORDMATH_DEPENDENCY_HLSL__

namespace CoordMath
{
    float2 DegreesToVector(float degrees) {float2 outV; sincos(radians(degrees), outV.y, outV.x); return outV;}
    float4 DegreesToVector(float2 degrees) {float4 outV; sincos(radians(degrees), outV.yw, outV.xz); return outV;}

    float ChebyDistance(float2 Coords) {return max(abs(Coords.x), abs(Coords.y));}
    float ChebyDistance(float2 PointA, float2 PointB) {float2 d = PointA - PointB; return max(abs(d.x), abs(d.y));}

    float2 CartesianToPolar(float2 coords){
        float2 output = {length(coords), atan2(coords.y, coords.x)};
        output.y = (output.x == 0.0) ? 0.0 : output.y;
        return output;}

    float2 PolarToCartesian(float2 polar){
        float2 coords; sincos(polar.y, coords.y, coords.x);
        return coords * polar.x;}

    float2 AtlasFetch4x4(float2 coords, uint texNum){
        texNum = clamp(texNum, 1, 4);
        static const float2 pos[4] = {
        float2(0.0, 0.0), float2(0.5, 0.0),
        float2(0.0, 0.5), float2(0.5, 0.5)};
        return mad(coords, 0.5, pos[texNum-1]);}

    float2 AtlasFetch1x4(float2 coords, uint texNum){
        texNum = clamp(texNum, 1, 4);
        static const float2 pos[4] = {
        float2(0.0, 0.00), float2(0.0, 0.25),
        float2(0.0, 0.50), float2(0.0, 0.75)};
        return mad(coords, float2(1.0, 0.25), pos[texNum-1]);}

    bool InsideRect(float2 p, float2 rectMin, float2 rectMax){
        return all(step(rectMin, p) * step(p, rectMax)); }
}
#endif