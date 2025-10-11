#include <arm_neon.h>
typedef float float4 __attribute__((ext_vector_type(4)));
typedef int int4 __attribute__((ext_vector_type(4)));

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

// the rest is cross platform
// clang ext_vector ftw


static inline float lengthsq(float4 x) { return dot(x, x); }

static inline float length(float4 x) { return sqrtf(dot(x, x)); }

static inline float4 normalize(float4 x) {
    float inv = rsqrtf_fast(lengthsq(x));
    return x * inv;
}

static inline float4 cross(float4 a, float4 b) { return (a * b.yzxw - a.yzxw * b).yzxw; }
