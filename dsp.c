#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#define STATE_VERSION 1
#define PI 3.14159265358979323846f
#define TAU 6.28318530717958647692f
#define HALF_PI 1.57079632679489661923f

typedef struct state {
    int state_version;
    float a, b, c;
} state;

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

void do_sample(state *G, float lin, float rin, float *lrout) {
    G->a += 0.05f;
    if (G->a > TAU) {
        G->a -= TAU;
    }
    float g = sinf(G->a);
    lrout[0] = g;
    lrout[1] = g;
}

__attribute__((visibility("default"))) state *dsp(state *G, float *outf, const float *inf, int frames) {
    if (G && G->state_version != STATE_VERSION)
        G = shutdown(G);
    if (!G)
        G = init();
    for (int i = 0; i < frames; i++) {
        do_sample(G, inf[i*2], inf[i*2+1], &outf[i*2]);
    }
    return G;
}
