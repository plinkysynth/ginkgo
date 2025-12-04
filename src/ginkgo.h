#pragma once
// this header tries to be a one-stop shop for the hot-recompiled audio function.
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "3rdparty/stb_ds.h"
#include "extvector.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__SANITIZE_ADDRESS__) || \
    (defined(__has_feature) && __has_feature(address_sanitizer))
#define USING_ASAN 1
#else
#define USING_ASAN 0
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
#define SVF_24_R_MUL1 ((QBUTTER_24A) / QBUTTER)
#define SVF_24_R_MUL2 ((QBUTTER_24B) / QBUTTER)
// size of virtual textmode screen:
#define TMW 512
#define TMH 256

typedef float F;
typedef int I;
typedef uint32_t U;

#define ctz __builtin_ctz
#define clz __builtin_clz
#define ffs __builtin_ffs
#define popcount __builtin_popcount
#define popcountll __builtin_popcountll
#define clzll __builtin_clzll
#define ctzll __builtin_ctzll
#define ffsll __builtin_ffsll
#define popcountll __builtin_popcountll



typedef struct stereo {
    float l, r;
} stereo;

inline stereo st(float l, float r) { return stereo{l, r}; }                                                                                                               \
inline stereo mono2st(float l) { return stereo{l, l}; }
#define mono2stereo mono2st
static inline float stmid(stereo s) { return (s.l + s.r) * 0.5f; }
static inline float stside(stereo s) { return (s.l - s.r) * 0.5f; }
static inline stereo midside2st(float mid, float side) { return st(mid + side, mid - side); }
static inline stereo rotate(stereo x, float c, float s) { return st(x.l * c - x.r * s, x.l * s + x.r * c); }
static inline stereo abs(stereo s) { return st(fabsf(s.l), fabsf(s.r)); }
static inline stereo operator+(stereo a, stereo b) { return st(a.l + b.l, a.r + b.r); }
static inline stereo operator+(stereo a, float b) { return st(a.l + b, a.r + b); }
static inline stereo operator-(stereo a, stereo b) { return st(a.l - b.l, a.r - b.r); }
static inline stereo operator*(stereo a, float b) { return st(a.l * b, a.r * b); }
static inline stereo operator*(stereo a, stereo b) { return st(a.l * b.l, a.r * b.r); }
static inline stereo operator/(stereo a, float b) { return st(a.l / b, a.r / b); }
static inline void operator+=(stereo &a, stereo b) { a.l += b.l; a.r += b.r; }
static inline void operator+=(stereo &a, float b) { a.l += b; a.r += b; }
static inline void operator-=(stereo &a, stereo b) { a.l -= b.l; a.r -= b.r; }
static inline void operator-=(stereo &a, float b) { a.l -= b; a.r -= b; }
static inline void operator*=(stereo &a, stereo b) { a.l *= b.l; a.r *= b.r; }
static inline void operator*=(stereo &a, float b) { a.l *= b; a.r *= b; }
static inline void operator/=(stereo &a, stereo b) { a.l /= b.l; a.r /= b.r; }
static inline void operator/=(stereo &a, float b) { a.l /= b; a.r /= b; }

#include "notes.h"


