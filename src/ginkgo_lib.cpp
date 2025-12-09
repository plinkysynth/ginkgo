// this c file is linked into the dsp.c as well as the main program
// it's essentially 'precompiled stuff' :)
#define PFFFT_IMPLEMENTATION
#include "3rdparty/pffft.h"
#include "ginkgo.h"
#include "utils.h"
#include "wavfile.h"
#define STB_DS_IMPLEMENTATION
#include "3rdparty/stb_ds.h"
#include "miniparse.h"
#include "multiband.h"
#include "ansicols.h"

const static char *param_names[P_LAST] = {
    #define X(x, shortname, ...) shortname,
    #include "params.h"
    };
    
    
bool get_key(int key) { return G && G->get_key_func && G->get_key_func(key); }

void update_camera_matrix(camera_state_t *cam) {
    float4 c_up = {0.f, 1.f, 0.f, 0.f}; // up
    if (!isfinite(cam->c_lookat) || !cam->c_lookat.w) {
        cam->c_lookat = {0.f, 0.f, 0.f, 1.f};
    }
    if (!isfinite(cam->c_pos) || !cam->c_pos.w) {
        cam->c_pos = {0.f, 0.f, -4.f, 1.f};
    }
    cam->c_lookat.w = 1.f;
    cam->c_pos.w = 1.f;
    cam->c_fwd = normalize(cam->c_lookat - cam->c_pos);
    cam->c_right = normalize(cross(c_up, cam->c_fwd));
    cam->c_up = normalize(cross(cam->c_fwd, cam->c_right));
}

void fps_camera(void) {
    camera_state_t *cam = &G->camera;
    if (G->ui_alpha_target < 0.5) {

        float cam_mx = (G->mx - G->old_mx) / G->fbw;
        float cam_my = (G->my - G->old_my) / G->fbh;
        float theta = cam_mx * TAU;
        float phi = cam_my * PI;
        float ctheta = cosf(theta), stheta = sinf(theta);
        float cphi   = cosf(phi),   sphi   = sinf(phi);
        float4 dir = cam->c_lookat - cam->c_pos;
        dir = float4{
            dir.x * ctheta + dir.z * stheta,
            dir.y,
            -dir.x * stheta + dir.z * ctheta,
            0.f
        };
        dir = float4{
            dir.x,
            dir.y * cphi - dir.z * sphi,
            dir.y * sphi + dir.z * cphi,
            0.f
        };
        cam->c_lookat = dir + cam->c_pos;
        const float speed = 0.1f;
        float4 v={};
        if (get_key('W')) {
            v += cam->c_fwd * speed;
        }
        if (get_key('S')) {
            v -= cam->c_fwd * speed;
        }
        if (get_key('A')) {
            v -= cam->c_right * speed;
        }
        if (get_key('D')) {
            v += cam->c_right * speed;
        }
        if (get_key('Q')) {
            v += cam->c_up * speed;
        }
        if (get_key('E')) {
            v -= cam->c_up * speed;
        }
        if (get_key('H')) {
            cam->c_lookat=float4{0,0,0,1};
            cam->c_pos=float4{0,0,-30,1};
        }
        cam->c_pos += v;
        cam->c_lookat += v;
    }
}

__attribute__((weak)) void update_frame(void) { fps_camera(); }

extern "C" {
__attribute__((visibility("default"))) void frame_update_func(get_key_func_t _get_key_func, song_base_t *_G) {
    G->get_key_func = _get_key_func;
    if (G) {
        update_frame();
    }
}
}


static float k_reverb_fade = 240 / 256.f;                // 240 originally
static float k_reverb_shim = 80 / 256.f;                 // 80?
static float k_reverb_wob = 0.3333f;                     // amount of wobble
static const float lforate1 = 1.f / 32777.f * 9.4f;      // * 2.f; // speed of wobble
static const float lforate2 = 1.3f / 32777.f * 3.15971f; // * 2.f; // speed of wobble

static inline float LINEARINTERPRV(int RVMASK, const float *buf, int basei, float wobpos) { // read buf[basei-wobpos>>12] basically
    float fwobpos = floorf(wobpos);
    basei -= (int)fwobpos;
    wobpos -= fwobpos;
    float a0 = buf[basei & RVMASK];
    float a1 = buf[(basei - 1) & RVMASK];
    return ((a0 * (1.f - wobpos) + a1 * wobpos));
}

stereo limiter(float state[4], stereo inp) {
    const static float k_attack_ms = 2.f;
    const static float k_decay_ms = 150.f;
    const static float k_attack = 1.f - expf(-1.f / (SAMPLE_RATE * k_attack_ms * 0.001f));
    const static float k_decay = 1.f - expf(-1.f / (SAMPLE_RATE * k_decay_ms * 0.001f));
    float detect_in = (inp.l + inp.r) * 0.5f;
    detect_in = svf_process_2pole(state+2, detect_in, svf_g(80.f), 1.f/QBUTTER).hp; // highpass filter to remove low bass 
    float peak = fabsf(detect_in);
    float &env_follow = state[0];
    float &hold_time = state[1];
    if (peak > env_follow) {
        hold_time = SAMPLE_RATE / 50; // 20ms hold time
        env_follow += (peak - env_follow) * k_attack;
    } else {
        if (hold_time > 0.f)
            hold_time--;
        else
            env_follow += (peak - env_follow) * k_decay;
    }
    float gain = 0.9f / (max(env_follow, 0.9f));
    return sclip(inp * gain); // tanh clipper
}

