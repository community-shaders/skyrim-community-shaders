// from ShortFuse (RenoDX)

float ApplyCurve(float x, float a, float b, float c, float d, float e, float f) {
  return ((x * (a * x + c * b) + d * e) / (x * (a * x + b) + d * f)) - e / f;
}

float3 ApplyCurve(float3 x, float a, float b, float c, float d, float e, float f) {
    return ((x * (a * x + c * b) + d * e) / (x * (a * x + b) + d * f)) - e / f;
}

static const float A = 0.22; // Shoulder Strength
static const float B = 0.30; // Linear Strength
static const float C = 0.10; // Linear Angle
static const float D = 0.20; // Toe Strength
static const float E = 0.01; // Toe Numerator
static const float F = 0.30; // Toe Denominator
static const float W = 11.2; // Linear White

float3 applyUncharted2Tonemap(float3 untonemapped, float linear_white = W) {
    return ApplyCurve(untonemapped, A, B, C, D, E, F)
         / ApplyCurve(linear_white, A, B, C, D, E, F);
}