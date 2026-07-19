#ifndef __OIT_COMMON__
#define __OIT_COMMON__

#ifndef OIT_NODE_COUNT 
#define OIT_NODE_COUNT			8
#endif

// Forces compression to only work on the second half of the nodes (cheaper and better IQ in most cases)
#if OIT_NODE_COUNT >= 8
#define AOIT_DONT_COMPRESS_FIRST_HALF 
#endif

//#define OIT_EARLY_Z_CULL
#define OIT_USE_ROV 1
#define OIT_TILED_ADDRESSING 1

#if OIT_NODE_COUNT == 2
#define OIT_RT_COUNT			1
#else
#define OIT_RT_COUNT			(OIT_NODE_COUNT / 4)
#endif

#if !defined(OIT_WRITE_DEPTH)
#define OIT_WRITE_DEPTH			0
#endif

#if !defined(OIT_CAPTURE_IGNORE_ALPHA_THRESHOULD)
#define OIT_CAPTURE_IGNORE_ALPHA_THRESHOULD 0
#endif

static const uint OIT_FEATURE_FLAGS_SHIFT    = 10UL;
static const uint OIT_FLAGS_ADDITIVE         = 0x1UL;
static const uint OIT_FLAGS_MULTIPLICATIVE   = 0x2UL;
static const uint OIT_FLAGS_MULTIPLICATIVE_A = 0x3UL;
static const uint OIT_FLAGS_BLEND_MODS       = 0x3UL;
static const uint OIT_FLAGS_DEPTH_WRITE      = 0x4UL;
static const uint OIT_FLAGS_DISABLED         = 0x8UL;
static const uint OIT_FLAGS_BITS             = 0x15UL;
static const float OIT_EMPTY_NODE_DEPTH = 3.40282E38f;
static const float OIT_FIRST_NODE_TRANS = 1.f;
#endif