stereo plinkyverb_t::_run_internal(stereo input) {
    if (RARE(dshimmerfade == 0)) {
        shimmerpos1 = 7000;
        shimmerpos2 = 8000;
        shimmerfade = 0;
        dshimmerfade = 32768 / 4096;
    }
    
    int i = reverb_pos;
    float outl = 0, outr = 0;
    float wob = aplfo(lforate1) * k_reverb_wob;
    float apwobpos = (wob + 1.f) * 64.f;
    wob = aplfo2(lforate2) * k_reverb_wob;
    float delaywobpos = (wob + 1.f) * 64.f;
#define RVDIV / 2
#define CHECKACC // assert(acc>=-32768 && acc<32767);
#define AP(len)                                                                                                                    \
    {                                                                                                                              \
        int j = (i + len RVDIV) & RVMASK;                                                                                          \
        float d = reverbbuf[j];                                                                                                    \
        acc -= d * 0.5f;                                                                                                           \
        reverbbuf[i] = (acc);                                                                                                      \
        acc = (acc * 0.5f) + d;                                                                                                    \
        i = j;                                                                                                                     \
        CHECKACC                                                                                                                   \
    }
#define AP_WOBBLE(len, wobpos)                                                                                                     \
    {                                                                                                                              \
        int j = (i + len RVDIV) & RVMASK;                                                                                          \
        float d = LINEARINTERPRV(RVMASK, reverbbuf, j, wobpos);                                                                            \
        acc -= d * 0.5f;                                                                                                           \
        reverbbuf[i] = (acc);                                                                                                      \
        acc = (acc * 0.5f) + d;                                                                                                    \
        i = j;                                                                                                                     \
        CHECKACC                                                                                                                   \
    }
#define DELAY(len)                                                                                                                 \
    {                                                                                                                              \
        int j = (i + len RVDIV) & RVMASK;                                                                                          \
        reverbbuf[i] = (acc);                                                                                                      \
        acc = reverbbuf[j];                                                                                                        \
        i = j;                                                                                                                     \
        CHECKACC                                                                                                                   \
    }
#define DELAY_WOBBLE(len, wobpos)                                                                                                  \
    {                                                                                                                              \
        int j = (i + len RVDIV) & RVMASK;                                                                                          \
        reverbbuf[i] = (acc);                                                                                                      \
        acc = LINEARINTERPRV(RVMASK, reverbbuf, j, wobpos);                                                                                \
        i = j;                                                                                                                     \
        CHECKACC                                                                                                                   \
    }

    float acc = ((input.l));
    AP(142);
    AP(379);
    acc += (input.r);
    AP(107);
    //	float reinject2 = acc;
    AP(277);
    float reinject = acc;

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
        //reverbbuf[(i+8192)&RVMASK] = acc;
        stereo shim1 = st(reverbbuf[(i + shimmerpos1) & RVMASK], reverbbuf[(i + shimmerpos2) & RVMASK]);
        stereo shim2 = st(reverbbuf[(i + shimmerpos1 + 1) & RVMASK], reverbbuf[(i + shimmerpos2 + 1) & RVMASK]);
        stereo shim = (shim1 + shim2);
        //i+=8192;

        // Fixed point crossfade:
        float shimo = shim.l * (SHIMMER_FADE_LEN - shimmerfade) + shim.r * shimmerfade;
        shimo *= 1.f / SHIMMER_FADE_LEN; // Divide by SHIMMER_FADE_LEN

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
    lpf += (((acc * k_reverb_fade)) - lpf) * k_reverb_color;
    dc += (lpf - dc) * 0.005f;
    acc = (lpf - dc);
    outl += acc;

    acc += reinject;
    AP_WOBBLE(908, delaywobpos);
    AP(2656);
    DELAY(3163);
    lpf2 += (((acc * k_reverb_fade)) - lpf2) * k_reverb_color;
    acc = (lpf2);

    outr += acc;

    reverb_pos = (reverb_pos - 1) & RVMASK;
    fb1 = (acc * k_reverb_fade);
    return st(outl, outr);
}

stereo delay_t::operator()(stereo inp, stereo time_qn, float feedback, float rotate_angle) {
    stereo ret;
    time_qn *= (SAMPLE_RATE * 60.f) / G->bpm;
    stereo readpos = mono2st(delay_pos) - time_qn;
    int il = int(floorf(readpos.l)) & (delaybuf_size - 1);
    int ir = int(floorf(readpos.r)) & (delaybuf_size - 1);
    ret.l = lerp(delaybuf[il].l, delaybuf[(il + 1) & (delaybuf_size - 1)].l, frac(readpos.l));
    ret.r = lerp(delaybuf[ir].r, delaybuf[(ir + 1) & (delaybuf_size - 1)].r, frac(readpos.r));
    ret = rotate(ret, cosf(rotate_angle), sinf(rotate_angle));
    delaybuf[delay_pos] = sclip(inp + ret * feedback);
    delay_pos = (delay_pos + 1) & (delaybuf_size - 1);
    return ret;
}

