# GPU Buffer Alignment Validation System

This document describes the GPU buffer alignment validation system for HLSL shaders in the Community Shaders project.

## Overview

GPU constant buffers (cbuffer structures) in HLSL must be aligned to 16-byte boundaries for optimal performance and correctness. This system provides compile-time validation to ensure all GPU buffers maintain proper alignment without relying solely on manual padding techniques.

## Problem Statement

Previously, the project relied on manual padding (e.g., `uint pad0[2]`) to ensure proper alignment, which was:
- Error-prone (easy to miscalculate padding)
- Difficult to maintain as structures evolve
- Lacked compile-time validation
- Could break when combining settings from multiple features

## Solution Components

### 1. HLSL Alignment Validation Headers

**File:** `package/Shaders/Common/BufferAlignment.hlsli`

This header provides:
- Compile-time assertions for buffer alignment
- Helper macros for automatic padding
- Validation macros that provide clear error messages

#### Key Features:
- `VALIDATE_CBUFFER_ALIGNMENT(cbuffer_name)` - Validates cbuffer alignment
- `VALIDATE_BUFFER_DETAILED(buffer_type)` - Detailed validation with error info
- `PADDING_TO_ALIGN(current_size)` - Automatic padding calculation
- `EXPLICIT_PADDING(name, current_size)` - Named padding for clarity

### 2. Build-Time Validation Tool

**File:** `tools/buffer_alignment_validator.py`

Python script that:
- Parses HLSL files to extract cbuffer structures
- Calculates buffer sizes and alignment
- Reports alignment issues with detailed error messages
- Can automatically fix alignment issues (future enhancement)

#### Usage:
```bash
# Validate all shaders (warnings only)
python tools/buffer_alignment_validator.py package/Shaders features/

# Strict validation (treat warnings as errors)
python tools/buffer_alignment_validator.py --strict package/Shaders features/

# Auto-fix alignment issues (future feature)
python tools/buffer_alignment_validator.py --fix package/Shaders features/
```

### 3. CMake Integration

**File:** `cmake/BufferAlignmentValidation.cmake`

Provides CMake functions to integrate validation into the build process:
- `add_buffer_alignment_validation(target_name)` - Adds validation to a target
- Creates custom targets for validation and fixing
- Configurable via `ENABLE_BUFFER_ALIGNMENT_VALIDATION` option

#### CMake Targets:
- `validate_buffer_alignment` - Check alignment (warnings only)
- `fix_buffer_alignment` - Auto-fix alignment issues
- `${target_name}_buffer_alignment_validation` - Strict validation as part of build

## Usage Examples

### Example 1: Validating Existing Buffers

Add validation to existing cbuffer declarations:

```hlsl
#include "Common/BufferAlignment.hlsli"

cbuffer PerFrame : register(b0)
{
    row_major float4x4 InvProjMatrix[2];  // 128 bytes
    float LightsNear;                     // 4 bytes
    float LightsFar;                      // 4 bytes
    // Total: 136 bytes (not aligned to 16-byte boundary)
    uint pad0[2];                         // 8 bytes padding -> 144 bytes (aligned)
}
// Validate the cbuffer alignment at compile time
VALIDATE_CBUFFER_ALIGNMENT(PerFrame);
```

### Example 2: Using Automatic Padding

For new structures, use automatic padding:

```hlsl
#include "Common/BufferAlignment.hlsli"

cbuffer PerFrame : register(b0)
{
    row_major float4x4 InvProjMatrix[2];  // 128 bytes
    float LightsNear;                     // 4 bytes
    float LightsFar;                      // 4 bytes
    // Automatically calculate and add padding
    PADDING_TO_ALIGN(sizeof(row_major float4x4) * 2 + sizeof(float) * 2);
}
VALIDATE_CBUFFER_ALIGNMENT(PerFrame);
```

### Example 3: Validating Structure Types

For reusable structures:

```hlsl
struct Light
{
    float3 color;           // 12 bytes
    float fade;             // 4 bytes
    float radius;           // 4 bytes
    float invRadius;        // 4 bytes
    float fadeZone;         // 4 bytes
    float sizeBias;         // 4 bytes
    float4 positionWS[2];   // 32 bytes
    float4 positionVS[2];   // 32 bytes
    uint4 roomFlags;        // 16 bytes
    uint lightFlags;        // 4 bytes
    uint shadowLightIndex;  // 4 bytes
    uint pad0;              // 4 bytes
    uint pad1;              // 4 bytes
    // Total: 128 bytes (aligned)
};
// Validate the structure alignment
VALIDATE_BUFFER_DETAILED(Light);
```

## Build Integration

### Enabling Validation

Buffer alignment validation is enabled by default. To disable it:

```cmake
set(ENABLE_BUFFER_ALIGNMENT_VALIDATION OFF)
```

### Build Process

1. **Automatic Validation**: When building the project, buffer alignment is validated automatically if enabled
2. **Strict Mode**: Build fails if any alignment issues are found
3. **Manual Validation**: Use `cmake --build . --target validate_buffer_alignment` for manual checks
4. **Auto-Fix**: Use `cmake --build . --target fix_buffer_alignment` to automatically fix issues

## Error Messages

The system provides clear error messages when alignment issues are detected:

```
✗ cbuffer PerFrame is NOT aligned to 16-byte boundary
  File: features/Light Limit Fix/Shaders/LightLimitFix/ClusterBuildingCS.hlsl:3-8
  Current size: 136 bytes
  Padding needed: 8 bytes
  Suggested fix: Add 'uint pad0[2];' as last member
```

## Type Size Reference

The validation system knows about standard HLSL types:

| Type | Size (bytes) | Notes |
|------|--------------|-------|
| `float`, `int`, `uint` | 4 | Basic scalar types |
| `float2`, `int2`, `uint2` | 8 | 2-component vectors |
| `float3`, `int3`, `uint3` | 12 | 3-component vectors |
| `float4`, `int4`, `uint4` | 16 | 4-component vectors |
| `float4x4` | 64 | 4x4 matrix (column-major) |
| `row_major float4x4` | 64 | 4x4 matrix (row-major) |
| Arrays | `element_size * count` | Each element aligned to 16 bytes |

## Migration Guide

### For Existing Shaders

1. Include the alignment header: `#include "Common/BufferAlignment.hlsli"`
2. Add validation macros after cbuffer declarations
3. Fix any reported alignment issues
4. Document the padding with comments

### For New Shaders

1. Include the alignment header from the start
2. Use automatic padding macros for new cbuffers
3. Add validation macros for compile-time checking
4. Use the build tools to verify alignment

## Best Practices

1. **Always validate**: Add validation macros to all cbuffer declarations
2. **Document padding**: Use comments to explain manual padding
3. **Use helpers**: Prefer automatic padding macros over manual calculations
4. **Test early**: Run validation frequently during development
5. **Fix promptly**: Address alignment issues as soon as they're detected

## Future Enhancements

- Automatic padding insertion (fix mode)
- Integration with IDE/editor for real-time validation
- Support for custom alignment requirements
- Validation of structured buffer layouts
- Performance impact analysis of alignment choices

## Troubleshooting

### Common Issues

1. **Unknown type sizes**: The validator may not recognize custom types
   - Solution: Add type definitions to the validator or use explicit sizing

2. **Complex array calculations**: Arrays with complex alignment rules
   - Solution: Use explicit padding or update the validator logic

3. **Platform differences**: Different alignment requirements on different platforms
   - Solution: Use conditional compilation based on target platform

### Getting Help

If you encounter issues with the buffer alignment validation system:

1. Check the error messages for specific guidance
2. Review the examples in this documentation
3. Run the validator in verbose mode for detailed information
4. Consult the HLSL specification for alignment rules