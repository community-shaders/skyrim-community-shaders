// © 2022 NVIDIA Corporation

//=============================================================================================
// SETTINGS
//=============================================================================================

// Fused or separate denoising selection
// 0 - DIFFUSE and SPECULAR
// 1 - DIFFUSE_SPECULAR
#define NRD_COMBINED                        1

// NORMAL - common (non specialized) denoisers
// SH - SH (spherical harmonics or spherical gaussian) denoisers
// OCCLUSION - OCCLUSION (ambient or specular occlusion only) denoisers
// DIRECTIONAL_OCCLUSION - DIRECTIONAL_OCCLUSION (ambient occlusion in SH mode) denoisers
#define NRD_MODE                            NORMAL // NRD sample recompilation required
#define SIGMA_TRANSLUCENCY                  0

// Default = 1
#define USE_IMPORTANCE_SAMPLING             1
#define USE_SHARC_DITHERING                 1.5 // radius in voxels
#define USE_TRANSLUCENCY                    1 // translucent foliage
#define USE_MOVING_EMISSION_FIX             1 // fixes a dark tail, left by an animated emissive object

// Default = 0
#define USE_SANITIZATION                    0 // NRD sample is NAN/INF free
#define USE_SIMULATED_MATERIAL_ID_TEST      0 // "material ID" debugging
#define USE_SIMULATED_FIREFLY_TEST          0 // "anti-firefly" debugging
#define USE_CAMERA_ATTACHED_REFLECTION_TEST 0 // test special treatment for reflections of objects attached to the camera
#define USE_RUSSIAN_ROULETTE                0 // bad practice for real-time denoising
#define USE_DRS_STRESS_TEST                 0 // NRD must not touch GARBAGE data outside of DRS rectangle
#define USE_INF_STRESS_TEST                 0 // NRD must not touch GARBAGE data outside of denoising range
#define USE_ANOTHER_COBALT                  0 // another cobalt variant
#define USE_PUDDLES                         0 // add puddles
#define USE_RANDOMIZED_ROUGHNESS            0 // randomize roughness ( a common case in games )
#define USE_STOCHASTIC_SAMPLING             0 // needed?
#define USE_LOAD                            0 // Load vs SampleLevel
#define USE_SHARC_DEBUG                     0 // 1 - show cache, 2 - show grid (NRD sample recompile required)
#define USE_TAA_DEBUG                       0 // 1 - show weight
#define USE_BIAS_FIX                        0 // fixes negligible hair and specular bias

//=============================================================================================
// CONSTANTS
//=============================================================================================

// NRD variant
#define NORMAL                              0
#define SH                                  1 // NORMAL + SH (SG) resolve
#define OCCLUSION                           2
#define DIRECTIONAL_OCCLUSION               3 // diffuse OCCLUSION + SH (SG) resolve

// Denoiser
#define DENOISER_REBLUR                     0
#define DENOISER_RELAX                      1
#define DENOISER_REFERENCE                  2

// Resolution
#define RESOLUTION_FULL                     0
#define RESOLUTION_FULL_PROBABILISTIC       1
#define RESOLUTION_HALF                     2

// What is on screen?
#define SHOW_FINAL                          0
#define SHOW_DENOISED_DIFFUSE               1
#define SHOW_DENOISED_SPECULAR              2
#define SHOW_AMBIENT_OCCLUSION              3
#define SHOW_SPECULAR_OCCLUSION             4
#define SHOW_SHADOW                         5
#define SHOW_BASE_COLOR                     6
#define SHOW_NORMAL                         7
#define SHOW_ROUGHNESS                      8
#define SHOW_METALNESS                      9
#define SHOW_MATERIAL_ID                    10
#define SHOW_PSR_THROUGHPUT                 11
#define SHOW_WORLD_UNITS                    12
#define SHOW_INSTANCE_INDEX                 13
#define SHOW_UV                             14
#define SHOW_CURVATURE                      15
#define SHOW_MIP_PRIMARY                    16
#define SHOW_MIP_SPECULAR                   17

// Predefined material override
#define MATERIAL_GYPSUM                     1
#define MATERIAL_COBALT                     2

// Material ID
#define MATERIAL_ID_DEFAULT                 0.0f
#define MATERIAL_ID_METAL                   1.0f
#define MATERIAL_ID_HAIR                    2.0f
#define MATERIAL_ID_SELF_REFLECTION         3.0f