stereo ginkgoverb_t::operator()(stereo inp) {
    float *state = filter_state;
    stereo *state2 = (stereo *)(state + 16);
    ////////////////////////// 4x DOWNSAMPLE
    const float hpf_g = svf_g(150.f);
    const float lpf_g = svf_g(5000.f);
    inp = (stereo){
        svf_process_2pole(state, inp.l, lpf_g, SQRT2).lp,
        svf_process_2pole(state + 2, inp.r, lpf_g, SQRT2).lp,
    };
    inp = (stereo){
        svf_process_2pole(state + 4, inp.l, hpf_g, SQRT2).hp,
        svf_process_2pole(state + 6, inp.r, hpf_g, SQRT2).hp,
    };
    int outslot = (G->sampleidx >> 2) & 3;
    if ((G->sampleidx & 3) == 0) {
        state2[outslot] = _run_internal(inp);
    }
    ////////////////////////// 4x CATMULL ROM UPSAMPLE
    int t0 = (outslot - 3) & 3, t1 = (outslot - 2) & 3, t2 = (outslot - 1) & 3, t3 = outslot;
    float f = ((G->sampleidx & 3) + 1) * 0.25f;
    float lout = catmull_rom(state2[t0].l, state2[t1].l, state2[t2].l, state2[t3].l, f);
    float rout = catmull_rom(state2[t0].r, state2[t1].r, state2[t2].r, state2[t3].r, f);
    return (stereo){lout, rout};
}

stereo ginkgoverb_t::_run_internal(stereo inp) {

    int i = reverb_pos;
    float acc = ((inp.l));
    AP(142);
    AP(379);
    acc += (inp.r);
    AP(107);
    AP(277);
    float reinject = acc;
    AP(672);
    AP(1800);
    #define DELAY_LEN 12000
    float ret[4]={};
    static int shimt=0;
    shimt++;
    for (int t = 0; t < NUM_TAPS; t++) {
        int shimi=(shimt+t*(16384/NUM_TAPS))&16383;
        int shimj = (shimi+16384/2)&16383;
        float lfofreq = frac((t + 1) * PHI) * (1.f / SAMPLE_RATE) + (1.f / SAMPLE_RATE);
        float pos = (1.f+taplofs[t](lfofreq)) * (DELAY_LEN/2.f - 100.f) + 50.f;
        int basei = i + (((t&3)+1)*DELAY_LEN);
        ret[t&3] += LINEARINTERPRV(RVMASK,reverbbuf, basei , pos);
        float shim1 = reverbbuf[(basei-shimi-64)&RVMASK] + reverbbuf[(basei-shimi-65)&RVMASK];
        float shim2 = reverbbuf[(basei-shimj-64)&RVMASK] + reverbbuf[(basei-shimj-65)&RVMASK];
        int shimfade = 16384/2-abs(shimi-16384/2);
        float shim = lerp(shim1, shim2, shimt*(0.5f/16384.f));
        //ret[t&3] += shim1*0.1f;
    }
    static float dc[4];
    dc[0] += (ret[0] - dc[0]) * 0.005f;
    dc[1] += (ret[1] - dc[1]) * 0.005f;
    dc[2] += (ret[2] - dc[2]) * 0.005f;
    dc[3] += (ret[3] - dc[3]) * 0.005f;
    ret[0] -= dc[0];
    ret[1] -= dc[1];
    ret[2] -= dc[2];
    ret[3] -= dc[3];

    float ret_rotated[4]={};
    const float g = 3.f / NUM_TAPS;
    const float svfg = svf_g(1600.f/4.f); // /4 because we run at 4x downsampled rate
    reverbbuf[(i+0)&RVMASK] =     g*( ret[0] + ret[1] + ret[2] + ret[3] ) + acc;
    reverbbuf[(i+DELAY_LEN)&RVMASK] = g*( ret[0] - ret[1] + ret[2] - ret[3] ) - acc;
    reverbbuf[(i+DELAY_LEN*2)&RVMASK] = g*( ret[0] + ret[1] - ret[2] - ret[3] ) - acc;
    reverbbuf[(i+DELAY_LEN*3)&RVMASK] = g*( ret[0] - ret[1] - ret[2] + ret[3] ) + acc;
    i+=DELAY_LEN*4;
       
    stereo outp = st(ret[0] + ret[2] + acc,ret[1] + ret[3] + acc);
    reverb_pos = (reverb_pos - 1) & RVMASK;
    return outp;
}

