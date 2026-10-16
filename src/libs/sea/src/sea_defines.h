#pragma once

#define G2MX_SEA 0
#define G3_SEA 1
#define R200_SEA 2

#define CUBEMAP_DELTA_TIME 50

// Planar reflection targets (Ocean3 rr_size equivalent). Cubemaps stay 128.
#define SEA_REFLECTION_SIZE 512

// Legacy AnimSpeed (baked frames/sec) -> FFT ocean-sec/sec. Anchor: old
// default 6 ~= Blood wave_speed 1.25. Single scale: script L1:L2 ratios kept.
// (MOP scenes carry ocean-sec natively and bypass the rescale.)
#define SEA_FFT_TIME_SCALE (1.25f / 6.0f)
// TEMP MOP-scene test override: forces every ocean member each frame,
// scripts bypassed. Scene datum Height -2 via fSeaHeight in the override.
#define SEA_MOP_TEST 0
#define SEA_FRENEL_TABLE_SIZE 512
#define SEA_COS_TABLE_SIZE 2048
#define SEA_EXP_TABLE_SIZE 256

extern void GetLineABC(CVECTOR &v1, CVECTOR &v2, float &A, float &B, float &C);
extern void RotateAroundY(float &x, float &y, float cos, float sin);