// Mip mode
#define MIP_VISIBILITY                      0 // for visibility: emission, shadow and alpha mask
#define MIP_LESS_SHARP                      1 // for normal
#define MIP_SHARP                           2 // for albedo and roughness

// Register spaces ( sets )
#define SET_OTHER                           0
#define SET_RAY_TRACING                     1
#define SET_SHARC                           2
#define SET_MORPH                           3
#define SET_ROOT                            4

// Path tracing
#define PT_THROUGHPUT_THRESHOLD             0.001
#define PT_IMPORTANCE_SAMPLES_NUM           16
#define PT_SPEC_LOBE_ENERGY                 0.95 // trimmed to 95%
#define PT_SHADOW_RAY_OFFSET                1.0 // pixels
#define PT_BOUNCE_RAY_OFFSET                0.25 // pixels
#define PT_GLASS_RAY_OFFSET                 0.05 // pixels
#define PT_MAX_FIREFLY_RELATIVE_INTENSITY   20.0 // no more than 20x energy increase in case of probabilistic sampling
#define PT_EVIL_TWIN_LOBE_TOLERANCE         0.005 // normalized %
#define PT_GLASS_MIN_F                      0.05 // adds a bit of stability and bias
#define PT_DELTA_BOUNCES_NUM                8
#define PT_PSR_BOUNCES_NUM                  2
#define PT_RAY_FLAGS                        0

// Spatial HAsh-based Radiance Cache ( SHARC )
#define SHARC_CAPACITY                      ( 1 << 22 )
#define SHARC_SCENE_SCALE                   45.0
#define SHARC_DOWNSCALE                     5
#define SHARC_ANTI_FIREFLY                  false
#define SHARC_STALE_FRAME_NUM_MIN           32 // new version uses 8 by default, old value offers more stability in voxels with low number of samples ( critical for glass )
#define SHARC_SEPARATE_EMISSIVE             1
#define SHARC_MATERIAL_DEMODULATION         1
#define SHARC_USE_FP16                      0

// Blue noise
#define BLUE_NOISE_SPATIAL_DIM              128 // see StaticTexture::ScramblingRanking
#define BLUE_NOISE_TEMPORAL_DIM             4 // good values: 4-8 for shadows, 8-16 for occlusion, 8-32 for lighting

// Other
#define FP16_MAX                            65504.0
#define INF                                 1e5
#define LINEAR_BLOCK_SIZE                   256
#define FP16_VIEWZ_SCALE                    0.125 // TODO: tuned for meters, needs to be scaled down for cm and mm
#define MAX_MIP_LEVEL                       11.0
#define LEAF_TRANSLUCENCY                   0.25
#define LEAF_THICKNESS                      0.001 // TODO: viewZ dependent?
#define STRAND_THICKNESS                    80e-6f
#define TAA_HISTORY_SHARPNESS               0.66 // sharper ( was 0.5 )
#define TAA_SIGMA_SCALE                     2.0 // allow nano ghosting ( was 1.0 ) // TODO: can negatively affect moving shadows
#define GARBAGE                             sqrt( -1.0 ) // sqrt( -1.0 ) or -log( 0.0 ) or 32768.0

#define MORPH_MAX_ACTIVE_TARGETS_NUM        8u
#define MORPH_ELEMENTS_PER_ROW_NUM          4
#define MORPH_ROWS_NUM                      ( MORPH_MAX_ACTIVE_TARGETS_NUM / MORPH_ELEMENTS_PER_ROW_NUM )

// Instance flags
#define FLAG_FIRST_BIT                      24 // this + number of flags must be <= 32
#define NON_FLAG_MASK                       ( ( 1 << FLAG_FIRST_BIT ) - 1 )

#define FLAG_NON_TRANSPARENT                0x01 // geometry flag: non-transparent
#define FLAG_TRANSPARENT                    0x02 // geometry flag: transparent
#define FLAG_FORCED_EMISSION                0x04 // animated emissive cube
#define FLAG_STATIC                         0x08 // no velocity
#define FLAG_HAIR                           0x10 // hair
#define FLAG_LEAF                           0x20 // leaf
#define FLAG_SKIN                           0x40 // skin
#define FLAG_MORPH                          0x80 // morph

#define GEOMETRY_ALL                        ( FLAG_NON_TRANSPARENT | FLAG_TRANSPARENT )