stereo plinkyverb_t::operator()(stereo inp) {
    float *state = filter_state;
    stereo *state2 = (stereo *)(filter_state + 4);
    ////////////////////////// 4x DOWNSAMPLE
    inp = (stereo){
        svf_process_2pole(state, inp.l, 0.125f, SQRT2).lp,
        svf_process_2pole(state + 2, inp.r, 0.125f, SQRT2).lp,
    };
    int outslot = (G->sampleidx >> 2) & 3;
    if ((G->sampleidx & 3) == 0) {
        state2[outslot] = _run_internal(inp);
    }
    ////////////////////////// 4x CATMULL ROM UPSAMPLE
    int t0 = (outslot - 3) & 3, t1 = (outslot - 2) & 3, t2 = (outslot - 1) & 3, t3 = outslot;
    float f = ((G->sampleidx & 3) + 1) * 0.25f;
    float lout = catmull_rom(state2[t0].l, state2[t1].l, state2[t2].l, state2[t3].l, f);
    float rout = catmull_rom(state2[t0].r, state2[t1].r, state2[t2].r, state2[t3].r, f);
    return (stereo){lout, rout};
}

void *dsp_preamble(song_base_t *_G, stereo *audio, int reloaded, size_t state_size, void (*init_state)(void)) {
    if (!_G || _G->_size != state_size) {
        /* free(G); - safer to just let it leak :)  virtual memory ftw  */
        // printf("CLEARING STATE\n");
        song_base_t *oldg = _G;
        _G = (song_base_t *)calloc(1, state_size);
        if (oldg) {
            size_t sz = sizeof(song_base_t); // min(sizeof(song_base_t), min(state_size, oldg->_size));
            memcpy(_G, oldg, sz);            // preserve the basic state...
        }
        // ...but update the size.
        _G->_size = state_size;
    }
    _G->reloaded = reloaded;
    G = _G; // set global variable for user code. nb the dll and the main program have their own copies of G. dsp() ends up setting
            // the dll copy from the main process copy.
    if (reloaded) {
        init_basic_state();
        (*init_state)();
    }
    // update bpm
    pattern_t *bpm_pattern = get_pattern("/bpm");
    if (bpm_pattern && bpm_pattern->key) {
        hap_t haps[8];
        hap_span_t hs = bpm_pattern->make_haps({haps, haps + 8}, 8, -1.f, _G->t + _G->dt * 0.5f);
        if (hs.s < hs.e) {
            float newbpm = hs.s->get_param(P_NUMBER, _G->bpm);
            if (newbpm > 20.f && newbpm < 400.f && newbpm != _G->bpm) {
                printf("setting bpm to %f\n", newbpm);
                _G->bpm = newbpm;
            }
        }
    }
    return _G;
}

wave_t *request_wave_load(wave_t *wave) {
    if (!G || !wave)
        return NULL;
    if (wave->frames)
        return wave;
    if (wave->download_in_progress)
        return NULL;
    spin_lock(&G->load_request_cs);
    wave_request_t key = {wave};
    wave->download_in_progress = 1;
    wave->sample_func = stbds_shgetp_null(G->waves, "_wave")->sample_func;
    stbds_hmputs(G->load_requests, key);
    spin_unlock(&G->load_request_cs);
    return NULL;
}

// conv ir block sizes are 256, 512, 1024, 2048...
#define CONV_IR_NUM_BLOCK_SIZES 5
#define CONV_IR_MIN_BLOCK_SIZE 256 // fft sizes are double the block size (zero pad), and complex (*2 again)
#define CONV_IR_MAX_BLOCK_SIZE (CONV_IR_MIN_BLOCK_SIZE << (CONV_IR_NUM_BLOCK_SIZES - 1))

inline stereo complexmul(stereo a, stereo b, float scale) {
    return st((a.l * b.l - a.r * b.r) * scale, (a.l * b.r + a.r * b.l) * scale);
}

