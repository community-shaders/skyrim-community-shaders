Texture2D<float4> GeometryNormalDepthTexture: register(t0, space0);
Texture2D<float4> AlbedoTexture             : register(t1, space0);
Texture2D<float4> ReflectanceTexture        : register(t2, space0);

RWTexture2D<float4> FinalTexture            : register(u0);
RWTexture2D<float4> DiffuseOutputTexture    : register(u1);
RWTexture2D<float4> SpecularOutputTexture   : register(u2);
RWTexture2D<float> SpecHitDistanceTexture   : register(u3);
RWTexture2D<unorm float> DepthTexture       : register(u4);

[numthreads(8, 8, 1)]
void main(uint2 id : SV_DispatchThreadID)
{
    float3 diffuseGI = DiffuseOutputTexture[id].rgb;
    float4 specularGI = SpecularOutputTexture[id];
     
    float3 color = FinalTexture[id].rgb;  
    
    color += diffuseGI * AlbedoTexture[id].rgb;  
    color += specularGI.rgb * saturate(ReflectanceTexture[id].x + 0.01f);
    
    FinalTexture[id] = float4(color, 1.0f);
    SpecHitDistanceTexture[id] = specularGI.a;
    DepthTexture[id] = GeometryNormalDepthTexture[id].a;
}