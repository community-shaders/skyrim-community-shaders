#include "Common/Math.hlsli"
//// Defines //////////////////////////////////////////////////////////////////////////////////////

#define LUM 0.333333
#define LUM_601 float3(0.299, 0.587, 0.114)
#define LUM_709 float3(0.212, 0.715, 0.072)
#define LUM_202 float3(0.262, 0.678, 0.059)

// Macros //
#define inv(x) (1.0 - (x))
#define delta(x) max(x, EPSILON_DIVISION)

// Debug //
#define CLEAR float3(0.0, 0.0, 0.0)
#define WHITE float3(1.0, 1.0, 1.0)
#define RED float3(1.0, 0.0, 0.0)
#define BLUE float3(0.0, 0.0, 1.0)

// Fetch //
#define TEX_ONE int3(1,0,0)
#define TEX_TWO int3(2,0,0)


//// Color ///////////////////////////////////////////////////////////////////////

float3 RGBtoHSV(float3 c) {
    float4 K = float4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
    float4 p = (c.g < c.b) ? float4(c.b, c.g, K.w, K.z) : float4(c.g, c.b, K.x, K.y);
    float4 q = (c.r < p.x) ? float4(p.x, p.y, p.z, c.r) : float4(c.r, p.y, p.z, p.x);
    float d = q.x - min(q.w, q.y); float e = 1.0e-10;
    return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);}

float3 HSVtoRGB(float3 c) {
    float4 K = float4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * lerp(K.xxx, saturate(p - K.xxx), c.y);}

float Luma(float3 Color){
    return dot(Color, LUM_709);}

float Luma(float3 Color, float3 Rec){
    return dot(Color, Rec);}

float Chroma(float3 Color){
    return max(Color.r, max(Color.g, Color.b)) - min(Color.r, min(Color.g, Color.b));}

float3 GammaToLinear(float3 Color){
    return pow(Color, 2.2);}

float3 LinearToGamma(float3 Color){
    return pow(Color, 1.0 / 2.2);}


//// Math /////////////////////////////////////////////////////////////////////////////////////////

float smootherstep(float edge0, float edge1, float x){
    float t = saturate((x - edge0) / (edge1 - edge0));
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);}

float LinearStep(float edge0, float edge1, float x){
    return saturate((x - edge0) / (edge1 - edge0));}

float2 LinearStep(float2 edge0, float2 edge1, float2 x){
    return saturate((x - edge0) / (edge1 - edge0));}

float3 LinearStep(float3 edge0, float3 edge1, float3 x){
    return saturate((x - edge0) / (edge1 - edge0));}

float MapRange(float x, float oldMin, float oldMax, float newMin, float newMax){
    return newMin + ((x - oldMin) / delta((oldMax - oldMin)) * (newMax - newMin));}

float Random(float seed){
    return frac(sin(seed * 12.9898) * 43758.5453);}

float Random(float2 coords){
	return frac(sin(dot(coords, float2(12.9898, 78.233))) * 43758.5453);}

float RandomUV(float2 coords){
	return frac(sin(dot(coords, float2(12.9898, 78.233))) * 43758.5453) * 0.5 + 0.5;}

float min2(float2 mn){
     return min(mn.x, mn.y);}

float min3(float3 mn){
     return min(mn.x, min(mn.y, mn.z));}

float max2(float2 mx){
    return max(mx.x, mx.y);}

float max3(float3 mx){
    return max(mx.x, max(mx.y, mx.z));}

float sum3(float3 v) {
    return v.x + v.y + v.z;}

float sum4(float4 v) {
    return v.x + v.y + v.z + v.w;}

float ufmod(float x, float y){
    return x - y * floor(x / delta(y));}

float2 ufmod(float2 x, float2 y){
    return x - y * floor(x / delta(y));}

float3 ufmod(float3 x, float3 y){
    return x - y * floor(x / delta(y));}

float NthRoot(float x, float n){
    return pow(x, rcp(n));}

float2 NthRoot(float2 x, float n){
    return pow(x, rcp(n));}

float2 sincos2(float In){
    float2 V; sincos(In, V.x, V.y); return V;}

float FastAtan(float x){
	return x * (-0.1784f * abs(x) - 0.0663f * x * x + 1.0301f);}


//// Coords ///////////////////////////////////////////////////////////////////////////////////////

float2 CartToPolar(float2 coords){
	float2 output = {length(coords), atan2(coords.y, coords.x)};
	output.y = (output.x == 0.0) ? 0.0 : output.y;
	return output;}

float2 PolarToCart(float2 polar){
	float2 coords; sincos(polar.y, coords.y, coords.x);
	return coords * polar.x;}

float2 DegreesToVect(float degrees){
    return sincos2(radians(degrees)).yx;}

float4 DegreesToVect(float2 degrees) {
    float4 outV; sincos(radians(degrees), outV.yw, outV.xz); return outV;}

float AbsDist(float2 Coords){
    return max(abs(Coords.x), abs(Coords.y));}

bool InsideRect(float2 p, float2 rectMin, float2 rectMax){
    return all(step(rectMin, p) * step(p, rectMax)); }

float2 AtlasFetch4(float2 coords, uint texNum){
    static const float2 pos[4] = {
    float2(0.0, 0.0), float2(0.5, 0.0),
    float2(0.0, 0.5), float2(0.5, 0.5)};
    return mad(coords, 0.5, pos[texNum - 1]);}

float2 AtlasFetch16(float2 coords, uint texNum){
    static const float2 pos[16] = {
    float2(0.00, 0.00), float2(0.25, 0.00), float2(0.50, 0.00), float2(0.75, 0.00),
    float2(0.00, 0.25), float2(0.25, 0.25), float2(0.50, 0.25), float2(0.75, 0.25),
    float2(0.00, 0.50), float2(0.25, 0.50), float2(0.50, 0.50), float2(0.75, 0.50),
    float2(0.00, 0.75), float2(0.25, 0.75), float2(0.50, 0.75), float2(0.75, 0.75)};
    return mad(coords, 0.25, pos[texNum - 1]);}

static const float2 BF4_Coords[4] = {
    float2( 0.5,  0.5),
    float2(-0.5,  0.5),
    float2( 0.5, -0.5),
    float2(-0.5, -0.5)};


//// Lighting /////////////////////////////////////////////////////////////////////////////////////

float Diffraction(float x, float Frequency, float Phase, float Amplitude){
	float  sinc = Math::PI * (x * Frequency + Phase) + EPSILON_DIVISION;
	sinc = sin(sinc) / sinc;
    return sinc * sinc * Amplitude;}

float3 DiffractionF3(float3 x, float Frequency, float Phase, float Amplitude){
	float3 sinc = Math::PI * (x * Frequency + Phase) + EPSILON_DIVISION;
	sinc = sin(sinc) / sinc;
    return sinc * sinc * Amplitude;}