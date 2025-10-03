// this c file is linked into the dsp.c as well as the main program
// it's essentially 'precompiled stuff' :)

#include "ginkgo.h"
#include "utils.h"
#define STB_DS_IMPLEMENTATION
#include "3rdparty/stb_ds.h"
basic_state_t dummy_state;
basic_state_t *_BG = &dummy_state;

void init_basic_state(void) {
    
}

#define RVMASK 65535
static float reverbbuf[65536];
static int reverb_pos = 0;

static float k_reverb_fade = 240  / 256.f; // 240 originally
static float k_reverb_shim = 120 / 256.f;
static float k_reverb_wob = 0.3333f; // amount of wobble
static const float lforate1 = 1.f / 32777.f * 9.4f;// * 2.f; // speed of wobble
static const float lforate2 = 1.3f / 32777.f * 3.15971f;// * 2.f; // speed of wobble
static int shimmerpos1 = 2000;
static int shimmerpos2 = 1000;
static int shimmerfade = 0;
static int dshimmerfade = 32768/4096;
static float aplfo[2]={1.f,0.f}, aplfo2[2]={1.f,0.f};

static inline float LINEARINTERPRV(const float* buf, int basei, float wobpos) { // read buf[basei-wobpos>>12] basically
	basei -= (int)wobpos;
	wobpos-=floorf(wobpos);
	float a0 = buf[basei & RVMASK];
	float a1 = buf[(basei - 1) & RVMASK];
	return ((a0 * (1.f - wobpos) + a1 * wobpos));
}


    stereo plinky_reverb(stereo input) {
        int i = reverb_pos;
        float outl = 0, outr = 0;
        float wob = update_lfo(aplfo, lforate1) * k_reverb_wob;
        float apwobpos = (wob + 1.f) * 64.f;
        wob = update_lfo(aplfo2, lforate2)  * k_reverb_wob;
        float delaywobpos = (wob + 1.f) * 64.f;
    #define RVDIV /2
    #define CHECKACC // assert(acc>=-32768 && acc<32767);
    #define AP(len) { \
            int j = (i + len RVDIV) & RVMASK; \
            float d = reverbbuf[j]; \
            acc -= d *0.5f; \
            reverbbuf[i] = (acc); \
            acc = (acc *0.5f) + d; \
            i = j; \
            CHECKACC \
        }
    #define AP_WOBBLE(len, wobpos) { \
            int j = (i + len RVDIV) & RVMASK;\
            float d = LINEARINTERPRV(reverbbuf, j, wobpos); \
            acc -= d * 0.5f; \
            reverbbuf[i] = (acc); \
            acc = (acc * 0.5f) + d; \
            i = j; \
            CHECKACC \
        }
    #define DELAY(len) { \
            int j = (i + len RVDIV) & RVMASK; \
            reverbbuf[i] = (acc); \
            acc = reverbbuf[j]; \
            i = j; \
            CHECKACC \
        }
    #define DELAY_WOBBLE(len, wobpos) { \
            int j = (i + len RVDIV) & RVMASK; \
            reverbbuf[i] = (acc); \
            acc = LINEARINTERPRV(reverbbuf, j, wobpos); \
            i=j; \
            CHECKACC \
        }
            
        float acc = ((input.l));
        AP(142);
        AP(379);
        acc += (input.r);
        AP(107);
    //	float reinject2 = acc;
        AP(277);
        float reinject = acc;
        static float fb1 = 0;
        acc += fb1;
        AP_WOBBLE(672, apwobpos);
        AP(1800);
        DELAY(4453);
    
        if (1) {
            // shimmer - we can read from up to about 2000 samples ago
    
            // Brief shimmer walkthrough:
            // - We walk backwards through the reverb buffer with 2 indices: shimmerpos1 and shimmerpos2.
            //   - shimmerpos1 is the *previous* shimmer position.
            //   - shimmerpos2 is the *current* shimmer position.
            //   - Note that we add these to i (based on reverbpos), which is also walking backwards
            //     through the buffer.
            // - shimmerfade controls the crossfade between the shimmer from shimmerpos1 and shimmerpos2.
            //   - When shimmerfade == 0, shimmerpos1 (the old shimmer) is chosen.
            //   - When shimmerfade == SHIMMER_FADE_LEN - 1, shimmerpos2 (the new shimmer) is chosen.
            //   - For everything in-between, we linearly interpolate (crossfade).
            //   - When we hit the end of the fade, we reset shimmerpos2 to a random new position and set
            //     shimmerpos1 to the old shimmerpos2.
            // - dshimmerfade controls the speed at which we fade.
    
            #define SHIMMER_FADE_LEN 32768
            shimmerfade += dshimmerfade;
    
            if (shimmerfade >= SHIMMER_FADE_LEN) {
                shimmerfade -= SHIMMER_FADE_LEN;
    
                shimmerpos1 = shimmerpos2;
                shimmerpos2 = (rand() & 4095) + 8192;
                dshimmerfade = (rand() & 7) + 8; // somewhere between SHIMMER_FADE_LEN/2048 and SHIMMER_FADE_LEN/4096 ie 8 and 16
            }
    
            // L = shimmer from shimmerpos1, R = shimmer from shimmerpos2
            stereo shim1 = STEREO(reverbbuf[(i + shimmerpos1) & RVMASK], reverbbuf[(i + shimmerpos2) & RVMASK]);
            stereo shim2 = STEREO(reverbbuf[(i + shimmerpos1 + 1) & RVMASK], reverbbuf[(i + shimmerpos2 + 1) & RVMASK]);
            stereo shim = (shim1+shim2);
    
            // Fixed point crossfade:
            float shimo = shim.l * ((SHIMMER_FADE_LEN - 1) - shimmerfade) +
                                    shim.r * shimmerfade;
            shimo *= 1.f/SHIMMER_FADE_LEN;  // Divide by SHIMMER_FADE_LEN
    
            // Apply user-selected shimmer amount.
            // Tone down shimmer amount.
            shimo *= k_reverb_shim * 0.25f;
        
            acc += shimo;
            outl = shimo;
            outr = shimo;
    
            shimmerpos1--;
            shimmerpos2--;
        }
    
    
        const static float k_reverb_color = 0.95f;
        static float lpf = 0.f, dc = 0.f;
        lpf += (((acc * k_reverb_fade)) - lpf) * k_reverb_color;
        dc += (lpf - dc) * 0.005f;
        acc = (lpf - dc);
        outl += acc;
    
        acc += reinject;
        AP_WOBBLE(908, delaywobpos);
        AP(2656);
        DELAY(3163);
        static float lpf2 = 0.f;
        lpf2+= (((acc * k_reverb_fade) ) - lpf2) * k_reverb_color;
        acc = (lpf2);
    
        outr += acc;
    
        reverb_pos = (reverb_pos - 1) & RVMASK;
        fb1=(acc*k_reverb_fade);
        return STEREO(outl, outr);
        
    
    }
    


