#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#define STATE_VERSION 1
#define PI 3.14159265358979323846f
#define TAU 6.28318530717958647692f
#define HALF_PI 1.57079632679489661923f
#define SAMPLE_RATE 48000

#define FREQ2LPF(freq) 1.f - exp2f(-freq * (TAU / SAMPLE_RATE))   // more accurate for lpf with high cutoff

static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float maxf(float a, float b) { return a > b ? a : b; }
static inline float fracf(float x) { return x - floorf(x); }
static inline float clampf(float a, float min, float max) { return a < min ? min : a > max ? max : a; }
static inline int min(int a, int b) { return a < b ? a : b; }
static inline int max(int a, int b) { return a > b ? a : b; }
static inline int clamp(int a, int min, int max) { return a < min ? min : a > max ? max : a; }

static inline float update_lfo(float state[2], float sin_dphase) { // quadrature oscillator magic
    state[0] -= (state[1] * sin_dphase);
    return state[1] += (state[0] * sin_dphase);
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

typedef struct state {
    int state_version;
    float saw;
} state;

void do_sample(state *G, float lin, float rin, float *lrout) {
    float saw = sawo(&G->saw, midi2dphase(69-24));
    lrout[0] = saw;
    lrout[1] = saw;
}

state *shutdown(state *G) {
    // printf("shutdown\n");
    free(G);
    return NULL;
}

state *init(void) {
    state *G = (state *)calloc(1, sizeof(state));
    G->state_version = STATE_VERSION;
    return G;
}

__attribute__((visibility("default"))) state *dsp(state *G, float *outf, const float *inf, int frames) {
    if (G && G->state_version != STATE_VERSION)
        G = shutdown(G);
    if (!G)
        G = init();
    for (int i = 0; i < frames; i++) {
        do_sample(G, inf[i * 2], inf[i * 2 + 1], &outf[i * 2]);
    }
    return G;
}
