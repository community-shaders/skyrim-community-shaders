#ifndef VERTEX_HLSI
#define VERTEX_HLSI

struct ubyte4f
{
    uint packedValue; // stores 4 unsigned bytes (unorm ubyte4)

    // Manual unpack function: returns float4 with components in [0,1]
    half4 unpack()
    {
        half4 result;
        result.x = ((packedValue >> 0)  & 0xFF) / 255.0;  // first component
        result.y = ((packedValue >> 8)  & 0xFF) / 255.0;  // second component
        result.z = ((packedValue >> 16) & 0xFF) / 255.0;  // third component
        result.w = ((packedValue >> 24) & 0xFF) / 255.0;  // fourth component
        return result;
    }
};

struct Vertex
{
	float3 Position;
	
	#if defined(__HLSL_VERSION) || defined(__INTELLISENSE__)
	half2 Texcoord;
	ubyte4f Normal;
	ubyte4f Color;		
	#else
	half2 Texcoord;
	uint16_t Texcoord[2];
	uint8_t Normal[4];
	uint8_t Color[4];
	#endif
};

#endif