stereo reverb(stereo inp) {
    float *state = ba_get(&G->audio_bump, 8 + 8); // 8 for filters, 8 for 4x stereo output samples
    stereo *state2 = (stereo *)(state + 8);
    ////////////////////////// 4x DOWNSAMPLE
    inp = (stereo){
        svf_process(state, inp.l, 0.5f, SQRT2).lp,
        svf_process(state + 2, inp.r, 0.5f, SQRT2).lp,
    };
    int outslot = (G->sampleidx >> 2) & 3;
    if ((G->sampleidx & 3) == 0) {
        state2[outslot] = plinky_reverb(inp);
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
        basic_state_t *oldg = _G;
        _G = (basic_state_t *)calloc(1, state_size);
        if (oldg) memcpy(_G, oldg, sizeof(basic_state_t)); // preserve the basic state...
        // ...but update the version number and size.
        _G->_ver = version;
        _G->_size = state_size;
    }
    _G->reloaded = reloaded;
    G = _G; // set global variable for user code. nb the dll and the main program have their own copies of G. dsp() ends up setting the dll copy from the main process copy.
    if (reloaded) {
        init_basic_state();
        (*init_state)();
    }
    if (!_G->audio_bump.data) {
        ba_grow(&_G->audio_bump, 65536);
    }
    return _G;
}


wave_t *request_wave_load(Sound *sound, int index) {
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
