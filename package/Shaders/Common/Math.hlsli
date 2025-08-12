#ifndef __MATH_DEPENDENCY_HLSL__
#define __MATH_DEPENDENCY_HLSL__

#define EPSILON_SSS_ALBEDO 1e-3f  // For albedo clamping in SSS calculations
#define EPSILON_DOT_CLAMP 1e-5f  // For dot product clamping
#define EPSILON_DIVISION  1e-6f	 // For division to avoid division by zero

namespace Math
{
	static const float4x4 IdentityMatrix = {
		{ 1, 0, 0, 0 },
		{ 0, 1, 0, 0 },
		{ 0, 0, 1, 0 },
		{ 0, 0, 0, 1 }
	};

	static const float PI = 3.1415926535897932384626433832795f;  // PI
	static const float HALF_PI = PI * 0.5f;                      // PI / 2
	static const float TAU = PI * 2.0f;                          // PI * 2

	float min2(float2 mn) {return min(mn.x, mn.y);}
	float min3(float3 mn) {return min(mn.x, min2(mn.yz));}
	float min4(float4 mn) {return min(mn.x, min3(mn.yzw));}

	float max2(float2 mx) {return max(mx.x, mx.y);}
	float max3(float3 mx) {return max(mx.x, max2(mx.yz));}
	float max4(float4 mx) {return max(mx.x, max3(mx.yzw));}

	float sum3(float3 v) {return v.x + v.y + v.z;}
	float sum4(float4 v) {return v.x + v.y + v.z + v.w;}

	float  ufmod(float x, float y)   {return x - y * floor(x / max(y, EPSILON_DIVISION));}
	float2 ufmod(float2 x, float2 y) {return x - y * floor(x / max(y, EPSILON_DIVISION));}

	float nRoot(float x, float n) {return (n == 0) ? 1.0 : pow(max(0, x), rcp(n));}

	float2 sincos2(float In){ float2 V; sincos(In, V.x, V.y); return V;}

	float LinearStep(float edge0, float edge1, float x){
    	return saturate((x - edge0) / max((edge1 - edge0), EPSILON_DIVISION));}

	float2 LinearStep(float2 edge0, float2 edge1, float2 x){
		return saturate((x - edge0) / max((edge1 - edge0), EPSILON_DIVISION));}

	float3 LinearStep(float3 edge0, float3 edge1, float3 x){
		return saturate((x - edge0) / max((edge1 - edge0), EPSILON_DIVISION));}

	float MapRange(float x, float oldMin, float oldMax, float newMin, float newMax){
    	return newMin + ((x - oldMin) / max(oldMax - oldMin, EPSILON_DIVISION)) * (newMax - newMin);}

	float smootherstep(float edge0, float edge1, float x){
		float t = LinearStep(edge0, edge1, x);
		return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);}

	float2 smootherstep(float2 edge0, float2 edge1, float2 x){
		float2 t = LinearStep(edge0, edge1, x);
		return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);}

	float3 smootherstep(float3 edge0, float3 edge1, float3 x){
		float3 t = LinearStep(edge0, edge1, x);
		return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);}

	float Diffraction(float x, float Frequency, float Phase, float Amplitude){
		float  sinc = PI * (x * Frequency + Phase);
		sinc = sin(sinc) / max(sinc, EPSILON_DIVISION);
		return sinc * sinc * Amplitude;}

	float3 Diffraction(float3 x, float Frequency, float Phase, float Amplitude){
		float3 sinc = PI * (x * Frequency + Phase);
		sinc = sin(sinc) / max(sinc, EPSILON_DIVISION);
		return sinc * sinc * Amplitude;}
}

#endif  //__MATH_DEPENDENCY_HLSL__