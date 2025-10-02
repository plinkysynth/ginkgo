// this c file is linked into the dsp.c as well as the main program
// it's essentially 'precompiled stuff' :)

#include "ginkgo.h"
#define STB_DS_IMPLEMENTATION
#include "3rdparty/stb_ds.h"
#include "utils.h"

basic_state_t *_BG;

void init_basic_state(void) {
    // init the darkening / downsampling filter for the reverb.
    G->reverb_lpf = bqlpf(0.0625, QBUTTER);
    G->reverb_hpf = bqhpf(0.125, QBUTTER);
}

static float reverbbuf[65536];
static int reverb_pos = 0;
#define AP(len)                                                                                                                    \
    {                                                                                                                              \
        int j = (i + len) & 65535;                                                                                                 \
        float d = reverbbuf[j];                                                                                                    \
        reverbbuf[i] = acc -= d * 0.5;                                                                                             \
        acc = (acc * 0.5) + d;                                                                                                     \
        i = j;                                                                                                                     \
    }
#define DELAY(len)                                                                                                                 \
    {                                                                                                                              \
        reverbbuf[i] = acc;                                                                                                        \
        int j = (i + len) & 65535;                                                                                                 \
        acc = reverbbuf[j];                                                                                                        \
        i = j;                                                                                                                     \
    }

static inline stereo reverb_internal(stereo inp) { // runs at 4x downsampled rate
    float acc = inp.l + inp.r;
    const float decay = 0.95f;
    static float top, right_fb;
    float lout, rout;
    int i = reverb_pos;
    reverb_pos = (reverb_pos - 1) & 65535;
    AP(142);
    AP(107);
    AP(379);
    AP(277);
    top = acc;
    // left branch
    acc = top + right_fb * decay;
    AP(672); // wobble
    lout = acc;
    DELAY(4453);
    acc *= decay;
    AP(1800);
    DELAY(3720);
    // right branch
    acc = top + acc * decay;
    AP(908); // wobble
    rout = acc;
    DELAY(4217);
    acc *= decay;
    AP(2656);
    DELAY(3162); // +1
    right_fb = acc;

    return STEREO(lout, rout);
}

stereo reverb(stereo inp) {

    float *state = ba_get(&G->audio_bump, 8 + 8); // 8 for filters, 8 for 4x stereo output samples
    stereo *state2 = (stereo *)(state + 8);
    ////////////////////////// 4x DOWNSAMPLE
    inp = (stereo){
        bqfilter(state, inp.l, G->reverb_lpf),
        bqfilter(state + 2, inp.r, G->reverb_lpf),
    };
    int outslot = (G->sampleidx >> 2) & 3;
    if ((G->sampleidx & 3) == 0) {
        inp = (stereo){
            bqfilter(state + 4, inp.l, G->reverb_hpf),
            bqfilter(state + 6, inp.r, G->reverb_hpf),
        };
        state2[outslot] = reverb_internal(inp);
    }
    ////////////////////////// 4x CATMULL ROM UPSAMPLE
    int t0 = (outslot - 3) & 3, t1 = (outslot - 2) & 3, t2 = (outslot - 1) & 3, t3 = outslot;
    float f = ((G->sampleidx & 3) + 1) * 0.25f;
    float lout = catmull_rom(state2[t0].l, state2[t1].l, state2[t2].l, state2[t3].l, f);
    float rout = catmull_rom(state2[t0].r, state2[t1].r, state2[t2].r, state2[t3].r, f);
    return (stereo){lout, rout};
}

void *dsp_preamble(basic_state_t *_G, stereo *audio, int reloaded, size_t state_size, int version, void (*init_state)(void)) {
    if (!_G || _G->_ver != version || _G->_size != state_size) {
        /* free(G); - safer to just let it leak :)  virtual memory ftw  */
        _G = calloc(1, state_size);
        _G->_ver = version;
        _G->_size = state_size;
    }
    _G->reloaded = reloaded;
    G = _G; // set global variable for user code. nb the dll and the main program have their own copies of G.
    if (reloaded) {
        init_basic_state();
        (*init_state)();
    }
    if (!_G->audio_bump.data) {
        ba_grow(&_G->audio_bump, 65536);
    }
    return _G;
}


const wave_t *request_wave_load(Sound *sound, int index) {
    if (!G) return NULL;
    spin_lock(&G->load_request_cs);
    sound_request_key_t key = {sound, index};
    stbds_hmput(G->load_requests, key, 0); // TODO: its unsafe to use the pointer to w, as the list of waves may get resized. I think this is ok so long as we dont load sample packs mid session.
    spin_unlock(&G->load_request_cs);
    return NULL;
}


int test_lib_func(void) {
    return 23;
}