// building blocks for conv reverb
void test_conv_reverb(void) {
    PFFFT_Setup *fft_setup[CONV_IR_NUM_BLOCK_SIZES] = {};
    static float *fft_work = 0;
    if (!fft_setup[0]) {
        fft_work = (float *)malloc(CONV_IR_MAX_BLOCK_SIZE * 4 * sizeof(float));
        for (int i = 0; i < CONV_IR_NUM_BLOCK_SIZES; i++) {
            fft_setup[i] = pffft_new_setup(CONV_IR_MIN_BLOCK_SIZE << (i + 1), PFFFT_COMPLEX);
        }
    }
    stereo *testblock = (stereo *)calloc(CONV_IR_MAX_BLOCK_SIZE, sizeof(float) * 2 * 2);
    float e = 1.f;
    for (int i = 0; i < CONV_IR_MAX_BLOCK_SIZE; i++) {
        testblock[i] = st(sinf(i * i * 0.0001f) * e, rndn() * e * 0.25f);
        e *= 0.999f;
    }

    stereo *irblock = (stereo *)calloc(CONV_IR_MAX_BLOCK_SIZE, sizeof(float) * 2 * 2);
    irblock[1000].l = 1.f;

    stereo *irblock_fft = (stereo *)calloc(CONV_IR_MAX_BLOCK_SIZE, sizeof(float) * 2 * 2);
    stereo *testblock_fft = (stereo *)calloc(CONV_IR_MAX_BLOCK_SIZE, sizeof(float) * 2 * 2);
    stereo *convblock_fft = (stereo *)calloc(CONV_IR_MAX_BLOCK_SIZE, sizeof(float) * 2 * 2);
    pffft_transform(fft_setup[CONV_IR_NUM_BLOCK_SIZES - 1], (float *)irblock, (float *)irblock_fft, fft_work, PFFFT_FORWARD);
    pffft_transform(fft_setup[CONV_IR_NUM_BLOCK_SIZES - 1], (float *)testblock, (float *)testblock_fft, fft_work, PFFFT_FORWARD);
    pffft_zconvolve_accumulate(fft_setup[CONV_IR_NUM_BLOCK_SIZES - 1], (float *)irblock_fft, (float *)testblock_fft,
                               (float *)convblock_fft, 1.f / (CONV_IR_MAX_BLOCK_SIZE * 2.f));
    pffft_transform(fft_setup[CONV_IR_NUM_BLOCK_SIZES - 1], (float *)convblock_fft, (float *)convblock_fft, fft_work,
                    PFFFT_BACKWARD);

    FILE *f = fopen("testblock.wav", "wb");
    write_wav_header(f, CONV_IR_MAX_BLOCK_SIZE * 2, 48000, 2);
    fwrite(testblock, 2 * CONV_IR_MAX_BLOCK_SIZE, sizeof(float) * 2, f);
    fclose(f);
    f = fopen("testblock2.wav", "wb");
    write_wav_header(f, CONV_IR_MAX_BLOCK_SIZE * 2, 48000, 2);
    fwrite(convblock_fft, 2 * CONV_IR_MAX_BLOCK_SIZE, sizeof(float) * 2, f);
    fclose(f);
}

hap_t *pat2hap(const char *pattern_name, hap_t *cache) {
    if ((G->sampleidx % 96) != 0)
        return cache;
    hap_time from = G->t;
    hap_time to = G->t + G->dt * 96.5f; // go epsilon (half a sample) over to ensure no cracks :)
    hap_t haps[8];
    hap_span_t hs = {};
    pattern_t *p = get_pattern(pattern_name);
    if (p && G->playing)
        hs = p->make_haps({haps, haps + 8}, 8, G->iTime, to + G->dt);
    // highest note priority
    float highest_note = -1;
    float highest_number = -1;
    int highest_index = -1;
    for (int i = 0; i < hs.e - hs.s; ++i) {
        hap_t *h = &haps[i];
        if (h->valid_params & ((1 << P_NOTE) | (1 << P_SOUND)) && h->t0 < to && h->t1 >= from) {
            float note = h->get_param(P_NOTE, 0.f);
            float number = h->get_param(P_NUMBER, 0.f);
            if (note >= highest_note || (note == highest_note && number > highest_number)) {
                highest_note = note;
                highest_number = number;
                highest_index = i;
            }
        }
    }
    if (highest_index != -1) {
        cache->hapid = haps[highest_index].hapid;
        merge_hap(cache, &haps[highest_index]);
    } else {
        cache->hapid = 0;
    }
    return cache;
}

stereo voice_state_t::synth_sample(hap_t *h, bool keydown, float env1, float env2, float fold_actual, float dist_actual,
                                   float cutoff_actual, float noise_actual, wave_t *w, bool *at_end) {
    stereo au;
    if (w && w->sample_func) {
        au = (w->sample_func)(this, h, w, at_end);
    } else {
        au={0.f,0.f};
    }
    au *= env1;
    if (fold_actual > 0.f) {
        float foldgain = fold_actual * fold_actual * 100.f + 1.f;
        // float invfoldgain = 1.f / (fold_actual * 10.f + 1.f);
        au.l = fold(au.l * foldgain); // * invfoldgain;
        au.r = fold(au.r * foldgain); // * invfoldgain;
    }
    if (dist_actual > 0.f) {
        float mix = saturate(dist_actual * 10.f);
        float distgain = exp(clamp(dist_actual, 0., 5.) * 4.f);
        au.l = lerp(au.l, sclip(au.l * distgain), mix);
        au.r = lerp(au.r, sclip(au.r * distgain), mix);
    }
    if (noise_actual > 0.f) {
        noise_lpf += (rnd01()-0.5f - noise_lpf) * 0.1f;
        float n = noise_lpf * noise_actual; 
        au.l += n;
        au.r += n;
    }
    float resonance = h->get_param(P_RESONANCE, 0.f);
    au = filter.lpf(au, cutoff_actual, saturate(1.f - resonance));
    float hpf_cutoff = h->get_param(P_HPF, 0.f);
    if (hpf_cutoff > 0.f) {
        au = hpf.hpf(au, hpf_cutoff);
    }
    if (!isfinite(au.l) || !isfinite(au.r)) {
        au = {};
        filter = {};
    }
    float gain = h->get_param(P_GAIN, 0.75f);
    gain = gain * gain; // gain curve
    float panpos = h->get_param(P_PAN, 0.5f);
    au = pan(au, panpos * 2.f - 1.f);
    au *= gain;
    return au;
}

