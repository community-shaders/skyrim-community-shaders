#ifndef DOF_GATHER_KERNEL_HLSLI
#define DOF_GATHER_KERNEL_HLSLI

// Eight samples per ring-distance matches the scalable-disc kernel density used by the
// high-quality gather path: 80 taps for four rings and 120 taps for five rings. Positions are
// supplied by BokehSamples so procedural and custom apertures share the same gather code.
static const uint2 GatherSamplePairs[60] = {
	uint2(0, 1), uint2(2, 3), uint2(4, 5), uint2(6, 7),
	uint2(8, 9), uint2(10, 11), uint2(12, 13), uint2(14, 15), uint2(16, 17), uint2(18, 19), uint2(20, 21), uint2(22, 23),
	uint2(24, 25), uint2(26, 27), uint2(28, 29), uint2(30, 31), uint2(32, 33), uint2(34, 35), uint2(36, 37), uint2(38, 39), uint2(40, 41), uint2(42, 43), uint2(44, 45), uint2(46, 47),
	uint2(48, 49), uint2(50, 51), uint2(52, 53), uint2(54, 55), uint2(56, 57), uint2(58, 59), uint2(60, 61), uint2(62, 63), uint2(64, 65), uint2(66, 67), uint2(68, 69), uint2(70, 71), uint2(72, 73), uint2(74, 75), uint2(76, 77), uint2(78, 79),
	uint2(80, 81), uint2(82, 83), uint2(84, 85), uint2(86, 87), uint2(88, 89), uint2(90, 91), uint2(92, 93), uint2(94, 95), uint2(96, 97), uint2(98, 99), uint2(100, 101), uint2(102, 103), uint2(104, 105), uint2(106, 107), uint2(108, 109), uint2(110, 111), uint2(112, 113), uint2(114, 115), uint2(116, 117), uint2(118, 119)
};

#if GATHER_RING_COUNT == 4
#	define GATHER_SAMPLE_PAIR_COUNT 40
// Samples are authored for five rings at r/(5+0.5). Rescale the first four rings to
// r/(4+0.5), retaining the half-sample guard band at the aperture boundary.
#	define GATHER_SAMPLE_SCALE 1.222222222f
#elif GATHER_RING_COUNT == 5
#	define GATHER_SAMPLE_PAIR_COUNT 60
#	define GATHER_SAMPLE_SCALE 1.0f
#else
#	error Unsupported GATHER_RING_COUNT
#endif

#endif
