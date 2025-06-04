Texture2D<float4> SSRTexture : register(t0);
Texture2D<float4> HitPDF : register(t1);
Texture2D<float4> TemporalRadiance : register(t2);

RWTexture2D<float4> OutputSSR : register(u0);
RWTexture2D<float4> OutputHistory : register(u1);

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID)
{
    float4 outputColor = float4(0, 0, 0, 0);
    float4 originColor = SSRTexture[DTid.xy];
    float4 hitPDF = HitPDF[DTid.xy];
    float4 temporalRadiance = TemporalRadiance[DTid.xy];

    outputColor = temporalRadiance != 0 ? temporalRadiance : originColor;

    OutputSSR[DTid.xy] = outputColor;
    OutputHistory[DTid.xy] = outputColor;
}