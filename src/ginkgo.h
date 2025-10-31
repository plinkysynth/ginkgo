#pragma once
// this header tries to be a one-stop shop for the hot-recompiled audio function.
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "3rdparty/stb_ds.h"
#ifdef __cplusplus
extern "C" {
#endif
#define STRINGIFY(x) #x

#define OVERSAMPLE 2
#define SAMPLE_RATE_OUTPUT 48000
#define SAMPLE_RATE (SAMPLE_RATE_OUTPUT * OVERSAMPLE)
#define RARE(x) __builtin_expect(x, 0)
#define COMMON(x) __builtin_expect(x, 1)
#define noinline __attribute__((noinline))
#define alwaysinline __attribute__((always_inline))

#define ROOT2 1.41421356237309504880f
#define SQRT2 1.41421356237309504880f
#define ROOT3 1.73205080756887729352f
#define SQRT3 1.73205080756887729352f
#define ROOT5 2.23606797749978969640f
#define SQRT5 2.23606797749978969640f
#define SIN45 0.70710678118654752440f
#define QBUTTER 0.70710678118654752440f
#define QBUTTER_24A 0.541196f
#define QBUTTER_24B 1.306563f
#define SVF_24_R_MUL1 (1.f / (QBUTTER_24A))
#define SVF_24_R_MUL2 (1.f / (QBUTTER_24B))
// size of virtual textmode screen:
#define TMW 512
#define TMH 256

typedef float F;
typedef int I;
typedef uint32_t U;

#include "notes.h"

typedef struct wave_t {
    const char *key; // interned url
    float *frames;   // malloc'd
    uint64_t num_frames;
    float sample_rate;
    uint32_t channels;
    int midi_note;
    int download_in_progress;
} wave_t;

typedef struct int_pair_t {
    int k, v;
} int_pair_t;

typedef struct float_pair_t {
    float k, v;
} float_pair_t;

static inline int compare_int_pair(int_pair_t a, int_pair_t b) { return (a.k != b.k) ? a.k - b.k : a.v - b.v; }

static inline int lower_bound_int_pair(int_pair_t *arr, int n, int_pair_t key) {
    int lo = 0, hi = n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (compare_int_pair(arr[mid], key) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

typedef struct Sound {
    const char *name;       // interned name.
    int *wave_indices;          // stb_ds array of indices into G->waves
    int_pair_t *midi_notes; // sorted stb_ds array of pairs <midinote, waveindex>
} Sound;


static inline int next_pow2(int n) {
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}


typedef struct sound_pair_t {
    char *key;
    Sound *value;
} sound_pair_t;


typedef struct wave_request_t {
    wave_t *key;
} wave_request_t;

struct reverb_state_t {
    float filter_state[16];
    float reverbbuf[65536];
    int reverb_pos;
    int shimmerpos1 = 2000;
    int shimmerpos2 = 1000;
    int shimmerfade = 0;
    int dshimmerfade = 32768 / 4096;
    float aplfo[2] = {1.f, 0.f};
    float aplfo2[2] = {1.f, 0.f};
    float fb1 = 0;
    float lpf = 0.f, dc = 0.f;
    float lpf2 = 0.f;
};

typedef struct pattern_t pattern_t;

typedef struct basic_state_t {
    int _ver;
    int _size;
    uint8_t reloaded;
    uint8_t playing;
    float bpm;
    float cpu_usage;
    float cpu_usage_smooth;
    double t; // the current musical time! as a double
    double dt; // change of t per sample
    int64_t t_q32; // the current musical time! as a 32.32 fixed point number
    uint8_t midi_cc[128];
    uint32_t midi_cc_gen[128];
    int cursor_x, cursor_y;
    float mx, my;
    float mscrollx, mscrolly;
    int mb;
    int old_mb;
    double iTime;
    uint32_t sampleidx;
    atomic_flag load_request_cs;
    sound_pair_t *sounds;
    wave_t *waves;
    pattern_t *patterns_map; // stbds_sh
    wave_request_t *load_requests;
    reverb_state_t R;
    double preview_wave_t;
    int preview_wave_idx_plus_one;
    float preview_wave_fade;
} basic_state_t;

extern basic_state_t *_BG;
#define G _BG

#define PI 3.14159265358979323846f
#define TAU 6.28318530717958647692f
#define HALF_PI 1.57079632679489661923f
#define QUARTER_PI 0.78539816339744830962f

#define countof(array) (sizeof(array) / sizeof(array[0]))

static inline uint32_t pcg_mix(uint32_t word) { return (word >> 22u) ^ word; }
static inline uint32_t pcg_next(uint32_t seed) { return seed * 747796405u + 2891336453u; }

static inline int min(int a, int b) { return a < b ? a : b; }
static inline int max(int a, int b) { return a > b ? a : b; }
static inline int clamp(int a, int min, int max) { return a < min ? min : a > max ? max : a; }

static inline uint32_t min(uint32_t a, uint32_t b) { return a < b ? a : b; }
static inline uint32_t max(uint32_t a, uint32_t b) { return a > b ? a : b; }
static inline uint32_t clamp(uint32_t a, uint32_t min, uint32_t max) { return a < min ? min : a > max ? max : a; }

static inline double min(double a, double b) { return a < b ? a : b; }
static inline double max(double a, double b) { return a > b ? a : b; }

static inline float min(float a, float b) { return a < b ? a : b; }
static inline float max(float a, float b) { return a > b ? a : b; }
static inline float clamp(float a, float min, float max) { return a < min ? min : a > max ? max : a; }
static inline float square(float x) { return x * x; }
static inline float frac(float x) { return x - floorf(x); }
static inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

static inline float pow2(float x) { return x * x; }
static inline float pow3(float x) { return x * x * x; }
static inline float pow4(float x) {
    x = x * x;
    return x * x;
}
static inline float vol(float x) { return pow3(x); } // a nice volume curve thats kinda db-like but goes to exactly 0 and 1.

static inline float saturate(float x) { return clamp(x, 0.f, 1.f); }

static inline float lin2db(float x) { return 8.6858896381f * logf(max(1e-20f, x)); }
static inline float db2lin(float x) { return expf(x / 8.6858896381f); }
static inline float squared2db(float x) { return 4.342944819f * logf(max(1e-20f, x)); }
static inline float db2squared(float x) { return expf(x / 4.342944819f); }

static inline float update_lfo(float state[2], float sin_dphase) { // quadrature oscillator magic
    state[0] -= (state[1] * sin_dphase);
    return state[1] += (state[0] * sin_dphase);
}

static inline float lfo(float state[2], float dphase) {
    update_lfo(state, dphase);
    return state[1];
}

static inline const float *lfo2(float state[2],float dphase) { // quadrature output
    update_lfo(state, dphase);
    return state;
}

static inline float slew(float state[1],float x, float upfac = 1e-5, float downfac = 1e-5) {
    float old = state[0];
    state[0] = old + (x - old) * ((x > old) ? upfac : downfac);
    return state[0];
}

// TODO: for some patterns, tidal/strudel prefers a rotation that puts the first stumble earlier in the cycle.
// but this is simple so we'll go with simple.
static inline int euclid_rhythm(int stepidx, int numset, int numsteps, int rot) {
    if (numsteps < 1 || numset < 1)
        return 0;
    stepidx = ((stepidx - rot) % numsteps) + numsteps;
    return ((stepidx * numset) % numsteps) < numset;
}

typedef struct stereo {
    float l, r;
} stereo;

#define STEREO(l, r)                                                                                                               \
    stereo { l, r }

static inline stereo operator+(stereo a, stereo b) { return STEREO(a.l + b.l, a.r + b.r); }
static inline stereo operator+(stereo a, float b) { return STEREO(a.l + b, a.r + b); }
static inline stereo operator-(stereo a, stereo b) { return STEREO(a.l - b.l, a.r - b.r); }
static inline stereo operator*(stereo a, float b) { return STEREO(a.l * b, a.r * b); }
static inline stereo operator*(stereo a, stereo b) { return STEREO(a.l * b.l, a.r * b.r); }
static inline stereo operator/(stereo a, float b) { return STEREO(a.l / b, a.r / b); }
static inline void operator+=(stereo &a, stereo b) { a.l += b.l; a.r += b.r; }
static inline void operator+=(stereo &a, float b) { a.l += b; a.r += b; }
static inline void operator-=(stereo &a, stereo b) { a.l -= b.l; a.r -= b.r; }
static inline void operator-=(stereo &a, float b) { a.l -= b; a.r -= b; }
static inline void operator*=(stereo &a, stereo b) { a.l *= b.l; a.r *= b.r; }
static inline void operator*=(stereo &a, float b) { a.l *= b; a.r *= b; }
static inline void operator/=(stereo &a, stereo b) { a.l /= b.l; a.r /= b.r; }
static inline void operator/=(stereo &a, float b) { a.l /= b; a.r /= b; }

static inline stereo mono2stereo(float mono) { return STEREO(mono, mono); }
static inline float stmid(stereo s) { return (s.l + s.r) * 0.5f; }
static inline float stside(stereo s) { return (s.l - s.r) * 0.5f; }
static inline stereo midside2st(float mid, float side) { return STEREO(mid + side, mid - side); }

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

static inline float ssclip(float x) { return atanf(x) * (2.f / PI); }
static inline float sclip(float x) { return tanhf(x); }
static inline float clip(float x) { return clamp(x, -1.f, 1.f); }

static inline float ensure_finite(float s) { return isfinite(s) ? s : 0.f; }

static inline stereo ensure_finite(stereo s) { return (stereo){.l = ensure_finite(s.l), .r = ensure_finite(s.r)}; }

static inline stereo ssclip(stereo s) { return (stereo){.l = atanf(s.l) * (2.f / PI), .r = atanf(s.r) * (2.f / PI)}; }
static inline stereo sclip(stereo s) { return (stereo){.l = tanhf(s.l), .r = tanhf(s.r)}; }
static inline stereo clip(stereo s) { return (stereo){.l = clamp(s.l, -1.f, 1.f), .r = clamp(s.r, -1.f, 1.f)}; }

typedef struct svf_output_t {
    float lp, bp, hp;
} svf_output_t;

// g = tanf(M_PI * fc / fs), R = 1/Q
static inline svf_output_t svf_process_2pole(float *f, float v0, float g, float R) {
    // https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf
    const float a1 = 1.f / (1.f + g * (g + R)); // precompute?
    const float a2 = g * a1;
    const float a3 = g * a2;
    const float v3 = v0 - f[1];
    const float v1 = a1 * f[0] + a2 * v3;
    const float v2 = f[1] + a2 * f[0] + a3 * v3;
    f[0] = 2.f * v1 - f[0];
    f[1] = 2.f * v2 - f[1];
    return (svf_output_t){.lp = v2, .bp = v1, .hp = v0 - R * v1 - v2};
}

static inline svf_output_t svf_process_1pole(float *f, float x, float g) {
    const float a = g / (1.f + g);
    float v = x - f[0];
    float lp = a * v + f[0];
    *f = lp + a * v; // TPT integrator update
    return (svf_output_t){.lp = lp, .hp = x - lp, .bp = a * v};
}

// approximation to tanh for 0-0.25 nyquist.
static inline float svf_g(float fc) { // fc is like a dphase, ie P_C4 etc constants work
    // return tanf(PI * fc / SAMPLE_RATE);
    //   https://www.desmos.com/calculator/qoy3dgydch
    static const float A = 3.272433237e-05f, B = 1.181248215e-14f;
    return fc * A; // * (A + B * fc * fc); // tan fit
}

// static inline float svf_g(float fc) { return fc/SAMPLE_RATE; }

static inline float svf_R(float q) { return 1.f / q; }

static inline float lpf(float state[2], float x, float fc, float q) {
    float y = svf_process_2pole(state, x, svf_g(fc), svf_R(q)).lp;
    return y;
}

static inline float hpf(float state[2], float x, float fc, float q) {
    return svf_process_2pole(state, x, svf_g(fc), svf_R(q)).hp;
}

static inline float bpf(float state[2], float x, float fc, float q) {
    return svf_process_2pole(state, x, svf_g(fc), svf_R(q)).bp;
}

// 4 pole lowpass
static inline float lpf4(float state[4], float x, float fc, float q) {
    float g = svf_g(fc), r = svf_R(q);
    x = svf_process_2pole(state, x, g, r * SVF_24_R_MUL1).lp;
    return svf_process_2pole(state + 2, x, g, r * SVF_24_R_MUL2).lp;
}

// 4 pole hipass
static inline float hpf4(float state[4], float x, float fc, float q) {
    float g = svf_g(fc), r = svf_R(q);
    x = svf_process_2pole(state, x, g, r * SVF_24_R_MUL1).hp;
    return svf_process_2pole(state + 2, x, g, r * SVF_24_R_MUL2).hp;
}

static inline float peakf(float state[2], float x, float gain, float fc, float q) {
    float R = svf_R(q);
    svf_output_t o = svf_process_2pole(state, x, svf_g(fc), R);
    return x + (gain - 1.f) * o.bp * R;
}

static inline float notchf(float state[2], float x, float fc, float q) {
    svf_output_t o = svf_process_2pole(state, x, svf_g(fc), svf_R(q));
    return o.lp + o.hp;
}

void init_basic_state(void);
stereo reverb(stereo inp);
wave_t *request_wave_load(wave_t *wave);

// for now, the set of sounds and waves is fixed at boot time to avoid having to think about syncing threads
// however, the audio data itself *is* lazy loaded, so the boot time isnt too bad.
static inline Sound *get_sound(const char *name) { return shget(G->sounds, name); }
static inline int get_sound_index(const char *name) { return shgeti(G->sounds, name); }
static inline Sound *get_sound_by_index(int i) {
    if (i < 0 || i >= stbds_hmlen(G->sounds))
        return NULL;
    return G->sounds[i].value;
}

static inline int num_sounds(void) { return stbds_hmlen(G->sounds); }

static inline wave_t *get_wave(Sound *sound, int index) {
    if (!sound)
        return NULL;
    if (sound->name[0] == '-' || sound->name[0] == '~')
        return NULL;
    int n = stbds_arrlen(sound->wave_indices);
    if (!n)
        return NULL;
    index %= n;
    wave_t *w = &G->waves[sound->wave_indices[index]];
    return w->frames ? w : request_wave_load(w);
}

static inline wave_t *get_wave_by_name(const char *name) {
    int index = 0;
    const char *colon = strchr(name, ':');
    if (!colon)
        return get_wave(get_sound(name), index);
    index = atoi(colon + 1);
    char name2[colon - name + 1];
    memcpy(name2, name, colon - name);
    name2[colon - name] = '\0';
    return get_wave(get_sound(name2), index);
}

static inline stereo sample_wave(wave_t *w, float pos, float loops, float loope) {
    if (!w || !w->frames) return {0.f, 0.f};
    //pos *= w->sample_rate;
    if (loops < loope && pos > loops) {
        loops *= w->num_frames;
        loope *= w->num_frames;
        pos = fmodf(pos - loops, loope - loops) + loops;
    } else if (pos >= w->num_frames) {
        return {0.f, 0.f};
    }
    int i = (int)floorf(pos);
    float t = pos - (float)i;
    i %= w->num_frames;
    bool wrap = (i==w->num_frames-1);
    if (w->channels>1) {
        i*=w->channels;
        stereo s0 = stereo{w->frames[i + 0], w->frames[i + 1]};
        i=wrap ? i : i+w->channels;
        stereo s1 = stereo{w->frames[i + 0], w->frames[i + 1]};
        return s0 + (s1 - s0) * t;
    } else {
        float s0 = w->frames[i + 0];
        i=wrap ? i : i+1;
        float s1 = w->frames[i + 0];
        s0 += (s1-s0)*t;
        return stereo{s0,s0};
    }
}

static inline float sample_linear(float pos, const float *smpl, int num_samples) {
    int i = (int)floorf(pos);
    float t = pos - (float)i;
    i %= num_samples;
    float s0 = smpl[i + 0];
    if (++i >=num_samples ) i=0;
    float s1 = smpl[i];
    return s0 + t * (s1 - s0);
}

static inline float catmull_rom(float s0, float s1, float s2, float s3, float t) {
    // Catmull–Rom coefficients (a = -0.5)
    float c0 = s1;
    float c1 = 0.5f * (s2 - s0);
    float c2 = s0 - 2.5f * s1 + 2.0f * s2 - 0.5f * s3;
    float c3 = -0.5f * s0 + 1.5f * s1 - 1.5f * s2 + 0.5f * s3;
    return ((c3 * t + c2) * t + c1) * t + c0;
}

static inline float sample_catmull_rom(float pos, const float *smpl) {
    int i = (int)floorf(pos);
    return catmull_rom(smpl[i + 0], smpl[i + 1], smpl[i + 2], smpl[i + 3], pos - (float)i);
}

static uint32_t rnd_seed;

static inline float rnd01(void) {
    rnd_seed = pcg_next(rnd_seed);
    return (float)(rnd_seed & 0xffffff) * (1.f / 16777216.f);
}
static inline uint32_t rndint(uint32_t m) {
    rnd_seed = pcg_next(rnd_seed);
    return rnd_seed % m;
}
static inline float rndt(void) { return rnd01() + rnd01() - 1.f; }
static inline float rndn(void) {
    float x = rnd01() + rnd01() + rnd01() + rnd01() + rnd01() + rnd01() - 3.f;
    return x * 1.4f;
}

static const int blep_os = 8;
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

static inline float sino(float state[1], float dphase) {
    state[0] = frac(state[0] + dphase);
    return sinf(TAU * state[0]);
}

static inline float minblep(float phase, float dphase) {
    if (dphase <= 0.f)
        return 0.f;
    float bleppos = (phase / dphase * (blep_os / OVERSAMPLE));
    if (bleppos >= 128 || bleppos < 0.f)
        return 0.f;
    int i = (int)bleppos;
    return minblep_table[i] + (minblep_table[i + 1] - minblep_table[i]) * (bleppos - i);
}

static inline float rndsmooth(float state[5], float dphase) {
    float ph = state[0] + dphase;
    if (ph >= 1.f) {
        state[1] = state[2];
        state[2] = state[3];
        state[3] = state[4];
        state[4] = rndt();
        ph -= 1.f;
    }
    state[0] = ph;
    return catmull_rom(state[1], state[2], state[3], state[4], ph);
}

static inline float sawo(float state[1], float dphase) {
    float ph = frac(state[0]);
    float saw = ph * 2.f - 1.f;
    saw -= 2.f * minblep(ph, dphase);
    state[0] = ph + dphase;
    return saw;
}

static inline float pwmo(float state[1], float dphase, float duty) {
    float ph = frac(state[0]);
    float saw = ph * 2.f - 1.f;
    saw -= 2.f * minblep(ph, dphase);
    state[0] = ph + dphase;
    ph = frac(ph + duty);
    saw -= ph * 2.f - 1.f;
    saw += 2.f * minblep(ph, dphase);
    return saw;
}

static inline float squareo(float state[1], float dphase) { return pwmo(state, dphase, 0.5f); }

static inline float trio(float state[2], float dphase) {
    float ph = frac(state[0] + dphase);
    float tri = ph * 4.f - 1.f;
    if (tri > 1.f)
        tri = 2.f - tri;
    return state[1] += (tri - state[1]) * 0.25f;
}

static inline float sawo_aliased(float state[1], float dphase) {
    float ph = frac(state[0] + dphase);
    return 2.f * ph - 1.f;
}

static inline float lpf1(float state[1], float x, float f) {
    return state[0] += (x - state[0]) * f;
}


// static inline float adsr(float gate, float attack, float decay, float sustain, float release) {
//     float *state = ba_get(&G->audio_bump, 2);
//     float decaying = state[0];
//     float envlevel = state[1];
//     bool down = gate>0.f;
//     float target = gate ? (decaying) ? sustain : 1.2f : 0.f;
//     float dlevel = target - envlevel;
//     float k = lpf_k((dlevel > 0.f) ? attack : (decaying && down) ? decay : release));
//     envlevel += (target - envlevel) * k;
//     if (envlevel>=1.f) state[0]=1; // switch to decay phase
//     return state[1] = envlevel;

//     if (gate<=0.f) {
//         // release phase
//     } else {
//         if (!decaying) {
//             // attack phase
//             if (envlevel>=1.f) state[0]=1; // switch to decay phase
//         } else {
//             // decay/sustain phase
//         }
//     }
//     }

// }

/*
static inline void lorenz_euler(float s[3], float dt, float sigma, float beta, float rho) {
    if (dt > 1e-3f)
        dt = 1e-3f;
    float x = s[0], y = s[1], z = s[2];
    float dx = sigma * (y - x);
    float dy = x * (rho - z) - y;
    float dz = x * y - beta * z;
    s[0] = x + dt * dx;
    s[1] = y + dt * dy;
    s[2] = z + dt * dz;
}

static inline float *lorenz(float dt) {
    float *s = ba_get(&G->audio_bump, 3);
    if (s[0] == 0.f && s[1] == 0.f && s[2] == 0.f) {
        s[0] = -1.f;
        s[1] = 1.f;
        s[2] = 10.f;
    }
    lorenz_euler(s, dt, 10.f, 2.66666666666666666666f, 28.f);
    return s;
}*/

// midi note 69 is A4 (440hz)
// what note is 48khz?
// 150.2326448623 :)
static inline float midi2dphase(float midi) { return exp2f((midi - 150.2326448623f) * (1.f / 12.f)); }

#define P_(x) midi2dphase(x)

// #define FREQ2LPF(freq) 1.f - exp2f(-freq *(TAU / SAMPLE_RATE)) // more accurate for lpf with high cutoff

typedef basic_state_t *(*dsp_fn_t)(basic_state_t *G, stereo *audio, int frames, int reloaded);

static inline float do_slider(float state[2],int slider_idx, int myline, float def) {
    slider_idx &= 15;
    myline &= 255;
    if (!state[1]) state[0] = def;
    state[1] = myline; // we count lines from 0
    return state[0];
}

#define S_(state, slider_idx, def) do_slider(state, slider_idx, __LINE__, def)
#define S0(state, def) S_(state, 0, def)
#define S1(state, def) S_(state, 1, def)
#define S2(state, def) S_(state, 2, def)
#define S3(state, def) S_(state, 3, def)
#define S4(state, def) S_(state, 4, def)
#define S5(state, def) S_(state, 5, def)
#define S6(state, def) S_(state, 6, def)
#define S7(state, def) S_(state, 7, def)
#define S8(state, def) S_(state, 8, def)
#define S9(state, def) S_(state, 9, def)
#define S10(state, def) S_(state, 10, def)
#define S11(state, def) S_(state, 11, def)
#define S12(state, def) S_(state, 12, def)
#define S13(state, def) S_(state, 13, def)
#define S14(state, def) S_(state, 14, def)
#define S15(state, def) S_(state, 15, def)
#define SA S_(state, 10, def)
#define SB S_(state, 11, def)
#define SC S_(state, 12, def)
#define SD S_(state, 13, def)
#define SE S_(state, 14, def)
#define SF S_(state, 15, def)
#define LOG(...)                                                                                                                   \
    {                                                                                                                              \
        static int count = 0;                                                                                                      \
        count++;                                                                                                                   \
        if (RARE(count > 100000)) {                                                                                                \
            count = 0;                                                                                                             \
            fprintf(stderr, __VA_ARGS__);                                                                                          \
        }                                                                                                                          \
    }

void *dsp_preamble(basic_state_t *_G, stereo *audio, int reloaded, size_t state_size, int version, void (*init_state)(void));

static inline float rompler(const char *fname) {
	wave_t *wave=get_wave_by_name(fname);
    if (!wave || !wave->frames) return 0.f;
    int64_t smpl = (G->t_q32 & 0xffffffffull);
    smpl *= wave->num_frames;
    smpl >>= 32;
	return wave->frames[smpl*wave->channels];    
}

#ifdef LIVECODE
typedef struct state state;
stereo do_sample(stereo inp);
void init_state(void);
__attribute__((weak)) void init_state(void) {}

size_t get_state_size(void);
int get_state_version(void);
stereo probe;
__attribute__((visibility("default"))) void *dsp(basic_state_t *_G, stereo *audio, int frames, int reloaded) {
    G = (basic_state_t *)dsp_preamble(_G, audio, reloaded, get_state_size(), get_state_version(), init_state);
    int dt_q32 = G->playing ? G->bpm * (4294967296.0 / (SAMPLE_RATE * 240.0)) : 0; // on the order of 22000
    G->dt = dt_q32 *(1./4294967296.);
    for (int i = 0; i < frames; i++) {
        probe = {};
        audio[i] = do_sample(audio[i]);
        audio[i + frames] = probe;
        G->sampleidx++;
        G->t_q32 += dt_q32;
        G->t=G->t_q32*(1./4294967296.);
        G->reloaded = 0;
    }
    return G;
}

#endif

stereo test_patterns(const char *pattern_name);

#define STATE_VERSION(version, ...)                                                                                                \
    typedef struct state : public basic_state_t{__VA_ARGS__} state;                                                                \
    size_t get_state_size(void) { return sizeof(state); }                                                                          \
    int get_state_version(void) { return version; }

#ifdef LIVECODE
#undef G
#define G ((state *)_BG)
#endif
#ifdef __cplusplus
}
#endif