void printf_bar(float v, int width=16) {
    static const char *blocks[] = { " ", "▏", "▎", "▍", "▌", "▋", "▊", "▉", "█" };
    int i, full, sub;
    float x;
    if (v < 0.f) v = 0.f;
    else if (v > 1.f) v = 1.f;
    x = v * width;
    full = (int)x;
    x = (x - full) * 8.f;
    sub = (int)(x + 0.5f);
    if (sub > 8) { sub = 0; full++; }
    for (i = 0; i < width; ++i) {
        if (i < full) fputs(blocks[8], stdout);                 // full block
        else if (i == full && sub > 0 ) fputs(blocks[sub], stdout); // partial
        else fputs(" ", stdout);
    }
}

void synth_t::debug_draw_voices(void) const {
    printf(CURSOR_TO_HOME);
    for (int i=0;i<synth_t::max_voices;i++) {
        const voice_state_t *v = &voices[i];
        printf("%02d ", i);
        bool keydown = G->playing && v->h.valid_params && (v->h.t0 <= G->t && v->h.t1 > G->t);
        if (v->h.valid_params) 
            printf(keydown ? COLOR_GREEN "*" : COLOR_RED "*");
        else 
            printf(COLOR_GREY " ");
        printf("id=%04x %5.1f-%5.1f ", v->h.hapid&0xffff, v->h.t0, v->h.t1);
        float env_level = fabsf(v->adsr1.state[0]);
        printf(COLOR_CYAN "%4.2f", env_level);
        printf_bar(env_level, 8);
        printf(COLOR_RESET " ");

        const hap_t *hap = &v->h;
        for (int i = 0; i < P_LAST; i++) {
            if (hap->valid_params & (1ull << i)) {
                if (i == P_SOUND) {
                    Sound *sound = get_sound_by_index((int)hap->params[i]);
                    printf("s=" COLOR_BRIGHT_YELLOW "%s" COLOR_RESET " ", sound ? sound->name : "<missing>");
                } else if (i == P_NOTE) {
                    printf("note=" COLOR_BRIGHT_YELLOW "%s" COLOR_RESET " ", print_midinote((int)hap->params[i]));
                } else {
                    printf("%s=" COLOR_BRIGHT_YELLOW "%5.2f" COLOR_RESET " ", param_names[i], hap->params[i]);
                }
            }
        }
        printf(CLEAR_TO_END_OF_LINE "\n");
    }
    printf("---------------------" CLEAR_TO_END_OF_LINE "\n");
}


