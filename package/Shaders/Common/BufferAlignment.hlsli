#ifndef __BUFFER_ALIGNMENT_DEPENDENCY_HLSL__
#define __BUFFER_ALIGNMENT_DEPENDENCY_HLSL__

// GPU Buffer Alignment Validation System
// This header provides compile-time validation for GPU constant buffer alignment
// to ensure all cbuffer structures maintain proper 16-byte boundaries.

// Alignment constants
#define BUFFER_ALIGNMENT_BYTES 16
#define BUFFER_ALIGNMENT_FLOATS 4

// Helper macro to calculate the size of a type in bytes (compile-time)
#define SIZEOF_BYTES(type) (sizeof(type))

// Helper macro to calculate the size of a type in float units (compile-time)
#define SIZEOF_FLOATS(type) ((sizeof(type) + 3) / 4)

// Helper macro to check if a size is aligned to 16-byte boundary
#define IS_ALIGNED_16(size_in_bytes) (((size_in_bytes) % BUFFER_ALIGNMENT_BYTES) == 0)

// Compile-time assertion for buffer alignment
// This will cause a compilation error if the buffer is not properly aligned
#define ASSERT_BUFFER_ALIGNED(buffer_type) \
    static_assert(IS_ALIGNED_16(sizeof(buffer_type)), \
        "Buffer " #buffer_type " is not aligned to 16-byte boundary. Size: " \
        /* Note: HLSL doesn't support string concatenation in static_assert, but the error will show the size */)

// Alignment helper struct to ensure proper padding
// Use this as the last member of your cbuffer struct to automatically pad to 16-byte alignment
template<int CurrentSizeBytes>
struct AlignmentPadding
{
    static const int padding_needed = (BUFFER_ALIGNMENT_BYTES - (CurrentSizeBytes % BUFFER_ALIGNMENT_BYTES)) % BUFFER_ALIGNMENT_BYTES;
    
    // Only add padding if needed (padding_needed > 0)
    uint padding[padding_needed > 0 ? (padding_needed + 3) / 4 : 0];
};

// Macro to automatically add padding to align a cbuffer to 16-byte boundary
// Usage: Add BUFFER_ALIGN_PAD(YourStructName) as the last member of your cbuffer
#define BUFFER_ALIGN_PAD(struct_name) \
    AlignmentPadding<sizeof(struct_name) - sizeof(AlignmentPadding<0>)> _alignment_padding

// Validation macro for cbuffer declarations
// Use this after your cbuffer declaration to validate alignment
#define VALIDATE_CBUFFER_ALIGNMENT(cbuffer_name) \
    static_assert(IS_ALIGNED_16(sizeof(cbuffer_name)), \
        "cbuffer " #cbuffer_name " is not aligned to 16-byte boundary")

// Helper macro to get the properly aligned size of a type
#define ALIGNED_SIZE(type) \
    (((sizeof(type) + BUFFER_ALIGNMENT_BYTES - 1) / BUFFER_ALIGNMENT_BYTES) * BUFFER_ALIGNMENT_BYTES)

// Macro to create properly aligned padding
// Usage: PADDING_TO_ALIGN(current_size_in_bytes)
#define PADDING_TO_ALIGN(current_size) \
    uint _pad[(BUFFER_ALIGNMENT_BYTES - ((current_size) % BUFFER_ALIGNMENT_BYTES)) % BUFFER_ALIGNMENT_BYTES > 0 ? \
              (BUFFER_ALIGNMENT_BYTES - ((current_size) % BUFFER_ALIGNMENT_BYTES)) / 4 : 0]

// Named padding macro for clarity
// Usage: EXPLICIT_PADDING(name, current_size_in_bytes)
#define EXPLICIT_PADDING(name, current_size) \
    uint name[(BUFFER_ALIGNMENT_BYTES - ((current_size) % BUFFER_ALIGNMENT_BYTES)) % BUFFER_ALIGNMENT_BYTES > 0 ? \
              (BUFFER_ALIGNMENT_BYTES - ((current_size) % BUFFER_ALIGNMENT_BYTES)) / 4 : 0]

// Validation macro that provides detailed error information
#define VALIDATE_BUFFER_DETAILED(buffer_type) \
    static_assert(sizeof(buffer_type) % BUFFER_ALIGNMENT_BYTES == 0, \
        "Buffer alignment error: " #buffer_type " size is not a multiple of 16 bytes"); \
    static_assert(sizeof(buffer_type) > 0, \
        "Buffer size error: " #buffer_type " has zero size")

// Example usage patterns:
//
// Method 1: Using validation macro (recommended for existing code)
// cbuffer PerFrame : register(b0)
// {
//     float4 data1;
//     float2 data2;
//     uint2 padding;  // Manual padding for 16-byte alignment
// };
// VALIDATE_CBUFFER_ALIGNMENT(PerFrame);
//
// Method 2: Using automatic padding (recommended for new code)
// struct PerFrameData
// {
//     float4 data1;
//     float2 data2;
//     PADDING_TO_ALIGN(sizeof(float4) + sizeof(float2));
// };
// cbuffer PerFrame : register(b0) { PerFrameData data; };
//
// Method 3: Using explicit named padding
// cbuffer PerFrame : register(b0)
// {
//     float4 data1;       // 16 bytes
//     float2 data2;       // 8 bytes
//     EXPLICIT_PADDING(pad0, 24); // Pads to next 16-byte boundary
// };

#endif  //__BUFFER_ALIGNMENT_DEPENDENCY_HLSL__