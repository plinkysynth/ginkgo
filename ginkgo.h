#pragma once
// this header tries to be a one-stop shop for the hot-recompiled audio function. 
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#define OVERSAMPLE 2
#define SAMPLE_RATE_OUTPUT 48000
#define SAMPLE_RATE (SAMPLE_RATE_OUTPUT * OVERSAMPLE)

#define PI 3.14159265358979323846f
#define TAU 6.28318530717958647692f
#define HALF_PI 1.57079632679489661923f

#define countof(array) (sizeof(array) / sizeof(array[0]))

static inline uint32_t pcg_mix(uint32_t word) {    return (word >> 22u) ^ word; }
static inline uint32_t pcg_next(uint32_t seed) { return seed * 747796405u + 2891336453u; } 

static inline int mini(int a, int b) { return a < b ? a : b; }
static inline int maxi(int a, int b) { return a > b ? a : b; }
static inline int clampi(int a, int min, int max) { return a < min ? min : a > max ? max : a; }

static inline uint32_t minu(uint32_t a, uint32_t b) { return a < b ? a : b; }
static inline uint32_t maxu(uint32_t a, uint32_t b) { return a > b ? a : b; }
static inline uint32_t clampu(uint32_t a, uint32_t min, uint32_t max) { return a < min ? min : a > max ? max : a; }

static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float maxf(float a, float b) { return a > b ? a : b; }
static inline float clampf(float a, float min, float max) { return a < min ? min : a > max ? max : a; }
static inline float squaref(float x) { return x * x; }
static inline float fracf(float x) { return x - floorf(x); }

static inline float lin2db(float x) { return 8.6858896381f * logf(maxf(1e-20f, x)); }
static inline float db2lin(float x) { return expf(x / 8.6858896381f); }
static inline float squared2db(float x) { return 4.342944819f * logf(maxf(1e-20f, x)); }
static inline float db2squared(float x) { return expf(x / 4.342944819f); }

static inline float update_lfo(float state[2], float sin_dphase) { // quadrature oscillator magic
    state[0] -= (state[1] * sin_dphase);
    return state[1] += (state[0] * sin_dphase);
}

typedef struct stereo {
    float l, r;
} stereo;

static inline float mid(stereo s) { return (s.l + s.r) * 0.5f; }
static inline float side(stereo s) { return (s.l - s.r) * 0.5f; }

static inline float saturate_soft(float x) { return atanf(x) * (2.f/PI); }
static inline float saturate_tanh(float x) { return tanhf(x); }
static inline float saturate_hard(float x) { return clampf(-1.f, 1.f, x); }

static inline stereo saturate_stereo_soft(stereo s) {
    return (stereo){.l = atanf(s.l) * (2.f/PI),.r = atanf(s.r) * (2.f/PI)};
}

static inline stereo saturate_stereo_tanh(stereo s) {
    return (stereo){.l = tanhf(s.l), .r = tanhf(s.r)};
}

static inline stereo saturate_stereo_hard(stereo s) {
    return (stereo){.l = clampf(-1.f, 1.f, s.l), .r = clampf(-1.f, 1.f, s.r)};
}

static inline float lpf_1pole(float state[1], float inp, float k) { return state[0] += (inp - state[0]) * k; }

static inline float biquad(float state[2], float x, float b0, float b1, float b2, float a1, float a2) {
    float y = b0 * x + state[0];
    state[0] = b1 * x - a1 * y + state[1];
    state[1] = b2 * x - a2 * y;
    return y;
}

static inline float polyblep(float phase, float dt) {
    if (phase < dt) {
        phase *= 1.f / dt;
        return phase + phase - phase * phase - 1;
    }
    if (phase > 1.f - dt) {
        phase = (phase - 1.f) * (1.f / dt);
        return phase * phase + phase + phase + 1;
    }
    return 0;
}

static inline float sino(float state[1], float dphase) { 
    float phase = state[0] = fracf(state[0] + dphase);
    return sinf(TAU * phase);
}

static inline float sawo(float state[1], float dphase) { 
    float phase = state[0] = fracf(state[0] + dphase);
    return 2.f * phase - 1.f - polyblep(phase, dphase);
}

// midi note 69 is A4 (440hz)
// what note is 48khz? 
// 150.2326448623 :)
float midi2dphase(float midi) { return exp2f((midi - 150.2326448623f) * (1.f / 12.f)); }

#define FREQ2LPF(freq) 1.f - exp2f(-freq * (TAU / SAMPLE_RATE))   // more accurate for lpf with high cutoff

#define EXPORT __attribute__((visibility("default"))) 

typedef void *(*dsp_fn_t)(void *G, stereo *audio, int frames, int reloaded);

// declare a dsp function like this:
// EXPORT void *dsp(void *G, stereo *audio, int frames, int reloaded)