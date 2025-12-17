#ifndef BYTE4_HLSL
#define BYTE4_HLSL

struct ubyte4f
{
    uint packed;

    #ifndef __cplusplus
    half4 unpack()
    {
        return half4(
            (half)(packed & 0xFF) / 255.0h,
            (half)((packed >> 8)  & 0xFF) / 255.0h,
            (half)((packed >> 16) & 0xFF) / 255.0h,
            (half)(packed >> 24) / 255.0h
        );
    }
    #endif
};

struct byte4f
{
    ubyte4f packed;

    #ifndef __cplusplus
    half4 unpack()
    {
        return packed.unpack() * 2.0h - 1.0h;
    }
    #endif
};

#endif