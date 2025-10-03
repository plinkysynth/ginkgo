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

// size of virtual textmode screen:
#define TMW 512
#define TMH 256

typedef float F;
typedef int I;
typedef uint32_t U;

#include "notes.h"

typedef struct wave_t {
    const char *url; // interned url
    float *frames;   // malloc'd
    uint64_t num_frames;
    float sample_rate;
    uint32_t channels;
    int midi_note;
} wave_t;

typedef struct int_pair_t {
    int k, v;
} int_pair_t;

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
    wave_t *waves;          // stb_ds array
    int_pair_t *midi_notes; // sorted stb_ds array of pairs <midinote, waveindex>
} Sound;

typedef struct bump_array_t {
    int i, n; // n is the high watermark, the data array points at data4 if possible, or allocates in blocks of power of 2
    float *data;
    float data4[4];
} bump_array_t;

static inline int next_pow2(int n) {
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

static noinline void ba_grow(bump_array_t *sa, int newn) {
    int oldallocsize = next_pow2(sa->n);
    int newallocsize = next_pow2(newn);
    if (oldallocsize == newallocsize)
        return;
    float *newdata = (newn <= 4) ? sa->data4 : (float *)calloc(newallocsize, sizeof(float));
    memcpy(newdata, sa->data, sa->n * sizeof(float));
    // if (sa->data != sa->data4)
    //     free(sa->data); // let it leak :)
    sa->data = newdata;
    sa->n = newn;
}

static inline float *ba_get(bump_array_t *sa, int count) {
    int i = sa->i;
    if (RARE(i + count > sa->n))
        ba_grow(sa, i + count);
    sa->i += count;
    return &sa->data[i];
}
static inline float *ba_get_default(bump_array_t *sa, int count, float def) {
    int i = sa->i;
    if (RARE(i + count > sa->n)) {
        ba_grow(sa, i + count);
        if (count>0) sa->data[i] = def;
    }
    sa->i += count;
    return &sa->data[i];
}


typedef struct sound_pair_t {
    char *key;
    Sound *value;
} sound_pair_t;

typedef struct sound_request_key_t {
    Sound *sound;
    int index;
} sound_request_key_t;

typedef struct sound_request_t {
    sound_request_key_t key;
    int value;
} sound_request_t;

#define STATE_BASIC_FIELDS                                                                                                         \
    int _ver;                                                                                                                      \
    int _size;                                                                                                                     \
    int reloaded;                                                                                                                  \
    uint8_t midi_cc[128];                                                                                                          \
    uint32_t midi_cc_gen[128];                                                                                                     \
    int cursor_x, cursor_y;                                                                                                        \
    float mx, my;                                                                                                                  \
    int mb;                                                                                                                        \
    int old_mb;                                                                                                                    \
    double iTime;                                                                                                                  \
    uint32_t sampleidx;                                                                                                            \
    atomic_flag load_request_cs;                                                                                                   \
    sound_pair_t *sounds;                                                                                                          \
    sound_request_t *load_requests;                                                                                                \
    bump_array_t sliders[16];                                                                                                      \
    int sliders_hwm[16];                                                                                                           \
    bump_array_t audio_bump;

typedef struct basic_state_t {
    STATE_BASIC_FIELDS
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

static inline float pow2(float x) { return x * x; }
static inline float pow3(float x) { return x * x * x; }
static inline float pow4(float x) {
    x = x * x;
    return x * x;
}
static inline float vol(float x) { return pow3(x); } // a nice volume curve thats kinda db-like but goes to exactly 0 and 1.

static inline float saturate(float x) { return clampf(x, 0.f, 1.f); }

static inline float lin2db(float x) { return 8.6858896381f * logf(maxf(1e-20f, x)); }
static inline float db2lin(float x) { return expf(x / 8.6858896381f); }
static inline float squared2db(float x) { return 4.342944819f * logf(maxf(1e-20f, x)); }
static inline float db2squared(float x) { return expf(x / 4.342944819f); }

static inline float update_lfo(float state[2], float sin_dphase) { // quadrature oscillator magic
    state[0] -= (state[1] * sin_dphase);
    return state[1] += (state[0] * sin_dphase);
}

static inline float lfo(float dphase) {
    float *state = ba_get_default(&G->audio_bump, 2, 1.f);
    update_lfo(state, dphase);
    return state[1];
}

static inline const float *lfo2(float dphase) { // quadrature output
    float *state = ba_get_default(&G->audio_bump, 2, 1.f);
    update_lfo(state, dphase);
    return state;
}

static inline float slew(float x, float upfac, float downfac) {
    float *state = ba_get(&G->audio_bump, 1);
    float old = state[0];
    state[0] = old + (x - old) * ((x > old) ? upfac : downfac);
    return state[0];
}

// TODO: for some patterns, tidal/strudel prefers a rotation that puts the first stumble earlier in the cycle.
// but this is simple so we'll go with simple.
static inline int euclid_rhythm(int stepidx, int numset, int numsteps, int rot) {
    if (numsteps < 1 || numset < 1)
        return 0;
    stepidx = ((stepidx + rot) % numsteps) + numsteps;
    return ((stepidx * numset) % numsteps) < numset;
}

typedef struct stereo {
    float l, r;
} stereo;

#define STEREO(l, r)                                                                                                               \
    (stereo) { l, r }

static inline stereo stadd(stereo a, stereo b) { return STEREO(a.l + b.l, a.r + b.r); }
static inline stereo staverage(stereo a, stereo b) { return STEREO((a.l + b.l) * 0.5f, (a.r + b.r) * 0.5f); }
static inline stereo stsub(stereo a, stereo b) { return STEREO(a.l - b.l, a.r - b.r); }
static inline stereo stmul(stereo a, float b) { return STEREO(a.l * b, a.r * b); }

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


static inline float soft(float x) { return atanf(x) * (2.f / PI); }
static inline float medium(float x) { return tanhf(x); }
static inline float hard(float x) { return clampf(x, -1.f, 1.f); }

static inline stereo ensure_finite_stereo(stereo s) {
    return (stereo){.l = isfinite(s.l) ? s.l : 0.f, .r = isfinite(s.r) ? s.r : 0.f};
}

static inline stereo stereo_soft(stereo s) { return (stereo){.l = atanf(s.l) * (2.f / PI), .r = atanf(s.r) * (2.f / PI)}; }

static inline stereo stereo_medium(stereo s) { return (stereo){.l = tanhf(s.l), .r = tanhf(s.r)}; }

static inline stereo stereo_hard(stereo s) { return (stereo){.l = clampf(s.l, -1.f, 1.f), .r = clampf(s.r, -1.f, 1.f)}; }


typedef struct svf_output_t {
    float lp, bp, hp;
} svf_output_t;

// g = tanf(M_PI * fc / fs), R = 1/Q
static inline svf_output_t svf_process(float *f, float x, float g, float R) {
    const float a1 = 1.f / (1.f + g * (g + R)); // precompute?
    const float v3 = x - f[1];
    const float v1 = a1 * (f[0] + g * v3);
    const float v2 = f[1] + g * v1;
    // state updates (trapezoidal integrators)
    //v1 = soft(v1); v2=soft(v2); // nonlinearity anyone?
    f[0] = 2.f * v1 - f[0];
    f[1] = 2.f * v2 - f[1];
    return (svf_output_t){.lp = v2, .bp = v1, .hp = x - R * v1 - v2};
}

// approximation to tanh for 0-0.25 nyquist.
static inline float svf_g_approx(float fc) { // fc is 0-1 by convention here. (1='nyquist' but we oversample 2x)
    // return tanf(QUARTER_PI * fc / FS);
    //  https://www.desmos.com/calculator/qoy3dgydch
    static const float A = 0.7715117872827156f;
    static const float B = 0.22026338589733901;
    return fc * (A + B * fc * fc); // tan fit
}

static inline float svf_R(float q) { return 1.f / q; }

// these functions use a hand tuned curve for g and R that is 'nice to use', not mapped to freq
static inline float svf_g_nice(float fc) { return pow2(fc); }
static inline float svf_R_nice(float q) { return SQRT2 * (1.02f - minf(q, 1.f)); }

static inline float lpf(float x, float fc, float q) {
    float *state = ba_get(&G->audio_bump, 2);
    float y = svf_process(state, x, svf_g_nice(fc), svf_R_nice(q)).lp;
    if (isnan(y)) {
        printf("nan in lpf\n");
    }
    return y;
}

static inline float hpf(float x, float fc, float q) {
    float *state = ba_get(&G->audio_bump, 2);
    return svf_process(state, x, svf_g_nice(fc), svf_R_nice(q)).hp;
}

static inline float bpf(float x, float fc, float q) {
    float *state = ba_get(&G->audio_bump, 2);
    return svf_process(state, x, svf_g_nice(fc), svf_R_nice(q)).bp;
}

// 4 pole lowpass
static inline float lpf4(float x, float fc, float q) {
    float *state = ba_get(&G->audio_bump, 4);
    float g = svf_g_nice(fc), r = svf_R_nice(q);
    x = svf_process(state, x, g, r).lp;
    return svf_process(state+1, x, g, r).lp;
}

// 4 pole hipass
static inline float hpf4(float x, float fc, float q) {
    float *state = ba_get(&G->audio_bump, 4);
    float g = svf_g_nice(fc), r = svf_R_nice(q);
    x = svf_process(state, x, g, r).hp;
    return svf_process(state+1, x, g, r).hp;
}

// 4 pole bandpass
static inline float bpf4(float x, float fclo, float fchi, float q) {
    float *state = ba_get(&G->audio_bump, 4);
    float r = svf_R_nice(q);
    x = svf_process(state, x, svf_g_nice(fchi), r).lp;
    return svf_process(state+1, x, svf_g_nice(fclo), r).hp;
}


static inline float peakf(float x, float gain, float fc, float q) {
    float *state = ba_get(&G->audio_bump, 2);
    svf_output_t o = svf_process(state, x, svf_g_nice(fc), svf_R_nice(q));
    return o.lp + o.hp + gain*o.bp;
}

static inline float notchf(float x, float fc, float q) {
    float *state = ba_get(&G->audio_bump, 2);
    svf_output_t o = svf_process(state, x, svf_g_nice(fc), svf_R_nice(q));
    return o.lp + o.hp;
}


int test_lib_func(void);
void init_basic_state(void);
stereo reverb(stereo inp);
wave_t *request_wave_load(Sound *sound, int index);

// for now, the set of sounds and waves is fixed at boot time to avoid having to think about syncing threads
// however, the audio data itself *is* lazy loaded, so the boot time isnt too bad.
static inline Sound *get_sound(const char *name) { return shget(G->sounds, name); }

static inline int num_sounds(void) { return stbds_hmlen(G->sounds); }

static inline wave_t *get_wave(Sound *sound, int index) {
    if (!sound)
        return NULL;
    int n = stbds_arrlen(sound->waves);
    if (!n)
        return NULL;
    index %= n;
    wave_t *w = &sound->waves[index];
    return w->frames ? w : request_wave_load(sound, index);
}

static inline wave_t *get_wave_by_name(char *name) {
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

static inline float sample_linear(float pos, const float *smpl) {
    int i = (int)floorf(pos);
    float t = pos - (float)i;
    float s0 = smpl[i + 0];
    float s1 = smpl[i + 1];
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
static inline float rdnn(void) {
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

static inline float sino(float dphase) {
    float *phase = ba_get(&G->audio_bump, 1);
    *phase = fracf(*phase + dphase);
    return sinf(TAU * *phase);
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

static inline float rndsmooth(float dphase) {
    float *phase = ba_get(&G->audio_bump, 5);
    float ph = (*phase + dphase);
    if (RARE(ph >= 1.f)) {
        phase[1] = phase[2];
        phase[2] = phase[3];
        phase[3] = phase[4];
        phase[4] = rndt();
        ph -= 1.f;
    }
    phase[0] = ph;
    return catmull_rom(phase[1], phase[2], phase[3], phase[4], ph);
}

static inline float sawo(float dphase) {
    float *phase = ba_get(&G->audio_bump, 1);
    float ph = fracf(*phase);
    float saw = ph * 2.f - 1.f;
    saw -= 2.f * minblep(ph, dphase);
    *phase = ph + dphase;
    return saw;
}

static inline float pwmo(float dphase, float duty) {
    float *phase = ba_get(&G->audio_bump, 1);
    float ph = fracf(*phase);
    float saw = ph * 2.f - 1.f;
    saw -= 2.f * minblep(ph, dphase);
    *phase = ph + dphase;
    ph = fracf(ph + duty);
    saw -= ph * 2.f - 1.f;
    saw += 2.f * minblep(ph, dphase);
    return saw;
}

static inline float squareo(float dphase) { return pwmo(dphase, 0.5f); }

static inline float trio(float dphase) {
    float *phase = ba_get(&G->audio_bump, 2);
    float ph = *phase = fracf(*phase + dphase);
    float tri = ph * 4.f - 1.f;
    if (tri > 1.f)
        tri = 2.f - tri;
    return phase[1] += (tri - phase[1]) * 0.25f;
}

static inline float sawo_aliased(float dphase) {
    float *phase = ba_get(&G->audio_bump, 1);
    float ph = *phase = fracf(*phase + dphase);
    return 2.f * ph - 1.f;
}

static inline float lpf1(float x, float f) {
    float *state = ba_get(&G->audio_bump, 1);
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
static inline float midi2dphase(float midi) { return exp2f((midi - 150.2326448623f - (OVERSAMPLE - 1) * 12.f) * (1.f / 12.f)); }

#define P_(x) midi2dphase(x)

// #define FREQ2LPF(freq) 1.f - exp2f(-freq *(TAU / SAMPLE_RATE)) // more accurate for lpf with high cutoff

typedef basic_state_t *(*dsp_fn_t)(basic_state_t *G, stereo *audio, int frames, int reloaded);

static inline float do_slider(int slider_idx, basic_state_t *G, int myline, float def) {
    slider_idx &= 15;
    myline &= 255;
    float *value_line = ba_get_default(&G->sliders[slider_idx], 2, def);
    value_line[1] = myline - 1; // we count lines from 0
    return value_line[0];
}

#define S_(slider_idx, def) do_slider(slider_idx, (basic_state_t *)G, __LINE__, def)
#define S0(def) S_(0,def)
#define S1(def) S_(1,def)
#define S2(def) S_(2,def)
#define S3(def) S_(3,def)
#define S4(def) S_(4,def)
#define S5(def) S_(5,def)
#define S6(def) S_(6,def)
#define S7(def) S_(7,def)
#define S8(def) S_(8,def)
#define S9(def) S_(9,def)
#define S10(def) S_(10,def)
#define S11(def) S_(11,def)
#define S12(def) S_(12,def)
#define S13(def) S_(13,def)
#define S14(def) S_(14,def)
#define S15 S_(15, def)  
#define SA S_(10,def)
#define SB S_(11,def)
#define SC S_(12,def)
#define SD S_(13,def)
#define SE S_(14,def)
#define SF S_(15,def)
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

#ifdef LIVECODE
typedef struct state state;
stereo do_sample(stereo inp);
void init_state(void);
size_t get_state_size(void);
int get_state_version(void);
__attribute__((visibility("default"))) void *dsp(basic_state_t *_G, stereo *audio, int frames, int reloaded) {
    G = (basic_state_t*)dsp_preamble(_G, audio, reloaded, get_state_size(), get_state_version(), init_state);
    for (int i = 0; i < frames; i++) {
        // reset the per-sample bump allocators
        for (int slider_idx = 0; slider_idx < 16; slider_idx++)
            G->sliders[slider_idx].i = 0;
        G->audio_bump.i = 0;
        audio[i] = do_sample(audio[i]);
        G->sampleidx++;
        G->reloaded = 0;
    }
    // remember the high watermark for the sliders
    for (int slider_idx = 0; slider_idx < 16; slider_idx++)
        G->sliders_hwm[slider_idx] = G->sliders[slider_idx].i;

    return G;
}

#endif

#define STATE_VERSION(version, ...)                                                                                                \
    typedef struct state {                                                                                                         \
        STATE_BASIC_FIELDS                                                                                                         \
        __VA_ARGS__                                                                                                                \
    } state;                                                                                                                       \
    size_t get_state_size(void) { return sizeof(state); }                                                                          \
    int get_state_version(void) { return version; }

#ifdef LIVECODE
#undef G
#define G ((state *)_BG)
#endif
#ifdef __cplusplus
}
#endif
