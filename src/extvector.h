#pragma once 
#define _EXT_VECTOR_H
#include <arm_neon.h>
typedef float float2 __attribute__((ext_vector_type(2)));
typedef int int2 __attribute__((ext_vector_type(2)));
typedef uint uint2 __attribute__((ext_vector_type(2)));

typedef float float3 __attribute__((ext_vector_type(3)));
typedef int int3 __attribute__((ext_vector_type(3)));
typedef uint uint3 __attribute__((ext_vector_type(3)));

typedef float float4 __attribute__((ext_vector_type(4)));
typedef int int4 __attribute__((ext_vector_type(4)));
typedef uint uint4 __attribute__((ext_vector_type(4)));


// TODO: x64 version of the following functions.

static inline float rsqrtf_fast(float x) {
    float32x2_t vx = vdup_n_f32(x);
    float32x2_t y  = vrsqrte_f32(vx);
    y = vmul_f32(y, vrsqrts_f32(vx, vmul_f32(y, y)));
    y = vmul_f32(y, vrsqrts_f32(vx, vmul_f32(y, y)));
    return vget_lane_f32(y, 0);
}

static inline float dot(float4 a, float4 b) { 
    float32x4_t p = vmulq_f32(*(const float32x4_t*)&a, *(const float32x4_t*)&b);
    return vaddvq_f32(p);
}

static inline float dot(float2 a, float2 b) { 
    return a.x * b.x + a.y * b.y;
}

// the rest is cross platform
// clang ext_vector ftw


static inline float lengthsq(float4 x) { return dot(x, x); }

static inline float length(float4 x) { return sqrtf(dot(x, x)); }

static inline float length(float2 x) { return sqrtf(dot(x, x)); }

static inline float4 normalize(float4 x) {
    float inv = rsqrtf_fast(lengthsq(x));
    return x * inv;
}

static inline float4 cross(float4 a, float4 b) { return (a * b.yzxw - a.yzxw * b).yzxw; }

static inline float4 min(float4 a, float4 b) { return vminq_f32(a, b); }
static inline float4 max(float4 a, float4 b) { return vmaxq_f32(a, b); }