stereo synth_t::operator()(const char *pattern_name, float level_target, synth_t_options options) {
    int max_voices = options.max_voices;
    if (max_voices > synth_t::max_voices || max_voices < 1)
        max_voices = synth_t::max_voices;
    if (level_target < 0.f)
        level_target = 0.f;
    level_target = level_target * level_target;
    float distortion = options.distortion;
    if (distortion <= 0.f) {
        distortion = 1.f;
    } else {
        distortion = exp(clamp(distortion, 0., 2.) * 4.f);
        level_target /= sqrtf(distortion); // compensate after the fact
    }
    

    if ((G->sampleidx % 96) == 0) {
        memset(audio, 0, sizeof(audio));
        pattern_t *p = get_pattern(pattern_name);
        hap_time from = G->t;
        hap_time to = G->t + G->dt * 96.5f; // go epsilon (half a sample) over to ensure no cracks :)
        hap_t haps[8];
        hap_span_t hs = {};
        if (p && G->playing)
            hs = p->make_haps({haps, haps + 8}, 8, (level_target > 0.f) ? G->iTime : -1.f, to);
        //pretty_print_haps(hs, from, to);
        for (hap_t *h = hs.s; h < hs.e; h++) {
            if (!(h->valid_params & ((1 << P_NOTE) | (1 << P_SOUND)) && h->t0 < to && h->t1 >= from))
                continue;
            int i = (int)h->get_param(P_STRING, -1);
            if (i < 0 || i >= max_voices) {
                for (; i < max_voices; ++i) {
                    hap_t *existing_hap = &voices[i].h;
                    if (existing_hap->hapid == h->hapid && existing_hap->t0 == h->t0)
                        break;
                }
                if (i == max_voices) {
                    if (h->t0 >= to || h->t0 < from) {
                        continue; // only trigger voices at the right time.
                    }
                    // ok its a new voice.
                    // find oldest one to steal
                    i = -1;
                    //float quietest = 1e10f;
                    double oldest_t = 1e10;
                    for (int j = 0; j < max_voices; ++j) {
                        if (!voices[j].h.valid_params) {
                            i = j;
                            break;
                        }
                        //float lvl = fabsf(voices[j].adsr1.state[0]);
                        double t = voices[j].h.t0;
                        //if (voices[j].cur_power < quietest && voices[j].h.t1 < to) {
                        hap_time t0 = voices[j].h.t0;
                        if (t0 < oldest_t && voices[j].h.t1 < to) {
                            oldest_t = t0;
                            i = j;
                        }
                    }
                    if (i<0) {
                        //printf("no voice found to steal\n");
                        continue;
                    }
                    if (voices[i].h.valid_params) {
                        //printf("voice %d stolen, %f\n", i, voices[i].adsr1.state[0]);
                    }
                }
            }
            // end of voice allocation
            assert(i < max_voices && i>=0);
            if (h->t0 < to && h->t0 >= from) {
                // trigger voices at the right time.
                voices[i].h.valid_params = 0;
                voices[i].h.hapid = 0;
                voices[i].h.t0 = h->t0;
                voices[i].h.t1 = h->t1;
                voices[i].retrig = true;
                voices[i].vibphase = rnd01();
                voices[i].tremphase = 0.f;
                //printf("voice %d allocated, now %d\n", i, num_in_use);
            }
            hap_t *dst = &voices[i].h;
            if (dst->hapid != h->hapid) {
                // hapid has changed. often that's when the voice allocation code above ran,
                // but for a monophonic voice where 'string' was specified, it may just be the note changing.
                dst->hapid = h->hapid;
                dst->node = h->node;
                dst->valid_params = 0;
                dst->scale_bits = 0;
                dst->t1 = h->t1; // we ONLY update t1, not t0; that way a sustained monosynth note doesnt get a 'hole' in it
                // due to first_smpl below.
                voices[i].retrig = true;
            }
            // actually lets always update t1, useful for live midi playing where we dont know when its going to end...
            dst->t1 = h->t1; 

            merge_hap(dst, h);
        }
        /////////////////////
        // ok thats it for triggering/param updating
        // time to make noise
        num_in_use = 0;

        for (int i = 0; i < max_voices; ++i) {
            voice_state_t *v = &voices[i];
           // v->cur_power *= 0.99f;
            hap_t *h = &v->h;
            if (!h->valid_params) {
                continue;
            }
            static int sawidx = get_sound_index("saw");
            int sound = h->get_param(P_SOUND, sawidx);
            int first_smpl = (h->t0 - from) / G->dt;
            if (first_smpl >= 96) {
                // printf("FIRST SAMPLE OUT OF BOUNDS. SKIPPING %d\n", i);
                continue;
            }
            if (first_smpl < 0)
                first_smpl = 0;
            float note = h->get_param(P_NOTE, C3);
            float number = h->get_param(P_NUMBER, 0.f);
            wave_t *w = get_wave(get_sound_by_index(sound), number, &note);
            float vib = h->get_param(P_VIB, 0.f);
            float vibfreq = h->get_param(P_VIB_FREQ, 1.5f);
            note += sino(v->vibphase) * vib;
            v->vibphase = frac(v->vibphase + vibfreq * 96.f * 8.f * G->dt);
            float trem = h->get_param(P_TREM, 0.f) * 0.5f;
            float tremfreq = h->get_param(P_TREM_FREQ, 1.5f);
            float oldtrem = square(1.f - trem - trem * cosf(v->tremphase * TAU));
            v->tremphase = frac(v->tremphase + tremfreq * 96.f * 8.f * G->dt);
            float newtrem = square(1.f - trem - trem * cosf(v->tremphase * TAU));
            float lpg = h->get_param(P_LPG, 0.f);
            float key_track_amount = saturate(1.f - lpg);

            double dphase = exp2((note - C3) / 12.f);         // midi2dphase(note);
            float key_track_gain = exp2(((note - C3) / -36.f) * key_track_amount); // gentle falloff of high notes
            
            level_target = level_target * 0.2f + level * 0.8f; // smooth level target.
            float newlevel = level_target;
            float curlevel = level;
            float predist_level = key_track_gain * oldtrem;
            float predist_dlevel = (key_track_gain * newtrem - predist_level) / (96.f - first_smpl);
            float dlevel = (newlevel - curlevel) / (96.f - first_smpl);
            level = level_target;

            float sustain = square(h->get_param(P_SUS, 1.f));
            float attack_k = env_k(h->get_param(P_ATT, 0.f));
            float decay_k = env_k(h->get_param(P_DEC, 0.3f));
            //float release_k = env_k(h->get_param(P_REL, 0.f));
            float release = (h->get_param(P_REL, -1.f));
            float release_k;
            if (release >= 0.f) {
                release_k = env_k(release);
            } else {
                // no release specified...
                bool has_any_env = h->valid_params & (1 << P_ATT | 1 << P_DEC | 1 << P_SUS | 1 << P_REL);
                if (has_any_env) {
                    release_k = decay_k;
                } else
                if (!G->playing) {
                    release_k = 0.005f; // stop immediately if not playing or if an envelope was set without release.
                } else if (w && w->num_frames > 0 && h->get_param(P_LOOPS, 0.f) >= h->get_param(P_LOOPE, 0.f)) {
                    release_k = 0.f; // it's a one shot sample so let it play out. nb this is now a _k value, not a time.
                } else {
                    release_k = decay_k; // 0.005f; // it's a looping sample, so we must immediately stop when the key goes up.
                }
            }

            float sustain2 = square(h->get_param(P_SUS2, 1.f));
            float attack2_k = env_k(h->get_param(P_ATT2, 0.f));
            float decay2_k = env_k(h->get_param(P_DEC2, 0.3f));
            float release2_k = env_k(h->get_param(P_REL2, 1000.f));

            float noise = h->get_param(P_NOISE, 0.f);
            float gate = h->get_param(P_GATE, 0.75f);
            float cutoff = h->get_param(P_CUTOFF, 24000.f);
            float dist = h->get_param(P_DIST, 0.f);
            float fold_amount = h->get_param(P_FOLD, 0.f);
            float env2vcf = h->get_param(P_ENV2VCF, 1.f);
            float env2dist = h->get_param(P_ENV2DIST, 0.f);
            float env2fold = h->get_param(P_ENV2FOLD, 0.f);
            float env2noise = h->get_param(P_ENV2NOISE, 0.f);
            float k_glide = env_k(0.5f * h->get_param(P_GLIDE, 0.f));
            hap_time t = from + first_smpl * G->dt;
            for (int smpl = first_smpl; smpl < 96; smpl++, t += G->dt) {
                ////////////// VOICE SAMPLE
                bool keydown = G->playing && t < h->t1 + G->dt * 0.5f;
                float env1 = v->adsr1(keydown ? gate : 0.f, attack_k, decay_k, sustain, release_k, v->retrig);
                float env2 = v->adsr2(keydown ? 1.f : 0.f, attack2_k, decay2_k, sustain2, release2_k, v->retrig);
                float noise_actual = noise * (min(1.f, 1.f - env2noise) + env2 * env2noise);
                float lpg_amount = lerp(1.f, env1 * env1, lpg);
                float cutoff_actual = max(20.f, lpg_amount * cutoff * (min(1.f, 1.f - env2vcf) + env2 * env2vcf));
                float fold_actual = fold_amount * (min(1.f, 1.f - env2fold) + env2 * env2fold);
                float dist_actual = dist * (min(1.f, 1.f - env2dist) + env2 * env2dist);
                v->dphase += (dphase - v->dphase) * k_glide;
                bool at_end = false;
                audio[smpl] += v->synth_sample(h, keydown, lerp(env1, min(1.f, env1 * 20.f), lpg), env2, fold_actual, dist_actual, cutoff_actual, noise_actual, w, &at_end);
                audio[smpl] *= predist_level;
                predist_level += predist_dlevel;
                
               // v->cur_power += fabsf(audio[smpl].l) + fabsf(audio[smpl].r);
                v->retrig = false;
                if (!keydown && (env1 < 0.0001f || at_end)) {
                    h->valid_params = 0;
                }
                if (options.distortion != 1.f) {
                    audio[smpl] = sclip(audio[smpl] * distortion);
                }
                audio[smpl] *= curlevel;

                curlevel += dlevel;
                if (v->h.valid_params == 0) {
                    //printf("voice %d released - from %f to %f - now %d\n", i, from, to, num_in_use);
                    break;
                }
            } // voice sample loop
            num_in_use++;
        } // voice loop
    } // are we first sample of 96 sample block loop
    if (options.debug_draw) {
        if ((G->sampleidx % 9600) == 0) {
            debug_draw_voices();
        }
    }
    return audio[G->sampleidx % 96];
}

