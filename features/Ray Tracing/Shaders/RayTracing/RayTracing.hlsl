// Ray Tracing Shaders for Skyrim Community Shaders

struct RayPayload
{
    float3 color;
    float hitDistance;
};

struct Attributes
{
    float2 barycentrics;
};

// Shared constant buffer
cbuffer GlobalConstants : register(b0)
{
    float4x4 viewInverseMatrix;
    float4x4 projInverseMatrix;
    float3 cameraPosition;
    float rayBias;
    uint frameCount;
};

// Acceleration structure
RaytracingAccelerationStructure SceneBVH : register(t0);

// Output texture
RWTexture2D<float4> gOutput : register(u0);

[shader("raygeneration")]
void RayGen()
{
    // Get dispatch dimensions
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDim = DispatchRaysDimensions().xy;

    // Generate ray
    float2 d = (launchIndex.xy + 0.5f) / launchDim;
    float4 target = mul(projInverseMatrix, float4(d.x * 2 - 1, 1 - d.y * 2, 0, 1));
    target.xyz /= target.w;

    RayDesc ray;
    ray.Origin = cameraPosition;
    ray.Direction = normalize(mul(viewInverseMatrix, float4(target.xyz, 0)).xyz);
    ray.TMin = 0.001;
    ray.TMax = 10000.0;

    // Initialize payload
    RayPayload payload = { float3(0, 0, 0), -1 };

    // Trace ray
    TraceRay(
        SceneBVH,
        RAY_FLAG_NONE,
        0xFF, // Instance mask
        0,    // Ray contribution to hit group index
        1,    // Multiplier for geometry contribution to hit group index
        0,    // Miss shader index
        ray,
        payload);

    // Write result
    gOutput[launchIndex] = float4(payload.color, 1.0f);
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in Attributes attrib)
{
    // Basic diffuse shading
    float3 hitNormal = normalize(HitWorldNormal());
    float3 lightDir = normalize(float3(0.5, 1, 0.25));

    float NdotL = saturate(dot(hitNormal, lightDir));
    payload.color = float3(0.8, 0.8, 0.8) * NdotL;
    payload.hitDistance = HitT();
}

[shader("anyhit")]
void AnyHit(inout RayPayload payload, in Attributes attrib)
{
    // Simple alpha testing (can be expanded for foliage etc.)
    if (Opacity() < 0.5)
        IgnoreHit();
}

[shader("miss")]
void Miss(inout RayPayload payload)
{
    // Sky gradient
    float t = saturate(payload.Direction.y * 0.5 + 0.5);
    payload.color = lerp(float3(0.3, 0.4, 0.7), float3(0.8, 0.9, 1.0), t);
}