inline float fast_tanh(float x) {
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

#define PHI  1.61803398875f
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
static inline float smoothstep(float x) { if (x<0.f) return 0.f; if (x>1.f) return 1.f; return x * x * (3.f - 2.f * x); }
static inline float smoothstep(float a, float b, float x) { return smoothstep((x-a)/(b-a)); }
static inline stereo lerp(stereo a, stereo b, float t) { return a+(b-a)*t; }

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
// double f = tan(PI * cutoff);
// double r = (40.0/9.0) * resonance;

static inline float ladder(float s[5],float inp, float f, float r) {
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

static inline bool isfinite(float4 x) { return isfinite(x.x) && isfinite(x.y) && isfinite(x.z) && isfinite(x.w); }

static inline stereo ensure_finite(stereo s) { return (stereo){.l = ensure_finite(s.l), .r = ensure_finite(s.r)}; }

static inline stereo ssclip(stereo s) { return (stereo){.l = atanf(s.l) * (2.f / PI), .r = atanf(s.r) * (2.f / PI)}; }
static inline stereo sclip(stereo s) { return (stereo){.l = tanhf(s.l), .r = tanhf(s.r)}; }
static inline stereo clip(stereo s) { return (stereo){.l = clamp(s.l, -1.f, 1.f), .r = clamp(s.r, -1.f, 1.f)}; }

typedef struct svf_output_t {
    float lp, bp, hp;
} svf_output_t;

// g = tanf(PI * fc / fs), R = 1/Q
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
    return (svf_output_t){.lp = lp, .bp = a * v, .hp = x - lp};
}

// approximation to tanh for 0-0.25 nyquist.
static inline float svf_g(float fc) { // fc is like a dphase, ie P_C4 etc constants work
    // return tanf(PI * fc / SAMPLE_RATE);
    //   https://www.desmos.com/calculator/qoy3dgydch
    static const float A = 3.272433237e-05f, B = 1.181248215e-14f;
    return fc * A; // * (A + B * fc * fc); // tan fit
}

// static inline float svf_g(float fc) { return fc/SAMPLE_RATE; }


typedef struct wave_t wave_t;
typedef struct voice_state_t voice_state_t;
typedef struct hap_t hap_t;
typedef stereo sample_func_t(voice_state_t *v, hap_t *h, wave_t *w, bool *at_end);

void register_osc(const char *name, sample_func_t *func);

//#define FLUX 
#ifdef FLUX
#define FLUX_HOP_LENGTH 128
#endif 

typedef struct wave_t {
    const char *key; // interned url
    float *frames;   // malloc'd
    sample_func_t *sample_func;
#ifdef FLUX
    float *flux; // one flux value per FLUX_HOP_LENGTH frames
    float *invflux; // inverse cdf of flux
#endif
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

typedef struct lfo_t {
    float state[2];
    inline float operator()(float sin_dphase) { // quadrature oscillator magic
        if (RARE(state[0]==0.f && state[1]==0.f)) state[0] = 1.f;
        state[0] -= (state[1] * sin_dphase);
        return state[1] += (state[0] * sin_dphase);
    }
} lfo_t;



struct ginkgoverb_t {
    const static int RVMASK = 32768-1;
    const static int NUM_TAPS = 16;
    float filter_state[24];
    lfo_t taplofs[NUM_TAPS];
    float reverbbuf[RVMASK+1];
    int reverb_pos;
    stereo operator()(stereo inp);
    stereo _run_internal(stereo inp);
};

struct plinkyverb_t {
    const static int RVMASK = 32768-1;
    float filter_state[16];
    float reverbbuf[RVMASK+1];
    int reverb_pos;
    int shimmerpos1;
    int shimmerpos2;
    int shimmerfade;
    int dshimmerfade;
    lfo_t aplfo;
    lfo_t aplfo2;
    float fb1;
    float lpf, dc;
    float lpf2;

    stereo operator()(stereo inp);
    stereo _run_internal(stereo inp);
};

struct delay_t {
    const static int delaybuf_size = 65536*2;
    stereo delaybuf[delaybuf_size];
    int delay_pos;

    stereo operator()(stereo inp, stereo time_qn={0.75f, 0.75f}, float feedback=0.5f, float rotate = 0.f);
};

typedef struct pattern_t pattern_t;


typedef struct camera_state_t {
    float4 c_cam2world_old[4];
    union {
        struct {
            float4 c_right;
            float4 c_up;
            float4 c_fwd;
            float4 c_pos;
        };
        float4 c_cam2world[4];
    };
    float4 c_lookat;
    float fov;
    float focal_distance;
    float aperture;
} camera_state_t;

typedef bool (*get_key_func_t)(int key);

static inline float env_k(float x, float epsilon = 0.00025f) {
    // 0.00001 (e^-11) is a few seconds; 0.5 is fast.
    return 1.f - exp(-0.00001f / (x*x + epsilon));
}

struct env_follower_t {
    float lp;
    float y;
    int line;
    float operator()(float x, float decay=0.1f, float attack = 0.05f) {
        lp += (fabsf(x)-lp)*0.01f;
        float d = lp-y;
        y += d * env_k((d>0.f) ? attack : decay);
        return x;
    }
    stereo operator()(stereo x, float decay=0.1f, float attack = 0.05f) {
       operator()(max(fabsf(x.l), fabsf(x.r)), decay, attack);
       return x;
    }
};


typedef struct song_base_t {
    int _size;
    float bpm;
    // float swing; // TODO
    float cpu_usage;
    float cpu_usage_smooth;
    double t; // the current musical time! as a double
    double dt; // change of t per sample
    stereo preview; // the preview audio. will be mixed at the end, so the shader code can modify it as it wishes.
    int64_t t_q32; // the current musical time! as a 32.32 fixed point number
    uint8_t midi_cc[128];
    uint8_t mutes; // 8 mutes to go with the 8 ccs
    //uint32_t midi_cc_gen[128];
    int cursor_x, cursor_y;
    float mx, my;
    float mscrollx, mscrolly;
    int mb;
    int old_mb;
    float old_mx, old_my;
    int fbw, fbh; // current framebuffer size in pixels
    float ui_alpha;
    float ui_alpha_target;
    double iTime;
    camera_state_t camera;
    get_key_func_t get_key_func;
    uint32_t sampleidx;
    atomic_flag load_request_cs;
    sound_pair_t *sounds;
    wave_t *waves;
    pattern_t *patterns_map; // stbds_sh
    wave_request_t *load_requests;
    double preview_wave_t;
    int preview_wave_idx_plus_one;
    float preview_wave_fade;
    float preview_fromt;
    float preview_tot;
    float limiter_state[2];
    env_follower_t vus[8];
    uint8_t reloaded;
    uint8_t playing;
    void init(void) {}
    
} song_base_t;

extern song_base_t *G;
static inline void init_basic_state(void) { 
    G->bpm = 120.f; 
}


#define cc(x) ((G->mutes & (1<<((x)&7))) ? 0.f : G->midi_cc[16+((x)&7)]/127.f)


static inline float fold(float x) { // folds x when it goes beyond +-1
    float t = x + 1.0f;                    // shift so the "cell" is [0,2] around 0
    t -= 4.0f * floorf(t * 0.25f);         // t in [0,4)
    return 1.0f - fabsf(t - 2.0f);         // triangle in [-1,1]
}

typedef double hap_time;

enum {
    #define X(x, ...) x,
    #include "params.h"
    P_LAST
};


typedef struct hap_t {
    int hapid;
    int node; // index of the node that generated this hap.
    uint64_t valid_params; // which params have been assigned for this hap.
    int scale_bits;
    hap_time t0, t1; 
    float params[P_LAST];
    inline float get_param(int param, float default_value) const { return valid_params & (1ull << param) ? params[param] : default_value; } 
    inline void set_param(int param, float value) { params[param] = value; valid_params |= 1ull << param; }
    bool has_param(int param) const { return valid_params & (1ull << param); }
} hap_t;

typedef struct adsr_t {
    float state[1];
    inline float operator()(float gate, float a_k, float d_k, float s, float r_k, bool retrig=false) {
        if (retrig) *state = fabsf(*state);
        if (gate > 0.f) {
            if (state[0] >= 0.f) {
                state[0] += (gate*1.05f - state[0]) * a_k; // attack (state is positive)
                if (state[0] > gate) { state[0] = -gate; return gate; } // go into decay...
                return state[0];
            } else {
                state[0] += (-(s*gate) - state[0]) * d_k; // decay (state is negative)
                return -state[0];
            }
        } else {
            if (state[0] < 0.f) state[0] = -state[0];
            state[0] -= state[0] * r_k; // release (state is positive)
            return state[0];
        }
    }
} adsr_t;

typedef struct filter_t {
    float state[4];
    inline float lpf(float x, float fc, float r= 1.f/QBUTTER) {
        return svf_process_2pole(state, x, svf_g(fc), r).lp;
    }
    inline stereo lpf(stereo x, float fc, float r= 1.f/QBUTTER) {
        float g= svf_g(fc);
        float yl = svf_process_2pole(state, x.l, g, r).lp;
        float yr = svf_process_2pole(state+2, x.r, g, r).lp;
        return st(yl,yr);
    }
    inline float lpf4(float x, float fc, float r= 1.f/QBUTTER) {
        float g= svf_g(fc);
        x = svf_process_2pole(state, x, g, r * SVF_24_R_MUL1).lp;
        return svf_process_2pole(state + 2, x, g, r * SVF_24_R_MUL2).lp;
    }
    inline float hpf(float x, float fc, float r= 1.f/QBUTTER) {
        return svf_process_2pole(state, x, svf_g(fc), r).hp;
    }
    inline stereo hpf(stereo x, float fc, float r= 1.f/QBUTTER) {
        float g= svf_g(fc);
        float yl = svf_process_2pole(state, x.l, g, r).hp;
        float yr = svf_process_2pole(state+2, x.r, g, r).hp;
        return st(yl,yr);
    }
    inline float hpf4(float x, float fc, float r= 1.f/QBUTTER) {
        float g= svf_g(fc);
        x = svf_process_2pole(state, x, g, r * SVF_24_R_MUL1).hp;
        return svf_process_2pole(state + 2, x, g, r * SVF_24_R_MUL2).hp;
    }
    inline float bpf(float x, float fc, float r= 1.f/QBUTTER) {
        return svf_process_2pole(state, x, svf_g(fc), r).bp;
    }
    inline stereo bpf(stereo x, float fc, float r= 1.f/QBUTTER) {
        float g= svf_g(fc);
        float yl = svf_process_2pole(state, x.l, g, r).bp;
        float yr = svf_process_2pole(state+2, x.r, g, r).bp;
        return st(yl,yr);
    }
    inline float peakf(float x, float gain, float fc, float r = 1.f/QBUTTER) {
        svf_output_t o = svf_process_2pole(state, x, svf_g(fc), r);
        return x + (gain - 1.f) * o.bp * r;
    }    
    inline float notchf(float x, float fc, float r = 1.f/QBUTTER) {
        svf_output_t o = svf_process_2pole(state, x, svf_g(fc), r);
        return o.lp + o.hp;
    }
    inline stereo notchf(stereo x, float fc, float r = 1.f/QBUTTER) {
        float g= svf_g(fc);
        svf_output_t o_l = svf_process_2pole(state, x.l, g, r);
        svf_output_t o_r = svf_process_2pole(state+2, x.r, g, r);
        stereo rv;
        rv.l = o_l.lp + o_l.hp;
        rv.r = o_r.lp + o_r.hp;
        return rv;
    }

} filter_t;



enum EInUse : uint8_t {
    UNUSED = 0,
    ALLOCATED = 1,
    IN_USE = 2,
};

typedef struct voice_state_t {
    hap_t h;
    adsr_t adsr1;
    adsr_t adsr2;
    filter_t filter;
    filter_t hpf;
    double phase;
    double dphase;
    double grainheads[3];
    float grainphase;
    float vibphase;
    float tremphase;
    float cur_power;
    EInUse in_use;
    stereo synth_sample(hap_t *h, bool keydown, float env1, float env2, float fold_actual, float dist_actual, float cutoff_actual, wave_t *w);
} voice_state_t;

// TODO: for some patterns, tidal/strudel prefers a rotation that puts the first stumble earlier in the cycle.
// but this is simple so we'll go with simple.
static inline int euclid_rhythm(int stepidx, int numset, int numsteps, int rot) {
    if (numsteps < 1 || numset < 1)
        return 0;
    stepidx = ((stepidx - rot) % numsteps) + numsteps;
    return ((stepidx * numset) % numsteps) < numset;
}




void init_basic_state(void);
wave_t *request_wave_load(wave_t *wave);


#define vu(idx, audio, ...) (G->vus[idx&7].line = __LINE__, G->vus[idx&7].operator()(audio, ##__VA_ARGS__)) // be careful to only use audio once.

typedef struct multiband_t {
    stereo state[12];
    void  operator()(stereo x, stereo out[3]);
} multiband_t;

typedef struct ott_t {
    multiband_t multiband;
    env_follower_t env[3];
    stereo operator()(stereo rv, float amount);
} ott_t;

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

static inline wave_t *get_wave(Sound *sound, int index, float *note= NULL) {
    if (!sound)
        return NULL;
    if (sound->name[0] == '-' || sound->name[0] == '~')
        return NULL;
    int n = stbds_arrlen(sound->wave_indices);
    if (!n)
        return NULL;
    if (note) {
        if (sound->midi_notes && index==0) {
            int inote = (int)*note;
            int_pair_t pair = {.k = inote, .v = index};
            int nmn = stbds_arrlen(sound->midi_notes);
            //int insert_min = lower_bound_int_pair(sound->midi_notes, nmn, pair);
            int from = 0;//max(0, insert_min - 1);
            int to = nmn;//min(nmn, insert_min + 1);
            int closest_dist = 1000;
            int closest_note = inote;
            for (int j = from; j < to; j++) {
                int dist = abs(sound->midi_notes[j].k - inote);
                if (dist < closest_dist) {
                    closest_dist = dist;
                    closest_note = sound->midi_notes[j].k;
                    index = sound->midi_notes[j].v;
                }
            }
            if (closest_note < 1000) {
                *note -= closest_note-C3;
            }
        } else {
            // they've specified a specific wave index, but it has a base note.
            index %= n;
            wave_t *w = &G->waves[sound->wave_indices[index]];
            if (w->midi_note>0)
                *note -= w->midi_note - C3;
        }
    }
    index %= n;
    wave_t *w = &G->waves[sound->wave_indices[index]];
    if (!w->frames) request_wave_load(w);
    return w;
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


static inline float catmull_rom(float s0, float s1, float s2, float s3, float t) {
    // Catmull–Rom coefficients (a = -0.5)
    float c0 = s1;
    float c1 = 0.5f * (s2 - s0);
    float c2 = s0 - 2.5f * s1 + 2.0f * s2 - 0.5f * s3;
    float c3 = -0.5f * s0 + 1.5f * s1 - 1.5f * s2 + 0.5f * s3;
    return ((c3 * t + c2) * t + c1) * t + c0;
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


static inline float minblep(float phase, float dphase) {
    if (dphase <= 0.f)
        return 0.f;
    float bleppos = (phase / dphase * (blep_os / OVERSAMPLE));
    if (bleppos >= 128 || bleppos < 0.f)
        return 0.f;
    int i = (int)bleppos;
    return minblep_table[i] + (minblep_table[i + 1] - minblep_table[i]) * (bleppos - i);
}

typedef struct rndsmooth_t {
    float state[5];
    inline float operator()(float dphase) {
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
} rndsmooth_t;

static inline float sino(float phase) {
    return sinf(TAU * phase);
}


static inline float sawo(float phase, float dphase) {
    float saw = phase * 2.f - 1.f - 2.f * minblep(phase, dphase);
    return saw;
}

static inline float pwmo(float phase, float dphase, float duty, float squareness = 1.f) {
    float saw1 = sawo(phase, dphase);
    float saw2 = sawo(frac(phase + duty), dphase);
    return saw1 - saw2 * squareness;
}

static inline float squareo(float phase, float dphase, float squareness = 1.f) { return pwmo(phase, dphase, 0.5f, squareness); }

static inline float trio(float phase, float dphase) {
    float tri = phase * 4.f - 1.f;
    if (tri > 1.f)
        tri = 2.f - tri;
    return tri;
}

static inline float sawo_aliased(float phase, float dphase) {
    return 2.f * phase - 1.f;
}

// a mix of sin (at -1) to saw (0) to square (1) to pulse (>1)
static inline float shapeo(double phase, double dphase, float wavetable_number) {
    float fphase = frac(phase);
    if (wavetable_number <= 0.f) {
        float s = sawo(fphase, dphase);
        return lerp(s, sino(fphase + 0.25f), min(1.f, -wavetable_number));
    } else {
        float duty = min(1.5f - wavetable_number, 0.5f);
        return pwmo(fphase, dphase, duty, min(1.f, wavetable_number));
    }
}



static inline stereo pan(stereo inp, float balance) { // 0=center, -1 = left, 1=right
    if (balance>0.f) {
        return st(inp.l * (1.f-balance), inp.r + inp.l * balance);
    } else {
        return st(inp.l + inp.r * -balance, inp.r * (1.f+balance));
    }
}




stereo limiter(float state[2] , stereo inp);


static inline float note2dphase(float midi) { return exp2f((midi - 150.2326448623f) * (1.f / 12.f)); }

#define LN2_OVER_12 0.05776226505f
#define LN440_MINUS_69_LN2_OVER_12 2.1011784387f
static inline float note2freq(float midi) { return expf(midi*LN2_OVER_12 + LN440_MINUS_69_LN2_OVER_12); }

#define P_(x) midi2dphase(x)

// #define FREQ2LPF(freq) 1.f - exp2f(-freq *(TAU / SAMPLE_RATE)) // more accurate for lpf with high cutoff

typedef song_base_t *(*dsp_fn_t)(song_base_t *G, stereo *audio, int frames, int reloaded);
typedef void (*frame_update_func_t)(get_key_func_t get_key_func, song_base_t *G);


void update_camera_matrix(camera_state_t *cam);
void fps_camera(void);

static inline void set_camera(float4 pos, float4 lookat, float fov = 0.4f, float aperture = 0.01f) {
    G->camera.c_pos = pos;
    G->camera.c_lookat = lookat;
    G->camera.fov = fov;
    G->camera.focal_distance = length(lookat - pos);
    G->camera.aperture = aperture;
    update_camera_matrix(&G->camera);
}

#define LOG(...)                                                                                                                   \
    {                                                                                                                              \
        static int count = 0;                                                                                                      \
        count++;                                                                                                                   \
        if (RARE(count > 100000)) {                                                                                                \
            count = 0;                                                                                                             \
            fprintf(stderr, __VA_ARGS__);                                                                                          \
        }                                                                                                                          \
    }

void *dsp_preamble(song_base_t *_G, stereo *audio, int reloaded, size_t state_size, void (*init_state)(void));


static inline float swing(double t, float swing_point = 0.66) { // swing point of 0.5 is even ie no-op
    t*=4.;
    double qn = floor(t);
    double phase = t-qn;
    if (phase<swing_point) phase/=(swing_point*2.); else phase=(phase-swing_point)/((1.-swing_point)*2.)+0.5;
    return (qn+phase)*0.25;
}

stereo prepare_preview(void);
bool get_key(int key);


#ifdef LIVECODE
struct song;
song_base_t *G = NULL;
stereo do_sample(stereo inp);
void init_state(void);

size_t get_state_size(void);
stereo probe;
__attribute__((visibility("default"))) void *dsp(song_base_t *_G, stereo *audio, int frames, int reloaded) {
    G = (song_base_t *)dsp_preamble(_G, audio, reloaded, get_state_size(), init_state);
    int dt_q32 = G->playing ? G->bpm * (4294967296.0 / (SAMPLE_RATE * 240.0)) : 0; // on the order of 22000
    G->dt = dt_q32 *(1./4294967296.);
    for (int i = 0; i < frames; i++) {
        probe = {};
        G->t=G->t_q32*(1./4294967296.);
        //G->t=swing(G->t);
        prepare_preview();
        audio[i] = do_sample(audio[i]);
        audio[i] += G->preview;
        audio[i + frames] = probe;
        G->sampleidx++;
        G->t_q32 += dt_q32;
        G->reloaded = 0;
    }
    return G;
}

#endif


typedef struct synth_t {
    const static int max_voices = 16;
    stereo audio[96];
    voice_state_t voices[max_voices]; // one voice per voice.
    int num_in_use;
    stereo operator()(const char *pattern_name, float level=1.f, int max_voices = 16);
} synth_t;


hap_t *pat2hap(const char *pattern_name, hap_t *cache);

#ifdef __cplusplus
}
#endif
