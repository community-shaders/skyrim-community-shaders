#ifndef RT_CACHE_HLSI
#define RT_CACHE_HLSI

#include "Common/Spherical Harmonics/SphericalHarmonics.hlsli"

struct SH1Irradiance
{
    sh2 SHR;
    sh2 SHG;
    sh2 SHB;
    
    void Store(in float3 dir, in float3 color)
    {
        sh2 basis = SphericalHarmonics::Evaluate(dir);
        
		SHR = SphericalHarmonics::Add(SHR, SphericalHarmonics::Scale(basis, color.r));
		SHG = SphericalHarmonics::Add(SHG, SphericalHarmonics::Scale(basis, color.g));
		SHB = SphericalHarmonics::Add(SHB, SphericalHarmonics::Scale(basis, color.b));        
    }    
    
    float3 Sample(in float3 dir)
    {
        return SphericalHarmonics::Unproject(SHR, SHG, SHB, dir);
    }
};

struct CacheEntry
{
    float3 Position;
    SH1Irradiance Irradiance;
};

#endif