stereo prepare_preview(void) {
    stereo preview = {};
    if (G->preview_wave_idx_plus_one) {
        wave_t *w = &G->waves[G->preview_wave_idx_plus_one - 1];
        if (w) {
            float pos = G->preview_wave_t;
            G->preview_wave_t += 1.f;
            bool at_end = false;
            voice_state_t v = {.dphase = 1.f / SAMPLE_RATE, .phase = pos, .grainheads = {pos}, .retrig = false};
            hap_t h = {
                .valid_params = (1 << P_FROM) | (1 << P_TO),
                .params[P_FROM] = G->preview_fromt,
                .params[P_TO] = G->preview_tot,
            };
            preview = (stbds_shgetp(G->waves,"_wave")->sample_func)(&v, &h, w, &at_end) * (G->preview_wave_fade * 0.33f);
            if (at_end) {
                G->preview_wave_fade = 0.f;
            }
        }
    }
    if (G->preview_wave_fade < 1.f) {
        G->preview_wave_fade *= 0.999f;
        if (G->preview_wave_fade < 0.001f) {
            G->preview_wave_fade = 0.f;
        }
    }
    G->preview = preview;
    return preview;
}

static float _silence[16] = {};

void register_osc(const char *name, sample_func_t *func) {
    wave_t w = {.key = name, .sample_func = func, .sample_rate = SAMPLE_RATE, .frames = _silence, .num_frames = 0};
    stbds_shputs(G->waves, w);
    int idx = stbds_shgeti(G->waves, w.key);
    Sound *osc = get_sound_init_only(name);
    stbds_arrsetlen(osc->wave_indices, 0);
    stbds_arrpush(osc->wave_indices, idx);
}
