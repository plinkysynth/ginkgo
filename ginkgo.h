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

static inline uint32_t pcg_mix(uint32_t word) { return (word >> 22u) ^ word; }
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

inline float fast_tanh(float x) {
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// ladder filter
// adapted from https://www.kvraudio.com/forum/viewtopic.php?f=33&t=349859
//// LICENSE TERMS: Copyright 2012 Teemu Voipio
//
// You can use this however you like for pretty much any purpose,
// as long as you don't claim you wrote it. There is no warranty.
//
// Distribution of substantial portions of this code in source form
// must include this copyright notice and list of conditions.
//

// tanh(x)/x approximation, flatline at very high inputs
// so might not be safe for very large feedback gains
// [limit is 1/15 so very large means ~15 or +23dB]
static inline float tanhXdX(float x) {
    float a = x * x;
    // IIRC I got this as Pade-approx for tanh(sqrt(x))/sqrt(x)
    return ((a + 105) * a + 945) / ((15 * a + 420) * a + 945);
}
// double f = tan(M_PI * cutoff);
// double r = (40.0/9.0) * resonance;

static inline float ladder(float inp, float s[5], float f, float r) {
    r *= 40.f / 9.f;
    // input with half delay, for non-linearities
    // we use s[4] as zi in the original code
    float ih = 0.5f * (inp + s[4]);
    s[4] = inp;

    // evaluate the non-linear gains
    float t0 = tanhXdX(ih - r * s[3]);
    float t1 = tanhXdX(s[0]);
    float t2 = tanhXdX(s[1]);
    float t3 = tanhXdX(s[2]);
    float t4 = tanhXdX(s[3]);

    // g# the denominators for solutions of individual stages
    float g0 = 1.f / (1.f + f * t1), g1 = 1.f / (1.f + f * t2);
    float g2 = 1.f / (1.f + f * t3), g3 = 1.f / (1.f + f * t4);

    // f# are just factored out of the feedback solution
    float f3 = f * t3 * g3, f2 = f * t2 * g2 * f3, f1 = f * t1 * g1 * f2, f0 = f * t0 * g0 * f1;

    // solve feedback
    float y3 = (g3 * s[3] + f3 * g2 * s[2] + f2 * g1 * s[1] + f1 * g0 * s[0] + f0 * inp) / (1.f + r * f0);

    // then solve the remaining outputs (with the non-linear gains here)
    float xx = t0 * (inp - r * y3);
    float y0 = t1 * g0 * (s[0] + f * xx);
    float y1 = t2 * g1 * (s[1] + f * y0);
    float y2 = t3 * g2 * (s[2] + f * y1);

    // update state
    s[0] += 2.f * f * (xx - y0);
    s[1] += 2.f * f * (y0 - y1);
    s[2] += 2.f * f * (y1 - y2);
    s[3] += 2.f * f * (y2 - t4 * y3);

    return y3;
}

static inline float saturate_soft(float x) { return atanf(x) * (2.f / PI); }
static inline float saturate_tanh(float x) { return tanhf(x); }
static inline float saturate_hard(float x) { return clampf(x, -1.f, 1.f); }

static inline stereo ensure_finite_stereo(stereo s) {
    return (stereo){.l = isfinite(s.l) ? s.l : 0.f, .r = isfinite(s.r) ? s.r : 0.f};
}

static inline stereo saturate_stereo_soft(stereo s) { return (stereo){.l = atanf(s.l) * (2.f / PI), .r = atanf(s.r) * (2.f / PI)}; }

static inline stereo saturate_stereo_tanh(stereo s) { return (stereo){.l = tanhf(s.l), .r = tanhf(s.r)}; }

static inline stereo saturate_stereo_hard(stereo s) { return (stereo){.l = clampf(s.l, -1.f, 1.f), .r = clampf(s.r, -1.f, 1.f)}; }

static inline float lpf_1pole(float state[1], float inp, float k) { return state[0] += (inp - state[0]) * k; }

static inline float biquad(float state[2], float x, float b0, float b1, float b2, float a1, float a2) {
    float y = b0 * x + state[0];
    state[0] = b1 * x - a1 * y + state[1];
    state[1] = b2 * x - a2 * y;
    return y;
}

static inline float sample_linear(float pos, const float *smpl) {
    int i = (int)floorf(pos);
    float t = pos - (float)i;
    float s0 = smpl[i + 0];
    float s1 = smpl[i + 1];
    return s0 + t * (s1 - s0);
}

static inline float sample_catmull_rom(float pos, const float *smpl) {
    int i = (int)floorf(pos);
    float t = pos - (float)i;
    float s0 = smpl[i + 0];
    float s1 = smpl[i + 1];
    float s2 = smpl[i + 2];
    float s3 = smpl[i + 3];
    // Catmull–Rom coefficients (a = -0.5)
    float c0 = s1;
    float c1 = 0.5f * (s2 - s0);
    float c2 = s0 - 2.5f * s1 + 2.0f * s2 - 0.5f * s3;
    float c3 = -0.5f * s0 + 1.5f * s1 - 1.5f * s2 + 0.5f * s3;
    return ((c3 * t + c2) * t + c1) * t + c0;
}
static const int blep_os = 16;
static const float minblep_table[129] = { // minBLEP correction for a unit step input; 16x oversampling
    -1.000000000f, -0.998811289f, -0.996531062f, -0.992795688f, -0.987211296f, -0.979364361f, -0.968834290f, -0.955206639f,
    -0.938089255f, -0.917128234f, -0.892024074f, -0.862547495f, -0.828554201f, -0.789997837f, -0.746941039f, -0.699562613f,
    -0.648161606f, -0.593157579f, -0.535086789f, -0.474594145f, -0.412420965f, -0.349388355f, -0.286377554f, -0.224307123f,
    -0.164107876f, -0.106696390f, -0.052947974f, -0.003670049f, 0.040422752f,  0.078731454f,  0.110792191f,  0.136291501f,
    0.155076819f,  0.167161723f,  0.172725675f,  0.172108498f,  0.165799199f,  0.154419794f,  0.138704628f,  0.119475970f,
    0.097616810f,  0.074041890f,  0.049667838f,  0.025383909f,  0.002024209f,  -0.019657569f, -0.039010001f, -0.055501624f,
    -0.068733874f, -0.078448626f, -0.084530510f, -0.087003901f, -0.086024863f, -0.081868483f, -0.074912196f, -0.065615874f,
    -0.054499809f, -0.042121280f, -0.029050821f, -0.015849132f, -0.003045552f, 0.008881074f,  0.019518462f,  0.028533550f,
    0.035681170f,  0.040808606f,  0.043856030f,  0.044853012f,  0.043911513f,  0.041215879f,  0.037010263f,  0.031584304f,
    0.025257772f,  0.018364895f,  0.011239082f,  0.004198655f,  -0.002465881f, -0.008502385f, -0.013705252f, -0.017920976f,
    -0.021051050f, -0.023052209f, -0.023934165f, -0.023755036f, -0.022614989f, -0.020648437f, -0.018015250f, -0.014891443f,
    -0.011459831f, -0.007901093f, -0.004385722f, -0.001067013f, 0.001924568f,  0.004485483f,  0.006541591f,  0.008049004f,
    0.008993458f,  0.009388380f,  0.009271761f,  0.008702077f,  0.007753563f,  0.006511133f,  0.005065253f,  0.003506995f,
    0.001923470f,  0.000393902f,  -0.001013496f, -0.002243806f, -0.003256807f, -0.004027535f, -0.004546005f, -0.004816159f,
    -0.004854204f, -0.004686516f, -0.004347232f, -0.003875660f, -0.003313636f, -0.002702999f, -0.002083351f, -0.001490174f,
    -0.000953324f, -0.000495978f, -0.000134058f, 0.000123860f,  0.000276211f,  0.000327791f,  0.000288819f,  0.000173761f,
    0.000000000f};

static inline float sino(float phase, float dphase) { return sinf(TAU * phase); }

static inline float minblep(float phase, float dphase) {
    float bleppos = (phase / dphase * (blep_os / OVERSAMPLE));
    if (bleppos >= 128 || bleppos < 0.f)
        return 0.f;
    int i = (int)bleppos;
    return minblep_table[i] + (minblep_table[i + 1] - minblep_table[i]) * (bleppos - i);
}

static inline float sawo(float phase, float dphase) {
    phase = fracf(phase);
    float saw = phase * 2. - 1.f;
    saw -= 2.f * minblep(phase, dphase);
    return saw;
}
static inline float sawo_aliased(float phase, float dphase) {
    phase = fracf(phase);
    return 2.f * phase - 1.f;
}

// midi note 69 is A4 (440hz)
// what note is 48khz?
// 150.2326448623 :)
float midi2dphase(float midi) { return exp2f((midi - 150.2326448623f) * (1.f / 12.f)); }

#define FREQ2LPF(freq) 1.f - exp2f(-freq *(TAU / SAMPLE_RATE)) // more accurate for lpf with high cutoff

typedef void *(*dsp_fn_t)(void *G, stereo *audio, int frames, int reloaded, uint32_t sampleidx);

#define STATE_BASIC_FIELDS                                                                                                         \
    int _ver;                                                                                                                      \
    int _size;                                                                                                                     \
    uint8_t midi_cc_raw[128];                                                                                                      \
    float cc[128];

#define STATE_VERSION(version, ...)                                                                                                \
    typedef struct state {                                                                                                         \
        STATE_BASIC_FIELDS                                                                                                         \
        __VA_ARGS__                                                                                                                \
    } state;                                                                                                                       \
    stereo do_sample(state *G, stereo inp, uint32_t sampleidx);                                                                    \
    __attribute__((visibility("default"))) void *dsp(void *_G, stereo *audio, int frames, int reloaded, uint32_t sampleidx) {      \
        state *G = (state *)_G;                                                                                                    \
        if (!G || G->_ver != version || G->_size != sizeof(state)) {                                                               \
            free(G);                                                                                                               \
            G = calloc(1, sizeof(state));                                                                                          \
            G->_ver = version;                                                                                                     \
            G->_size = sizeof(state);                                                                                              \
        }                                                                                                                          \
        for (int i = 0; i < frames; i++) {                                                                                         \
            audio[i] = do_sample((state *)G, audio[i], sampleidx++);                                                               \
            for (int i = 0; i < 128; i++) {                                                                                        \
                float target = G->midi_cc_raw[i] / 127.f;                                                                          \
                G->cc[i] += (target - G->cc[i]) * 0.001f;                                                                          \
            }                                                                                                                      \
        }                                                                                                                          \
        return G;                                                                                                                  \
    }
