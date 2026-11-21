Texture2D<float4> BaseTex : register(t0);
RWStructuredBuffer<uint> Result : register(u0);

[numthreads(1, 1, 1)] void main() {
	// Texture metadata may still report zero dimensions while cells load.
	uint w, h;
	BaseTex.GetDimensions(w, h);

	// A complex-grass atlas stores normals in its lower half.
	float3 complexTest = BaseTex.Load(int3(0, (int)h - 1, 0)).xyz * 2.0 - 1.0;

	// Reject diffuse texels that resemble unit normals but point below the surface.
	Result[0] = asuint(complexTest.z > 0.0 ? length(complexTest) : 2.0);
}
